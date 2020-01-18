//    Copyright (C) 2019-2020 Jakub Melka
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

#ifndef PDFRENDERINGERRORSWIDGET_H
#define PDFRENDERINGERRORSWIDGET_H

#include "pdfglobal.h"

#include <QDialog>

namespace Ui
{
class PDFRenderingErrorsWidget;
}

namespace pdf
{
class PDFWidget;

class PDFFORQTLIBSHARED_EXPORT PDFRenderingErrorsWidget : public QDialog
{
    Q_OBJECT

public:
    explicit PDFRenderingErrorsWidget(QWidget* parent, PDFWidget* pdfWidget);
    virtual ~PDFRenderingErrorsWidget() override;

private:
    Ui::PDFRenderingErrorsWidget* ui;
};

}   // namespace pdf

#endif // PDFRENDERINGERRORSWIDGET_H
