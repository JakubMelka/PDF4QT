//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "assembleoutputsettingsdialog.h"
#include "ui_assembleoutputsettingsdialog.h"

#include "pdfwidgetutils.h"

#include <QFileDialog>

namespace pdfdocpage
{

AssembleOutputSettingsDialog::AssembleOutputSettingsDialog(QString directory, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::AssembleOutputSettingsDialog)
{
    ui->setupUi(this);
    ui->directoryEdit->setText(directory);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(450, 150));
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

void AssembleOutputSettingsDialog::on_selectDirectoryButton_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(this, tr("Select output directory"), ui->directoryEdit->text());
    if (!directory.isEmpty())
    {
        ui->directoryEdit->setText(directory);
    }
}

}   // namespace pdfdocpage
