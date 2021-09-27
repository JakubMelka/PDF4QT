//    Copyright (C) 2019-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFWIDGETUTILS_H
#define PDFWIDGETUTILS_H

#include "pdfglobal.h"

#include <QWidget>

namespace pdf
{

class PDF4QTLIBSHARED_EXPORT PDFWidgetUtils
{
public:
    PDFWidgetUtils() = delete;

    /// Converts size in MM to pixel size
    static int getPixelSize(const QPaintDevice* device, pdf::PDFReal sizeMM);

    /// Scale horizontal DPI value
    /// \param device Paint device to obtain logical DPI for scaling
    static int scaleDPI_x(const QPaintDevice* device, int unscaledSize);

    /// Scale vertical DPI value
    /// \param device Paint device to obtain logical DPI for scaling
    static int scaleDPI_y(const QPaintDevice* device, int unscaledSize);

    /// Scale horizontal DPI value
    /// \param device Paint device to obtain logical DPI for scaling
    static PDFReal scaleDPI_x(const QPaintDevice* device, PDFReal unscaledSize);

    /// Scales widget based on DPI
    /// \param widget Widget to be scaled
    /// \param unscaledSize Unscaled size of the widget
    static void scaleWidget(QWidget* widget, QSize unscaledSize);

    /// Scales size based on DPI
    /// \param device Paint device to obtain logical DPI for scaling
    /// \param unscaledSize Unscaled size
    static QSize scaleDPI(const QPaintDevice* widget, QSize unscaledSize);

    /// Apply style to the widget
    static void style(QWidget* widget);
};

} // namespace pdf

#endif // PDFWIDGETUTILS_H
