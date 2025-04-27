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
