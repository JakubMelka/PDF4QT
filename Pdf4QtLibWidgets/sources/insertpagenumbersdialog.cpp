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

#include "insertpagenumbersdialog.h"
#include "ui_insertpagenumbersdialog.h"

#include "pdfpagerangewidget.h"
#include "pdfwidgetutils.h"
#include "pdfdbgheap.h"

#include <QColorDialog>
#include <QMessageBox>
#include <QPixmap>

namespace pdf
{

InsertPageNumbersDialog::InsertPageNumbersDialog(pdf::PDFInteger pageCount,
                                                 const std::vector<pdf::PDFInteger>& visiblePages,
                                                 QWidget* parent) :
    QDialog(parent),
    ui(new Ui::InsertPageNumbersDialog),
    m_pageRangeWidget(nullptr),
    m_pageCount(pageCount),
    m_color(Qt::black)
{
    ui->setupUi(this);

    ui->numberingStyleCombo->addItem(tr("Arabic numerals (1, 2, 3, ...)"), int(pdf::PDFPageLabel::NumberingStyle::DecimalArabic));
    ui->numberingStyleCombo->addItem(tr("Uppercase roman numerals (I, II, III, ...)"), int(pdf::PDFPageLabel::NumberingStyle::UppercaseRoman));
    ui->numberingStyleCombo->addItem(tr("Lowercase roman numerals (i, ii, iii, ...)"), int(pdf::PDFPageLabel::NumberingStyle::LowercaseRoman));
    ui->numberingStyleCombo->addItem(tr("Uppercase letters (A, B, C, ...)"), int(pdf::PDFPageLabel::NumberingStyle::UppercaseLetters));
    ui->numberingStyleCombo->addItem(tr("Lowercase letters (a, b, c, ...)"), int(pdf::PDFPageLabel::NumberingStyle::LowercaseLetters));

    ui->formatCombo->addItem("%1");
    ui->formatCombo->addItem("%1 / %2");
    ui->formatCombo->addItem(tr("Page %1"));
    ui->formatCombo->addItem(tr("Page %1 of %2"));
    ui->formatCombo->setCurrentIndex(0);

    ui->alignmentCombo->addItem(tr("Left"), int(Qt::AlignLeft | Qt::AlignVCenter));
    ui->alignmentCombo->addItem(tr("Center"), int(Qt::AlignCenter));
    ui->alignmentCombo->addItem(tr("Right"), int(Qt::AlignRight | Qt::AlignVCenter));
    ui->alignmentCombo->setCurrentIndex(1);

    m_pageRangeWidget = new PDFPageRangeWidget(tr("Pages to Number"), pageCount, visiblePages, this);
    qobject_cast<QBoxLayout*>(layout())->insertWidget(layout()->count() - 1, m_pageRangeWidget);

    setColor(m_color);

    connect(ui->numberingStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InsertPageNumbersDialog::updatePreview);
    connect(ui->formatCombo, &QComboBox::editTextChanged, this, &InsertPageNumbersDialog::updatePreview);
    connect(ui->startNumberSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &InsertPageNumbersDialog::updatePreview);
    connect(ui->selectColorButton, &QToolButton::clicked, this, &InsertPageNumbersDialog::onSelectColorButtonClicked);

    updatePreview();

    setMinimumWidth(pdf::PDFWidgetUtils::scaleDPI_x(this, 420));
    pdf::PDFWidgetUtils::style(this);
}

InsertPageNumbersDialog::~InsertPageNumbersDialog()
{
    delete ui;
}

pdf::PDFPageLabel::NumberingStyle InsertPageNumbersDialog::getNumberingStyle() const
{
    return static_cast<pdf::PDFPageLabel::NumberingStyle>(ui->numberingStyleCombo->currentData().toInt());
}

QString InsertPageNumbersDialog::getFormatPattern() const
{
    return ui->formatCombo->currentText();
}

int InsertPageNumbersDialog::getStartNumber() const
{
    return ui->startNumberSpinBox->value();
}

QFont InsertPageNumbersDialog::getFont() const
{
    QFont font = ui->fontComboBox->currentFont();
    font.setPointSize(ui->fontSizeSpinBox->value());
    return font;
}

QColor InsertPageNumbersDialog::getColor() const
{
    return m_color;
}

Qt::Alignment InsertPageNumbersDialog::getAlignment() const
{
    return static_cast<Qt::Alignment>(ui->alignmentCombo->currentData().toInt());
}

std::vector<pdf::PDFInteger> InsertPageNumbersDialog::getSelectedPages() const
{
    return m_pageRangeWidget->getSelectedPages();
}

void InsertPageNumbersDialog::accept()
{
    QString errorMessage;
    if (!m_pageRangeWidget->validate(&errorMessage))
    {
        QMessageBox::critical(this, tr("Error"), errorMessage);
        return;
    }

    QDialog::accept();
}

void InsertPageNumbersDialog::updatePreview()
{
    const QString numberText = pdf::PDFPageLabel::formatPageNumber(getNumberingStyle(), getStartNumber());
    const QString totalText = QString::number(m_pageCount);
    const QString formatted = QString(getFormatPattern()).arg(numberText).arg(totalText);

    ui->previewLabel->setText(formatted);
}

void InsertPageNumbersDialog::onSelectColorButtonClicked()
{
    QColor color = QColorDialog::getColor(m_color, this, tr("Select Text Color"));
    setColor(color);
}

void InsertPageNumbersDialog::setColor(QColor color)
{
    if (!color.isValid())
    {
        return;
    }

    m_color = color;

    QSize iconSize = pdf::PDFWidgetUtils::scaleDPI(this, QSize(16, 16));
    QPixmap pixmap(iconSize.width(), iconSize.height());
    pixmap.fill(m_color);
    ui->selectColorButton->setIcon(QIcon(pixmap));
}

}   // namespace pdf
