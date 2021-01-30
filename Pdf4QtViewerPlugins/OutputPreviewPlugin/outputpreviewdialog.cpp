//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "outputpreviewdialog.h"
#include "ui_outputpreviewdialog.h"

namespace pdfplugin
{

OutputPreviewDialog::OutputPreviewDialog(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::OutputPreviewDialog)
{
    ui->setupUi(this);
}

OutputPreviewDialog::~OutputPreviewDialog()
{
    delete ui;
}

} // namespace pdfplugin
