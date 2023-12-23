//    Copyright (C) 2023 Jakub Melka
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

#include "pdfcertificatelisthelper.h"
#include "pdfwidgetutils.h"

#include <QComboBox>
#include <QTableView>
#include <QStandardItemModel>
#include <QHeaderView>

namespace pdf
{

void PDFCertificateListHelper::initComboBox(QComboBox* comboBox)
{
    QStandardItemModel* model = new QStandardItemModel(comboBox);
    model->setColumnCount(COLUMN_COUNT);
    model->setRowCount(0);
    comboBox->setModel(model);

    QTableView* tableView = new QTableView(comboBox);

    tableView->verticalHeader()->setVisible(false);
    tableView->horizontalHeader()->setVisible(false);

    tableView->setVerticalScrollMode(QTableView::ScrollPerPixel);
    tableView->setAlternatingRowColors(true);
    tableView->setAutoScroll(false);
    tableView->setShowGrid(false);
    tableView->setSortingEnabled(false);
    tableView->setColumnWidth(0, PDFWidgetUtils::scaleDPI_x(tableView, 150));
    tableView->setColumnWidth(1, PDFWidgetUtils::scaleDPI_x(tableView, 150));
    tableView->setColumnWidth(2, PDFWidgetUtils::scaleDPI_x(tableView, 75));
    tableView->setColumnWidth(3, PDFWidgetUtils::scaleDPI_x(tableView, 75));
    tableView->setSelectionBehavior(QAbstractItemView::SelectRows);

    comboBox->setView(tableView);
}

void PDFCertificateListHelper::fillComboBox(QComboBox* comboBox, const PDFCertificateEntries& entries)
{
    QString currentText = comboBox->currentText();

    const bool updatesEnabled = comboBox->updatesEnabled();
    comboBox->setUpdatesEnabled(false);

    QStandardItemModel* model = qobject_cast<QStandardItemModel*>(comboBox->model());
    model->setRowCount(int(entries.size()));
    for (int i = 0; i < entries.size(); ++i)
    {
        const PDFCertificateEntry& entry = entries[i];

        QString secondInfoEntry = entry.info.getName(PDFCertificateInfo::NameEntry::OrganizationName);
        if (secondInfoEntry.isEmpty())
        {
            secondInfoEntry = entry.info.getName(PDFCertificateInfo::NameEntry::Email);
        }

        if (entry.pkcs12fileName.isEmpty())
        {
            model->setItem(i, 0, new QStandardItem(entry.info.getName(PDFCertificateInfo::NameEntry::CommonName)));
            model->setItem(i, 1, new QStandardItem(secondInfoEntry));
            model->setItem(i, 2, new QStandardItem(entry.info.getNotValidBefore().toLocalTime().toString()));
            model->setItem(i, 3, new QStandardItem(entry.info.getNotValidAfter().toLocalTime().toString()));
        }
        else
        {
            model->setItem(i, 0, new QStandardItem(entry.pkcs12fileName));
            model->setItem(i, 1, new QStandardItem(tr("Password protected")));
            model->setItem(i, 2, new QStandardItem(QString()));
            model->setItem(i, 3, new QStandardItem(QString()));
        }
    }

    comboBox->setUpdatesEnabled(updatesEnabled);

    int newCurrentIndex = comboBox->findText(currentText);

    if (newCurrentIndex < 0)
    {
        newCurrentIndex = 0;
    }

    comboBox->setCurrentIndex(newCurrentIndex);

    QTableView* tableView = qobject_cast<QTableView*>(comboBox->view());
    tableView->resizeColumnsToContents();
}

}   // namespace pdf
