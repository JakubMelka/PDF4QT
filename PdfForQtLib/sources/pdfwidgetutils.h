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

#ifndef PDFWIDGETUTILS_H
#define PDFWIDGETUTILS_H

#include "pdfglobal.h"

#include <QWidget>

namespace pdf
{

class PDFFORQTLIBSHARED_EXPORT PDFWidgetUtils
{
public:
    PDFWidgetUtils() = delete;

    /// Converts size in MM to pixel size
    static int getPixelSize(QPaintDevice* device, pdf::PDFReal sizeMM);

    /// Scale horizontal DPI value
    /// \param device Paint device to obtain logical DPI for scaling
    static int scaleDPI_x(QPaintDevice* device, int unscaledSize);

    /// Scale horizontal DPI value
    /// \param device Paint device to obtain logical DPI for scaling
    static PDFReal scaleDPI_x(QPaintDevice* device, PDFReal unscaledSize);

    /// Scales widget based on DPI
    /// \param widget Widget to be scaled
    /// \param unscaledSize Unscaled size of the widget
    static void scaleWidget(QWidget* widget, QSize unscaledSize);

    /// Scales size based on DPI
    /// \param device Paint device to obtain logical DPI for scaling
    /// \param unscaledSize Unscaled size
    static QSize scaleDPI(QPaintDevice* widget, QSize unscaledSize);
};

} // namespace pdf

#endif // PDFWIDGETUTILS_H
