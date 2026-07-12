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

#include "pdfpagerangewidget.h"
#include "ui_pdfpagerangewidget.h"

#include "pdfdbgheap.h"

#include <numeric>

namespace pdf
{

PDFPageRangeWidget::PDFPageRangeWidget(QString groupBoxTitle,
                                       pdf::PDFInteger pageCount,
                                       const std::vector<pdf::PDFInteger>& visiblePages,
                                       QWidget* parent) :
    QGroupBox(parent),
    ui(new Ui::PDFPageRangeWidget),
    m_pageCount(pageCount),
    m_visiblePages(visiblePages)
{
    ui->setupUi(this);

    setTitle(groupBoxTitle);

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

    connect(ui->customPageRangeRadioButton, &QRadioButton::toggled, this, &PDFPageRangeWidget::updateUi);

    updateUi();
}

PDFPageRangeWidget::~PDFPageRangeWidget()
{
    delete ui;
}

std::vector<pdf::PDFInteger> PDFPageRangeWidget::getSelectedPages() const
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

bool PDFPageRangeWidget::validate(QString* errorMessage) const
{
    if (ui->customPageRangeRadioButton->isChecked())
    {
        QString parseErrorMessage;
        pdf::PDFClosedIntervalSet::parse(1, m_pageCount, ui->customPageRangeEdit->text(), &parseErrorMessage);
        if (!parseErrorMessage.isEmpty())
        {
            *errorMessage = parseErrorMessage;
            return false;
        }
    }

    if (getSelectedPages().empty())
    {
        *errorMessage = tr("Selected page range is empty.");
        return false;
    }

    return true;
}

void PDFPageRangeWidget::updateUi()
{
    ui->customPageRangeEdit->setEnabled(ui->customPageRangeRadioButton->isChecked());
}

} // namespace pdf
