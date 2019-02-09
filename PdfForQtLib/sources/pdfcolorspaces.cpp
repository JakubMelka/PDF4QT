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

#include "pdfcolorspaces.h"

namespace pdf
{

QColor PDFDeviceGrayColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    PDFColorComponent component = clip01(color[0]);

    QColor result(QColor::Rgb);
    result.setRgbF(component, component, component, 1.0);
    return result;
}

size_t PDFDeviceGrayColorSpace::getColorComponentCount() const
{
    return 1;
}

QColor PDFDeviceRGBColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    PDFColorComponent r = clip01(color[0]);
    PDFColorComponent g = clip01(color[1]);
    PDFColorComponent b = clip01(color[2]);

    QColor result(QColor::Rgb);
    result.setRgbF(r, g, b, 1.0);
    return result;
}

size_t PDFDeviceRGBColorSpace::getColorComponentCount() const
{
    return 3;
}

QColor PDFDeviceCMYKColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    PDFColorComponent c = clip01(color[0]);
    PDFColorComponent m = clip01(color[1]);
    PDFColorComponent y = clip01(color[2]);
    PDFColorComponent k = clip01(color[3]);

    QColor result(QColor::Cmyk);
    result.setCmykF(c, m, y, k, 1.0);
    return result;
}

size_t PDFDeviceCMYKColorSpace::getColorComponentCount() const
{
    return 4;
}

}   // namespace pdf
