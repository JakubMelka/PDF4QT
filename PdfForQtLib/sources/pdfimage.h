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

#ifndef PDFIMAGE_H
#define PDFIMAGE_H

#include "pdfcolorspaces.h"

#include <QByteArray>

class QByteArray;

namespace pdf
{
class PDFStream;
class PDFDocument;
class PDFRenderErrorReporter;

class PDFImage
{
public:

    /// Creates image from the content and the dictionary. If image can't be created, then exception is thrown.
    /// \param document Document
    /// \param stream Stream with image
    /// \param colorSpace Color space of the image
    /// \param isSoftMask Is it a soft mask image?
    /// \param renderingIntent Default rendering intent of the image
    /// \param errorReporter Error reporter for reporting errors (or warnings)
    static PDFImage createImage(const PDFDocument* document,
                                const PDFStream* stream,
                                PDFColorSpacePointer colorSpace,
                                bool isSoftMask,
                                RenderingIntent renderingIntent,
                                PDFRenderErrorReporter* errorReporter);

    /// Returns image transformed from image data and color space
    QImage getImage(const PDFCMS* cms, PDFRenderErrorReporter* reporter) const;

    /// Returns rendering intent of the image
    RenderingIntent getRenderingIntent() const { return m_renderingIntent; }

private:
    PDFImage() = default;

    PDFImageData m_imageData;
    PDFImageData m_softMask;
    PDFColorSpacePointer m_colorSpace;
    RenderingIntent m_renderingIntent = RenderingIntent::Perceptual;
};

}   // namespace pdf

#endif // PDFIMAGE_H
