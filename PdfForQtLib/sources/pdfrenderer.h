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
#include "pdfcolorspaces.h"

#include <QMatrix>
#include <QSharedPointer>

#include <stack>

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

/// Process the contents of the page.
class PDFPageContentProcessor
{
public:
    explicit PDFPageContentProcessor(const PDFPage* page, const PDFDocument* document);

    /// Process the contents of the page
    QList<PDFRenderError> processContents();

protected:
    /// Process the content stream
    QList<PDFRenderError> processContentStream(const PDFStream* stream);

    /// Represents graphic state of the PDF (holding current graphic state parameters).
    /// Please see PDF Reference 1.7, Chapter 4.3 "Graphic State"
    class PDFPageContentProcessorState
    {
    public:
        explicit PDFPageContentProcessorState();
        ~PDFPageContentProcessorState();

    private:
        QMatrix m_currentTransformationMatrix;
        QSharedPointer<PDFAbstractColorSpace> m_fillColorSpace;
        QSharedPointer<PDFAbstractColorSpace> m_strokeColorSpace;
        QColor m_fillColor;
        QColor m_strokeColor;
        PDFReal m_lineWidth;
        Qt::PenCapStyle m_lineCapStyle;
        Qt::PenJoinStyle m_lineJoinStyle;
        PDFReal m_mitterLimit;
        QByteArray m_renderingIntent;
        PDFReal m_flatness;
        PDFReal m_smoothness;
    };

private:

    const PDFPage* m_page;
    const PDFDocument* m_document;

    /// Stack with current graphic states
    std::stack<PDFPageContentProcessorState> m_stack;
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFRenderer::Features)

#endif // PDFRENDERER_H
