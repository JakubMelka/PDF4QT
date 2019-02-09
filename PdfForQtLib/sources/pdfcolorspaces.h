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

#ifndef PDFCOLORSPACES_H
#define PDFCOLORSPACES_H

#include "pdfflatarray.h"

#include <QColor>

namespace pdf
{

using PDFColorComponent = float;
using PDFColor = PDFFlatArray<PDFColorComponent, 4>;

/// Represents PDF's color space
class PDFAbstractColorSpace
{
public:
    explicit PDFAbstractColorSpace() = default;
    virtual ~PDFAbstractColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const = 0;
    virtual size_t getColorComponentCount() const = 0;

protected:
    /// Clips the color component to range [0, 1]
    static constexpr PDFColorComponent clip01(PDFColorComponent component) { return qBound<PDFColorComponent>(0.0, component, 1.0); }
};

class PDFDeviceGrayColorSpace : public PDFAbstractColorSpace
{
public:
    explicit PDFDeviceGrayColorSpace() = default;
    virtual ~PDFDeviceGrayColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;
};

class PDFDeviceRGBColorSpace : public PDFAbstractColorSpace
{
public:
    explicit PDFDeviceRGBColorSpace() = default;
    virtual ~PDFDeviceRGBColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;
};

class PDFDeviceCMYKColorSpace : public PDFAbstractColorSpace
{
public:
    explicit PDFDeviceCMYKColorSpace() = default;
    virtual ~PDFDeviceCMYKColorSpace() = default;

    virtual QColor getColor(const PDFColor& color) const override;
    virtual size_t getColorComponentCount() const override;
};

}   // namespace pdf

#endif // PDFCOLORSPACES_H
