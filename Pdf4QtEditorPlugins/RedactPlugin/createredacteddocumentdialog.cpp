//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

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


