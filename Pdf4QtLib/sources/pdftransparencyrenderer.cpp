//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftransparencyrenderer.h"
#include "pdfdocument.h"
#include "pdfcms.h"

namespace pdf
{

PDFFloatBitmap::PDFFloatBitmap() :
    m_width(0),
    m_height(0),
    m_pixelSize(0)
{

}

PDFFloatBitmap::PDFFloatBitmap(size_t width, size_t height, PDFPixelFormat format) :
    m_format(format),
    m_width(width),
    m_height(height),
    m_pixelSize(format.getChannelCount())
{
    Q_ASSERT(format.isValid());

    m_data.resize(format.calculateBitmapDataLength(width, height), static_cast<PDFColorComponent>(0.0f));
}

PDFColorBuffer PDFFloatBitmap::getPixel(size_t x, size_t y)
{
    const size_t index = getPixelIndex(x, y);
    return PDFColorBuffer(m_data.data() + index, m_pixelSize);
}

PDFConstColorBuffer PDFFloatBitmap::getPixel(size_t x, size_t y) const
{
    const size_t index = getPixelIndex(x, y);
    return PDFConstColorBuffer(m_data.data() + index, m_pixelSize);
}

PDFColorBuffer PDFFloatBitmap::getPixels()
{
    return PDFColorBuffer(m_data.data(), m_data.size());
}

const PDFColorComponent* PDFFloatBitmap::begin() const
{
    return m_data.data();
}

const PDFColorComponent* PDFFloatBitmap::end() const
{
    return m_data.data() + m_data.size();
}

PDFColorComponent* PDFFloatBitmap::begin()
{
    return m_data.data();
}

PDFColorComponent* PDFFloatBitmap::end()
{
    return m_data.data() + m_data.size();
}

void PDFFloatBitmap::makeTransparent()
{
    if (m_format.hasShapeChannel())
    {
        fillChannel(m_format.getShapeChannelIndex(), 0.0f);
    }

    if (m_format.hasOpacityChannel())
    {
        fillChannel(m_format.getOpacityChannelIndex(), 0.0f);
    }
}

void PDFFloatBitmap::makeOpaque()
{
    if (m_format.hasShapeChannel())
    {
        fillChannel(m_format.getShapeChannelIndex(), 1.0f);
    }

    if (m_format.hasOpacityChannel())
    {
        fillChannel(m_format.getOpacityChannelIndex(), 1.0f);
    }
}

size_t PDFFloatBitmap::getPixelIndex(size_t x, size_t y) const
{
    return (y * m_width + x) * m_pixelSize;
}

PDFFloatBitmap PDFFloatBitmap::extractProcessColors()
{
    PDFPixelFormat format = PDFPixelFormat::createFormat(m_format.getProcessColorChannelCount(), 0, false, m_format.hasProcessColorsSubtractive());
    PDFFloatBitmap result(getWidth(), getHeight(), format);

    for (size_t x = 0; x < getWidth(); ++x)
    {
        for (size_t y = 0; y < getHeight(); ++y)
        {
            PDFColorBuffer sourceProcessColorBuffer = getPixel(x, y);
            PDFColorBuffer targetProcessColorBuffer = result.getPixel(x, y);

            Q_ASSERT(sourceProcessColorBuffer.size() >= targetProcessColorBuffer.size());
            std::copy(sourceProcessColorBuffer.cbegin(), std::next(sourceProcessColorBuffer.cbegin(), targetProcessColorBuffer.size()), targetProcessColorBuffer.begin());
        }
    }

    return result;
}

void PDFFloatBitmap::blend(const PDFFloatBitmap& source,
                           PDFFloatBitmap& target,
                           const PDFFloatBitmap& backdrop,
                           const PDFFloatBitmap& initialBackdrop,
                           PDFFloatBitmap& softMask,
                           bool alphaIsShape,
                           PDFColorComponent constantAlpha,
                           BlendMode mode,
                           bool knockoutGroup,
                           uint32_t activeColorChannels,
                           OverprintMode overprintMode)
{
    Q_ASSERT(source.getWidth() == target.getWidth());
    Q_ASSERT(source.getHeight() == target.getHeight());
    Q_ASSERT(source.getPixelFormat() == target.getPixelFormat());
    Q_ASSERT(source.getWidth() == softMask.getWidth());
    Q_ASSERT(source.getHeight() == softMask.getHeight());
    Q_ASSERT(softMask.getPixelFormat() == PDFPixelFormat::createOpacityMask());

    const size_t width = source.getWidth();
    const size_t height = source.getHeight();
    const PDFPixelFormat pixelFormat = source.getPixelFormat();
    const uint8_t shapeChannel = pixelFormat.getShapeChannelIndex();
    const uint8_t opacityChannel = pixelFormat.getOpacityChannelIndex();
    const uint8_t colorChannelStart = pixelFormat.getColorChannelIndexStart();
    const uint8_t colorChannelEnd = pixelFormat.getColorChannelIndexEnd();
    const uint8_t processColorChannelStart = pixelFormat.getProcessColorChannelIndexStart();
    const uint8_t processColorChannelEnd = pixelFormat.getProcessColorChannelIndexEnd();
    const uint8_t spotColorChannelStart = pixelFormat.getSpotColorChannelIndexStart();
    const uint8_t spotColorChannelEnd = pixelFormat.getSpotColorChannelIndexEnd();
    std::vector<PDFColorComponent> B_i(source.getPixelSize(), 0.0f);
    std::vector<BlendMode> channelBlendModes(source.getPixelSize(), mode);

    // For blending spot colors, only white preserving blend modes are possible.
    // If this is not the case, revert spot color blend mode to normal blending.
    // See 11.7.4.2 of PDF 2.0 specification.
    if (pixelFormat.hasSpotColors() && !PDFBlendModeInfo::isWhitePreserving(mode))
    {
        auto itBegin = std::next(channelBlendModes.begin(), spotColorChannelStart);
        auto itEnd = std::next(channelBlendModes.begin(), spotColorChannelEnd);
        std::fill(itBegin, itEnd, BlendMode::Normal);
    }

    // Handle overprint mode for normal blend mode. We do not support
    // oveprinting for other blend modes, than normal.

    switch (overprintMode)
    {
        case OverprintMode::NoOveprint:
            break;

        case OverprintMode::Overprint_Mode_0:
        {
            // Select source color, if channel is active,
            // otherwise select backdrop color.
            for (uint8_t colorChannelIndex = colorChannelStart; colorChannelIndex < colorChannelEnd; ++colorChannelIndex)
            {
                uint32_t flag = (static_cast<uint32_t>(1)) << colorChannelIndex;
                if (channelBlendModes[colorChannelIndex] == BlendMode::Normal && !(activeColorChannels & flag))
                {
                    // Color channel is inactive
                    channelBlendModes[colorChannelIndex] = BlendMode::Overprint_SelectBackdrop;
                }
            }

            break;
        }

        case OverprintMode::Overprint_Mode_1:
        {
            // For process colors, select source color, if it is nonzero,
            // otherwise select backdrop. If process color channel is inactive,
            // select backdrop.
            if (pixelFormat.hasProcessColors() && mode == BlendMode::Normal)
            {
                for (uint8_t colorChannelIndex = processColorChannelStart; colorChannelIndex < processColorChannelEnd; ++colorChannelIndex)
                {
                    uint32_t flag = (static_cast<uint32_t>(1)) << colorChannelIndex;
                    if (!(activeColorChannels & flag))
                    {
                        // Color channel is inactive
                        channelBlendModes[colorChannelIndex] = BlendMode::Overprint_SelectBackdrop;
                    }
                    else
                    {
                        // Color channel is active, but select source color only, if it is nonzero
                        channelBlendModes[colorChannelIndex] = pixelFormat.hasSpotColorsSubtractive() ? BlendMode::Overprint_SelectNonOneSourceOrBackdrop
                                                                                                      : BlendMode::Overprint_SelectNonZeroSourceOrBackdrop;
                    }
                }
            }

            if (pixelFormat.hasSpotColors())
            {
                // For spot colors, select backdrop, if channel is inactive,
                // otherwise select source color.
                for (uint8_t colorChannelIndex = spotColorChannelStart; colorChannelIndex < spotColorChannelEnd; ++colorChannelIndex)
                {
                    uint32_t flag = (static_cast<uint32_t>(1)) << colorChannelIndex;
                    if (channelBlendModes[colorChannelIndex] == BlendMode::Normal && !(activeColorChannels & flag))
                    {
                        // Color channel is inactive
                        channelBlendModes[colorChannelIndex] = BlendMode::Overprint_SelectBackdrop;
                    }
                }
            }

            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    for (size_t x = 0; x < width; ++x)
    {
        for (size_t y = 0; y < height; ++y)
        {
            PDFConstColorBuffer sourceColor = source.getPixel(x, y);
            PDFColorBuffer targetColor = target.getPixel(x, y);
            PDFConstColorBuffer backdropColor = backdrop.getPixel(x, y);
            PDFConstColorBuffer initialBackdropColor = initialBackdrop.getPixel(x, y);
            PDFColorBuffer alphaColorBuffer = softMask.getPixel(x, y);

            const PDFColorComponent softMaskValue = alphaColorBuffer[0];
            const PDFColorComponent f_j_i = sourceColor[shapeChannel];
            const PDFColorComponent f_m_i = alphaIsShape ? softMaskValue : 1.0f;
            const PDFColorComponent f_k_i = alphaIsShape ? constantAlpha : 1.0f;
            const PDFColorComponent q_m_i = !alphaIsShape ? softMaskValue : 1.0f;
            const PDFColorComponent q_k_i = !alphaIsShape ? constantAlpha : 1.0f;
            const PDFColorComponent f_s_i = f_j_i * f_m_i * f_k_i;
            const PDFColorComponent alpha_j_i = sourceColor[opacityChannel];
            const PDFColorComponent alpha_s_i = alpha_j_i * (f_m_i * q_m_i) * (f_k_i * q_k_i);

            // Old alpha (alpha_g_i_1) is stored in target (immediate) buffer
            const PDFColorComponent alpha_g_i_1 = targetColor[opacityChannel];

            // alpha_g_0 == 0.0f according to the specification, otherwise select alpha_g_i_1 from target color
            const PDFColorComponent alpha_g_b = knockoutGroup ? 0.0f : alpha_g_i_1;

            // alpha_0 is taken from initial backdrop color buffer
            const PDFColorComponent alpha_0 = initialBackdropColor[opacityChannel];

            // f_g_i_1 is stored in target (immediate) buffer
            const PDFColorComponent f_g_i_1 = targetColor[shapeChannel];

            // Formulas taken from
            const PDFColorComponent f_g_i = PDFBlendFunction::blend_Union(f_g_i_1, f_s_i);
            const PDFColorComponent alpha_g_i = (1.0f - f_s_i) * alpha_g_i_1 + (f_s_i - alpha_s_i) * alpha_g_b + alpha_s_i;
            const PDFColorComponent alpha_i_1 = PDFBlendFunction::blend_Union(alpha_0, alpha_g_i_1);
            const PDFColorComponent alpha_i = PDFBlendFunction::blend_Union(alpha_0, alpha_g_i);

            // alpha_b is either alpha_0 (for knockout group) or alpha_i_1
            const PDFColorComponent alpha_b = knockoutGroup ? alpha_0 : alpha_i_1;

            if (qFuzzyIsNull(alpha_g_i))
            {
                // If alpha_i is zero, then color is undefined, just fill shape/opacity
                targetColor[shapeChannel] = f_g_i;
                targetColor[opacityChannel] = alpha_g_i;
                continue;
            }

            std::fill(B_i.begin(), B_i.end(), 0.0f);

            // Calculate blended pixel
            if (PDFBlendModeInfo::isSeparable(mode))
            {
                // Separable blend mode - process each color separately
                const bool isProcessColorSubtractive = pixelFormat.hasProcessColorsSubtractive();
                const bool isSpotColorSubtractive = pixelFormat.hasSpotColorsSubtractive();

                if (pixelFormat.hasProcessColors())
                {
                    if (!isProcessColorSubtractive)
                    {
                        for (uint8_t i = pixelFormat.getProcessColorChannelIndexStart(); i < pixelFormat.getProcessColorChannelIndexEnd(); ++i)
                        {
                            B_i[i] = PDFBlendFunction::blend(channelBlendModes[i], backdropColor[i], sourceColor[i]);
                        }
                    }
                    else
                    {
                        for (uint8_t i = pixelFormat.getProcessColorChannelIndexStart(); i < pixelFormat.getProcessColorChannelIndexEnd(); ++i)
                        {
                            B_i[i] = 1.0f - PDFBlendFunction::blend(channelBlendModes[i], 1.0f - backdropColor[i], 1.0f - sourceColor[i]);
                        }
                    }
                }

                if (pixelFormat.hasSpotColors())
                {

                    if (!isSpotColorSubtractive)
                    {
                        for (uint8_t i = pixelFormat.getSpotColorChannelIndexStart(); i < pixelFormat.getSpotColorChannelIndexEnd(); ++i)
                        {
                            B_i[i] = PDFBlendFunction::blend(channelBlendModes[i], backdropColor[i], sourceColor[i]);
                        }
                    }
                    else
                    {
                        for (uint8_t i = pixelFormat.getSpotColorChannelIndexStart(); i < pixelFormat.getSpotColorChannelIndexEnd(); ++i)
                        {
                            B_i[i] = 1.0f - PDFBlendFunction::blend(channelBlendModes[i], 1.0f - backdropColor[i], 1.0f - sourceColor[i]);
                        }
                    }
                }
            }
            else
            {
                // Nonseparable blend mode - process colors together
                if (pixelFormat.hasProcessColors())
                {
                    switch (pixelFormat.getProcessColorChannelCount())
                    {
                        case 1:
                        {
                            // Gray
                            const PDFGray Cb = backdropColor[pixelFormat.getProcessColorChannelIndexStart()];
                            const PDFGray Cs = sourceColor[pixelFormat.getProcessColorChannelIndexStart()];
                            const PDFGray blended = PDFBlendFunction::blend_Nonseparable(mode, Cb, Cs);
                            B_i[pixelFormat.getProcessColorChannelIndexStart()] = blended;
                            break;
                        }

                        case 3:
                        {
                            // RGB
                            const PDFRGB Cb = { backdropColor[pixelFormat.getProcessColorChannelIndexStart() + 0],
                                                backdropColor[pixelFormat.getProcessColorChannelIndexStart() + 1],
                                                backdropColor[pixelFormat.getProcessColorChannelIndexStart() + 2] };
                            const PDFRGB Cs = { sourceColor[pixelFormat.getProcessColorChannelIndexStart() + 0],
                                                sourceColor[pixelFormat.getProcessColorChannelIndexStart() + 1],
                                                sourceColor[pixelFormat.getProcessColorChannelIndexStart() + 2] };
                            const PDFRGB blended = PDFBlendFunction::blend_Nonseparable(mode, Cb, Cs);
                            B_i[pixelFormat.getProcessColorChannelIndexStart() + 0] = blended[0];
                            B_i[pixelFormat.getProcessColorChannelIndexStart() + 1] = blended[1];
                            B_i[pixelFormat.getProcessColorChannelIndexStart() + 2] = blended[2];
                            break;
                        }

                        case 4:
                        {
                            // CMYK
                            const PDFCMYK Cb = { backdropColor[pixelFormat.getProcessColorChannelIndexStart() + 0],
                                                 backdropColor[pixelFormat.getProcessColorChannelIndexStart() + 1],
                                                 backdropColor[pixelFormat.getProcessColorChannelIndexStart() + 2],
                                                 backdropColor[pixelFormat.getProcessColorChannelIndexStart() + 3] };
                            const PDFCMYK Cs = { sourceColor[pixelFormat.getProcessColorChannelIndexStart() + 0],
                                                 sourceColor[pixelFormat.getProcessColorChannelIndexStart() + 1],
                                                 sourceColor[pixelFormat.getProcessColorChannelIndexStart() + 2],
                                                 sourceColor[pixelFormat.getProcessColorChannelIndexStart() + 3] };
                            const PDFCMYK blended = PDFBlendFunction::blend_Nonseparable(mode, Cb, Cs);
                            B_i[pixelFormat.getProcessColorChannelIndexStart() + 0] = blended[0];
                            B_i[pixelFormat.getProcessColorChannelIndexStart() + 1] = blended[1];
                            B_i[pixelFormat.getProcessColorChannelIndexStart() + 2] = blended[2];
                            B_i[pixelFormat.getProcessColorChannelIndexStart() + 3] = blended[3];
                            break;
                        }

                        default:
                        {
                            // This is a serious error. Blended buffer remains unchanged (zero)
                            Q_ASSERT(false);
                            break;
                        }
                    }
                }

                if (pixelFormat.hasSpotColors())
                {
                    const bool isSpotColorSubtractive = pixelFormat.hasSpotColorsSubtractive();
                    if (!isSpotColorSubtractive)
                    {
                        for (uint8_t i = pixelFormat.getSpotColorChannelIndexStart(); i < pixelFormat.getSpotColorChannelIndexEnd(); ++i)
                        {
                            B_i[i] = PDFBlendFunction::blend(channelBlendModes[i], backdropColor[i], sourceColor[i]);
                        }
                    }
                    else
                    {
                        for (uint8_t i = pixelFormat.getSpotColorChannelIndexStart(); i < pixelFormat.getSpotColorChannelIndexEnd(); ++i)
                        {
                            B_i[i] = 1.0f - PDFBlendFunction::blend(channelBlendModes[i], 1.0f - backdropColor[i], 1.0f - sourceColor[i]);
                        }
                    }
                }
            }

            for (uint8_t i = colorChannelStart; i < colorChannelEnd; ++i)
            {
                const PDFColorComponent C_s_i = sourceColor[i];
                const PDFColorComponent C_b = backdropColor[i];
                const PDFColorComponent C_i_1 = targetColor[i];

                PDFColorComponent C_t = (f_s_i - alpha_s_i) * alpha_b * C_b + alpha_s_i * ((1.0f - alpha_b) * C_s_i + alpha_b * B_i[i]);
                PDFColorComponent C_i = ((1.0f - f_s_i) * alpha_i_1 * C_i_1 + C_t) / alpha_i;

                targetColor[i] = C_i;
            }

            targetColor[shapeChannel] = f_g_i;
            targetColor[opacityChannel] = alpha_g_i;
        }
    }
}

void PDFFloatBitmap::fillChannel(size_t channel, PDFColorComponent value)
{
    // Do we have just one channel?
    if (m_format.getChannelCount() == 1)
    {
        Q_ASSERT(channel == 0);
        std::fill(m_data.begin(), m_data.end(), value);
        return;
    }

    for (PDFColorComponent* pixel = begin(); pixel != end(); pixel += m_pixelSize)
    {
        pixel[channel] = value;
    }
}

PDFFloatBitmapWithColorSpace::PDFFloatBitmapWithColorSpace()
{

}

PDFFloatBitmapWithColorSpace::PDFFloatBitmapWithColorSpace(size_t width, size_t height, PDFPixelFormat format, PDFColorSpacePointer blendColorSpace) :
    PDFFloatBitmap(width, height, format),
    m_colorSpace(blendColorSpace)
{
    Q_ASSERT(!blendColorSpace || blendColorSpace->isBlendColorSpace());
}

PDFColorSpacePointer PDFFloatBitmapWithColorSpace::getColorSpace() const
{
    return m_colorSpace;
}

void PDFFloatBitmapWithColorSpace::setColorSpace(const PDFColorSpacePointer& colorSpace)
{
    m_colorSpace = colorSpace;
}

void PDFFloatBitmapWithColorSpace::convertToColorSpace(const PDFCMS* cms,
                                                       RenderingIntent intent,
                                                       const PDFColorSpacePointer& targetColorSpace,
                                                       PDFRenderErrorReporter* reporter)
{
    Q_ASSERT(m_colorSpace);
    if (m_colorSpace->equals(targetColorSpace.get()))
    {
        return;
    }

    const uint8_t targetDeviceColors = static_cast<uint8_t>(targetColorSpace->getColorComponentCount());
    PDFPixelFormat newFormat = getPixelFormat();
    newFormat.setProcessColors(targetDeviceColors);
    newFormat.setProcessColorsSubtractive(targetDeviceColors == 4);

    PDFFloatBitmap sourceProcessColors = extractProcessColors();
    PDFFloatBitmap targetProcessColors(sourceProcessColors.getWidth(), sourceProcessColors.getHeight(), PDFPixelFormat::createFormat(targetDeviceColors, 0, false, newFormat.hasProcessColorsSubtractive()));

    if (!PDFAbstractColorSpace::transform(m_colorSpace.data(), targetColorSpace.data(), cms, intent, sourceProcessColors.getPixels(), targetProcessColors.getPixels(), reporter))
    {
        reporter->reportRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Transformation between blending color spaces failed."));
    }

    PDFFloatBitmapWithColorSpace temporary(getWidth(), getHeight(), newFormat, targetColorSpace);
    for (size_t x = 0; x < getWidth(); ++x)
    {
        for (size_t y = 0; y < getHeight(); ++y)
        {
            PDFColorBuffer sourceProcessColorBuffer = targetProcessColors.getPixel(x, y);
            PDFColorBuffer sourceSpotColorAndOpacityBuffer = getPixel(x, y);
            PDFColorBuffer targetBuffer = temporary.getPixel(x, y);

            Q_ASSERT(sourceProcessColorBuffer.size() <= targetBuffer.size());

            // Copy process colors
            auto targetIt = targetBuffer.begin();
            targetIt = std::copy(sourceProcessColorBuffer.cbegin(), sourceProcessColorBuffer.cend(), targetIt);

            Q_ASSERT(std::distance(targetIt, targetBuffer.end()) == temporary.getPixelFormat().getSpotColorChannelCount() + temporary.getPixelFormat().getAuxiliaryChannelCount());

            auto sourceIt = std::next(sourceSpotColorAndOpacityBuffer.cbegin(), temporary.getPixelFormat().getProcessColorChannelCount());
            targetIt = std::copy(sourceIt, sourceSpotColorAndOpacityBuffer.cend(), targetIt);

            Q_ASSERT(targetIt == targetBuffer.cend());
        }
    }

    *this = qMove(temporary);
}

PDFTransparencyRenderer::PDFTransparencyRenderer(const PDFPage* page,
                                                 const PDFDocument* document,
                                                 const PDFFontCache* fontCache,
                                                 const PDFCMS* cms,
                                                 const PDFOptionalContentActivity* optionalContentActivity,
                                                 const PDFInkMapper* inkMapper,
                                                 QMatrix pagePointToDevicePointMatrix) :
    BaseClass(page, document, fontCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, PDFMeshQualitySettings()),
    m_inkMapper(inkMapper),
    m_active(false)
{
    m_deviceColorSpace.reset(new PDFDeviceRGBColorSpace());
    m_processColorSpace.reset(new PDFDeviceCMYKColorSpace());
}

void PDFTransparencyRenderer::setDeviceColorSpace(PDFColorSpacePointer colorSpace)
{
    if (!colorSpace || colorSpace->isBlendColorSpace())
    {
        // Set device color space only, when it is a blend color space
        m_deviceColorSpace = colorSpace;
    }
}

void PDFTransparencyRenderer::setProcessColorSpace(PDFColorSpacePointer colorSpace)
{
    if (!colorSpace || colorSpace->isBlendColorSpace())
    {
        // Set process color space only, when it is a blend color space
        m_processColorSpace = colorSpace;
    }
}

void PDFTransparencyRenderer::beginPaint(QSize pixelSize)
{
    Q_ASSERT(!m_active);
    m_active = true;

    Q_ASSERT(pixelSize.isValid());
    Q_ASSERT(m_deviceColorSpace);
    Q_ASSERT(m_processColorSpace);

    PDFPixelFormat pixelFormat = PDFPixelFormat::createFormat(uint8_t(m_deviceColorSpace->getColorComponentCount()),
                                                              uint8_t(m_inkMapper->getActiveSpotColorCount()),
                                                              true, m_deviceColorSpace->getColorComponentCount() == 4);

    PDFTransparencyGroupPainterData deviceGroup;
    deviceGroup.alphaIsShape = getGraphicState()->getAlphaIsShape();
    deviceGroup.alphaStroke = getGraphicState()->getAlphaStroking();
    deviceGroup.alphaFill = getGraphicState()->getAlphaFilling();
    deviceGroup.blendMode = getGraphicState()->getBlendMode();
    deviceGroup.blackPointCompensationMode = getGraphicState()->getBlackPointCompensationMode();
    deviceGroup.renderingIntent = RenderingIntent::RelativeColorimetric;
    deviceGroup.initialBackdrop = PDFFloatBitmapWithColorSpace(pixelSize.width(), pixelSize.height(), pixelFormat, m_deviceColorSpace);
    deviceGroup.immediateBackdrop = deviceGroup.initialBackdrop;
    deviceGroup.blendColorSpace = m_deviceColorSpace;

    m_transparencyGroupDataStack.emplace_back(qMove(deviceGroup));

    // Create page transparency group
    PDFObject pageTransparencyGroupObject = getPage()->getTransparencyGroup(&getDocument()->getStorage());
    PDFTransparencyGroup transparencyGroup = parseTransparencyGroup(pageTransparencyGroupObject);
    transparencyGroup.isolated = true;

    if (!transparencyGroup.colorSpacePointer)
    {
        transparencyGroup.colorSpacePointer = m_processColorSpace;
    }

    m_pageTransparencyGroupGuard.reset(new PDFTransparencyGroupGuard(this, qMove(transparencyGroup)));
}

const PDFFloatBitmap& PDFTransparencyRenderer::endPaint()
{
    Q_ASSERT(m_active);
    m_pageTransparencyGroupGuard.reset();
    m_active = false;

    return *getImmediateBackdrop();
}

void PDFTransparencyRenderer::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
}

void PDFTransparencyRenderer::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
}

void PDFTransparencyRenderer::performUpdateGraphicsState(const PDFPageContentProcessorState& state)
{
}

void PDFTransparencyRenderer::performSaveGraphicState(ProcessOrder order)
{
}

void PDFTransparencyRenderer::performRestoreGraphicState(ProcessOrder order)
{
}

void PDFTransparencyRenderer::performBeginTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup)
{
    if (order == ProcessOrder::BeforeOperation)
    {
        PDFTransparencyGroupPainterData data;
        data.group = transparencyGroup;
        data.alphaIsShape = getGraphicState()->getAlphaIsShape();
        data.alphaFill = getGraphicState()->getAlphaFilling();
        data.alphaStroke = getGraphicState()->getAlphaStroking();
        data.blendMode = getGraphicState()->getBlendMode();
        data.blackPointCompensationMode = getGraphicState()->getBlackPointCompensationMode();
        data.renderingIntent = getGraphicState()->getRenderingIntent();
        data.blendColorSpace = transparencyGroup.colorSpacePointer;

        if (!data.blendColorSpace)
        {
            data.blendColorSpace = getBlendColorSpace();
        }

        // Create initial backdrop, according to 11.4.8 of PDF 2.0 specification.
        // If group is knockout, use initial backdrop.
        PDFFloatBitmapWithColorSpace* oldBackdrop = getBackdrop();
        data.initialBackdrop = *getBackdrop();

        if (isTransparencyGroupIsolated())
        {
            // Make initial backdrop transparent
            data.initialBackdrop.makeTransparent();
        }
        else if (!isTransparencyGroupKnockout())
        {
            // We have stored alpha_g_i in immediate buffer. We must mix it with alpha_0 to get alpha_i
            const PDFFloatBitmapWithColorSpace* initialBackdrop = getInitialBackdrop();
            const uint8_t opacityChannelIndex = initialBackdrop->getPixelFormat().getOpacityChannelIndex();
            const size_t width = data.initialBackdrop.getWidth();
            const size_t height = data.initialBackdrop.getHeight();

            for (size_t x = 0; x < width; ++x)
            {
                for (size_t y = 0; y < height; ++y)
                {
                    PDFConstColorBuffer oldPixel = initialBackdrop->getPixel(x, y);
                    PDFColorBuffer newPixel = data.initialBackdrop.getPixel(x, y);
                    newPixel[opacityChannelIndex] = PDFBlendFunction::blend_Union(oldPixel[opacityChannelIndex], newPixel[opacityChannelIndex]);
                }
            }
        }

        // Prepare soft mask
        data.softMask = PDFFloatBitmap(oldBackdrop->getWidth(), oldBackdrop->getHeight(), PDFPixelFormat::createOpacityMask());
        // TODO: Create soft mask
        data.softMask.makeOpaque();

        data.initialBackdrop.convertToColorSpace(getCMS(), data.renderingIntent, data.blendColorSpace, this);
        data.immediateBackdrop = data.initialBackdrop;

        // Jakub Melka: According to 11.4.8 of PDF 2.0 specification, we must
        // initialize f_g_0 and alpha_g_0 to zero. We store f_g_0 and alpha_g_0
        // in the immediate backdrop, so we will make it transparent.
        data.immediateBackdrop.makeTransparent();

        m_transparencyGroupDataStack.emplace_back(qMove(data));
    }
}

