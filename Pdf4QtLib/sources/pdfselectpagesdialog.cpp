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

#include "pdfselectpagesdialog.h"
#include "ui_pdfselectpagesdialog.h"

#include "pdfwidgetutils.h"

#include <QMessageBox>

namespace pdf
{

PDFSelectPagesDialog::PDFSelectPagesDialog(QString windowTitle,
                                           QString groupBoxTitle,
                                           pdf::PDFInteger pageCount,
                                           const std::vector<pdf::PDFInteger>& visiblePages,
                                           QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFSelectPagesDialog),
    m_pageCount(pageCount),
    m_visiblePages(visiblePages)
{
    ui->setupUi(this);

    setWindowTitle(windowTitle);
    ui->selectPagesGroupBox->setTitle(groupBoxTitle);

    for (pdf::PDFInteger& pageIndex : m_visiblePages)
    {
        ++pageIndex;
    }

    m_evenPages.reserve(pageCount / 2);
    m_oddPages.reserve(pageCount / 2 + 1);

    for (pdf::PDFInteger i = 1; i <= pageCount; ++i)
    {
        if (i % 2 == 1)
        {
            m_oddPages.push_back(i);
        }
        else
        {
            m_evenPages.push_back(i);
        }
    }

    connect(ui->customPageRangeRadioButton, &QRadioButton::toggled, this, &PDFSelectPagesDialog::updateUi);

    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 400));
    updateUi();
    pdf::PDFWidgetUtils::style(this);
}

PDFSelectPagesDialog::~PDFSelectPagesDialog()
{
    delete ui;
}

std::vector<pdf::PDFInteger> PDFSelectPagesDialog::getSelectedPages() const
{
    std::vector<pdf::PDFInteger> result;

    if (ui->visiblePagesRadioButton->isChecked())
    {
        result = m_visiblePages;
    }
    else if (ui->allPagesRadioButton->isChecked())
    {
        result.resize(m_pageCount, 0);
        std::iota(result.begin(), result.end(), 1);
    }
    else if (ui->evenPagesRadioButton->isChecked())
    {
        result = m_evenPages;
    }
    else if (ui->oddPagesRadioButton->isChecked())
    {
        result = m_oddPages;
    }
    else if (ui->customPageRangeRadioButton->isChecked())
    {
        QString errorMessage;
        result = pdf::PDFClosedIntervalSet::parse(1, m_pageCount, ui->customPageRangeEdit->text(), &errorMessage).unfold();
    }
    else
    {
        Q_ASSERT(false);
    }

    return result;
}

void PDFSelectPagesDialog::updateUi()
{
    ui->customPageRangeEdit->setEnabled(ui->customPageRangeRadioButton->isChecked());
}

void PDFSelectPagesDialog::accept()
{
    if (ui->customPageRangeRadioButton->isChecked())
    {
        QString errorMessage;
        pdf::PDFClosedIntervalSet::parse(1, m_pageCount, ui->customPageRangeEdit->text(), &errorMessage);
        if (!errorMessage.isEmpty())
        {
            QMessageBox::critical(this, tr("Error"), errorMessage);
            return;
        }
    }

    if (getSelectedPages().empty())
    {
        QMessageBox::critical(this, tr("Error"), tr("Selected page range is empty."));
        return;
    }

    QDialog::accept();
}

} // namespace pdf
