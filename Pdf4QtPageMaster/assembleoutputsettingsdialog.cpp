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
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace pdfpagemaster
{

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
                              "<p>Named tokens: {source_name}, {source_base}, {source_ext}, {source_page}, {output_index}, {group_index}, {date}.</p>"
                              "<p>Examples: {source_base}.pdf, document-{output_index}.pdf, {date}-{source_base}.pdf</p>"
                              "</body></html>"));

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
