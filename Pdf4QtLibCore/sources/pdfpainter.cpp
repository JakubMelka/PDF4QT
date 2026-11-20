// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfpainter.h"
#include "pdfpattern.h"
#include "pdfcms.h"
#include "pdfpainterutils.h"
#include "pdfpathboolean.h"

#include <QPainter>
#include <QPaintEngine>
#include <QCryptographicHash>
#include <QtMath>

#include <cmath>
#include <limits>

#include "pdfdbgheap.h"

namespace pdf
{

PDFPainterBase::PDFPainterBase(PDFRenderer::Features features,
                               const PDFPage* page,
                               const PDFDocument* document,
                               const PDFFontCache* fontCache,
                               const PDFICCProfileCache* iccProfileCache,
                               const PDFCMS* cms,
                               const PDFOptionalContentActivity* optionalContentActivity,
                               QTransform pagePointToDevicePointMatrix,
                               const PDFMeshQualitySettings& meshQualitySettings) :
    BaseClass(page, document, fontCache, iccProfileCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
    m_features(features)
{

}

void PDFPainterBase::performUpdateGraphicsState(const PDFPageContentProcessorState& state)
{
    const PDFPageContentProcessorState::StateFlags flags = state.getStateFlags();

    // If current transformation matrix has changed, then update it
    if (flags.testFlag(PDFPageContentProcessorState::StateCurrentTransformationMatrix))
    {
        setWorldMatrix(getCurrentWorldMatrix());
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
        // Try to simulate transparency groups. Use only first composition mode,
        // outside the transparency groups (so we are on pages main transparency
        // groups).

        const BlendMode blendMode = state.getBlendMode();
        if (canSetBlendMode(blendMode))
        {
            if (!PDFBlendModeInfo::isSupportedByQt(blendMode))
            {
                reportRenderErrorOnce(RenderErrorType::NotSupported, PDFTranslationContext::tr("Blend mode '%1' not supported.").arg(PDFBlendModeInfo::getBlendModeName(blendMode)));
            }

            const QPainter::CompositionMode compositionMode = PDFBlendModeInfo::getCompositionModeFromBlendMode(blendMode);
            setCompositionMode(compositionMode);
        }
        else if (blendMode != BlendMode::Normal && blendMode != BlendMode::Compatible)
        {
            reportRenderErrorOnce(RenderErrorType::NotSupported, PDFTranslationContext::tr("Blend mode '%1' is in transparency group, which is not supported.").arg(PDFBlendModeInfo::getBlendModeName(blendMode)));
        }
    }

    if (flags.testFlag(PDFPageContentProcessorState::StateSoftMask) && state.getSoftMask())
    {
        reportRenderErrorOnce(RenderErrorType::NotSupported, PDFTranslationContext::tr("Soft masks not supported."));
    }

    BaseClass::performUpdateGraphicsState(state);
}

bool PDFPainterBase::isContentSuppressedByOC(PDFObjectReference ocgOrOcmd)
{
    if (m_features.testFlag(PDFRenderer::IgnoreOptionalContent))
    {
        return false;
    }

    return PDFPageContentProcessor::isContentSuppressedByOC(ocgOrOcmd);
}

QPen PDFPainterBase::getCurrentPenImpl() const
{
    return PDFPainterHelper::createPenFromState(getGraphicState(), getEffectiveStrokingAlpha());
}

QBrush PDFPainterBase::getCurrentBrushImpl() const
{
    return PDFPainterHelper::createBrushFromState(getGraphicState(), getEffectiveFillingAlpha());
}

PDFReal PDFPainterBase::getEffectiveStrokingAlpha() const
{
    PDFReal alpha = getGraphicState()->getAlphaStroking();

    auto it = m_transparencyGroupDataStack.crbegin();
    auto itEnd = m_transparencyGroupDataStack.crend();
    for (; it != itEnd; ++it)
    {
        const PDFTransparencyGroupPainterData& transparencyGroup = *it;
        alpha *= transparencyGroup.alphaStroke;

        if (transparencyGroup.group.isolated)
        {
            break;
        }
    }

    return alpha;
}

PDFReal PDFPainterBase::getEffectiveFillingAlpha() const
{
    PDFReal alpha = getGraphicState()->getAlphaFilling();

    auto it = m_transparencyGroupDataStack.crbegin();
    auto itEnd = m_transparencyGroupDataStack.crend();
    for (; it != itEnd; ++it)
    {
        const PDFTransparencyGroupPainterData& transparencyGroup = *it;
        alpha *= transparencyGroup.alphaFill;

        if (transparencyGroup.group.isolated)
        {
            break;
        }
    }

    return alpha;
}

bool PDFPainterBase::canSetBlendMode(BlendMode mode) const
{
    // We will assume, that we can set blend mode, when
    // all other blend modes on transparency stack are normal,
    // or compatible. It should work.

    Q_UNUSED(mode);
    return std::all_of(m_transparencyGroupDataStack.cbegin(), m_transparencyGroupDataStack.cend(), [](const PDFTransparencyGroupPainterData& group) { return group.blendMode == BlendMode::Normal || group.blendMode == BlendMode::Compatible; });
}

void PDFPainterBase::performBeginTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup)
{
    if (order == ProcessOrder::BeforeOperation)
    {
        PDFTransparencyGroupPainterData data;
        data.group = transparencyGroup;
        data.alphaFill = getGraphicState()->getAlphaFilling();
        data.alphaStroke = getGraphicState()->getAlphaStroking();
        data.blendMode = getGraphicState()->getBlendMode();
        m_transparencyGroupDataStack.emplace_back(qMove(data));
    }
}

void PDFPainterBase::performEndTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup)
{
    Q_UNUSED(transparencyGroup);

    if (order == ProcessOrder::AfterOperation)
    {
        m_transparencyGroupDataStack.pop_back();
    }
}

