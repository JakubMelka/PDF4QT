//    Copyright (C) 2021 Jakub Melka
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

#include "aboutdialog.h"
#include "ui_aboutdialog.h"

#include "pdfutils.h"
#include "pdfwidgetutils.h"

namespace pdfdocdiff
{

PDFAboutDialog::PDFAboutDialog(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFAboutDialog)
{
    ui->setupUi(this);

    QString html = ui->copyrightLabel->text();
    html.replace("PdfForQtViewer", QApplication::applicationDisplayName());
    ui->copyrightLabel->setText(html);

    std::vector<pdf::PDFDependentLibraryInfo> infos = pdf::PDFDependentLibraryInfo::getLibraryInfo();

    ui->tableWidget->setColumnCount(4);
    ui->tableWidget->setRowCount(static_cast<int>(infos.size()));
    ui->tableWidget->setHorizontalHeaderLabels(QStringList() << tr("Library") << tr("Version") << tr("License") << tr("URL"));
    ui->tableWidget->setEditTriggers(QTableWidget::NoEditTriggers);
    ui->tableWidget->setSelectionMode(QTableView::SingleSelection);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    for (int i = 0; i < infos.size(); ++i)
    {
        const pdf::PDFDependentLibraryInfo& info = infos[i];
        ui->tableWidget->setItem(i, 0, new QTableWidgetItem(info.library));
        ui->tableWidget->setItem(i, 1, new QTableWidgetItem(info.version));
        ui->tableWidget->setItem(i, 2, new QTableWidgetItem(info.license));
        ui->tableWidget->setItem(i, 3, new QTableWidgetItem(info.url));
    }

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(750, 600));
    pdf::PDFWidgetUtils::style(this);
}

PDFAboutDialog::~PDFAboutDialog()
{
    delete ui;
}

}   // namespace pdfdocdiff
