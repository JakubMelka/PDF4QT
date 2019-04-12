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

#include "pdfpainter.h"

#include <QPainter>

namespace pdf
{


PDFPainter::PDFPainter(QPainter* painter, PDFRenderer::Features features, QMatrix pagePointToDevicePointMatrix, const PDFPage* page, const PDFDocument* document, const PDFFontCache* fontCache) :
    PDFPageContentProcessor(page, document, fontCache),
    m_painter(painter),
    m_features(features),
    m_pagePointToDevicePointMatrix(pagePointToDevicePointMatrix)
{
    Q_ASSERT(painter);
    Q_ASSERT(pagePointToDevicePointMatrix.isInvertible());

    m_painter->save();
}

PDFPainter::~PDFPainter()
{
    m_painter->restore();
}

void PDFPainter::performPathPainting(const QPainterPath& path, bool stroke, bool fill, Qt::FillRule fillRule)
{
    if ((!stroke && !fill) || path.isEmpty())
    {
        // No operation requested - either path is empty, or neither stroking or filling
        return;
    }

    if (stroke)
    {
        m_painter->setPen(getCurrentPen());
    }
    else
    {
        m_painter->setPen(Qt::NoPen);
    }

    if (fill)
    {
        m_painter->setBrush(getCurrentBrush());
    }
    else
    {
        m_painter->setBrush(Qt::NoBrush);
    }

    m_painter->setRenderHint(QPainter::Antialiasing, m_features.testFlag(PDFRenderer::Antialiasing));

    Q_ASSERT(path.fillRule() == fillRule);
    m_painter->drawPath(path);
}

void PDFPainter::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
    Q_ASSERT(path.fillRule() == fillRule);
    m_painter->setClipPath(path, Qt::IntersectClip);
}

void PDFPainter::performUpdateGraphicsState(const PDFPageContentProcessorState& state)
{
    const PDFPageContentProcessorState::StateFlags flags = state.getStateFlags();

    // If current transformation matrix has changed, then update it
    if (flags.testFlag(PDFPageContentProcessorState::StateCurrentTransformationMatrix))
    {
        m_painter->setWorldMatrix(m_pagePointToDevicePointMatrix * state.getCurrentTransformationMatrix(), false);
    }

    if (flags.testFlag(PDFPageContentProcessorState::StateStrokeColor) ||
        flags.testFlag(PDFPageContentProcessorState::StateLineWidth) ||
        flags.testFlag(PDFPageContentProcessorState::StateLineCapStyle) ||
        flags.testFlag(PDFPageContentProcessorState::StateLineJoinStyle) ||
        flags.testFlag(PDFPageContentProcessorState::StateMitterLimit) ||
        flags.testFlag(PDFPageContentProcessorState::StateLineDashPattern))
    {
        m_currentPen.dirty();
    }

    if (flags.testFlag(PDFPageContentProcessorState::StateFillColor))
    {
        m_currentBrush.dirty();
    }

    PDFPageContentProcessor::performUpdateGraphicsState(state);
}

void PDFPainter::performSaveGraphicState(ProcessOrder order)
{
    if (order == ProcessOrder::AfterOperation)
    {
        m_painter->save();
    }
}

void PDFPainter::performRestoreGraphicState(ProcessOrder order)
{
    if (order == ProcessOrder::BeforeOperation)
    {
        m_painter->restore();
    }
}

QPen PDFPainter::getCurrentPenImpl() const
{
    const PDFPageContentProcessorState* graphicState = getGraphicState();
    const QColor& color = graphicState->getStrokeColor();
    if (color.isValid())
    {
        const PDFReal lineWidth = graphicState->getLineWidth();
        Qt::PenCapStyle penCapStyle = graphicState->getLineCapStyle();
        Qt::PenJoinStyle penJoinStyle = graphicState->getLineJoinStyle();
        const PDFLineDashPattern& lineDashPattern = graphicState->getLineDashPattern();
        const PDFReal mitterLimit = graphicState->getMitterLimit();

        QPen pen(color);

        pen.setWidthF(lineWidth);
        pen.setCapStyle(penCapStyle);
        pen.setJoinStyle(penJoinStyle);
        pen.setMiterLimit(mitterLimit);

        if (lineDashPattern.isSolid())
        {
            pen.setStyle(Qt::SolidLine);
        }
        else
        {
            pen.setStyle(Qt::CustomDashLine);
            pen.setDashPattern(QVector<PDFReal>::fromStdVector(lineDashPattern.getDashArray()));
            pen.setDashOffset(lineDashPattern.getDashOffset());
        }

        return pen;
    }
    else
    {
        return QPen(Qt::NoPen);
    }
}

QBrush PDFPainter::getCurrentBrushImpl() const
{
    const PDFPageContentProcessorState* graphicState = getGraphicState();
    const QColor& color = graphicState->getFillColor();
    if (color.isValid())
    {
        return QBrush(color, Qt::SolidPattern);
    }
    else
    {
        return QBrush(Qt::NoBrush);
    }
}

}   // namespace pdf