PDFPainter::PDFPainter(QPainter* painter,
                       PDFRenderer::Features features,
                       QTransform pagePointToDevicePointMatrix,
                       const PDFPage* page,
                       const PDFDocument* document,
                       const PDFFontCache* fontCache,
                       const PDFICCProfileCache* iccProfileCache,
                       const PDFCMS* cms,
                       const PDFOptionalContentActivity* optionalContentActivity,
                       const PDFMeshQualitySettings& meshQualitySettings) :
    BaseClass(features, page, document, fontCache, iccProfileCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings),
    m_painter(painter)
{
    Q_ASSERT(painter);
    Q_ASSERT(pagePointToDevicePointMatrix.isInvertible());

    m_painter->save();

    if (features.testFlag(PDFRenderer::ClipToCropBox))
    {
        QRectF cropBox = page->getCropBox();
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
    const bool antialiasing = (text && hasFeature(PDFRenderer::TextAntialiasing)) || (!text && hasFeature(PDFRenderer::Antialiasing));
    m_painter->setRenderHint(QPainter::Antialiasing, antialiasing);

    // Jakub Melka: only text is treated as a foreground color, other graphics keep
    // their lightness relationship (see PDFColorConvertor::mixCustomColors). This is
    // the same rule which PDFPrecompiledPage::convertColors uses for the page content,
    // so a content stream is converted the same way, no matter whether it is played
    // from a precompiled page or painted directly by this painter.
    if (stroke)
    {
        m_painter->setPen(m_colorConvertor.isActive() ? m_colorConvertor.convert(getCurrentPen(), false, text) : getCurrentPen());
    }
    else
    {
        m_painter->setPen(Qt::NoPen);
    }

    if (fill)
    {
        m_painter->setBrush(m_colorConvertor.isActive() ? m_colorConvertor.convert(getCurrentBrush(), false, text) : getCurrentBrush());
    }
    else
    {
        m_painter->setBrush(Qt::NoBrush);
    }

    Q_ASSERT(path.fillRule() == fillRule);
    m_painter->drawPath(path);
}

bool PDFPainter::performTextCharacterDrawing(const PDFRealTextDrawInfo& info)
{
    if (!hasFeature(PDFRenderer::RealText))
    {
        return false;
    }

    if (isContentSuppressed())
    {
        return false;
    }

    const PDFFontCache::TextDrawingFontInfo* fontInfo = getFontCache()->getFontForTextDrawing(info.font, this);
    if (!fontInfo || !fontInfo->isUsable)
    {
        return false;
    }

    // QFont::setPointSizeF() would resolve to a local (pre-world-transform) pixel size using
    // the paint device's own logical DPI - but the world transform below already contains its
    // own, independent points-to-device-pixels factor (via pagePointToDevicePointMatrix), so
    // combining the two would apply that DPI scaling twice. QFont::setPixelSize() sets the
    // font size directly in local units, bypassing any DPI conversion - the same way FreeType's
    // glyph outlines (used by path-based painting) are generated directly in fontSize-scaled
    // units. We render at a fixed, large reference pixel size for good rasterization precision,
    // and bake the missing (fontSize / referenceSize) factor into the local transform instead.
    constexpr int referencePixelSize = 1000;
    QFont font = fontInfo->font;
    font.setPixelSize(referencePixelSize);

    m_painter->save();

    // Glyph outlines (used by the path-based painting) are in a y-up local space, while
    // QPainter::drawText's own local glyph space is y-down (baseline at origin, ascenders
    // in the -y direction) - the same axis mismatch handled for images in performImagePainting.
    // We must flip locally, so the result matches the path-based rendering after being mapped
    // by textRenderingMatrix and the current world transform (which already contains the
    // PDF-page-space-to-device-space flip). The sign of fontSize is already carried by
    // textRenderingMatrix (via adjustMatrix's fontFactor), so only its magnitude is used here.
    const qreal fontScale = qAbs(info.fontSize) / referencePixelSize;
    const QTransform localFlipAndScale(fontScale, 0.0, 0.0, -fontScale, 0.0, 0.0);
    m_painter->setWorldTransform(localFlipAndScale * info.textRenderingMatrix * m_painter->worldTransform(), false);
    m_painter->setRenderHint(QPainter::Antialiasing, hasFeature(PDFRenderer::TextAntialiasing));
    m_painter->setFont(font);

    QColor textColor = getCurrentBrush().color();
    if (m_colorConvertor.isActive())
    {
        textColor = m_colorConvertor.convert(textColor, false, true);
    }
    m_painter->setPen(QPen(textColor));
    m_painter->drawText(QPointF(0.0, 0.0), QString(info.character));

    m_painter->restore();
    return true;
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

    // Jakub Melka: if the image is being shrunk, then we must downscale it explicitly.
    // The raster paint engines resample from a 2x2 neighborhood only, so most of the
    // source pixels would never be sampled at all. Bitonal images are downscaled by
    // the ink coverage downscaler, so thin strokes of a scan do not wash out, and they
    // are downscaled even without the feature SmoothImages. The size is computed from
    // the device transform and not from the world transform - the world transform is
    // in the logical pixels, which are not the real pixels of the target on a display
    // with a device pixel ratio above one.
    const QSize downscaledSize = PDFImageScaling::getDownscaledSize(adjustedImage.size(), m_painter->deviceTransform());
    if (!downscaledSize.isEmpty())
    {
        const PDFImageScaling::ImageType imageType = PDFImageScaling::getImageType(adjustedImage);
        if (PDFImageScaling::isDownscalingEnabled(m_painter->device(), imageType, hasFeature(PDFRenderer::SmoothImages)))
        {
            QImage downscaledImage = PDFImageScaling::scaleDown(adjustedImage, downscaledSize, imageType);
            if (!downscaledImage.isNull())
            {
                adjustedImage = qMove(downscaledImage);
            }
        }
    }

    if (m_colorConvertor.isActive())
    {
        // Jakub Melka: convert the image after the eventual downscaling above, so we
        // convert as few pixels as possible.
        adjustedImage = m_colorConvertor.convert(qMove(adjustedImage));
    }

    QTransform imageTransform(1.0 / adjustedImage.width(), 0, 0, 1.0 / adjustedImage.height(), 0, 0);
    QTransform worldTransform = imageTransform * m_painter->worldTransform();

    // Because Qt uses opposite axis direction than PDF, then we must transform the y-axis
    // to the opposite (so the image is then unchanged)
    worldTransform.translate(0, adjustedImage.height());
    worldTransform.scale(1, -1);

    m_painter->setWorldTransform(worldTransform);
    m_painter->drawImage(0, 0, adjustedImage);

    m_painter->restore();
}

void PDFPainter::performMeshPainting(const PDFMesh& mesh)
{
    m_painter->save();
    m_painter->setWorldTransform(QTransform());

    if (m_colorConvertor.isActive())
    {
        PDFMesh convertedMesh = mesh;
        convertedMesh.convertColors(m_colorConvertor);
        convertedMesh.paint(m_painter, getEffectiveFillingAlpha());
    }
    else
    {
        mesh.paint(m_painter, getEffectiveFillingAlpha());
    }

    m_painter->restore();
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

void PDFPainter::setWorldMatrix(const QTransform& matrix)
{
    m_painter->setWorldTransform(QTransform(matrix), false);
}

void PDFPainter::setCompositionMode(QPainter::CompositionMode mode)
{
    m_painter->setCompositionMode(mode);
}

PDFPrecompiledPageGenerator::PDFPrecompiledPageGenerator(PDFPrecompiledPage* precompiledPage,
                                                         PDFRenderer::Features features,
                                                         const PDFPage* page,
                                                         const PDFDocument* document,
                                                         const PDFFontCache* fontCache,
                                                         const PDFICCProfileCache* iccProfileCache,
                                                         const PDFCMS* cms,
                                                         const PDFOptionalContentActivity* optionalContentActivity,
                                                         const PDFMeshQualitySettings& meshQualitySettings) :
    BaseClass(features, page, document, fontCache, iccProfileCache, cms, optionalContentActivity, QTransform(), meshQualitySettings),
    m_precompiledPage(precompiledPage)
{
    m_precompiledPage->setPaperColor(cms->getPaperColor());
    m_precompiledPage->getSnapInfo()->addPageMediaBox(page->getRotatedMediaBox());
}

void PDFPrecompiledPageGenerator::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
    Q_ASSERT(stroke || fill);
    Q_ASSERT(path.fillRule() == fillRule);

    QPen pen = stroke ? getCurrentPen() : QPen(Qt::NoPen);
    QBrush brush = fill ? getCurrentBrush() : QBrush(Qt::NoBrush);
    m_precompiledPage->addPath(qMove(pen), qMove(brush), path, text);
}

void PDFPrecompiledPageGenerator::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
    Q_ASSERT(path.fillRule() == fillRule);
    m_precompiledPage->addClip(path);
}

