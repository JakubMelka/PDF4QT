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

#ifndef PDFPAGERANGEWIDGET_H
#define PDFPAGERANGEWIDGET_H

#include "pdfwidgetsglobal.h"
#include "pdfutils.h"

#include <QGroupBox>

namespace Ui
{
class PDFPageRangeWidget;
}

namespace pdf
{

/// Widget for selecting a range of pages (visible pages, all pages, even/odd pages,
/// or a custom range such as "1-5,8,10-12"). Used by \c PDFSelectPagesDialog and by
/// other dialogs which need to let the user pick a page range.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageRangeWidget : public QGroupBox
{
    Q_OBJECT

public:
    explicit PDFPageRangeWidget(QString groupBoxTitle,
                                pdf::PDFInteger pageCount,
                                const std::vector<pdf::PDFInteger>& visiblePages,
                                QWidget* parent);

    virtual ~PDFPageRangeWidget() override;

    std::vector<pdf::PDFInteger> getSelectedPages() const;

    /// Validates the current selection (e.g. parses the custom page range).
    /// Returns true if the selection is valid, false otherwise and fills
    /// \p errorMessage with a human readable error message.
    bool validate(QString* errorMessage) const;

private:
    void updateUi();

    Ui::PDFPageRangeWidget* ui;
    pdf::PDFInteger m_pageCount;
    std::vector<pdf::PDFInteger> m_visiblePages;
    std::vector<pdf::PDFInteger> m_evenPages;
    std::vector<pdf::PDFInteger> m_oddPages;
};

}   // namespace pdf

#endif // PDFPAGERANGEWIDGET_H
