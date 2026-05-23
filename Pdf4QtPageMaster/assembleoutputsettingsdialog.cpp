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

#include "assembleoutputsettingsdialog.h"
#include "ui_assembleoutputsettingsdialog.h"

#include "imageoptimizationsettingsdialog.h"
#include "pdfwidgetutils.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

namespace pdfpagemaster
{

class AssembleOutputSettingsHelper final
{
public:
    template<typename Enum>
    static Enum readEnumValue(QSettings& settings, const QString& key, Enum fallback, int minimum, int maximum)
    {
        const int value = settings.value(key, int(fallback)).toInt();
        return (value >= minimum && value <= maximum) ? Enum(value) : fallback;
    }

    static void loadCompressionProfile(QSettings& settings, const QString& groupName, pdf::PDFImageOptimizer::CompressionProfile* profile)
    {
        settings.beginGroup(groupName);
        profile->algorithm = readEnumValue(settings, "algorithm", profile->algorithm, int(pdf::PDFImageOptimizer::CompressionAlgorithm::Auto), int(pdf::PDFImageOptimizer::CompressionAlgorithm::JBIG2));
        profile->targetDpi = qMax(0, settings.value("targetDpi", profile->targetDpi).toInt());
        profile->resampleFilter = readEnumValue(settings, "resampleFilter", profile->resampleFilter, int(pdf::PDFImage::ResampleFilter::Nearest), int(pdf::PDFImage::ResampleFilter::Lanczos));
        profile->jpegQuality = qBound(0, settings.value("jpegQuality", profile->jpegQuality).toInt(), 100);
        profile->jpeg2000Rate = qMax(0.0f, settings.value("jpeg2000Rate", profile->jpeg2000Rate).toFloat());
        profile->monochromeThreshold = qBound(-1, settings.value("monochromeThreshold", profile->monochromeThreshold).toInt(), 255);
        profile->enablePngPredictor = settings.value("enablePngPredictor", profile->enablePngPredictor).toBool();
        settings.endGroup();
    }

    static void saveCompressionProfile(QSettings& settings, const QString& groupName, const pdf::PDFImageOptimizer::CompressionProfile& profile)
    {
        settings.beginGroup(groupName);
        settings.setValue("algorithm", int(profile.algorithm));
        settings.setValue("targetDpi", profile.targetDpi);
        settings.setValue("resampleFilter", int(profile.resampleFilter));
        settings.setValue("jpegQuality", profile.jpegQuality);
        settings.setValue("jpeg2000Rate", profile.jpeg2000Rate);
        settings.setValue("monochromeThreshold", profile.monochromeThreshold);
        settings.setValue("enablePngPredictor", profile.enablePngPredictor);
        settings.endGroup();
    }
};

AssembleOutputSettingsDialog::AssembleOutputSettingsDialog(QString directory, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::AssembleOutputSettingsDialog),
    m_previewTable(new QTableWidget(this)),
    m_imageOptimizationSettings(pdf::PDFImageOptimizer::Settings::createDefault())
{
    ui->setupUi(this);
    ui->directoryEdit->setText(directory);
    m_imageOptimizationSettings.enabled = true;

    ui->outlineModeComboBox->addItem(tr("No Outline"), int(pdf::PDFDocumentManipulator::OutlineMode::NoOutline));
    ui->outlineModeComboBox->addItem(tr("Join Outlines"), int(pdf::PDFDocumentManipulator::OutlineMode::Join));
    ui->outlineModeComboBox->addItem(tr("Document Parts"), int(pdf::PDFDocumentManipulator::OutlineMode::DocumentParts));
    ui->outlineModeComboBox->setCurrentIndex(ui->outlineModeComboBox->findData(int(pdf::PDFDocumentManipulator::OutlineMode::DocumentParts)));

    ui->infoLabel->setText(tr("<html><body>"
                              "<p><b>File template placeholders</b></p>"
                              "<p># = output number, @ = source page, % = source document index. Repeat them to pad with zeroes, for example ###.</p>"
                              "<p>Named tokens: {source_name}, {source_base}, {source_ext}, {source_page}, {output_index}, {group_index}, {group_name}, {date}.</p>"
                              "<p>Examples: {source_base}.pdf, {group_name}.pdf, document-{output_index}.pdf, {date}-{source_base}.pdf</p>"
                              "</body></html>"));
    loadSettings();

    m_previewTable->setColumnCount(5);
    m_previewTable->setHorizontalHeaderLabels({ tr("Output file"), tr("Pages"), tr("First source"), tr("Mode"), tr("Status") });
    m_previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_previewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_previewTable->horizontalHeader()->setStretchLastSection(true);
    m_previewTable->verticalHeader()->hide();
    ui->verticalLayout->insertWidget(1, m_previewTable);
    setSizeGripEnabled(true);
    connect(ui->directoryEdit, &QLineEdit::textChanged, this, &AssembleOutputSettingsDialog::refreshOutputPreview);
    connect(ui->fileTemplateEdit, &QLineEdit::textChanged, this, &AssembleOutputSettingsDialog::refreshOutputPreview);
    connect(ui->overwriteFilesCheckBox, &QCheckBox::toggled, this, &AssembleOutputSettingsDialog::refreshOutputPreview);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(720, 420));
    pdf::PDFWidgetUtils::style(this);
}