void PDFPrecompiledPageGenerator::performImagePainting(const QImage& image)
{
    if (isContentSuppressed())
    {
        // Content is suppressed, do not paint anything
        return;
    }

    // Add snap info for image to the snapper
    QTransform matrix = getCurrentWorldMatrix();
    PDFSnapInfo* snapInfo = m_precompiledPage->getSnapInfo();
    snapInfo->addImage({
                           matrix.map(QPointF(0.0, 0.0)),
                           matrix.map(QPointF(1.0, 0.0)),
                           matrix.map(QPointF(1.0, 1.0)),
                           matrix.map(QPointF(0.0, 1.0)),
                           matrix.map(QPointF(0.5, 0.5)),
                       }, image);

    if (isTransparencyGroupActive())
    {
        PDFReal alpha = getEffectiveFillingAlpha();
        if (alpha != 1.0)
        {
            // Try to approximate transparency group using alpha channel
            QImage imageWithAlpha = image;
            QImage alphaChannel = imageWithAlpha.convertToFormat(QImage::Format_Alpha8);
            uchar* bits = alphaChannel.bits();

            for (qsizetype i = 0, sizeInBytes = alphaChannel.sizeInBytes(); i < sizeInBytes; ++i)
            {
                bits[i] *= alpha;
            }

            imageWithAlpha.setAlphaChannel(alphaChannel);
            m_precompiledPage->addImage(imageWithAlpha);
            return;
        }
    }

    m_precompiledPage->addImage(image);
}

void PDFPrecompiledPageGenerator::performMeshPainting(const PDFMesh& mesh)
{
    m_precompiledPage->addMesh(mesh, getEffectiveFillingAlpha());
}

void PDFPrecompiledPageGenerator::performSaveGraphicState(PDFPageContentProcessor::ProcessOrder order)
{
    if (order == ProcessOrder::AfterOperation)
    {
        m_precompiledPage->addSaveGraphicState();
    }
}

void PDFPrecompiledPageGenerator::performRestoreGraphicState(PDFPageContentProcessor::ProcessOrder order)
{
    if (order == ProcessOrder::BeforeOperation)
    {
        m_precompiledPage->addRestoreGraphicState();
    }
}

void PDFPrecompiledPageGenerator::setWorldMatrix(const QTransform& matrix)
{
    m_precompiledPage->addSetWorldMatrix(matrix);
}

void PDFPrecompiledPageGenerator::setCompositionMode(QPainter::CompositionMode mode)
{
    m_precompiledPage->addSetCompositionMode(mode);
}

void PDFPrecompiledPage::draw(QPainter* painter,
                              const QRectF& cropBox,
                              const QTransform& pagePointToDevicePointMatrix,
                              PDFRenderer::Features features,
                              PDFReal opacity,
                              PDFScaledImageCache* scaledImageCache) const
{
    Q_ASSERT(painter);
    Q_ASSERT(pagePointToDevicePointMatrix.isInvertible());

    painter->save();
    painter->setWorldTransform(QTransform());
    painter->setOpacity(opacity);

    if (features.testFlag(PDFRenderer::ClipToCropBox))
    {
        if (cropBox.isValid())
        {
            QPainterPath path;
            path.addPolygon(pagePointToDevicePointMatrix.map(cropBox));
            painter->setClipPath(path, Qt::IntersectClip);
        }
    }

    painter->setRenderHint(QPainter::SmoothPixmapTransform, features.testFlag(PDFRenderer::SmoothImages));

    // Process all instructions
    for (const Instruction& instruction : m_instructions)
    {
        switch (instruction.type)
        {
            case InstructionType::DrawPath:
            {
                const PathPaintData& data = m_paths[instruction.dataIndex];

                // Set antialiasing
                const bool antialiasing = (data.isText && features.testFlag(PDFRenderer::TextAntialiasing)) || (!data.isText && features.testFlag(PDFRenderer::Antialiasing));
                painter->setRenderHint(QPainter::Antialiasing, antialiasing);
                painter->setPen(data.pen);
                painter->setBrush(data.brush);
                painter->drawPath(data.path);
                break;
            }

            case InstructionType::DrawImage:
            {
                const ImageData& data = m_images[instruction.dataIndex];

                painter->save();

                // Jakub Melka: a precompiled page is independent on the zoom, so the images
                // are stored in it at their full resolution. The raster paint engines resample
                // from a 2x2 neighborhood only, so we must downscale the image to the target
                // resolution ourselves - otherwise most of the source pixels would never be
                // sampled at all and thin strokes of a bitonal scan would wash out. The
                // downscaled images are memoized in the cache of the caller, because they
                // depend on the zoom and so they cannot become a part of this page. Bitonal
                // images are downscaled even without the feature SmoothImages, but only for
                // a raster target - the redaction replays the page into a pdf writer, which
                // must get the image at its full resolution.
                QImage downscaledImage;
                const QImage* image = &data.image;

                if (PDFImageScaling::isDownscalingEnabled(painter->device(), data.imageType, features.testFlag(PDFRenderer::SmoothImages)))
                {
                    const QSize downscaledSize = PDFImageScaling::getDownscaledSize(image->size(), painter->deviceTransform());
                    if (!downscaledSize.isEmpty())
                    {
                        downscaledImage = scaledImageCache ? scaledImageCache->getScaledImage(*image, downscaledSize, data.imageType)
                                                           : PDFImageScaling::scaleDown(*image, downscaledSize, data.imageType);

                        if (!downscaledImage.isNull())
                        {
                            image = &downscaledImage;
                        }
                    }
                }

                QTransform imageTransform(1.0 / image->width(), 0, 0, 1.0 / image->height(), 0, 0);
                QTransform worldTransform = imageTransform * painter->worldTransform();

                // Jakub Melka: Because Qt uses opposite axis direction than PDF, then we must transform the y-axis
                // to the opposite (so the image is then unchanged)
                worldTransform.translate(0, image->height());
                worldTransform.scale(1, -1);

                painter->setWorldTransform(worldTransform);
                painter->drawImage(0, 0, *image);
                painter->restore();
                break;
            }

            case InstructionType::DrawMesh:
            {
                const MeshPaintData& data = m_meshes[instruction.dataIndex];

                painter->save();
                painter->setWorldTransform(QTransform(pagePointToDevicePointMatrix));
                data.mesh.paint(painter, data.alpha);
                painter->restore();
                break;
            }

            case InstructionType::Clip:
            {
                painter->setClipPath(m_clips[instruction.dataIndex].clipPath, Qt::IntersectClip);
                break;
            }

            case InstructionType::SaveGraphicState:
            {
                painter->save();
                break;
            }

            case InstructionType::RestoreGraphicState:
            {
                painter->restore();
                break;
            }

            case InstructionType::SetWorldMatrix:
            {
                painter->setWorldTransform(QTransform(m_matrices[instruction.dataIndex] * pagePointToDevicePointMatrix));
                break;
            }

            case InstructionType::SetCompositionMode:
            {
                painter->setCompositionMode(m_compositionModes[instruction.dataIndex]);
                break;
            }

            default:
            {
                Q_ASSERT(false);
                break;
            }
        }
    }

    painter->restore();
}

/// The redaction of a precompiled page (plan of step 6 of phase 2, 5.4): the region of the redacted
/// areas and of the content outside of the kept rectangle, and the operations on the kinds of the
/// content (PDFPrecompiledPage::redact)
class PDFPrecompiledPageRedactHelper
{
public:
    /// Width of the outline of a hairline (a cosmetic pen, line width 0: the thinnest line of the
    /// device, ISO 32000-2, 8.4.3.2), which reaches an area, in points of the page
    static constexpr PDFReal HAIRLINE_WIDTH = 0.5;

    /// Margin of the quick test of an object against the region, in points of the page
    static constexpr PDFReal QUICK_TEST_MARGIN = 1.0;

