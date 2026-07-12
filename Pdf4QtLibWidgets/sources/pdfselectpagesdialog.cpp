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

#include "pdfselectpagesdialog.h"
#include "ui_pdfselectpagesdialog.h"

#include "pdfpagerangewidget.h"
#include "pdfwidgetutils.h"
#include "pdfdbgheap.h"

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
    m_pageRangeWidget(nullptr)
{
    ui->setupUi(this);

    setWindowTitle(windowTitle);

    m_pageRangeWidget = new PDFPageRangeWidget(groupBoxTitle, pageCount, visiblePages, this);
    qobject_cast<QBoxLayout*>(layout())->insertWidget(0, m_pageRangeWidget);

    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 400));
    pdf::PDFWidgetUtils::style(this);
}

PDFSelectPagesDialog::~PDFSelectPagesDialog()
{
    delete ui;
}

std::vector<pdf::PDFInteger> PDFSelectPagesDialog::getSelectedPages() const
{
    return m_pageRangeWidget->getSelectedPages();
}

void PDFSelectPagesDialog::accept()
{
    QString errorMessage;
    if (!m_pageRangeWidget->validate(&errorMessage))
    {
        QMessageBox::critical(this, tr("Error"), errorMessage);
        return;
    }

    QDialog::accept();
}

} // namespace pdf
