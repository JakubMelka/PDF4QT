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

#include "pdfwidgetutils.h"

#include <QFileDialog>

namespace pdfpagemaster
{

AssembleOutputSettingsDialog::AssembleOutputSettingsDialog(QString directory, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::AssembleOutputSettingsDialog)
{
    ui->setupUi(this);
    ui->directoryEdit->setText(directory);

    ui->outlineModeComboBox->addItem(tr("No Outline"), int(pdf::PDFDocumentManipulator::OutlineMode::NoOutline));
    ui->outlineModeComboBox->addItem(tr("Join Outlines"), int(pdf::PDFDocumentManipulator::OutlineMode::Join));
    ui->outlineModeComboBox->addItem(tr("Document Parts"), int(pdf::PDFDocumentManipulator::OutlineMode::DocumentParts));
    ui->outlineModeComboBox->setCurrentIndex(ui->outlineModeComboBox->findData(int(pdf::PDFDocumentManipulator::OutlineMode::DocumentParts)));

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(450, 150));
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

void AssembleOutputSettingsDialog::on_selectDirectoryButton_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(this, tr("Select output directory"), ui->directoryEdit->text());
    if (!directory.isEmpty())
    {
        ui->directoryEdit->setText(directory);
    }
}

}   // namespace pdfpagemaster