    /// The region of a redaction in the page space, split into its parts for a quick test of an
    /// object (B142): only the parts near an object take part in its boolean operation, so the
    /// operation is fast and its working space (the flattening of the curves of the object, the
    /// grid of the clipper) is the size of the object, not of the page
    class Region
    {
    public:
        explicit Region(const QPainterPath& redactPath, const QRectF& keptRect)
        {
            // The region by its fill rule, normalized: its outer boundaries and its holes are
            // oriented oppositely and do not overlap, so any subset of them, which keeps each hole
            // with its outer boundary, is a part of the region under the non-zero rule (the caller
            // has united its areas, R6-7)
            const QPainterPath united = redactPath.isEmpty() ? QPainterPath() : PDFPathBoolean::unite(redactPath, redactPath);
            for (const QPolygonF& polygon : united.toSubpathPolygons())
            {
                QPainterPath part;
                part.setFillRule(Qt::WindingFill);
                part.addPolygon(polygon);
                part.closeSubpath();
                m_parts.push_back({ part, polygon.boundingRect() });
            }

            if (keptRect.isValid())
            {
                m_keptRect = keptRect.normalized();
            }
        }

        bool isEmpty() const { return m_parts.empty() && !m_keptRect.isValid(); }

        /// The removed region near the box (page space): the parts of the areas and the outside of
        /// the kept rectangle within the box grown by QUICK_TEST_MARGIN; empty = the box is far from
        /// the region
        QPainterPath getLocalRegion(const QRectF& box) const
        {
            const QRectF localBox = getLocalBox(box);
            if (!localBox.isValid())
            {
                return QPainterPath();
            }

            QPainterPath region = getParts(localBox);
            if (m_keptRect.isValid() && !m_keptRect.contains(localBox))
            {
                // The outside of the kept rectangle within the box: the box minus the rectangle
                QPainterPath boxPath;
                boxPath.setFillRule(Qt::WindingFill);
                boxPath.addRect(localBox);
                QPainterPath keptPath;
                keptPath.setFillRule(Qt::WindingFill);
                keptPath.addRect(m_keptRect);
                region.addPath(PDFPathBoolean::subtract(boxPath, keptPath));
            }

            return clipToBox(region, localBox);
        }

        /// The redacted areas inside of the kept rectangle near the box (page space): the pixels,
        /// which get the fill color
        QPainterPath getLocalAreas(const QRectF& box) const
        {
            QRectF localBox = getLocalBox(box);
            if (localBox.isValid() && m_keptRect.isValid())
            {
                localBox = localBox.intersected(m_keptRect);
            }
            if (!localBox.isValid() || localBox.isEmpty())
            {
                return QPainterPath();
            }
            return clipToBox(getParts(localBox), localBox);
        }

    private:
        struct Part
        {
            QPainterPath path;
            QRectF boundingBox;
        };

        static QRectF getLocalBox(const QRectF& box)
        {
            const QRectF normalized = box.normalized();
            if (!std::isfinite(normalized.left()) || !std::isfinite(normalized.top()) || !std::isfinite(normalized.right()) || !std::isfinite(normalized.bottom()))
            {
                return QRectF();
            }
            return normalized.adjusted(-QUICK_TEST_MARGIN, -QUICK_TEST_MARGIN, QUICK_TEST_MARGIN, QUICK_TEST_MARGIN);
        }

        /// The parts, whose boxes meet the box: the outer boundaries and the holes of a normalized
        /// region keep their orientation, so their concatenation under the non-zero rule is the
        /// region (a hole lies in the box of its outer boundary, so it is never taken alone)
        QPainterPath getParts(const QRectF& box) const
        {
            QPainterPath parts;
            parts.setFillRule(Qt::WindingFill);
            for (const Part& part : m_parts)
            {
                if (part.boundingBox.intersects(box))
                {
                    parts.addPath(part.path);
                }
            }
            return parts;
        }

        static QPainterPath clipToBox(const QPainterPath& region, const QRectF& box)
        {
            if (region.isEmpty())
            {
                return QPainterPath();
            }

            QPainterPath boxPath;
            boxPath.setFillRule(Qt::WindingFill);
            boxPath.addRect(box);
            return PDFPathBoolean::intersect(region, boxPath);
        }

        std::vector<Part> m_parts;
        QRectF m_keptRect;
    };

    /// The triangles and the background of a mesh as PDFMesh::paint draws them (the mesh gives no
    /// access to its triangles): a paint device, whose engine records the filled polygons with
    /// their colors and the filled paths (the background); the bounding path is the mesh's own
    class MeshRecorder : public QPaintDevice
    {
    public:
        struct Polygon
        {
            QPolygonF polygon;
            QRgb color = 0;
        };

        struct FilledPath
        {
            QPainterPath path;
            QColor color;
        };

        MeshRecorder() = default;

        virtual QPaintEngine* paintEngine() const override { return const_cast<Engine*>(&m_engine); }

        const std::vector<Polygon>& getPolygons() const { return m_engine.polygons; }
        const std::vector<FilledPath>& getFilledPaths() const { return m_engine.filledPaths; }

    protected:
        virtual int metric(PaintDeviceMetric metric) const override
        {
            switch (metric)
            {
                case PdmWidth:
                case PdmHeight:
                    return 1 << 24;
                case PdmWidthMM:
                case PdmHeightMM:
                    return 1 << 22;
                case PdmNumColors:
                    return std::numeric_limits<int>::max();
                case PdmDepth:
                    return 32;
                case PdmDpiX:
                case PdmDpiY:
                case PdmPhysicalDpiX:
                case PdmPhysicalDpiY:
                    return 72;
                default:
                    return QPaintDevice::metric(metric);
            }
        }

    private:
        class Engine : public QPaintEngine
        {
        public:
            Engine() : QPaintEngine(QPaintEngine::AllFeatures) { }

            using QPaintEngine::drawPolygon;

            virtual bool begin(QPaintDevice* device) override { Q_UNUSED(device); return true; }
            virtual bool end() override { return true; }
            virtual Type type() const override { return QPaintEngine::User; }

            virtual void updateState(const QPaintEngineState& engineState) override
            {
                if (engineState.state() & QPaintEngine::DirtyBrush)
                {
                    brush = engineState.brush();
                }
            }

            virtual void drawPolygon(const QPointF* points, int pointCount, PolygonDrawMode mode) override
            {
                Q_UNUSED(mode);
                if (brush.style() == Qt::NoBrush || pointCount < 3)
                {
                    return;
                }
                polygons.push_back({ QPolygonF(QList<QPointF>(points, points + pointCount)), brush.color().rgba() });
            }

            virtual void drawPath(const QPainterPath& path) override
            {
                if (brush.style() != Qt::NoBrush)
                {
                    filledPaths.push_back({ path, brush.color() });
                }
            }

            virtual void drawPixmap(const QRectF& rectangle, const QPixmap& pixmap, const QRectF& sourceRectangle) override
            {
                Q_UNUSED(rectangle);
                Q_UNUSED(pixmap);
                Q_UNUSED(sourceRectangle);
            }

            std::vector<Polygon> polygons;
            std::vector<FilledPath> filledPaths;
            QBrush brush;
        };

        Engine m_engine;
    };

    /// The scale of the matrix (the length of a unit of the source space in the target space)
    static PDFReal getScale(const QTransform& matrix)
    {
        return std::sqrt(std::abs(matrix.determinant()));
    }

    /// The path of a polygon, filled by the non-zero rule
    static QPainterPath createPolygonPath(const QPolygonF& polygon)
    {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        path.addPolygon(polygon);
        path.closeSubpath();
        return path;
    }