AssembleOutputSettingsDialog::~AssembleOutputSettingsDialog()
{
    delete ui;
}

void AssembleOutputSettingsDialog::loadSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("AssembleOutputSettingsDialog");

    const QString savedDirectory = settings.value("directory").toString();
    if (!savedDirectory.isEmpty())
    {
        ui->directoryEdit->setText(savedDirectory);
    }
    if (settings.contains("fileTemplate"))
    {
        ui->fileTemplateEdit->setText(settings.value("fileTemplate").toString());
    }
    ui->overwriteFilesCheckBox->setChecked(settings.value("overwriteFiles", ui->overwriteFilesCheckBox->isChecked()).toBool());
    ui->optimizeImagesCheckBox->setChecked(settings.value("optimizeImages", ui->optimizeImagesCheckBox->isChecked()).toBool());

    const pdf::PDFDocumentManipulator::OutlineMode outlineMode = AssembleOutputSettingsHelper::readEnumValue(settings, "outlineMode", pdf::PDFDocumentManipulator::OutlineMode::DocumentParts, int(pdf::PDFDocumentManipulator::OutlineMode::NoOutline), int(pdf::PDFDocumentManipulator::OutlineMode::DocumentParts));
    const int outlineModeIndex = ui->outlineModeComboBox->findData(int(outlineMode));
    if (outlineModeIndex != -1)
    {
        ui->outlineModeComboBox->setCurrentIndex(outlineModeIndex);
    }

    settings.beginGroup("ImageOptimization");
    m_imageOptimizationSettings.enabled = ui->optimizeImagesCheckBox->isChecked();
    m_imageOptimizationSettings.autoMode = settings.value("autoMode", m_imageOptimizationSettings.autoMode).toBool();
    m_imageOptimizationSettings.colorMode = AssembleOutputSettingsHelper::readEnumValue(settings, "colorMode", m_imageOptimizationSettings.colorMode, int(pdf::PDFImageOptimizer::ColorMode::Auto), int(pdf::PDFImageOptimizer::ColorMode::Bitonal));
    m_imageOptimizationSettings.goal = AssembleOutputSettingsHelper::readEnumValue(settings, "goal", m_imageOptimizationSettings.goal, int(pdf::PDFImageOptimizer::OptimizationGoal::PreferQuality), int(pdf::PDFImageOptimizer::OptimizationGoal::MinimumSize));
    m_imageOptimizationSettings.keepOriginalIfLarger = settings.value("keepOriginalIfLarger", m_imageOptimizationSettings.keepOriginalIfLarger).toBool();
    m_imageOptimizationSettings.preserveTransparency = settings.value("preserveTransparency", m_imageOptimizationSettings.preserveTransparency).toBool();
    AssembleOutputSettingsHelper::loadCompressionProfile(settings, "ColorProfile", &m_imageOptimizationSettings.colorProfile);
    AssembleOutputSettingsHelper::loadCompressionProfile(settings, "GrayProfile", &m_imageOptimizationSettings.grayProfile);
    AssembleOutputSettingsHelper::loadCompressionProfile(settings, "BitonalProfile", &m_imageOptimizationSettings.bitonalProfile);
    settings.endGroup();

    settings.endGroup();
}

