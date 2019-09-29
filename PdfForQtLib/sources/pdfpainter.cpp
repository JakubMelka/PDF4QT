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
#include "pdfpattern.h"

#include <QPainter>

namespace pdf
{

PDFPainter::PDFPainter(QPainter* painter,
                       PDFRenderer::Features features,
                       QMatrix pagePointToDevicePointMatrix,
                       const PDFPage* page,
                       const PDFDocument* document,
                       const PDFFontCache* fontCache,
                       const PDFOptionalContentActivity* optionalContentActivity,
                       const PDFMeshQualitySettings& meshQualitySettings) :
    PDFPageContentProcessor(page, document, fontCache, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
    m_painter(painter),
    m_features(features)
{
    Q_ASSERT(painter);
    Q_ASSERT(pagePointToDevicePointMatrix.isInvertible());

    m_painter->save();

    if (features.testFlag(PDFRenderer::ClipToCropBox))
    {
        QRectF cropBox = page->getRotatedCropBox();
        if (cropBox.isValid())
        {
            QPainterPath path;
            path.addPolygon(pagePointToDevicePointMatrix.map(cropBox));

            m_painter->setClipPath(path, Qt::IntersectClip);
        }
    }

    m_painter->setRenderHint(QPainter::SmoothPixmapTransform, features.testFlag(PDFRenderer::SmoothImages));
}

PDFPainter::~PDFPainter()
{
    m_painter->restore();
}

void PDFPainter::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
    Q_ASSERT(stroke || fill);

    // Set antialiasing
    const bool antialiasing = (text && m_features.testFlag(PDFRenderer::TextAntialiasing)) || (!text && m_features.testFlag(PDFRenderer::Antialiasing));
    m_painter->setRenderHint(QPainter::Antialiasing, antialiasing);

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

    Q_ASSERT(path.fillRule() == fillRule);
    m_painter->drawPath(path);
}

void PDFPainter::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
    Q_ASSERT(path.fillRule() == fillRule);
    m_painter->setClipPath(path, Qt::IntersectClip);
}

void PDFPainter::performImagePainting(const QImage& image)
{
    if (isContentSuppressed())
    {
        // Content is suppressed, do not paint anything
        return;
    }

    m_painter->save();

    QImage adjustedImage = image;

    if (m_features.testFlag(PDFRenderer::SmoothImages))
    {
        // Test, if we can use smooth images. We can use them under following conditions:
        //  1) Transformed rectangle is not skewed or deformed (so vectors (0, 1) and (1, 0) are orthogonal)
        //  2) Image enlargement is not too big (so we doesn't run out of memory)

        QMatrix matrix = m_painter->worldMatrix();
        QLineF mappedWidthVector = matrix.map(QLineF(0, 0, 1, 0));
        QLineF mappedHeightVector = matrix.map(QLineF(0, 0, 0, 1));
        qreal angle = mappedWidthVector.angleTo(mappedHeightVector);
        if (qFuzzyCompare(angle, 90.0))
        {
            // Image is not skewed, so we test enlargement factor
            const int newWidth = mappedWidthVector.length();
            const int newHeight = mappedHeightVector.length();

            const int newPixels = newWidth * newHeight;
            const int oldPixels = image.width() * image.height();

            if (newPixels < oldPixels * 8)
            {
                QSize size = adjustedImage.size();
                QSize adjustedImageSize = size.scaled(newWidth, newHeight, Qt::KeepAspectRatio);
                adjustedImage = adjustedImage.scaled(adjustedImageSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        }
    }

    QMatrix imageTransform(1.0 / adjustedImage.width(), 0, 0, 1.0 / adjustedImage.height(), 0, 0);
    QMatrix worldMatrix = imageTransform * m_painter->worldMatrix();

    // Because Qt uses opposite axis direction than PDF, then we must transform the y-axis
    // to the opposite (so the image is then unchanged)
    worldMatrix.translate(0, adjustedImage.height());
    worldMatrix.scale(1, -1);

    m_painter->setWorldMatrix(worldMatrix);
    m_painter->drawImage(0, 0, adjustedImage);

    m_painter->restore();
}

void PDFPainter::performMeshPainting(const PDFMesh& mesh)
{
    m_painter->save();
    m_painter->setWorldMatrix(QMatrix());
    mesh.paint(m_painter, getGraphicState()->getAlphaFilling());
    m_painter->restore();
}

void PDFPainter::performUpdateGraphicsState(const PDFPageContentProcessorState& state)
{
    const PDFPageContentProcessorState::StateFlags flags = state.getStateFlags();

    // If current transformation matrix has changed, then update it
    if (flags.testFlag(PDFPageContentProcessorState::StateCurrentTransformationMatrix))
    {
        m_painter->setWorldMatrix(getCurrentWorldMatrix(), false);
    }

    if (flags.testFlag(PDFPageContentProcessorState::StateStrokeColor) ||
        flags.testFlag(PDFPageContentProcessorState::StateLineWidth) ||
        flags.testFlag(PDFPageContentProcessorState::StateLineCapStyle) ||
        flags.testFlag(PDFPageContentProcessorState::StateLineJoinStyle) ||
        flags.testFlag(PDFPageContentProcessorState::StateMitterLimit) ||
        flags.testFlag(PDFPageContentProcessorState::StateLineDashPattern) ||
        flags.testFlag(PDFPageContentProcessorState::StateAlphaStroking))
    {
        m_currentPen.dirty();
    }

    if (flags.testFlag(PDFPageContentProcessorState::StateFillColor) ||
        flags.testFlag(PDFPageContentProcessorState::StateAlphaFilling))
    {
        m_currentBrush.dirty();
    }

    // If current blend mode has changed, then update it
    if (flags.testFlag(PDFPageContentProcessorState::StateBlendMode))
    {
        const BlendMode blendMode = state.getBlendMode();
        const QPainter::CompositionMode compositionMode = PDFBlendModeInfo::getCompositionModeFromBlendMode(blendMode);

        if (!PDFBlendModeInfo::isSupportedByQt(blendMode))
        {
            reportRenderError(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Blend mode '%1' not supported.").arg(PDFBlendModeInfo::getBlendModeName(blendMode)));
        }

        m_painter->setCompositionMode(compositionMode);
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

bool PDFPainter::isContentSuppressedByOC(PDFObjectReference ocgOrOcmd)
{
    if (m_features.testFlag(PDFRenderer::IgnoreOptionalContent))
    {
        return false;
    }

    return PDFPageContentProcessor::isContentSuppressedByOC(ocgOrOcmd);
}

QPen PDFPainter::getCurrentPenImpl() const
{
    const PDFPageContentProcessorState* graphicState = getGraphicState();
    const QColor& color = graphicState->getStrokeColorWithAlpha();
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
    const QColor& color = graphicState->getFillColorWithAlpha();
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