    /// The pixels of the mask (its pixels in \p bounds of the image) of an image of a color table (1 bit,
    /// 8 bits) get the opaque color by its entry of the table: an existing entry keeps the image as it
    /// is, a missing one is added (an image of 1 bit becomes Format_Indexed8 - 8 times its memory, never
    /// 32). \returns false for an image of another format, of no table of two colors at least, or of a
    /// full table (the caller converts it then)
    static bool fillIndexedPixels(QImage* image, const QImage& mask, const QRect& bounds, QColor color)
    {
        const QImage::Format format = image->format();
        const bool isMono = format == QImage::Format_Mono || format == QImage::Format_MonoLSB;
        if (!isMono && format != QImage::Format_Indexed8)
        {
            return false;
        }

        QList<QRgb> table = image->colorTable();
        if (table.size() < 2)
        {
            return false;
        }

        const QRgb value = qRgba(qRed(color.rgb()), qGreen(color.rgb()), qBlue(color.rgb()), 255);
        qsizetype index = table.indexOf(value);
        if (index < 0)
        {
            if (table.size() >= 256)
            {
                return false;
            }
            if (isMono)
            {
                *image = image->convertToFormat(QImage::Format_Indexed8);
                table = image->colorTable();
            }
            index = table.size();
            table.push_back(value);
            image->setColorTable(table);
        }

        const bool isIndexed8 = image->format() == QImage::Format_Indexed8;
        for (int y = 0; y < bounds.height(); ++y)
        {
            const uchar* maskLine = mask.constScanLine(y);
            uchar* imageLine = isIndexed8 ? image->scanLine(bounds.top() + y) : nullptr;
            for (int x = 0; x < bounds.width(); ++x)
            {
                if (!maskLine[x])
                {
                    continue;
                }

                if (imageLine)
                {
                    imageLine[bounds.left() + x] = uchar(index);
                }
                else
                {
                    image->setPixel(bounds.left() + x, bounds.top() + y, uint(index));
                }
            }
        }
        return true;
    }

    /// The pixels of the image in the region (pixel space, a region of the non-zero rule) get the
    /// opaque color (B138). A grown region is grown by one pixel, so every pixel it touches is
    /// changed; otherwise the pixels, whose centers lie in it, are changed (the outside of the crop
    /// box: a pixel, which is mostly visible, stays). The region is filled without antialiasing. The
    /// image keeps its format when it can hold the result (gray of 8 and 16 bits, a color table -
    /// fillIndexedPixels), otherwise it becomes 32 bits per pixel
    /// (an alpha channel only when it had one); an image without a changed pixel is not touched.
    static void fillPixels(QImage* image, const QPainterPath& region, QColor color, bool isGrown)
    {
        if (region.isEmpty() || image->isNull())
        {
            return;
        }

        QPainterPath grown = region;
        grown.setFillRule(Qt::WindingFill);
        if (isGrown)
        {
            QPainterPathStroker stroker;
            stroker.setWidth(2.0);
            stroker.setJoinStyle(Qt::MiterJoin);
            stroker.setCapStyle(Qt::SquareCap);
            grown = PDFPathBoolean::unite(grown, stroker.createStroke(region));
            grown.setFillRule(Qt::WindingFill);
        }

        const QRectF boundingRect = grown.boundingRect();
        if (!std::isfinite(boundingRect.left()) || !std::isfinite(boundingRect.top()) || !std::isfinite(boundingRect.right()) || !std::isfinite(boundingRect.bottom()))
        {
            return;
        }
        const QRect bounds = boundingRect.adjusted(-1.0, -1.0, 1.0, 1.0).toAlignedRect().intersected(image->rect());
        if (bounds.isEmpty())
        {
            return;
        }

        QImage mask(bounds.size(), QImage::Format_Alpha8);
        mask.fill(0);
        {
            QPainter painter(&mask);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.translate(-bounds.topLeft());
            painter.fillPath(grown, QColor(0, 0, 0, 255));
        }

        bool hasPixel = false;
        for (int y = 0; y < bounds.height() && !hasPixel; ++y)
        {
            const uchar* maskLine = mask.constScanLine(y);
            hasPixel = std::any_of(maskLine, maskLine + bounds.width(), [](uchar value) { return value != 0; });
        }
        if (!hasPixel)
        {
            return;
        }

        const bool isGray = qRed(color.rgb()) == qGreen(color.rgb()) && qGreen(color.rgb()) == qBlue(color.rgb());
        if (image->format() == QImage::Format_Grayscale8 && isGray)
        {
            const uchar gray = uchar(qRed(color.rgb()));
            for (int y = 0; y < bounds.height(); ++y)
            {
                const uchar* maskLine = mask.constScanLine(y);
                uchar* imageLine = image->scanLine(bounds.top() + y);
                for (int x = 0; x < bounds.width(); ++x)
                {
                    if (maskLine[x])
                    {
                        imageLine[bounds.left() + x] = gray;
                    }
                }
            }
            return;
        }

        if (image->format() == QImage::Format_Grayscale16 && isGray)
        {
            const quint16 gray = quint16(qRed(color.rgb()) * 257);
            for (int y = 0; y < bounds.height(); ++y)
            {
                const uchar* maskLine = mask.constScanLine(y);
                quint16* imageLine = reinterpret_cast<quint16*>(image->scanLine(bounds.top() + y));
                for (int x = 0; x < bounds.width(); ++x)
                {
                    if (maskLine[x])
                    {
                        imageLine[bounds.left() + x] = gray;
                    }
                }
            }
            return;
        }

        // RS6SEC Low of step 6: an image of a color table (a bitonal scan of 1 bit, an indexed image)
        // keeps it - the color of the fill is an entry of its table (black or white of a scan), else it
        // is added to the table of an image of 8 bits (a scan of 1 bit becomes 8 bits, never 32)
        if (fillIndexedPixels(image, mask, bounds, color))
        {
            return;
        }

        // No transparency is added: QPdfWriter writes an alpha of only 0 and 255 as a mask of 1 bit
        // (/Mask), the paper (white) is the look of an area without a fill
        const QImage::Format format = image->hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
        if (image->format() != format)
        {
            *image = image->convertToFormat(format);
        }

        const QRgb value = qPremultiply(color.rgba() | 0xFF000000u);
        for (int y = 0; y < bounds.height(); ++y)
        {
            const uchar* maskLine = mask.constScanLine(y);
            QRgb* imageLine = reinterpret_cast<QRgb*>(image->scanLine(bounds.top() + y));
            for (int x = 0; x < bounds.width(); ++x)
            {
                if (maskLine[x])
                {
                    imageLine[bounds.left() + x] = value;
                }
            }
        }
    }