void AssembleOutputSettingsDialog::saveSettings() const
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("AssembleOutputSettingsDialog");
    settings.setValue("directory", ui->directoryEdit->text());
    settings.setValue("fileTemplate", ui->fileTemplateEdit->text());
    settings.setValue("overwriteFiles", ui->overwriteFilesCheckBox->isChecked());
    settings.setValue("optimizeImages", ui->optimizeImagesCheckBox->isChecked());
    settings.setValue("outlineMode", ui->outlineModeComboBox->currentData().toInt());

    const pdf::PDFImageOptimizer::Settings imageOptimizationSettings = getImageOptimizationSettings();
    settings.beginGroup("ImageOptimization");
    settings.setValue("enabled", imageOptimizationSettings.enabled);
    settings.setValue("autoMode", imageOptimizationSettings.autoMode);
    settings.setValue("colorMode", int(imageOptimizationSettings.colorMode));
    settings.setValue("goal", int(imageOptimizationSettings.goal));
    settings.setValue("keepOriginalIfLarger", imageOptimizationSettings.keepOriginalIfLarger);
    settings.setValue("preserveTransparency", imageOptimizationSettings.preserveTransparency);
    AssembleOutputSettingsHelper::saveCompressionProfile(settings, "ColorProfile", imageOptimizationSettings.colorProfile);
    AssembleOutputSettingsHelper::saveCompressionProfile(settings, "GrayProfile", imageOptimizationSettings.grayProfile);
    AssembleOutputSettingsHelper::saveCompressionProfile(settings, "BitonalProfile", imageOptimizationSettings.bitonalProfile);
    settings.endGroup();

    settings.endGroup();
}

QString AssembleOutputSettingsDialog::getDirectory() const
{
    return ui->directoryEdit->text();
}

QString AssembleOutputSettingsDialog::getFileName() const
{
    return ui->fileTemplateEdit->text();
}

bool AssembleOutputSettingsDialog::isOverwriteFiles() const
{
    return ui->overwriteFilesCheckBox->isChecked();
}

pdf::PDFDocumentManipulator::OutlineMode AssembleOutputSettingsDialog::getOutlineMode() const
{
    return pdf::PDFDocumentManipulator::OutlineMode(ui->outlineModeComboBox->currentData().toInt());
}

bool AssembleOutputSettingsDialog::isImageOptimizationEnabled() const
{
    return ui->optimizeImagesCheckBox->isChecked();
}

pdf::PDFImageOptimizer::Settings AssembleOutputSettingsDialog::getImageOptimizationSettings() const
{
    pdf::PDFImageOptimizer::Settings settings = m_imageOptimizationSettings;
    settings.enabled = isImageOptimizationEnabled();
    return settings;
}

void AssembleOutputSettingsDialog::setOutputPreview(const std::vector<OutputPreviewItem>& items)
{
    m_hasBlockingPreviewItems = items.empty();
    m_previewTable->setRowCount(int(items.size()));
    if (items.empty())
    {
        m_previewTable->setRowCount(1);
        m_previewTable->setItem(0, 4, new QTableWidgetItem(tr("Empty output")));
        m_previewTable->resizeColumnsToContents();
        return;
    }

    for (int row = 0; row < int(items.size()); ++row)
    {
        const OutputPreviewItem& item = items[row];
        m_previewTable->setItem(row, 0, new QTableWidgetItem(item.fileName));
        m_previewTable->setItem(row, 1, new QTableWidgetItem(item.pageCount));
        m_previewTable->setItem(row, 2, new QTableWidgetItem(item.firstSource));
        m_previewTable->setItem(row, 3, new QTableWidgetItem(item.mode));
        m_previewTable->setItem(row, 4, new QTableWidgetItem(item.status));
        m_hasBlockingPreviewItems = m_hasBlockingPreviewItems || item.isBlocking;
    }
    m_previewTable->resizeColumnsToContents();
}

void AssembleOutputSettingsDialog::setOutputPreviewFactory(std::function<std::vector<OutputPreviewItem>()> factory)
{
    m_outputPreviewFactory = qMove(factory);
    refreshOutputPreview();
}

void AssembleOutputSettingsDialog::accept()
{
    refreshOutputPreview();
    if (m_hasBlockingPreviewItems)
    {
        QMessageBox::warning(this, tr("Output Preview"), tr("The output preview contains errors. Please fix them before assembling documents."));
        return;
    }

    saveSettings();
    QDialog::accept();
}

void AssembleOutputSettingsDialog::on_selectDirectoryButton_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(this, tr("Select output directory"), ui->directoryEdit->text());
    if (!directory.isEmpty())
    {
        ui->directoryEdit->setText(directory);
    }
}

void AssembleOutputSettingsDialog::on_imageOptimizationSettingsButton_clicked()
{
    ImageOptimizationSettingsDialog dialog(m_imageOptimizationSettings, this);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_imageOptimizationSettings = dialog.getSettings();
        m_imageOptimizationSettings.enabled = true;
    }
}

void AssembleOutputSettingsDialog::refreshOutputPreview()
{
    if (m_outputPreviewFactory)
    {
        setOutputPreview(m_outputPreviewFactory());
    }
}

}   // namespace pdfpagemaster
