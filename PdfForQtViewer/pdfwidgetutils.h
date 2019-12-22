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

#ifndef PDFWIDGETUTILS_H
#define PDFWIDGETUTILS_H

#include "pdfglobal.h"

#include <QWidget>

namespace pdfviewer
{

class PDFWidgetUtils
{
public:
    PDFWidgetUtils() = delete;

    /// Converts size in MM to pixel size
    static int getPixelSize(QWidget* widget, pdf::PDFReal sizeMM);

    /// Scale horizontal DPI value
    static int scaleDPI_x(QWidget* widget, int unscaledSize);

    /// Scales widget based on DPI
    /// \param widget Widget to be scaled
    /// \param unscaledSize Unscaled size of the widget
    static void scaleWidget(QWidget* widget, QSize unscaledSize);
};

} // namespace pdfviewer

#endif // PDFWIDGETUTILS_H