    /// The mesh without the region (page space, B140, R6-18): a triangle inside of the region is
    /// dropped (its color is never written), the others are clipped by the bounding path without the
    /// region, the background loses the region. \returns false, when nothing of the mesh is left.
    static bool redactMesh(PDFMesh* mesh, const Region& region)
    {
        MeshRecorder recorder;
        {
            QPainter painter(&recorder);
            mesh->paint(&painter, 1.0);
        }

        QRectF meshBox;
        for (const MeshRecorder::Polygon& polygon : recorder.getPolygons())
        {
            meshBox = meshBox.united(polygon.polygon.boundingRect());
        }
        for (const MeshRecorder::FilledPath& filledPath : recorder.getFilledPaths())
        {
            meshBox = meshBox.united(filledPath.path.boundingRect());
        }

        // The bounding path of the mesh itself: PDFMesh::paint clips by it between save and restore, so
        // the clip of the recorder is cleared again by the restore at its end - a mesh of a shading
        // pattern over the page (8.7.3.3) would lose the path, which it fills, and cover the page
        const QPainterPath clipPath = mesh->getBoundingPath();
        if (!clipPath.isEmpty())
        {
            meshBox = meshBox.intersected(clipPath.boundingRect());
        }

        const QPainterPath localRegion = region.getLocalRegion(meshBox);
        if (localRegion.isEmpty())
        {
            // The mesh does not reach the region
            return true;
        }

        PDFMesh redactedMesh;
        const QRectF localRegionBox = localRegion.boundingRect();
        for (const MeshRecorder::Polygon& polygon : recorder.getPolygons())
        {
            const QRectF polygonBox = polygon.polygon.boundingRect();
            if (polygonBox.intersects(localRegionBox) && PDFPathBoolean::subtract(createPolygonPath(polygon.polygon), localRegion).isEmpty())
            {
                // The triangle is inside of the region
                continue;
            }

            for (int i = 1; i + 1 < polygon.polygon.size(); ++i)
            {
                PDFMesh::Triangle triangle;
                triangle.v1 = redactedMesh.addVertex(polygon.polygon[0]);
                triangle.v2 = redactedMesh.addVertex(polygon.polygon[i]);
                triangle.v3 = redactedMesh.addVertex(polygon.polygon[i + 1]);
                triangle.color = polygon.color;
                redactedMesh.addTriangle(triangle);
            }
        }

        QPainterPath boundingPath = clipPath;
        if (boundingPath.isEmpty())
        {
            boundingPath.addRect(meshBox);
        }
        boundingPath = PDFPathBoolean::subtract(boundingPath, localRegion);
        if (boundingPath.isEmpty())
        {
            return false;
        }
        redactedMesh.setBoundingPath(boundingPath);

        bool hasBackground = false;
        for (const MeshRecorder::FilledPath& filledPath : recorder.getFilledPaths())
        {
            const QPainterPath background = PDFPathBoolean::subtract(filledPath.path, localRegion);
            if (!background.isEmpty())
            {
                redactedMesh.setBackgroundPath(background);
                redactedMesh.setBackgroundColor(filledPath.color);
                hasBackground = true;
            }
        }

        if (redactedMesh.isEmpty() && !hasBackground)
        {
            return false;
        }

        *mesh = std::move(redactedMesh);
        return true;
    }
};

void PDFPrecompiledPage::redact(const QPainterPath& redactPath, const QRectF& keptRect, const QTransform& matrix, QColor imageFillColor)
{
    using Helper = PDFPrecompiledPageRedactHelper;

    const Helper::Region region(redactPath, keptRect);
    if (region.isEmpty())
    {
        // Nothing to be redacted
        return;
    }

    // The matrices map the coordinates of the instructions to the page: SetWorldMatrix sets the
    // matrix of the user space, an instruction before any of them is in the device space of draw
    std::vector<QTransform> matrixStack;
    matrixStack.push_back(matrix.isInvertible() ? matrix.inverted() : QTransform());

    std::vector<Instruction> instructions;
    std::vector<PathPaintData> paths;
    instructions.reserve(m_instructions.size());
    paths.reserve(m_paths.size());

    auto addPathInstruction = [&instructions, &paths](PathPaintData data)
    {
        instructions.emplace_back(InstructionType::DrawPath, paths.size());
        paths.push_back(std::move(data));
    };

    for (const Instruction& instruction : m_instructions)
    {
        const QTransform& userToPage = matrixStack.back();

        switch (instruction.type)
        {
            case InstructionType::DrawPath:
            {
                PathPaintData& data = m_paths[instruction.dataIndex];
                const bool isStroked = data.pen.style() != Qt::NoPen;
                const bool isFilled = data.brush.style() != Qt::NoBrush;
                if (data.path.isEmpty() || (!isStroked && !isFilled))
                {
                    addPathInstruction(std::move(data));
                    break;
                }

                const PDFReal scale = Helper::getScale(userToPage);
                PDFReal strokeMargin = 0.0;
                if (isStroked)
                {
                    const PDFReal width = data.pen.widthF() > 0.0 ? data.pen.widthF() : (scale > 0.0 ? Helper::HAIRLINE_WIDTH / scale : 0.0);
                    strokeMargin = width * qMax(PDFReal(1.0), PDFReal(data.pen.miterLimit())) + width;
                }

                const QRectF userBox = data.path.controlPointRect().adjusted(-strokeMargin, -strokeMargin, strokeMargin, strokeMargin);
                const QPainterPath localRegion = region.getLocalRegion(userToPage.mapRect(userBox));
                if (localRegion.isEmpty())
                {
                    // The path is far from the region (B142)
                    addPathInstruction(std::move(data));
                    break;
                }

                if (!(scale > 0.0) || !userToPage.isInvertible())
                {
                    // A singular matrix paints no area; the path near the region is dropped
                    break;
                }

                const QPainterPath userRegion = userToPage.inverted().map(localRegion);

                // ISO 32000-2, 8.5.3: a path is filled first, then stroked
                if (isFilled)
                {
                    QPainterPath filledPath = PDFPathBoolean::subtract(data.path, userRegion);
                    if (!filledPath.isEmpty())
                    {
                        addPathInstruction(PathPaintData(Qt::NoPen, data.brush, std::move(filledPath), data.isText));
                    }
                }

                if (isStroked)
                {
                    // B141: the outline of the stroke loses the region and is filled by the color of
                    // the pen - an open path stays open, the dashes, the caps and the joins are kept
                    QPainterPathStroker stroker(data.pen);
                    if (!(data.pen.widthF() > 0.0) || data.pen.isCosmetic())
                    {
                        stroker.setWidth(Helper::HAIRLINE_WIDTH / scale);
                    }
                    QPainterPath outline = stroker.createStroke(data.path);
                    outline.setFillRule(Qt::WindingFill);
                    QPainterPath strokedPath = PDFPathBoolean::subtract(outline, userRegion);
                    if (!strokedPath.isEmpty())
                    {
                        addPathInstruction(PathPaintData(Qt::NoPen, data.pen.brush(), std::move(strokedPath), data.isText));
                    }
                }
                break;
            }

            case InstructionType::DrawImage:
            {
                ImageData& data = m_images[instruction.dataIndex];
                QImage& image = data.image;
                if (image.isNull())
                {
                    instructions.push_back(instruction);
                    break;
                }

                // The pixel space of the image to the page (the image is drawn upside down, see draw)
                QTransform pixelToPage = QTransform(1.0 / image.width(), 0, 0, 1.0 / image.height(), 0, 0) * userToPage;
                pixelToPage.translate(0, image.height());
                pixelToPage.scale(1, -1);

                const QRectF imageRect(0, 0, image.width(), image.height());
                const QRectF pageBox = pixelToPage.mapRect(imageRect);
                const QPainterPath localRegion = region.getLocalRegion(pageBox);
                if (localRegion.isEmpty())
                {
                    instructions.push_back(instruction);
                    break;
                }

                if (!pixelToPage.isInvertible() || PDFPathBoolean::subtract(Helper::createPolygonPath(pixelToPage.map(QPolygonF(imageRect))), localRegion).isEmpty())
                {
                    // The whole image is in the region: none of its pixels is written
                    break;
                }

                // The areas inside of the kept rectangle get the fill (the paper without one),
                // grown by a pixel; the rest of the region (outside of the crop box, never shown)
                // gets the paper by the centers of its pixels
                const QColor paperColor(Qt::white);
                const QTransform pageToPixel = pixelToPage.inverted();
                const QPainterPath filledArea = region.getLocalAreas(pageBox);
                Helper::fillPixels(&image, pageToPixel.map(filledArea), imageFillColor.isValid() ? imageFillColor : paperColor, true);
                Helper::fillPixels(&image, pageToPixel.map(PDFPathBoolean::subtract(localRegion, filledArea)), paperColor, false);

                data.imageType = PDFImageScaling::getImageType(image);
                instructions.push_back(instruction);
                break;
            }

            case InstructionType::DrawMesh:
            {
                // The vertices of a mesh are in the page space (draw paints it by the page matrix)
                MeshPaintData& data = m_meshes[instruction.dataIndex];
                if (Helper::redactMesh(&data.mesh, region))
                {
                    instructions.push_back(instruction);
                }
                break;
            }

            case InstructionType::Clip:
            {
                ClipData& data = m_clips[instruction.dataIndex];
                const QPainterPath localRegion = region.getLocalRegion(userToPage.mapRect(data.clipPath.controlPointRect()));
                if (!localRegion.isEmpty() && userToPage.isInvertible())
                {
                    data.clipPath = PDFPathBoolean::subtract(data.clipPath, userToPage.inverted().map(localRegion));
                }
                instructions.push_back(instruction);
                break;
            }

            case InstructionType::SaveGraphicState:
                matrixStack.push_back(userToPage);
                instructions.push_back(instruction);
                break;

            case InstructionType::RestoreGraphicState:
                // B152: a restore without its save keeps the matrix of the page
                if (matrixStack.size() > 1)
                {
                    matrixStack.pop_back();
                }
                instructions.push_back(instruction);
                break;

            case InstructionType::SetWorldMatrix:
                matrixStack.back() = m_matrices[instruction.dataIndex];
                instructions.push_back(instruction);
                break;

            case InstructionType::SetCompositionMode:
                instructions.push_back(instruction);
                break;

            default:
            {
                Q_ASSERT(false);
                break;
            }
        }
    }

    m_instructions = std::move(instructions);
    m_paths = std::move(paths);
}

