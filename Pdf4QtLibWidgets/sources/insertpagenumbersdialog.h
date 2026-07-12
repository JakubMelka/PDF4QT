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

#ifndef INSERTPAGENUMBERSDIALOG_H
#define INSERTPAGENUMBERSDIALOG_H

#include "pdfwidgetsglobal.h"
#include "pdfutils.h"
#include "pdfcatalog.h"

#include <QDialog>
#include <QColor>

namespace Ui
{
class InsertPageNumbersDialog;
}

namespace pdf
{

class PDFPageRangeWidget;

/// Dialog for configuring how page numbers should be stamped into the document:
/// numbering style (arabic, roman, letters), text format pattern, starting number,
/// font/color/alignment of the stamped text, and the range of pages to number.
class PDF4QTLIBWIDGETSSHARED_EXPORT InsertPageNumbersDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InsertPageNumbersDialog(pdf::PDFInteger pageCount,
                                     const std::vector<pdf::PDFInteger>& visiblePages,
                                     QWidget* parent);

    virtual ~InsertPageNumbersDialog() override;

    virtual void accept() override;

    pdf::PDFPageLabel::NumberingStyle getNumberingStyle() const;
    QString getFormatPattern() const;
    int getStartNumber() const;
    QFont getFont() const;
    QColor getColor() const;
    Qt::Alignment getAlignment() const;
    std::vector<pdf::PDFInteger> getSelectedPages() const;

private:
    void updatePreview();
    void onSelectColorButtonClicked();
    void setColor(QColor color);

    Ui::InsertPageNumbersDialog* ui;
    PDFPageRangeWidget* m_pageRangeWidget;
    pdf::PDFInteger m_pageCount;
    QColor m_color;
};

}   // namespace pdf

#endif // INSERTPAGENUMBERSDIALOG_H