void PDFTransparencyRenderer::performEndTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup)
{
    Q_UNUSED(transparencyGroup);

    if (order == ProcessOrder::AfterOperation)
    {
        // "Unblend" the initial backdrop from immediate backdrop, according to 11.4.8
        removeInitialBackdrop();

        PDFTransparencyGroupPainterData sourceData = qMove(m_transparencyGroupDataStack.back());
        m_transparencyGroupDataStack.pop_back();

        PDFTransparencyGroupPainterData& targetData = m_transparencyGroupDataStack.back();
        sourceData.immediateBackdrop.convertToColorSpace(getCMS(), targetData.renderingIntent, targetData.blendColorSpace, this);

        PDFFloatBitmap::blend(sourceData.immediateBackdrop, targetData.immediateBackdrop, *getBackdrop(), *getInitialBackdrop(), sourceData.softMask,
                              sourceData.alphaIsShape, sourceData.alphaFill, BlendMode::Normal, targetData.group.knockout, 0xFFFF, PDFFloatBitmap::OverprintMode::NoOveprint);
    }
}

void PDFTransparencyRenderer::removeInitialBackdrop()
{
    PDFFloatBitmapWithColorSpace* immediateBackdrop = getImmediateBackdrop();
    PDFFloatBitmapWithColorSpace* initialBackdrop = getInitialBackdrop();
    PDFPixelFormat pixelFormat = immediateBackdrop->getPixelFormat();

    const uint8_t alphaChannelIndex = pixelFormat.getOpacityChannelIndex();
    const uint8_t colorChannelIndexStart = pixelFormat.getColorChannelIndexStart();
    const uint8_t colorChannelIndexEnd= pixelFormat.getColorChannelIndexEnd();

    Q_ASSERT(alphaChannelIndex != PDFPixelFormat::INVALID_CHANNEL_INDEX);
    Q_ASSERT(colorChannelIndexStart != PDFPixelFormat::INVALID_CHANNEL_INDEX);
    Q_ASSERT(colorChannelIndexEnd != PDFPixelFormat::INVALID_CHANNEL_INDEX);

    for (size_t x = 0; x < immediateBackdrop->getWidth(); ++x)
    {
        for (size_t y = 0; y < immediateBackdrop->getHeight(); ++y)
        {
            PDFColorBuffer initialBackdropColorBuffer = initialBackdrop->getPixel(x, y);
            PDFColorBuffer immediateBackdropColorBuffer = immediateBackdrop->getPixel(x, y);

            const PDFColorComponent alpha_0 = initialBackdropColorBuffer[alphaChannelIndex];
            const PDFColorComponent alpha_g_n = immediateBackdropColorBuffer[alphaChannelIndex];

            if (!qFuzzyIsNull(alpha_g_n))
            {
                for (uint8_t i = colorChannelIndexStart; i < colorChannelIndexEnd; ++i)
                {
                    const PDFColorComponent C_0 = initialBackdropColorBuffer[i];
                    const PDFColorComponent C_n = immediateBackdropColorBuffer[i];
                    const PDFColorComponent C = C_n + (C_n - C_0) * alpha_0 * (1.0f / alpha_g_n - 1.0f);
                    const PDFColorComponent C_clipped = qBound(0.0f, C, 1.0f);
                    immediateBackdropColorBuffer[i] = C_clipped;
                }
            }
        }
    }
}