void PDFPrecompiledPage::addPath(QPen pen, QBrush brush, QPainterPath path, bool isText)
{
    m_instructions.emplace_back(InstructionType::DrawPath, m_paths.size());
    m_paths.emplace_back(qMove(pen), qMove(brush), qMove(path), isText);
}

void PDFPrecompiledPage::addClip(QPainterPath path)
{
    m_instructions.emplace_back(InstructionType::Clip, m_clips.size());
    m_clips.emplace_back(qMove(path));
}

void PDFPrecompiledPage::addImage(QImage image)
{
    // Jakub Melka: determine the type of the image here, when the page is being compiled
    // on a worker thread, and not when the page is drawn - determining it can require
    // a scan of all the pixels of the image.
    const PDFImageScaling::ImageType imageType = PDFImageScaling::getImageType(image);

    m_instructions.emplace_back(InstructionType::DrawImage, m_images.size());
    m_images.emplace_back(qMove(image), imageType);
}

void PDFPrecompiledPage::addMesh(PDFMesh mesh, PDFReal alpha)
{
    m_instructions.emplace_back(InstructionType::DrawMesh, m_meshes.size());
    m_meshes.emplace_back(qMove(mesh), alpha);
}

void PDFPrecompiledPage::addSetWorldMatrix(const QTransform& matrix)
{
    m_instructions.emplace_back(InstructionType::SetWorldMatrix, m_matrices.size());
    m_matrices.push_back(matrix);
}

void PDFPrecompiledPage::addSetCompositionMode(QPainter::CompositionMode compositionMode)
{
    m_instructions.emplace_back(InstructionType::SetCompositionMode, m_compositionModes.size());
    m_compositionModes.push_back(compositionMode);
}

void PDFPrecompiledPage::optimize()
{
    m_instructions.shrink_to_fit();
    m_paths.shrink_to_fit();
    m_clips.shrink_to_fit();
    m_images.shrink_to_fit();
    m_meshes.shrink_to_fit();
    m_matrices.shrink_to_fit();
    m_compositionModes.shrink_to_fit();
}

void PDFPrecompiledPage::convertColors(const PDFColorConvertor& colorConvertor)
{
    // Jakub Melka: we must apply color convertor in following areas:
    //     - painter paths
    //     - images
    //     - meshes

    if (!colorConvertor.isActive())
    {
        return;
    }

    for (PathPaintData& pathData : m_paths)
    {
        if (pathData.pen.style() != Qt::NoPen)
        {
            pathData.pen.setColor(colorConvertor.convert(pathData.pen.color(), false, pathData.isText));
        }
        if (pathData.brush.style() == Qt::SolidPattern)
        {
            pathData.brush.setColor(colorConvertor.convert(pathData.brush.color(), false, pathData.isText));
        }
    }

    for (ImageData& imageData : m_images)
    {
        imageData.image = colorConvertor.convert(imageData.image);

        // Jakub Melka: the conversion can both destroy the bitonal representation of the
        // image (the grayscale mode returns a 32 bit image) and create it (the bitonal
        // mode thresholds a gray scan into a monochromatic image), so the type of the
        // image must be determined again. It is still done just once, here, and not
        // each time the image is drawn.
        imageData.imageType = PDFImageScaling::getImageType(imageData.image);
    }

    for (MeshPaintData& meshPaintData : m_meshes)
    {
        meshPaintData.mesh.convertColors(colorConvertor);
    }

    m_paperColor = colorConvertor.convert(m_paperColor, true, false);
}

void PDFPrecompiledPage::finalize(qint64 compilingTimeNS, QList<PDFRenderError> errors)
{
    m_compilingTimeNS = compilingTimeNS;
    m_errors = qMove(errors);

    // Determine memory consumption
    m_memoryConsumptionEstimate = sizeof(*this);
    m_memoryConsumptionEstimate += sizeof(Instruction) * m_instructions.capacity();
    m_memoryConsumptionEstimate += sizeof(PathPaintData) * m_paths.capacity();
    m_memoryConsumptionEstimate += sizeof(ClipData) * m_clips.capacity();
    m_memoryConsumptionEstimate += sizeof(ImageData) * m_images.capacity();
    m_memoryConsumptionEstimate += sizeof(MeshPaintData) * m_meshes.capacity();
    m_memoryConsumptionEstimate += sizeof(QTransform) * m_matrices.capacity();
    m_memoryConsumptionEstimate += sizeof(QPainter::CompositionMode) * m_compositionModes.capacity();
    m_memoryConsumptionEstimate += sizeof(PDFRenderError) * m_errors.size();

    auto calculateQPathMemoryConsumption = [](const QPainterPath& path)
    {
        return sizeof(QPainterPath::Element) * path.capacity();
    };
    for (const PathPaintData& data : m_paths)
    {
        m_memoryConsumptionEstimate += calculateQPathMemoryConsumption(data.path);
    }
    for (const ClipData& data : m_clips)
    {
        m_memoryConsumptionEstimate += calculateQPathMemoryConsumption(data.clipPath);
    }
    for (const ImageData& data : m_images)
    {
        m_memoryConsumptionEstimate += data.image.sizeInBytes();
    }
    for (const MeshPaintData& data : m_meshes)
    {
        m_memoryConsumptionEstimate += data.mesh.getMemoryConsumptionEstimate();
    }
}

