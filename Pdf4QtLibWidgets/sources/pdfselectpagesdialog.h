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

#ifndef PDFSELECTPAGESDIALOG_H
#define PDFSELECTPAGESDIALOG_H

#include "pdfwidgetsglobal.h"
#include "pdfutils.h"

#include <QDialog>

namespace Ui
{
class PDFSelectPagesDialog;
}

namespace pdf
{

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFSelectPagesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PDFSelectPagesDialog(QString windowTitle,
                                  QString groupBoxTitle,
                                  pdf::PDFInteger pageCount,
                                  const std::vector<pdf::PDFInteger>& visiblePages,
                                  QWidget* parent);

    virtual ~PDFSelectPagesDialog() override;

    virtual void accept() override;

    std::vector<pdf::PDFInteger> getSelectedPages() const;

private:
    void updateUi();

    Ui::PDFSelectPagesDialog* ui;
    pdf::PDFInteger m_pageCount;
    std::vector<pdf::PDFInteger> m_visiblePages;
    std::vector<pdf::PDFInteger> m_evenPages;
    std::vector<pdf::PDFInteger> m_oddPages;
};

}   // namespace pdf

#endif // PDFSELECTPAGESDIALOG_H
