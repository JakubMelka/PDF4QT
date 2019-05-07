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

#ifndef PDFIMAGE_H
#define PDFIMAGE_H

#include "pdfcolorspaces.h"

#include <QByteArray>

class QByteArray;

namespace pdf
{
class PDFStream;
class PDFDocument;

class PDFImage
{
public:

    /// Creates image from the content and the dictionary. If image can't be created, then exception is thrown.
    /// \param document Document
    /// \param stream Stream with image
    /// \param colorSpace Color space of the image
    static PDFImage createImage(const PDFDocument* document, const PDFStream* stream, PDFColorSpacePointer colorSpace);

    /// Returns image transformed from image data and color space
    QImage getImage() const;

private:
    PDFImage() = default;

    PDFImageData m_imageData;
    PDFColorSpacePointer m_colorSpace;
};

}   // namespace pdf

#endif // PDFIMAGE_H