/// The hash of the pixels of an image for the comparison of pages: only the bytes of the pixels,
/// never the padding at the end of a scan line (QImage leaves it uninitialized, so the same image
/// would give a different hash after each compilation)
class PDFPrecompiledPageImageHashHelper
{
public:
    static void writePixels(QDataStream& stream, const QImage& image)
    {
        stream << qint32(image.width()) << qint32(image.height()) << qint32(image.format());

        const qsizetype bitsPerLine = qsizetype(image.width()) * image.depth();
        const qsizetype fullBytes = bitsPerLine / 8;
        const int remainingBits = int(bitsPerLine % 8);
        const bool isLeastSignificantBitFirst = image.format() == QImage::Format_MonoLSB;

        for (int y = 0; y < image.height(); ++y)
        {
            const uchar* scanLine = image.constScanLine(y);
            stream.writeRawData(reinterpret_cast<const char*>(scanLine), int(fullBytes));

            if (remainingBits > 0)
            {
                // Only the used bits of the last byte; the order of the bits depends on the format
                const uchar mask = isLeastSignificantBitFirst ? uchar((1u << remainingBits) - 1u) : uchar(0xFFu << (8 - remainingBits));
                stream << quint8(scanLine[fullBytes] & mask);
            }
        }
    }
};

PDFPrecompiledPage::GraphicPieceInfos PDFPrecompiledPage::calculateGraphicPieceInfos(QRectF mediaBox,
                                                                                     PDFReal epsilon) const
{
    GraphicPieceInfos infos;

    struct State
    {
        QTransform matrix;
    };
    std::stack<State> stateStack;
    stateStack.emplace();

    // Check, if epsilon is not too small
    if (qFuzzyIsNull(epsilon))
    {
        epsilon = 0.000001;
    }
    PDFReal factor = 1.0 / epsilon;

    QImage shadingTestImage;

    // Process all instructions
    for (const Instruction& instruction : m_instructions)
    {
        switch (instruction.type)
        {
            case InstructionType::DrawPath:
            {
                const PathPaintData& data = m_paths[instruction.dataIndex];

                GraphicPieceInfo info;
                QByteArray serializedPath;

                // Serialize data
                if (true)
                {
                    QDataStream stream(&serializedPath, QIODevice::WriteOnly);

                    stream << data.isText;
                    stream << data.pen;
                    stream << data.brush;

                    // Translate map to page coordinates
                    QPainterPath pagePath = stateStack.top().matrix.map(data.path);

                    info.type = data.isText ? GraphicPieceInfo::Type::Text : GraphicPieceInfo::Type::VectorGraphics;
                    info.boundingRect = pagePath.controlPointRect();
                    info.pagePath = pagePath;

                    const int elementCount = pagePath.elementCount();
                    for (int i = 0; i < elementCount; ++i)
                    {
                        QPainterPath::Element element = pagePath.elementAt(i);

                        PDFReal roundedX = qFloor(element.x * factor);
                        PDFReal roundedY = qFloor(element.y * factor);

                        stream << roundedX;
                        stream << roundedY;
                        stream << element.type;
                    }
                }

                QByteArray hash = QCryptographicHash::hash(serializedPath, QCryptographicHash::Sha512);
                Q_ASSERT(QCryptographicHash::hashLength(QCryptographicHash::Sha512) == 64);

                size_t size = qMin<size_t>(hash.length(), info.hash.size());
                std::copy(hash.data(), hash.data() + size, info.hash.data());

                infos.emplace_back(std::move(info));
                break;
            }

            case InstructionType::DrawImage:
            {
                const ImageData& data = m_images[instruction.dataIndex];
                const QImage& image = data.image;

                GraphicPieceInfo info;
                QByteArray serializedPath;
                QByteArray serializedImage;

                // Serialize data
                if (true)
                {
                    QDataStream stream(&serializedPath, QIODevice::WriteOnly);
                    QDataStream streamImage(&serializedImage, QIODevice::WriteOnly);

                    // Jakub Melka: serialize image position
                    QTransform worldMatrix = stateStack.top().matrix;

                    QPainterPath pagePath;
                    pagePath.addRect(0, 0, 1, 1);
                    pagePath = worldMatrix.map(pagePath);

                    info.type = GraphicPieceInfo::Type::Image;
                    info.boundingRect = pagePath.controlPointRect();
                    info.pagePath = pagePath;

                    const int elementCount = pagePath.elementCount();
                    for (int i = 0; i < elementCount; ++i)
                    {
                        QPainterPath::Element element = pagePath.elementAt(i);

                        PDFReal roundedX = qRound(element.x * factor);
                        PDFReal roundedY = qRound(element.y * factor);

                        stream << roundedX;
                        stream << roundedY;
                        stream << element.type;
                    }

                    // serialize image data
                    PDFPrecompiledPageImageHashHelper::writePixels(stream, image);
                    PDFPrecompiledPageImageHashHelper::writePixels(streamImage, image);
                }

                QByteArray hash = QCryptographicHash::hash(serializedPath, QCryptographicHash::Sha512);
                Q_ASSERT(QCryptographicHash::hashLength(QCryptographicHash::Sha512) == 64);

                QByteArray imageHash = QCryptographicHash::hash(serializedImage, QCryptographicHash::Sha512);

                size_t size = qMin<size_t>(hash.length(), info.hash.size());
                std::copy(hash.data(), hash.data() + size, info.hash.data());

                size_t sizeImage = qMin<size_t>(imageHash.length(), info.imageHash.size());
                std::copy(imageHash.data(), imageHash.data() + sizeImage, info.imageHash.data());

                infos.emplace_back(std::move(info));
                break;
            }

            case InstructionType::DrawMesh:
            {
                const MeshPaintData& data = m_meshes[instruction.dataIndex];

                if (shadingTestImage.isNull())
                {
                    QSizeF mediaBoxSize = mediaBox.size();
                    mediaBoxSize = mediaBoxSize.scaled(256, 256, Qt::KeepAspectRatio);
                    QSize imageSize = mediaBoxSize.toSize();
                    shadingTestImage = QImage(imageSize, QImage::Format_ARGB32);
                }

                shadingTestImage.fill(Qt::transparent);

                QTransform pagePointToDevicePointMatrix;
                pagePointToDevicePointMatrix.scale(shadingTestImage.width() / mediaBox.width(), -shadingTestImage.height() / mediaBox.height());

                {
                    QPainter painter(&shadingTestImage);
                    painter.setWorldTransform(pagePointToDevicePointMatrix);
                    data.mesh.paint(&painter, data.alpha);
                }

                GraphicPieceInfo info;
                QByteArray serializedMesh;

                // Serialize data
                if (true)
                {
                    QDataStream stream(&serializedMesh, QIODevice::WriteOnly);

                    // serialize image data
                    PDFPrecompiledPageImageHashHelper::writePixels(stream, shadingTestImage);
                }

                QByteArray hash = QCryptographicHash::hash(serializedMesh, QCryptographicHash::Sha512);
                Q_ASSERT(QCryptographicHash::hashLength(QCryptographicHash::Sha512) == 64);

                size_t size = qMin<size_t>(hash.length(), info.hash.size());
                std::copy(hash.data(), hash.data() + size, info.hash.data());

                info.boundingRect = QRectF();
                info.type = GraphicPieceInfo::Type::Shading;
                infos.emplace_back(std::move(info));
                break;
            }

            case InstructionType::Clip:
            {
                // Do nothing, we are just collecting information
                break;
            }

            case InstructionType::SaveGraphicState:
            {
                stateStack.push(stateStack.top());
                break;
            }

            case InstructionType::RestoreGraphicState:
            {
                stateStack.pop();
                break;
            }

            case InstructionType::SetWorldMatrix:
            {
                stateStack.top().matrix = m_matrices[instruction.dataIndex];
                break;
            }

            case InstructionType::SetCompositionMode:
            {
                // Do nothing, we are just collecting information
                break;
            }

            default:
            {
                Q_ASSERT(false);
                break;
            }
        }
    }

    return infos;
}

}   // namespace pdf
