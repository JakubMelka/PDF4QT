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

#ifndef PDFRENDERER_H
#define PDFRENDERER_H

#include "pdfpage.h"
#include "pdfexception.h"

class QPainter;

namespace pdf
{
class PDFFontCache;
class PDFOptionalContentActivity;

/// Renders the PDF page on the painter, or onto an image.
class PDFRenderer
{
public:
    explicit PDFRenderer(const PDFDocument* document, const PDFFontCache* fontCache, const PDFOptionalContentActivity* optionalContentActivity);

    enum Feature
    {
        Antialiasing        = 0x0001,   ///< Antialiasing for lines, shapes, etc.
        TextAntialiasing    = 0x0002,   ///< Antialiasing for drawing text
        SmoothImages        = 0x0004    ///< Adjust images to the device space using smooth transformation (slower, but better performance quality)
    };

    Q_DECLARE_FLAGS(Features, Feature)

    /// Paints desired page onto the painter. Page is painted in the rectangle using best-fit method.
    /// If the page doesn't exist, then error is returned. No exception is thrown. Rendering errors
    /// are reported and returned in the error list. If no error occured, empty list is returned.
    /// \param painter Painter
    /// \param rectangle Paint area for the page
    /// \param pageIndex Index of the page to be painted
    QList<PDFRenderError> render(QPainter* painter, const QRectF& rectangle, size_t pageIndex) const;

    /// Paints desired page onto the painter. Page is painted using \p matrix, which maps page coordinates
    /// to the device coordinates. If the page doesn't exist, then error is returned. No exception is thrown.
    /// Rendering errors are reported and returned in the error list. If no error occured, empty list is returned.
    QList<PDFRenderError> render(QPainter* painter, const QMatrix& matrix, size_t pageIndex) const;

private:
    const PDFDocument* m_document;
    const PDFFontCache* m_fontCache;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    Features m_features;
};


}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFRenderer::Features)

#endif // PDFRENDERER_H
