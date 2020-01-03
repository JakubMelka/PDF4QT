//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFADVANCEDFINDWIDGET_H
#define PDFADVANCEDFINDWIDGET_H

#include "pdfglobal.h"

#include <QWidget>

namespace Ui
{
class PDFAdvancedFindWidget;
}

namespace pdf
{
class PDFDocument;
class PDFDrawWidgetProxy;
}

namespace pdfviewer
{

class PDFAdvancedFindWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PDFAdvancedFindWidget(pdf::PDFDrawWidgetProxy* proxy, QWidget* parent = nullptr);
    virtual ~PDFAdvancedFindWidget() override;

    void setDocument(const pdf::PDFDocument* document);

private slots:
    void on_searchButton_clicked();

private:
    void updateUI();
    void performSearch();

    struct SearchParameters
    {
        QString phrase;
        bool isCaseSensitive = false;
        bool isWholeWordsOnly = false;
        bool isRegularExpression = false;
        bool isDotMatchingEverything = false;
        bool isMultiline = false;
        bool isSearchFinished = false;
    };

    Ui::PDFAdvancedFindWidget* ui;

    pdf::PDFDrawWidgetProxy* m_proxy;
    const pdf::PDFDocument* m_document;
    SearchParameters m_parameters;
};

}   // namespace pdfviewer

#endif // PDFADVANCEDFINDWIDGET_H
