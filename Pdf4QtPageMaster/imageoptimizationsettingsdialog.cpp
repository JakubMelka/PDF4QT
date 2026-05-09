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

#include "imageoptimizationsettingsdialog.h"

#include "pdfwidgetutils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QVariant>
#include <QVBoxLayout>

#include <utility>

namespace pdfpagemaster
{
namespace
{
template<typename Enum>
void addItem(QComboBox* comboBox, const QString& text, Enum value)
{
    comboBox->addItem(text, QVariant::fromValue(static_cast<int>(value)));
}

template<typename Enum>
Enum currentEnum(const QComboBox* comboBox, Enum fallback)
{
    QVariant value = comboBox->currentData();
    if (!value.isValid())
    {
        return fallback;
    }
    return static_cast<Enum>(value.toInt());
}

template<typename Enum>
void setCurrentEnum(QComboBox* comboBox, Enum value, Enum fallback)
{
    int index = comboBox->findData(static_cast<int>(value));
    if (index < 0)
    {
        index = comboBox->findData(static_cast<int>(fallback));
    }
    if (index >= 0)
    {
        comboBox->setCurrentIndex(index);
    }
}
}   // namespace

ImageOptimizationSettingsDialog::ImageOptimizationSettingsDialog(pdf::PDFImageOptimizer::Settings settings, QWidget* parent) :
    QDialog(parent),
    m_settings(std::move(settings))
{
    setWindowTitle(tr("Image Optimization Settings"));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QGroupBox* globalGroupBox = new QGroupBox(tr("Global Settings"), this);
    QFormLayout* globalLayout = new QFormLayout(globalGroupBox);

    m_modeComboBox = new QComboBox(globalGroupBox);
    m_modeComboBox->addItem(tr("Auto"), 0);
    m_modeComboBox->addItem(tr("Custom"), 1);
    globalLayout->addRow(tr("Mode"), m_modeComboBox);

    m_colorModeComboBox = new QComboBox(globalGroupBox);
    addItem(m_colorModeComboBox, tr("Auto"), pdf::PDFImageOptimizer::ColorMode::Auto);
    addItem(m_colorModeComboBox, tr("Preserve"), pdf::PDFImageOptimizer::ColorMode::Preserve);
    addItem(m_colorModeComboBox, tr("Color (RGB)"), pdf::PDFImageOptimizer::ColorMode::Color);
    addItem(m_colorModeComboBox, tr("Grayscale"), pdf::PDFImageOptimizer::ColorMode::Grayscale);
    addItem(m_colorModeComboBox, tr("Bitonal"), pdf::PDFImageOptimizer::ColorMode::Bitonal);
    globalLayout->addRow(tr("Color mode"), m_colorModeComboBox);

    m_goalComboBox = new QComboBox(globalGroupBox);
    addItem(m_goalComboBox, tr("Prefer quality"), pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality);
    addItem(m_goalComboBox, tr("Minimum size"), pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize);
    globalLayout->addRow(tr("Goal"), m_goalComboBox);

    m_keepOriginalCheckBox = new QCheckBox(tr("Keep original image if re-encoded stream is larger"), globalGroupBox);
    globalLayout->addRow(QString(), m_keepOriginalCheckBox);

    m_preserveTransparencyCheckBox = new QCheckBox(tr("Preserve transparency"), globalGroupBox);
    globalLayout->addRow(QString(), m_preserveTransparencyCheckBox);

    mainLayout->addWidget(globalGroupBox);

    QTabWidget* profilesTabWidget = new QTabWidget(this);
    QWidget* colorTab = new QWidget(profilesTabWidget);
    QWidget* grayTab = new QWidget(profilesTabWidget);
    QWidget* bitonalTab = new QWidget(profilesTabWidget);
    QSpinBox* unusedBitonalJpegQualitySpinBox = nullptr;
    setupProfileTab(colorTab, m_colorAlgorithmComboBox, m_colorDpiSpinBox, m_colorResampleComboBox, m_colorJpegQualitySpinBox, true);
    setupProfileTab(grayTab, m_grayAlgorithmComboBox, m_grayDpiSpinBox, m_grayResampleComboBox, m_grayJpegQualitySpinBox, true);
    setupProfileTab(bitonalTab, m_bitonalAlgorithmComboBox, m_bitonalDpiSpinBox, m_bitonalResampleComboBox, unusedBitonalJpegQualitySpinBox, false);
    profilesTabWidget->addTab(colorTab, tr("Color"));
    profilesTabWidget->addTab(grayTab, tr("Grayscale"));
    profilesTabWidget->addTab(bitonalTab, tr("Bitonal"));
    mainLayout->addWidget(profilesTabWidget);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]()
    {
        applyUiToSettings();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);

    loadSettingsToUi();
    pdf::PDFWidgetUtils::scaleWidget(this, QSize(520, 420));
    pdf::PDFWidgetUtils::style(this);
}

