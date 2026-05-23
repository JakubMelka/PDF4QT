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

#include "pdfpageimportdialog.h"
#include "ui_pdfpageimportdialog.h"

#include "pdfwidgetutils.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>

#include <algorithm>

namespace pdfpagemaster
{

PDFPageImportDialog::PDFPageImportDialog(QString fileName, pdf::PDFInteger pageCount, QWidget* parent) :
    QDialog(parent),
    ui(new Ui::PDFPageImportDialog),
    m_fileName(qMove(fileName)),
    m_pageCount(pageCount),
    m_okButton(nullptr)
{
    ui->setupUi(this);

    QFileInfo fileInfo(m_fileName);
    ui->sourceFileValueLabel->setText(fileInfo.fileName());
    ui->sourceFileValueLabel->setToolTip(fileInfo.absoluteFilePath());
    ui->documentPagesValueLabel->setText(QString::number(m_pageCount));
    ui->pageRangeEdit->setText(QString("1-%1").arg(m_pageCount));
    m_okButton = ui->buttonBox->button(QDialogButtonBox::Ok);

    connect(ui->allPagesCheckBox, &QCheckBox::toggled, this, [this](bool checked)
    {
        ui->pageRangeEdit->setEnabled(!checked);
        updateState();
    });
    connect(ui->pageRangeEdit, &QLineEdit::textChanged, this, &PDFPageImportDialog::updateState);

    pdf::PDFWidgetUtils::scaleWidget(this, QSize(520, 220));
    pdf::PDFWidgetUtils::style(this);
    updateState();
}

PDFPageImportDialog::~PDFPageImportDialog()
{
    delete ui;
}

std::vector<pdf::PDFInteger> PDFPageImportDialog::parsePageRangeText(QString text, pdf::PDFInteger pageCount, QString* errorMessage)
{
    std::vector<pdf::PDFInteger> result;
    text = text.trimmed();

    auto appendPage = [&result](pdf::PDFInteger page)
    {
        if (std::find(result.cbegin(), result.cend(), page) == result.cend())
        {
            result.push_back(page);
        }
    };

    auto appendRange = [&appendPage](pdf::PDFInteger first, pdf::PDFInteger last)
    {
        for (pdf::PDFInteger page = first; page <= last; ++page)
        {
            appendPage(page);
        }
    };

    if (text.isEmpty())
    {
        appendRange(1, pageCount);
        return result;
    }

    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    for (QString part : parts)
    {
        part = part.trimmed();
        if (part.isEmpty())
        {
            continue;
        }

        if (part.compare(QStringLiteral("all"), Qt::CaseInsensitive) == 0)
        {
            appendRange(1, pageCount);
            continue;
        }

        if (part.compare(QStringLiteral("odd"), Qt::CaseInsensitive) == 0)
        {
            for (pdf::PDFInteger page = 1; page <= pageCount; page += 2)
            {
                appendPage(page);
            }
            continue;
        }

        if (part.compare(QStringLiteral("even"), Qt::CaseInsensitive) == 0)
        {
            for (pdf::PDFInteger page = 2; page <= pageCount; page += 2)
            {
                appendPage(page);
            }
            continue;
        }

        const int dashIndex = part.indexOf('-');
        bool okStart = false;
        bool okEnd = false;

        if (dashIndex == -1)
        {
            const pdf::PDFInteger page = part.toLongLong(&okStart);
            if (!okStart || page < 1 || page > pageCount)
            {
                if (errorMessage)
                {
                    *errorMessage = tr("Invalid page number '%1'.").arg(part);
                }
                return {};
            }
            appendPage(page);
            continue;
        }

        const QString startText = part.left(dashIndex).trimmed();
        const QString endText = part.mid(dashIndex + 1).trimmed();
        const pdf::PDFInteger start = startText.isEmpty() ? 1 : startText.toLongLong(&okStart);
        const pdf::PDFInteger end = endText.isEmpty() ? pageCount : endText.toLongLong(&okEnd);

        if ((startText.isEmpty() || okStart) && (endText.isEmpty() || okEnd) && start >= 1 && end <= pageCount && start <= end)
        {
            appendRange(start, end);
        }
        else
        {
            if (errorMessage)
            {
                *errorMessage = tr("Invalid page range '%1'.").arg(part);
            }
            return {};
        }
    }

    if (result.empty() && errorMessage)
    {
        *errorMessage = tr("No pages selected.");
    }

    return result;
}

void PDFPageImportDialog::updateState()
{
    QString errorMessage;
    if (ui->allPagesCheckBox->isChecked())
    {
        m_pages = parsePageRangeText(QStringLiteral("all"), m_pageCount, &errorMessage);
    }
    else
    {
        m_pages = parsePageRangeText(ui->pageRangeEdit->text(), m_pageCount, &errorMessage);
    }

    const bool isValid = !m_pages.empty();
    m_okButton->setEnabled(isValid);
    ui->errorLabel->setText(isValid ? QString() : errorMessage);
    ui->errorLabel->setStyleSheet(isValid ? QString() : QStringLiteral("color: #B00020;"));

    if (isValid)
    {
        ui->previewLabel->setText(tr("%1 of %2 pages selected.")
                                .arg(m_pages.size())
                                .arg(m_pageCount));
    }
    else
    {
        ui->previewLabel->setText(tr("No valid pages selected."));
    }
}

}   // namespace pdfpagemaster
