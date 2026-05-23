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

#ifndef PDFPAGEMASTER_PDFPAGEIMPORTDIALOG_H
#define PDFPAGEMASTER_PDFPAGEIMPORTDIALOG_H

#include "pdfglobal.h"

#include <QDialog>

#include <vector>

namespace Ui
{
class PDFPageImportDialog;
}

class QPushButton;

namespace pdfpagemaster
{

class PDFPageImportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFPageImportDialog(QString fileName, pdf::PDFInteger pageCount, QWidget* parent);
    virtual ~PDFPageImportDialog() override;

    std::vector<pdf::PDFInteger> getPages() const { return m_pages; }
    static std::vector<pdf::PDFInteger> parsePageRangeText(QString text, pdf::PDFInteger pageCount, QString* errorMessage);

private slots:
    void updateState();

private:
    Ui::PDFPageImportDialog* ui;
    QString m_fileName;
    pdf::PDFInteger m_pageCount;
    std::vector<pdf::PDFInteger> m_pages;
    QPushButton* m_okButton;
};

}   // namespace pdfpagemaster

#endif // PDFPAGEMASTER_PDFPAGEIMPORTDIALOG_H
