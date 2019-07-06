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

#ifndef PDFPAINTER_H
#define PDFPAINTER_H

#include "pdfutils.h"
#include "pdfrenderer.h"
#include "pdfpagecontentprocessor.h"

#include <QPen>
#include <QBrush>

namespace pdf
{

/// Processor, which processes PDF's page commands on the QPainter. It works with QPainter
/// and with transformation matrix, which translates page points to the device points.
class PDFPainter : public PDFPageContentProcessor
{
public:
    /// Constructs new PDFPainter object, with default parameters.
    /// \param painter Painter, on which page content is drawn
    /// \param features Features of the painter
    /// \param pagePointToDevicePointMatrix Matrix, which translates page points to device points
    /// \param page Page, which will be drawn
    /// \param document Document owning the page
    /// \param fontCache Font cache
    /// \param optionalContentActivity Activity of optional content
    explicit PDFPainter(QPainter* painter,
                        PDFRenderer::Features features,
                        QMatrix pagePointToDevicePointMatrix,
                        const PDFPage* page,
                        const PDFDocument* document,
                        const PDFFontCache* fontCache,
                        const PDFOptionalContentActivity* optionalContentActivity);
    virtual ~PDFPainter() override;

protected:
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override;
    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule) override;
    virtual void performImagePainting(const QImage& image);
    virtual void performUpdateGraphicsState(const PDFPageContentProcessorState& state) override;
    virtual void performSaveGraphicState(ProcessOrder order) override;
    virtual void performRestoreGraphicState(ProcessOrder order) override;
    virtual bool isContentSuppressedByOC(PDFObjectReference ocgOrOcmd) override;

private:
    /// Returns current pen
    const QPen& getCurrentPen() { return m_currentPen.get(this, &PDFPainter::getCurrentPenImpl); }

    /// Returns current brush
    const QBrush& getCurrentBrush() { return m_currentBrush.get(this, &PDFPainter::getCurrentBrushImpl); }

    /// Returns current pen (implementation)
    QPen getCurrentPenImpl() const;

    /// Returns current brush (implementation)
    QBrush getCurrentBrushImpl() const;

    QPainter* m_painter;
    PDFRenderer::Features m_features;
    QMatrix m_pagePointToDevicePointMatrix;
    PDFCachedItem<QPen> m_currentPen;
    PDFCachedItem<QBrush> m_currentBrush;
};

}   // namespace pdf

#endif // PDFPAINTER_H
