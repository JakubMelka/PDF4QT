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

class QPainter;

namespace pdf
{

enum RenderErrorType
{
    Error,
    NotImplemented
};

struct PDFRenderError
{
    explicit PDFRenderError() = default;
    explicit PDFRenderError(RenderErrorType type, QString message) :
        type(type),
        message(std::move(message))
    {

    }

    RenderErrorType type = RenderErrorType::Error;
    QString message;
};

/// Renders the PDF page on the painter, or onto an image.
class PDFRenderer
{
public:
    explicit PDFRenderer(const PDFDocument* document);

    enum Feature
    {
        Antialasing,        ///< Antialiasing for lines, shapes, etc.
        TextAntialiasing,   ///< Antialiasing for drawing text
        SmoothImages        ///< Adjust images to the device space using smooth transformation (slower, but better performance quality)
    };

    Q_DECLARE_FLAGS(Features, Feature)

    /// Paints desired page onto the painter. Page is painted in the rectangle using best-fit method.
    /// If the page doesn't exist, then error is returned. No exception is thrown. Rendering errors
    /// are reported and returned in the error list. If no error occured, empty list is returned.
    /// \param painter Painter
    /// \param rectangle Paint area for the page
    /// \param pageIndex Index of the page to be painted
    QList<PDFRenderError> render(QPainter* painter, const QRectF& rectangle, size_t pageIndex) const;

private:
    const PDFDocument* m_document;
    Features m_features;
};


}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFRenderer::Features)

#endif // PDFRENDERER_H
