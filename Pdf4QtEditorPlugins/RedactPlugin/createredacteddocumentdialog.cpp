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

#include "createredacteddocumentdialog.h"
#include "ui_createredacteddocumentdialog.h"

#include <QFileDialog>
#include <QMessageBox>

#include "pdfwidgetutils.h"

namespace pdfplugin
{

CreateRedactedDocumentDialog::CreateRedactedDocumentDialog(QString fileName, QColor fillColor, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::CreateRedactedDocumentDialog)
{
    ui->setupUi(this);
    ui->fileNameEdit->setText(fileName);
    ui->fillRedactedAreaColorEdit->setText(fillColor.name(QColor::HexRgb));

    connect(ui->copyMetadataCheckBox, &QCheckBox::clicked, this, &CreateRedactedDocumentDialog::updateUi);

    updateUi();
    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 300));
    pdf::PDFWidgetUtils::style(this);
}

CreateRedactedDocumentDialog::~CreateRedactedDocumentDialog()
{
    delete ui;
}

QString CreateRedactedDocumentDialog::getFileName() const
{
    return ui->fileNameEdit->text();
}

QColor CreateRedactedDocumentDialog::getRedactColor() const
{
    QColor color;

    if (ui->fillRedactedAreaCheckBox->isChecked())
    {
        color = QColor::fromString(ui->fillRedactedAreaColorEdit->text());
    }

    return color;
}

bool CreateRedactedDocumentDialog::isCopyingTitle() const
{
    return ui->copyTitleCheckBox->isChecked();
}

bool CreateRedactedDocumentDialog::isCopyingMetadata() const
{
    return ui->copyMetadataCheckBox->isChecked();
}

bool CreateRedactedDocumentDialog::isCopyingOutline() const
{
    return ui->copyOutlineCheckBox->isChecked();
}

void CreateRedactedDocumentDialog::on_selectDirectoryButton_clicked()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("File Name"), ui->fileNameEdit->text());
    if (!fileName.isEmpty())
    {
        ui->fileNameEdit->setText(fileName);
    }
}

void CreateRedactedDocumentDialog::updateUi()
{
    if (ui->copyMetadataCheckBox->isChecked())
    {
        ui->copyTitleCheckBox->setChecked(true);
        ui->copyTitleCheckBox->setEnabled(false);
    }
    else
    {
        ui->copyTitleCheckBox->setEnabled(true);
    }

    ui->fillRedactedAreaColorEdit->setEnabled(ui->fillRedactedAreaCheckBox->isChecked());
}

void CreateRedactedDocumentDialog::accept()
{
    if (ui->fillRedactedAreaCheckBox->isChecked())
    {
        QColor color = QColor::fromString(ui->fillRedactedAreaColorEdit->text());

        if (!color.isValid())
        {
            QMessageBox::critical(this, tr("Error"), tr("Cannot convert '%1' to color value.").arg(ui->fillRedactedAreaColorEdit->text()));
            return;
        }
    }

    QDialog::accept();
}

} // namespace pdfplugin