PDFFloatBitmapWithColorSpace* PDFTransparencyRenderer::getInitialBackdrop()
{
    return &m_transparencyGroupDataStack.back().initialBackdrop;
}

PDFFloatBitmapWithColorSpace* PDFTransparencyRenderer::getImmediateBackdrop()
{
    return &m_transparencyGroupDataStack.back().immediateBackdrop;
}

PDFFloatBitmapWithColorSpace* PDFTransparencyRenderer::getBackdrop()
{
    if (isTransparencyGroupKnockout())
    {
        return getInitialBackdrop();
    }
    else
    {
        return getImmediateBackdrop();
    }
}

const PDFColorSpacePointer& PDFTransparencyRenderer::getBlendColorSpace() const
{
    return m_transparencyGroupDataStack.back().blendColorSpace;
}

bool PDFTransparencyRenderer::isTransparencyGroupIsolated() const
{
    return m_transparencyGroupDataStack.back().group.isolated;
}

bool PDFTransparencyRenderer::isTransparencyGroupKnockout() const
{
    return m_transparencyGroupDataStack.back().group.knockout;
}

PDFInkMapper::PDFInkMapper(const PDFDocument* document) :
    m_document(document)
{

}

void PDFInkMapper::createSpotColors(bool activate)
{
    m_spotColors.clear();
    m_activeSpotColors = 0;

    const PDFCatalog* catalog = m_document->getCatalog();
    const size_t pageCount = catalog->getPageCount();
    for (size_t i = 0; i < pageCount; ++i)
    {
        const PDFPage* page = catalog->getPage(i);
        PDFObject resources = m_document->getObject(page->getResources());

        if (resources.isDictionary() && resources.getDictionary()->hasKey("ColorSpace"))
        {
            const PDFDictionary* colorSpaceDictionary = m_document->getDictionaryFromObject(resources.getDictionary()->get("ColorSpace"));
            if (colorSpaceDictionary)
            {
                std::size_t colorSpaces = colorSpaceDictionary->getCount();
                for (size_t csIndex = 0; csIndex < colorSpaces; ++ csIndex)
                {
                    PDFColorSpacePointer colorSpacePointer;
                    try
                    {
                        colorSpacePointer = PDFAbstractColorSpace::createColorSpace(colorSpaceDictionary, m_document, m_document->getObject(colorSpaceDictionary->getValue(csIndex)));
                    }
                    catch (PDFException)
                    {
                        // Ignore invalid color spaces
                        continue;
                    }

                    if (!colorSpacePointer)
                    {
                        continue;
                    }

                    switch (colorSpacePointer->getColorSpace())
                    {
                        case PDFAbstractColorSpace::ColorSpace::Separation:
                        {
                            const PDFSeparationColorSpace* separationColorSpace = dynamic_cast<const PDFSeparationColorSpace*>(colorSpacePointer.data());

                            if (!separationColorSpace->isNone() && !separationColorSpace->isAll() && !separationColorSpace->getColorName().isEmpty())
                            {
                                // Try to add spot color
                                const QByteArray& colorName = separationColorSpace->getColorName();
                                if (!containsSpotColor(colorName))
                                {
                                    SpotColorInfo info;
                                    info.name = colorName;
                                    info.colorSpace = colorSpacePointer;
                                    m_spotColors.emplace_back(qMove(info));
                                }
                            }

                            break;
                        }

                        case PDFAbstractColorSpace::ColorSpace::DeviceN:
                        {
                            const PDFDeviceNColorSpace* deviceNColorSpace = dynamic_cast<const PDFDeviceNColorSpace*>(colorSpacePointer.data());

                            if (!deviceNColorSpace->isNone())
                            {
                                const PDFDeviceNColorSpace::Colorants& colorants = deviceNColorSpace->getColorants();
                                for (size_t i = 0; i < colorants.size(); ++i)
                                {
                                    const PDFDeviceNColorSpace::ColorantInfo& colorantInfo = colorants[i];
                                    if (!containsSpotColor(colorantInfo.name))
                                    {
                                        SpotColorInfo info;
                                        info.name = colorantInfo.name;
                                        info.index = uint32_t(i);
                                        info.colorSpace = colorSpacePointer;
                                        m_spotColors.emplace_back(qMove(info));
                                    }
                                }
                            }

                            break;
                        }

                        default:
                            break;
                    }
                }
            }
        }
    }

    if (activate)
    {
        size_t minIndex = qMin<uint32_t>(uint32_t(m_spotColors.size()), MAX_SPOT_COLOR_COMPONENTS);
        for (size_t i = 0; i < minIndex; ++i)
        {
            m_spotColors[i].active = true;
        }
        m_activeSpotColors = minIndex;
    }
}

bool PDFInkMapper::containsSpotColor(const QByteArray& colorName) const
{
    return getSpotColor(colorName) != nullptr;
}

const PDFInkMapper::SpotColorInfo* PDFInkMapper::getSpotColor(const QByteArray& colorName) const
{
    auto it = std::find_if(m_spotColors.cbegin(), m_spotColors.cend(), [&colorName](const auto& info) { return info.name == colorName; });
    if (it != m_spotColors.cend())
    {
        return &*it;
    }

    return nullptr;
}

}   // namespace pdf