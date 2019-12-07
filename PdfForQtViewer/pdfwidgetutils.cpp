//    Copyright (C) 2019 Jakub Melka
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

#include "pdfwidgetutils.h"


namespace pdfviewer
{

int PDFWidgetUtils::getPixelSize(QWidget* widget, pdf::PDFReal sizeMM)
{
    const int width = widget->width();
    const int height = widget->height();

    if (width > height)
    {
        return pdf::PDFReal(width) * sizeMM / pdf::PDFReal(widget->widthMM());
    }
    else
    {
        return pdf::PDFReal(height) * sizeMM / pdf::PDFReal(widget->heightMM());
    }
}

} // namespace pdfviewer
