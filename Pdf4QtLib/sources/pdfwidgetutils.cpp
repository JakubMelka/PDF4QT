//    Copyright (C) 2019-2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfwidgetutils.h"

#ifdef Q_OS_MAC
int qt_default_dpi_x() { return 72; }
int qt_default_dpi_y() { return 72; }
#else
int qt_default_dpi_x() { return 96; }
int qt_default_dpi_y() { return 96; }
#endif

namespace pdf
{

int PDFWidgetUtils::getPixelSize(const QPaintDevice* device, pdf::PDFReal sizeMM)
{
    const int width = device->width();
    const int height = device->height();

    if (width > height)
    {
        return PDFReal(width) * sizeMM / PDFReal(device->widthMM());
    }
    else
    {
        return PDFReal(height) * sizeMM / PDFReal(device->heightMM());
    }
}

int PDFWidgetUtils::scaleDPI_x(const QPaintDevice* device, int unscaledSize)
{
    const double logicalDPI_x = device->logicalDpiX();
    const double defaultDPI_x = qt_default_dpi_x();
    return (logicalDPI_x / defaultDPI_x) * unscaledSize;
}

int PDFWidgetUtils::scaleDPI_y(const QPaintDevice* device, int unscaledSize)
{
    const double logicalDPI_y = device->logicalDpiY();
    const double defaultDPI_y = qt_default_dpi_y();
    return (logicalDPI_y / defaultDPI_y) * unscaledSize;
}

PDFReal PDFWidgetUtils::scaleDPI_x(const QPaintDevice* device, PDFReal unscaledSize)
{
    const double logicalDPI_x = device->logicalDpiX();
    const double defaultDPI_x = qt_default_dpi_x();
    return (logicalDPI_x / defaultDPI_x) * unscaledSize;
}

void PDFWidgetUtils::scaleWidget(QWidget* widget, QSize unscaledSize)
{
    const double logicalDPI_x = widget->logicalDpiX();
    const double logicalDPI_y = widget->logicalDpiY();
    const double defaultDPI_x = qt_default_dpi_x();
    const double defaultDPI_y = qt_default_dpi_y();

    const int width = (logicalDPI_x / defaultDPI_x) * unscaledSize.width();
    const int height = (logicalDPI_y / defaultDPI_y) * unscaledSize.height();

    widget->resize(width, height);
}

QSize PDFWidgetUtils::scaleDPI(const QPaintDevice* widget, QSize unscaledSize)
{
    const double logicalDPI_x = widget->logicalDpiX();
    const double logicalDPI_y = widget->logicalDpiY();
    const double defaultDPI_x = qt_default_dpi_x();
    const double defaultDPI_y = qt_default_dpi_y();

    const int width = (logicalDPI_x / defaultDPI_x) * unscaledSize.width();
    const int height = (logicalDPI_y / defaultDPI_y) * unscaledSize.height();

    return QSize(width, height);
}

} // namespace pdf