void ImageOptimizationSettingsDialog::setupProfileTab(QWidget* tab,
                                                     QComboBox*& algorithmComboBox,
                                                     QSpinBox*& dpiSpinBox,
                                                     QComboBox*& resampleComboBox,
                                                     QSpinBox*& jpegQualitySpinBox,
                                                     bool addJpegQuality)
{
    QFormLayout* layout = new QFormLayout(tab);

    algorithmComboBox = new QComboBox(tab);
    addItem(algorithmComboBox, tr("Auto"), pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    addItem(algorithmComboBox, tr("Flate"), pdf::PDFImageOptimizer::CompressionAlgorithm::Flate);
    if (addJpegQuality)
    {
        addItem(algorithmComboBox, tr("JPEG"), pdf::PDFImageOptimizer::CompressionAlgorithm::JPEG);
        addItem(algorithmComboBox, tr("JPEG2000"), pdf::PDFImageOptimizer::CompressionAlgorithm::JPEG2000);
    }
    addItem(algorithmComboBox, tr("RunLength"), pdf::PDFImageOptimizer::CompressionAlgorithm::RunLength);
    layout->addRow(tr("Algorithm"), algorithmComboBox);

    dpiSpinBox = new QSpinBox(tab);
    dpiSpinBox->setRange(0, 1200);
    dpiSpinBox->setSuffix(tr(" dpi"));
    layout->addRow(tr("Target DPI"), dpiSpinBox);

    resampleComboBox = new QComboBox(tab);
    addItem(resampleComboBox, tr("Nearest"), pdf::PDFImage::ResampleFilter::Nearest);
    addItem(resampleComboBox, tr("Bilinear"), pdf::PDFImage::ResampleFilter::Bilinear);
    addItem(resampleComboBox, tr("Bicubic"), pdf::PDFImage::ResampleFilter::Bicubic);
    addItem(resampleComboBox, tr("Lanczos"), pdf::PDFImage::ResampleFilter::Lanczos);
    layout->addRow(tr("Resample"), resampleComboBox);

    if (addJpegQuality)
    {
        jpegQualitySpinBox = new QSpinBox(tab);
        jpegQualitySpinBox->setRange(0, 100);
        layout->addRow(tr("JPEG quality"), jpegQualitySpinBox);
    }
    else
    {
        jpegQualitySpinBox = nullptr;
    }
}

void ImageOptimizationSettingsDialog::loadSettingsToUi()
{
    m_modeComboBox->setCurrentIndex(m_settings.autoMode ? 0 : 1);
    setCurrentEnum(m_colorModeComboBox, m_settings.colorMode, pdf::PDFImageOptimizer::ColorMode::Auto);
    setCurrentEnum(m_goalComboBox, m_settings.goal, pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality);
    m_keepOriginalCheckBox->setChecked(m_settings.keepOriginalIfLarger);
    m_preserveTransparencyCheckBox->setChecked(m_settings.preserveTransparency);

    setCurrentEnum(m_colorAlgorithmComboBox, m_settings.colorProfile.algorithm, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    m_colorDpiSpinBox->setValue(m_settings.colorProfile.targetDpi);
    setCurrentEnum(m_colorResampleComboBox, m_settings.colorProfile.resampleFilter, pdf::PDFImage::ResampleFilter::Bicubic);
    m_colorJpegQualitySpinBox->setValue(m_settings.colorProfile.jpegQuality);

    setCurrentEnum(m_grayAlgorithmComboBox, m_settings.grayProfile.algorithm, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    m_grayDpiSpinBox->setValue(m_settings.grayProfile.targetDpi);
    setCurrentEnum(m_grayResampleComboBox, m_settings.grayProfile.resampleFilter, pdf::PDFImage::ResampleFilter::Bicubic);
    m_grayJpegQualitySpinBox->setValue(m_settings.grayProfile.jpegQuality);

    setCurrentEnum(m_bitonalAlgorithmComboBox, m_settings.bitonalProfile.algorithm, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    m_bitonalDpiSpinBox->setValue(m_settings.bitonalProfile.targetDpi);
    setCurrentEnum(m_bitonalResampleComboBox, m_settings.bitonalProfile.resampleFilter, pdf::PDFImage::ResampleFilter::Bicubic);
}

void ImageOptimizationSettingsDialog::applyUiToSettings()
{
    m_settings.autoMode = m_modeComboBox->currentIndex() == 0;
    m_settings.colorMode = currentEnum(m_colorModeComboBox, pdf::PDFImageOptimizer::ColorMode::Auto);
    m_settings.goal = currentEnum(m_goalComboBox, pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality);
    m_settings.keepOriginalIfLarger = m_keepOriginalCheckBox->isChecked();
    m_settings.preserveTransparency = m_preserveTransparencyCheckBox->isChecked();

    m_settings.colorProfile.algorithm = currentEnum(m_colorAlgorithmComboBox, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    m_settings.colorProfile.targetDpi = m_colorDpiSpinBox->value();
    m_settings.colorProfile.resampleFilter = currentEnum(m_colorResampleComboBox, pdf::PDFImage::ResampleFilter::Bicubic);
    m_settings.colorProfile.jpegQuality = m_colorJpegQualitySpinBox->value();

    m_settings.grayProfile.algorithm = currentEnum(m_grayAlgorithmComboBox, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    m_settings.grayProfile.targetDpi = m_grayDpiSpinBox->value();
    m_settings.grayProfile.resampleFilter = currentEnum(m_grayResampleComboBox, pdf::PDFImage::ResampleFilter::Bicubic);
    m_settings.grayProfile.jpegQuality = m_grayJpegQualitySpinBox->value();

    m_settings.bitonalProfile.algorithm = currentEnum(m_bitonalAlgorithmComboBox, pdf::PDFImageOptimizer::CompressionAlgorithm::Auto);
    m_settings.bitonalProfile.targetDpi = m_bitonalDpiSpinBox->value();
    m_settings.bitonalProfile.resampleFilter = currentEnum(m_bitonalResampleComboBox, pdf::PDFImage::ResampleFilter::Bicubic);
}

}   // namespace pdfpagemaster
