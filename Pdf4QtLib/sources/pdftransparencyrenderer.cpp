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
#include "pdfexecutionpolicy.h"

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

void PDFFloatBitmap::makeColorBlack()
{
    fillProcessColorChannels(m_format.hasProcessColorsSubtractive() ? 1.0 : 0.0);
}

void PDFFloatBitmap::makeColorWhite()
{
    fillProcessColorChannels(m_format.hasProcessColorsSubtractive() ? 0.0 : 1.0);
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
                           OverprintMode overprintMode,
                           QRect blendRegion)
{
    Q_ASSERT(source.getWidth() == target.getWidth());
    Q_ASSERT(source.getHeight() == target.getHeight());
    Q_ASSERT(source.getPixelFormat() == target.getPixelFormat());
    Q_ASSERT(source.getWidth() == softMask.getWidth());
    Q_ASSERT(source.getHeight() == softMask.getHeight());
    Q_ASSERT(softMask.getPixelFormat() == PDFPixelFormat::createOpacityMask());

    Q_ASSERT(blendRegion.left() >= 0);
    Q_ASSERT(blendRegion.top() >= 0);
    Q_ASSERT(blendRegion.right() < source.getWidth());
    Q_ASSERT(blendRegion.bottom() < source.getHeight());

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

    for (size_t x = blendRegion.left(); x <= blendRegion.right(); ++x)
    {
        for (size_t y = blendRegion.top(); y <= blendRegion.bottom(); ++y)
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

void PDFFloatBitmap::fillProcessColorChannels(PDFColorComponent value)
{
    if (!m_format.hasProcessColors())
    {
        // No process colors
        return;
    }

    const uint8_t channelStart = m_format.getProcessColorChannelIndexStart();
    const uint8_t channelEnd = m_format.getProcessColorChannelIndexEnd();

    for (PDFColorComponent* pixel = begin(); pixel != end(); pixel += m_pixelSize)
    {
        std::fill(pixel + channelStart, pixel + channelEnd, value);
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
        reporter->reportRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Transformation between blending color space failed."));
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
            PDFColorComponent* targetIt = targetBuffer.begin();
            targetIt = std::copy(sourceProcessColorBuffer.cbegin(), sourceProcessColorBuffer.cend(), targetIt);

            Q_ASSERT(std::distance(targetIt, targetBuffer.end()) == temporary.getPixelFormat().getSpotColorChannelCount() + temporary.getPixelFormat().getAuxiliaryChannelCount());

            const PDFColorComponent* sourceIt = std::next(sourceSpotColorAndOpacityBuffer.cbegin(), getPixelFormat().getProcessColorChannelCount());
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
                                                 PDFTransparencyRendererSettings settings,
                                                 QMatrix pagePointToDevicePointMatrix) :
    BaseClass(page, document, fontCache, cms, optionalContentActivity, pagePointToDevicePointMatrix, PDFMeshQualitySettings()),
    m_inkMapper(inkMapper),
    m_active(false),
    m_settings(settings)
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

    m_transparencyGroupDataStack.clear();
    m_painterStateStack.push(PDFTransparencyPainterState());
    m_painterStateStack.top().softMask = PDFFloatBitmap(pixelSize.width(), pixelSize.height(), PDFPixelFormat::createOpacityMask());
    m_painterStateStack.top().softMask.makeOpaque();

    PDFPixelFormat pixelFormat = PDFPixelFormat::createFormat(uint8_t(m_deviceColorSpace->getColorComponentCount()),
                                                              uint8_t(m_inkMapper->getActiveSpotColorCount()),
                                                              true, m_deviceColorSpace->getColorComponentCount() == 4);

    PDFFloatBitmapWithColorSpace paper = PDFFloatBitmapWithColorSpace(pixelSize.width(), pixelSize.height(), pixelFormat, m_deviceColorSpace);
    paper.makeColorWhite();

    PDFTransparencyGroupPainterData deviceGroup;
    deviceGroup.alphaIsShape = getGraphicState()->getAlphaIsShape();
    deviceGroup.alphaStroke = getGraphicState()->getAlphaStroking();
    deviceGroup.alphaFill = getGraphicState()->getAlphaFilling();
    deviceGroup.blendMode = getGraphicState()->getBlendMode();
    deviceGroup.blackPointCompensationMode = getGraphicState()->getBlackPointCompensationMode();
    deviceGroup.renderingIntent = RenderingIntent::RelativeColorimetric;
    deviceGroup.initialBackdrop = qMove(paper);
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
    m_transparencyGroupDataStack.back().filterColorsUsingMask = (m_settings.flags.testFlag(PDFTransparencyRendererSettings::ActiveColorMask) &&
                                                                 m_settings.activeColorMask != PDFPixelFormat::getAllColorsMask());
    m_transparencyGroupDataStack.back().activeColorMask = m_settings.activeColorMask;
}

const PDFFloatBitmap& PDFTransparencyRenderer::endPaint()
{
    Q_ASSERT(m_active);
    m_pageTransparencyGroupGuard.reset();
    m_active = false;
    m_painterStateStack.pop();

    return *getImmediateBackdrop();
}

QImage PDFTransparencyRenderer::toImageImpl(const PDFFloatBitmapWithColorSpace& floatImage, bool use16Bit) const
{
    QImage image;

    Q_ASSERT(floatImage.getPixelFormat().getProcessColorChannelCount() == 3);

    if (use16Bit)
    {
        image = QImage(int(floatImage.getWidth()), int(floatImage.getHeight()), QImage::Format_RGBA64);

        const PDFPixelFormat pixelFormat = floatImage.getPixelFormat();
        const int height = image.height();
        const int width = image.width();
        const float scale = std::numeric_limits<quint16>::max();
        const uint8_t channelStart = pixelFormat.getProcessColorChannelIndexStart();
        const uint8_t channelEnd = pixelFormat.getProcessColorChannelIndexEnd();
        const uint8_t opacityChannel = pixelFormat.getOpacityChannelIndex();

        for (int y = 0; y < height; ++y)
        {
            quint16* pixels = reinterpret_cast<quint16*>(image.bits() + y * image.bytesPerLine());

            for (int x = 0; x < width; ++x)
            {
                PDFConstColorBuffer colorBuffer = floatImage.getPixel(x, y);

                for (uint8_t channel = channelStart; channel < channelEnd; ++channel)
                {
                    *pixels++ = quint16(colorBuffer[channel] * scale);
                }

                *pixels++ = quint16(colorBuffer[opacityChannel] * scale);
            }
        }
    }
    else
    {
        image = QImage(int(floatImage.getWidth()), int(floatImage.getHeight()), QImage::Format_RGBA8888);

        const PDFPixelFormat pixelFormat = floatImage.getPixelFormat();
        const int height = image.height();
        const int width = image.width();
        const float scale = std::numeric_limits<quint8>::max();
        const uint8_t channelStart = pixelFormat.getProcessColorChannelIndexStart();
        const uint8_t channelEnd = pixelFormat.getProcessColorChannelIndexEnd();
        const uint8_t opacityChannel = pixelFormat.getOpacityChannelIndex();

        for (int y = 0; y < height; ++y)
        {
            quint8* pixels = reinterpret_cast<quint8*>(image.bits() + y * image.bytesPerLine());

            for (int x = 0; x < width; ++x)
            {
                PDFConstColorBuffer colorBuffer = floatImage.getPixel(x, y);

                for (uint8_t channel = channelStart; channel < channelEnd; ++channel)
                {
                    *pixels++ = quint8(colorBuffer[channel] * scale);
                }

                *pixels++ = quint8(colorBuffer[opacityChannel] * scale);
            }
        }
    }

    return image;
}

QImage PDFTransparencyRenderer::toImage(bool use16Bit, bool usePaper, PDFRGB paperColor) const
{
    QImage image;

    if (m_transparencyGroupDataStack.size() == 1 && // We have finished the painting
        m_transparencyGroupDataStack.back().immediateBackdrop.getPixelFormat().getProcessColorChannelCount() == 3) // We have exactly three process colors (RGB)
    {
        const PDFFloatBitmapWithColorSpace& floatImage = m_transparencyGroupDataStack.back().immediateBackdrop;
        Q_ASSERT(floatImage.getPixelFormat().hasOpacityChannel());

        if (!usePaper)
        {
            return toImageImpl(floatImage, use16Bit);
        }

        PDFFloatBitmapWithColorSpace paperImage(floatImage.getWidth(), floatImage.getHeight(), floatImage.getPixelFormat(), floatImage.getColorSpace());
        paperImage.makeOpaque();
        paperImage.fillChannel(0, paperColor[0]);
        paperImage.fillChannel(1, paperColor[1]);
        paperImage.fillChannel(2, paperColor[2]);

        PDFFloatBitmap softMask(paperImage.getWidth(), paperImage.getHeight(), PDFPixelFormat::createOpacityMask());
        softMask.makeOpaque();

        QRect blendRegion(0, 0, int(floatImage.getWidth()), int(floatImage.getHeight()));
        PDFFloatBitmapWithColorSpace::blend(floatImage, paperImage, paperImage, paperImage, softMask, false, 1.0f, BlendMode::Normal, false, 0xFFFF, PDFFloatBitmap::OverprintMode::NoOveprint, blendRegion);

        return toImageImpl(paperImage, use16Bit);
    }

    return image;
}

void PDFTransparencyRenderer::performPixelSampling(const PDFReal shape,
                                                   const PDFReal opacity,
                                                   const uint8_t shapeChannel,
                                                   const uint8_t opacityChannel,
                                                   const uint8_t colorChannelStart,
                                                   const uint8_t colorChannelEnd,
                                                   int x,
                                                   int y,
                                                   const PDFMappedColor& fillColor,
                                                   const PDFPainterPathSampler& clipSampler,
                                                   const PDFPainterPathSampler& pathSampler)
{
    const PDFColorComponent clipValue = clipSampler.sample(QPoint(x, y));
    const PDFColorComponent objectShapeValue = pathSampler.sample(QPoint(x, y));
    const PDFColorComponent shapeValue = objectShapeValue * clipValue * shape;

    if (shapeValue > 0.0f)
    {
        // We consider old object shape - we use Union function to
        // set shape channel value.

        PDFColorBuffer pixel = m_drawBuffer.getPixel(x, y);
        pixel[shapeChannel] = PDFBlendFunction::blend_Union(shapeValue, pixel[shapeChannel]);
        pixel[opacityChannel] = pixel[shapeChannel]  * opacity;

        // Copy color
        for (uint8_t colorChannelIndex = colorChannelStart; colorChannelIndex < colorChannelEnd; ++colorChannelIndex)
        {
            pixel[colorChannelIndex] = fillColor.mappedColor[colorChannelIndex];
        }
    }
}

void PDFTransparencyRenderer::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
    Q_UNUSED(fillRule);

    QMatrix worldMatrix = getCurrentWorldMatrix();

    const PDFReal shapeStroking = getShapeStroking();
    const PDFReal opacityStroking = getOpacityStroking();
    const PDFReal shapeFilling = getShapeFilling();
    const PDFReal opacityFilling = getOpacityFilling();

    PDFPixelFormat format = m_drawBuffer.getPixelFormat();
    Q_ASSERT(format.hasShapeChannel());
    Q_ASSERT(format.hasOpacityChannel());

    const uint8_t shapeChannel = format.getShapeChannelIndex();
    const uint8_t opacityChannel = format.getOpacityChannelIndex();
    const uint8_t colorChannelStart = format.getColorChannelIndexStart();
    const uint8_t colorChannelEnd = format.getColorChannelIndexEnd();

    if (fill)
    {
        QPainterPath worldPath = worldMatrix.map(path);
        QRect fillRect = getActualFillRect(worldPath.controlPointRect());

        // Fill rect may be, or may not be valid. It depends on the painter path
        // and world matrix. Path can be translated outside of the paint area.
        if (fillRect.isValid())
        {
            PDFPainterPathSampler clipSampler(m_painterStateStack.top().clipPath, m_settings.samplesCount, 1.0f, fillRect, m_settings.flags.testFlag(PDFTransparencyRendererSettings::PrecisePathSampler));
            PDFPainterPathSampler pathSampler(worldPath, m_settings.samplesCount, 0.0f, fillRect, m_settings.flags.testFlag(PDFTransparencyRendererSettings::PrecisePathSampler));
            const PDFMappedColor& fillColor = getMappedFillColor();

            if (isMultithreadedPathSamplingUsed(fillRect))
            {
                if (fillRect.width() > fillRect.height())
                {
                    // Columns
                    PDFIntegerRange<int> range(fillRect.left(), fillRect.right() + 1);
                    auto processEntry = [&, this](int x)
                    {
                        for (int y = fillRect.top(); y <= fillRect.bottom(); ++y)
                        {
                            performPixelSampling(shapeFilling, opacityFilling, shapeChannel, opacityChannel, colorChannelStart, colorChannelEnd, x, y, fillColor, clipSampler, pathSampler);
                        }
                    };
                    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Content, range.begin(), range.end(), processEntry);
                }
                else
                {
                    // Rows
                    PDFIntegerRange<int> range(fillRect.top(), fillRect.bottom() + 1);
                    auto processEntry = [&, this](int y)
                    {
                        for (int x = fillRect.left(); x <= fillRect.right(); ++x)
                        {
                            performPixelSampling(shapeFilling, opacityFilling, shapeChannel, opacityChannel, colorChannelStart, colorChannelEnd, x, y, fillColor, clipSampler, pathSampler);
                        }
                    };
                    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Content, range.begin(), range.end(), processEntry);
                }
            }
            else
            {
                for (int x = fillRect.left(); x <= fillRect.right(); ++x)
                {
                    for (int y = fillRect.top(); y <= fillRect.bottom(); ++y)
                    {
                        performPixelSampling(shapeFilling, opacityFilling, shapeChannel, opacityChannel, colorChannelStart, colorChannelEnd, x, y, fillColor, clipSampler, pathSampler);
                    }
                }
            }

            m_drawBuffer.markActiveColors(fillColor.activeChannels);
            m_drawBuffer.modify(fillRect, true, false);
        }
    }

    if (stroke)
    {
        // We must stroke the path.
        QPainterPathStroker stroker;
        stroker.setCapStyle(getGraphicState()->getLineCapStyle());
        stroker.setWidth(getGraphicState()->getLineWidth());
        stroker.setMiterLimit(getGraphicState()->getMitterLimit());
        stroker.setJoinStyle(getGraphicState()->getLineJoinStyle());

        const PDFLineDashPattern& lineDashPattern = getGraphicState()->getLineDashPattern();
        if (!lineDashPattern.isSolid())
        {
            stroker.setDashPattern(QVector<PDFReal>::fromStdVector(lineDashPattern.getDashArray()));
            stroker.setDashOffset(lineDashPattern.getDashOffset());
        }
        QPainterPath strokedPath = stroker.createStroke(path);

        QPainterPath worldPath = worldMatrix.map(strokedPath);
        QRect strokeRect = getActualFillRect(worldPath.controlPointRect());

        // Fill rect may be, or may not be valid. It depends on the painter path
        // and world matrix. Path can be translated outside of the paint area.
        if (strokeRect.isValid())
        {
            PDFPainterPathSampler clipSampler(m_painterStateStack.top().clipPath, m_settings.samplesCount, 1.0f, strokeRect, m_settings.flags.testFlag(PDFTransparencyRendererSettings::PrecisePathSampler));
            PDFPainterPathSampler pathSampler(worldPath, m_settings.samplesCount, 0.0f, strokeRect, m_settings.flags.testFlag(PDFTransparencyRendererSettings::PrecisePathSampler));
            const PDFMappedColor& strokeColor = getMappedStrokeColor();

            if (isMultithreadedPathSamplingUsed(strokeRect))
            {
                if (strokeRect.width() > strokeRect.height())
                {
                    // Columns
                    PDFIntegerRange<int> range(strokeRect.left(), strokeRect.right() + 1);
                    auto processEntry = [&, this](int x)
                    {
                        for (int y = strokeRect.top(); y <= strokeRect.bottom(); ++y)
                        {
                            performPixelSampling(shapeStroking, opacityStroking, shapeChannel, opacityChannel, colorChannelStart, colorChannelEnd, x, y, strokeColor, clipSampler, pathSampler);
                        }
                    };
                    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Content, range.begin(), range.end(), processEntry);
                }
                else
                {
                    // Rows
                    PDFIntegerRange<int> range(strokeRect.top(), strokeRect.bottom() + 1);
                    auto processEntry = [&, this](int y)
                    {
                        for (int x = strokeRect.left(); x <= strokeRect.right(); ++x)
                        {
                            performPixelSampling(shapeStroking, opacityStroking, shapeChannel, opacityChannel, colorChannelStart, colorChannelEnd, x, y, strokeColor, clipSampler, pathSampler);
                        }
                    };
                    PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Content, range.begin(), range.end(), processEntry);
                }
            }
            else
            {
                for (int x = strokeRect.left(); x <= strokeRect.right(); ++x)
                {
                    for (int y = strokeRect.top(); y <= strokeRect.bottom(); ++y)
                    {
                        performPixelSampling(shapeStroking, opacityStroking, shapeChannel, opacityChannel, colorChannelStart, colorChannelEnd, x, y, strokeColor, clipSampler, pathSampler);
                    }
                }
            }

            m_drawBuffer.markActiveColors(strokeColor.activeChannels);
            m_drawBuffer.modify(strokeRect, false, true);
        }
    }

    if (!text || !getGraphicState()->getTextKnockout())
    {
        flushDrawBuffer();
    }
}

void PDFTransparencyRenderer::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
    Q_UNUSED(fillRule);

    PDFTransparencyPainterState* painterState = getPainterState();

    if (!painterState->clipPath.isEmpty())
    {
        painterState->clipPath = painterState->clipPath.intersected(getCurrentWorldMatrix().map(path));
    }
    else
    {
        painterState->clipPath = getCurrentWorldMatrix().map(path);
    }
}

void PDFTransparencyRenderer::performUpdateGraphicsState(const PDFPageContentProcessorState& state)
{
    PDFPageContentProcessorState::StateFlags stateFlags = state.getStateFlags();

    const bool colorTransformAffected = stateFlags.testFlag(PDFPageContentProcessorState::StateRenderingIntent) ||
                                        stateFlags.testFlag(PDFPageContentProcessorState::StateBlackPointCompensation);

    if (colorTransformAffected ||
        stateFlags.testFlag(PDFPageContentProcessorState::StateStrokeColor) ||
        stateFlags.testFlag(PDFPageContentProcessorState::StateStrokeColorSpace))
    {
        m_mappedStrokeColor.dirty();
    }

    if (colorTransformAffected ||
        stateFlags.testFlag(PDFPageContentProcessorState::StateFillColor) ||
        stateFlags.testFlag(PDFPageContentProcessorState::StateFillColorSpace))
    {
        m_mappedFillColor.dirty();
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateSoftMask))
    {
        if (getGraphicState()->getSoftMask())
        {
            reportRenderErrorOnce(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Soft mask not implemented."));
        }
    }

    BaseClass::performUpdateGraphicsState(state);
}

void PDFTransparencyRenderer::performSaveGraphicState(ProcessOrder order)
{
    if (order == ProcessOrder::AfterOperation)
    {
        m_painterStateStack.push(m_painterStateStack.top());
    }
}

void PDFTransparencyRenderer::performRestoreGraphicState(ProcessOrder order)
{
    if (order == ProcessOrder::BeforeOperation)
    {
        m_painterStateStack.pop();
    }
    if (order == ProcessOrder::AfterOperation)
    {
        invalidateCachedItems();
    }
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
            data.makeInitialBackdropTransparent();
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
        data.makeSoftMaskOpaque();

        data.initialBackdrop.convertToColorSpace(getCMS(), data.renderingIntent, data.blendColorSpace, this);
        data.immediateBackdrop = data.initialBackdrop;

        // Jakub Melka: According to 11.4.8 of PDF 2.0 specification, we must
        // initialize f_g_0 and alpha_g_0 to zero. We store f_g_0 and alpha_g_0
        // in the immediate backdrop, so we will make it transparent.
        data.makeImmediateBackdropTransparent();

        // Create draw buffer
        m_drawBuffer = PDFDrawBuffer(data.immediateBackdrop.getWidth(), data.immediateBackdrop.getHeight(), data.immediateBackdrop.getPixelFormat());

        m_transparencyGroupDataStack.emplace_back(qMove(data));
        invalidateCachedItems();
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

        // Filter inactive colors - clear all colors in immediate mask,
        // which are set to inactive.
        if (sourceData.filterColorsUsingMask)
        {
            const PDFPixelFormat pixelFormat = sourceData.immediateBackdrop.getPixelFormat();
            const uint32_t colorChannelStart = pixelFormat.getColorChannelIndexStart();
            const uint32_t colorChannelEnd = pixelFormat.getColorChannelIndexEnd();
            const uint32_t processColorChannelEnd = pixelFormat.getProcessColorChannelIndexEnd();

            for (uint32_t colorChannelIndex = colorChannelStart; colorChannelIndex < colorChannelEnd; ++colorChannelIndex)
            {
                const uint32_t flag = 1 << colorChannelIndex;
                if (!(sourceData.activeColorMask & flag))
                {
                    const bool isProcessColor = colorChannelIndex < processColorChannelEnd;
                    const bool isSubtractive = isProcessColor ? pixelFormat.hasProcessColorsSubtractive() : pixelFormat.hasSpotColorsSubtractive();

                    sourceData.immediateBackdrop.fillChannel(colorChannelIndex, isSubtractive ? 0.0f : 1.0f);
                }
            }
        }

        PDFTransparencyGroupPainterData& targetData = m_transparencyGroupDataStack.back();
        sourceData.immediateBackdrop.convertToColorSpace(getCMS(), targetData.renderingIntent, targetData.blendColorSpace, this);

        PDFFloatBitmap::blend(sourceData.immediateBackdrop, targetData.immediateBackdrop, *getBackdrop(), *getInitialBackdrop(), sourceData.softMask,
                              sourceData.alphaIsShape, sourceData.alphaFill, BlendMode::Normal, targetData.group.knockout, 0xFFFF,
                              PDFFloatBitmap::OverprintMode::NoOveprint, getPaintRect());

        // Create draw buffer
        PDFFloatBitmapWithColorSpace* backdrop = getImmediateBackdrop();
        m_drawBuffer = PDFDrawBuffer(backdrop->getWidth(), backdrop->getHeight(), backdrop->getPixelFormat());

        invalidateCachedItems();
    }
}

void PDFTransparencyRenderer::performTextEnd(ProcessOrder order)
{
    if (order == ProcessOrder::AfterOperation)
    {
        flushDrawBuffer();
    }
}

void PDFTransparencyRenderer::performImagePainting(const QImage& image)
{
    Q_UNUSED(image);

    reportRenderErrorOnce(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Image painting not implemented."));
}

void PDFTransparencyRenderer::performMeshPainting(const PDFMesh& mesh)
{
    Q_UNUSED(mesh);

    reportRenderErrorOnce(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Mesh painting not implemented."));
}

PDFReal PDFTransparencyRenderer::getShapeStroking() const
{
    return getGraphicState()->getAlphaIsShape() ? getGraphicState()->getAlphaStroking() : 1.0;
}

PDFReal PDFTransparencyRenderer::getOpacityStroking() const
{
    return !getGraphicState()->getAlphaIsShape() ? getGraphicState()->getAlphaStroking() : 1.0;
}

PDFReal PDFTransparencyRenderer::getShapeFilling() const
{
    return getGraphicState()->getAlphaIsShape() ? getGraphicState()->getAlphaFilling() : 1.0;
}

PDFReal PDFTransparencyRenderer::getOpacityFilling() const
{
    return !getGraphicState()->getAlphaIsShape() ? getGraphicState()->getAlphaFilling() : 1.0;
}

void PDFTransparencyRenderer::invalidateCachedItems()
{
    m_mappedStrokeColor.dirty();
    m_mappedFillColor.dirty();
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

const PDFFloatBitmapWithColorSpace* PDFTransparencyRenderer::getInitialBackdrop() const
{
    return &m_transparencyGroupDataStack.back().initialBackdrop;
}

const PDFFloatBitmapWithColorSpace* PDFTransparencyRenderer::getImmediateBackdrop() const
{
    return &m_transparencyGroupDataStack.back().immediateBackdrop;
}

const PDFFloatBitmapWithColorSpace* PDFTransparencyRenderer::getBackdrop() const
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

const PDFTransparencyRenderer::PDFMappedColor& PDFTransparencyRenderer::getMappedStrokeColor()
{
    return m_mappedStrokeColor.get(this, &PDFTransparencyRenderer::getMappedStrokeColorImpl);
}

const PDFTransparencyRenderer::PDFMappedColor& PDFTransparencyRenderer::getMappedFillColor()
{
    return m_mappedFillColor.get(this, &PDFTransparencyRenderer::getMappedFillColorImpl);
}

void PDFTransparencyRenderer::fillMappedColorUsingMapping(const PDFPixelFormat pixelFormat,
                                                          PDFMappedColor& result,
                                                          const PDFInkMapping& inkMapping,
                                                          const PDFColor& sourceColor)
{
    result.mappedColor.resize(pixelFormat.getColorChannelCount());

    // Zero the color
    for (size_t i = 0; i < pixelFormat.getColorChannelCount(); ++i)
    {
        result.mappedColor[i] = 0.0f;
    }

    for (const PDFInkMapping::Mapping& ink : inkMapping.mapping)
    {
        // Sanity check of source color
        if (ink.source >= sourceColor.size())
        {
            reportRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Invalid source ink index %1.").arg(ink.source));
            continue;
        }

        // Sanity check of target color
        if (ink.target >= result.mappedColor.size())
        {
            reportRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Invalid target ink index %1.").arg(ink.target));
            continue;
        }

        switch (ink.type)
        {
            case pdf::PDFInkMapping::Pass:
                result.mappedColor[ink.target] = sourceColor[ink.source];
                break;

            default:
                Q_ASSERT(false);
                break;
        }
    }

    result.activeChannels = inkMapping.activeChannels;
}

PDFTransparencyRenderer::PDFMappedColor PDFTransparencyRenderer::createMappedColor(const PDFColor& sourceColor, const PDFAbstractColorSpace* sourceColorSpace)
{
    PDFMappedColor result;

    const PDFAbstractColorSpace* targetColorSpace = getBlendColorSpace().data();
    const PDFPixelFormat pixelFormat = getImmediateBackdrop()->getPixelFormat();

    Q_ASSERT(targetColorSpace->equals(getImmediateBackdrop()->getColorSpace().data()));

    PDFInkMapping inkMapping = m_inkMapper->createMapping(sourceColorSpace, targetColorSpace, pixelFormat);
    if (inkMapping.isValid())
    {
        fillMappedColorUsingMapping(pixelFormat, result, inkMapping, sourceColor);
    }
    else
    {
        // Jakub Melka: We must convert color form source color space to target color space
        std::vector<PDFColorComponent> sourceColorVector(sourceColor.size(), 0.0f);
        for (size_t i = 0; i < sourceColorVector.size(); ++i)
        {
            sourceColorVector[i] = sourceColor[i];
        }
        PDFColorBuffer sourceColorBuffer(sourceColorVector.data(), sourceColorVector.size());

        std::vector<PDFColorComponent> targetColorVector(pixelFormat.getProcessColorChannelCount(), 0.0f);
        PDFColorBuffer targetColorBuffer(targetColorVector.data(), targetColorVector.size());

        if (!PDFAbstractColorSpace::transform(sourceColorSpace, targetColorSpace, getCMS(), getGraphicState()->getRenderingIntent(), sourceColorBuffer, targetColorBuffer, this))
        {
            reportRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Transformation from source color space to target blending color space failed."));
        }

        PDFColor adjustedSourceColor;
        adjustedSourceColor.resize(targetColorBuffer.size());

        for (size_t i = 0; i < targetColorBuffer.size(); ++i)
        {
            adjustedSourceColor[i] = targetColorBuffer[i];
        }

        inkMapping = m_inkMapper->createMapping(targetColorSpace, targetColorSpace, pixelFormat);

        Q_ASSERT(inkMapping.isValid());
        fillMappedColorUsingMapping(pixelFormat, result, inkMapping, adjustedSourceColor);
    }

    return result;
}

PDFTransparencyRenderer::PDFMappedColor PDFTransparencyRenderer::getMappedStrokeColorImpl()
{
    const PDFAbstractColorSpace* sourceColorSpace = getGraphicState()->getStrokeColorSpace();
    const PDFColor& sourceColor = getGraphicState()->getStrokeColorOriginal();

    return createMappedColor(sourceColor, sourceColorSpace);
}

PDFTransparencyRenderer::PDFMappedColor PDFTransparencyRenderer::getMappedFillColorImpl()
{
    const PDFAbstractColorSpace* sourceColorSpace = getGraphicState()->getFillColorSpace();
    const PDFColor& sourceColor = getGraphicState()->getFillColorOriginal();

    return createMappedColor(sourceColor, sourceColorSpace);
}

QRect PDFTransparencyRenderer::getPaintRect() const
{
    return QRect(0, 0, int(getBackdrop()->getWidth()), int(getBackdrop()->getHeight()));
}

QRect PDFTransparencyRenderer::getActualFillRect(const QRectF& fillRect) const
{
    int xLeft = qFloor(fillRect.left()) - 1;
    int xRight = qCeil(fillRect.right()) + 1;
    int yTop = qFloor(fillRect.top()) - 1;
    int yBottom = qCeil(fillRect.bottom()) + 1;

    QRect drawRect(xLeft, yTop, xRight - xLeft, yBottom - yTop);
    return getPaintRect().intersected(drawRect);
}

void PDFTransparencyRenderer::flushDrawBuffer()
{
    if (m_drawBuffer.isModified())
    {
        PDFOverprintMode overprintMode = getGraphicState()->getOverprintMode();
        const bool useOverprint = (overprintMode.overprintFilling && m_drawBuffer.isContainsFilling()) ||
                                  (overprintMode.overprintStroking && m_drawBuffer.isContainsStroking());

        PDFFloatBitmap::OverprintMode selectedOverprintMode = PDFFloatBitmap::OverprintMode::NoOveprint;
        if (useOverprint)
        {
            selectedOverprintMode = overprintMode.overprintMode == 0 ? PDFFloatBitmap::OverprintMode::Overprint_Mode_0
                                                                     : PDFFloatBitmap::OverprintMode::Overprint_Mode_1;
        }

        PDFFloatBitmap::blend(m_drawBuffer, *getImmediateBackdrop(), *getBackdrop(), *getInitialBackdrop(), m_painterStateStack.top().softMask,
                              getGraphicState()->getAlphaIsShape(), 1.0f, getGraphicState()->getBlendMode(), isTransparencyGroupKnockout(),
                              m_drawBuffer.getActiveColors(), selectedOverprintMode, m_drawBuffer.getModifiedRect());


        m_drawBuffer.clear();
    }
}

bool PDFTransparencyRenderer::isMultithreadedPathSamplingUsed(QRect fillRect) const
{
    if (!m_settings.flags.testFlag(PDFTransparencyRendererSettings::MultithreadedPathSampler))
    {
        return false;
    }

    return fillRect.width() * fillRect.height() > m_settings.multithreadingPathSampleThreshold && fillRect.width() > 1;
}

PDFInkMapper::PDFInkMapper(const PDFDocument* document) :
    m_document(document)
{

}

std::vector<PDFInkMapper::ColorInfo> PDFInkMapper::getSeparations(uint32_t processColorCount) const
{
    std::vector<ColorInfo> result;
    result.reserve(getActiveSpotColorCount() + processColorCount);

    switch (processColorCount)
    {
        case 1:
        {
            ColorInfo gray;
            gray.name = "Process Gray";
            gray.textName = PDFTranslationContext::tr("Process Gray");
            gray.canBeActive = true;
            gray.active = true;
            gray.isSpot = false;
            result.emplace_back(qMove(gray));
            break;
        }

        case 3:
        {
            ColorInfo red;
            red.name = "Process Red";
            red.textName = PDFTranslationContext::tr("Process Red");
            red.canBeActive = true;
            red.active = true;
            red.isSpot = false;
            result.emplace_back(qMove(red));

            ColorInfo green;
            green.name = "Process Green";
            green.textName = PDFTranslationContext::tr("Process Green");
            green.canBeActive = true;
            green.active = true;
            green.isSpot = false;
            result.emplace_back(qMove(green));

            ColorInfo blue;
            blue.name = "Process Blue";
            blue.textName = PDFTranslationContext::tr("Process Blue");
            blue.canBeActive = true;
            blue.active = true;
            blue.isSpot = false;
            result.emplace_back(qMove(blue));
            break;
        }

        case 4:
        {
            ColorInfo cyan;
            cyan.name = "Process Cyan";
            cyan.textName = PDFTranslationContext::tr("Process Cyan");
            cyan.canBeActive = true;
            cyan.active = true;
            cyan.isSpot = false;
            result.emplace_back(qMove(cyan));

            ColorInfo magenta;
            magenta.name = "Process Magenta";
            magenta.textName = PDFTranslationContext::tr("Process Magenta");
            magenta.canBeActive = true;
            magenta.active = true;
            magenta.isSpot = false;
            result.emplace_back(qMove(magenta));

            ColorInfo yellow;
            yellow.name = "Process Yellow";
            yellow.textName = PDFTranslationContext::tr("Process Yellow");
            yellow.canBeActive = true;
            yellow.active = true;
            yellow.isSpot = false;
            result.emplace_back(qMove(yellow));

            ColorInfo black;
            black.name = "Process Black";
            black.textName = PDFTranslationContext::tr("Process Black");
            black.canBeActive = true;
            black.active = true;
            black.isSpot = false;
            result.emplace_back(qMove(black));
            break;
        }

        default:
        {
            for (uint32_t i = 0; i < processColorCount; ++i)
            {
                ColorInfo generic;
                generic.textName = PDFTranslationContext::tr("Process Generic%1").arg(i + 1);
                generic.name = generic.textName.toLatin1();
                generic.canBeActive = true;
                generic.active = true;
                generic.isSpot = false;
                result.emplace_back(qMove(generic));
            }
        }
    }

    for (const auto& spotColor : m_spotColors)
    {
        if (!spotColor.active)
        {
            // Skip inactive spot colors
            continue;
        }

        result.emplace_back(spotColor);
    }

    return result;
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
                                    ColorInfo info;
                                    info.name = colorName;
                                    info.textName = PDFEncoding::convertTextString(info.name);
                                    info.colorSpace = colorSpacePointer;
                                    info.spotColorIndex = uint32_t(m_spotColors.size());
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
                                        ColorInfo info;
                                        info.name = colorantInfo.name;
                                        info.textName = PDFEncoding::convertTextString(info.name);
                                        info.colorSpaceIndex = uint32_t(i);
                                        info.colorSpace = colorSpacePointer;
                                        info.spotColorIndex = uint32_t(m_spotColors.size());
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

    size_t minIndex = qMin<uint32_t>(uint32_t(m_spotColors.size()), MAX_SPOT_COLOR_COMPONENTS);
    for (size_t i = 0; i < minIndex; ++i)
    {
        m_spotColors[i].canBeActive = true;
    }

    setSpotColorsActive(activate);
}

bool PDFInkMapper::containsSpotColor(const QByteArray& colorName) const
{
    return getSpotColor(colorName) != nullptr;
}

const PDFInkMapper::ColorInfo* PDFInkMapper::getSpotColor(const QByteArray& colorName) const
{
    auto it = std::find_if(m_spotColors.cbegin(), m_spotColors.cend(), [&colorName](const auto& info) { return info.name == colorName; });
    if (it != m_spotColors.cend())
    {
        return &*it;
    }

    return nullptr;
}

void PDFInkMapper::setSpotColorsActive(bool active)
{
    m_activeSpotColors = 0;

    if (active)
    {
        for (auto& spotColor : m_spotColors)
        {
            if (spotColor.canBeActive)
            {
                spotColor.active = true;
                ++m_activeSpotColors;
            }
        }
    }
    else
    {
        for (auto& spotColor : m_spotColors)
        {
            spotColor.active = false;
        }
    }
}

PDFInkMapping PDFInkMapper::createMapping(const PDFAbstractColorSpace* sourceColorSpace,
                                          const PDFAbstractColorSpace* targetColorSpace,
                                          PDFPixelFormat targetPixelFormat) const
{
    PDFInkMapping mapping;

    Q_ASSERT(targetColorSpace->getColorComponentCount() == targetPixelFormat.getProcessColorChannelCount());

    if (sourceColorSpace->equals(targetColorSpace))
    {
        Q_ASSERT(sourceColorSpace->getColorComponentCount() == targetColorSpace->getColorComponentCount());

        uint8_t colorCount = uint8_t(targetColorSpace->getColorComponentCount());
        mapping.mapping.reserve(colorCount);

        for (uint8_t i = 0; i < colorCount; ++i)
        {
            mapping.map(i, i);
        }
    }
    else
    {
        switch (sourceColorSpace->getColorSpace())
        {
            case PDFAbstractColorSpace::ColorSpace::Separation:
            {
                const PDFSeparationColorSpace* separationColorSpace = dynamic_cast<const PDFSeparationColorSpace*>(sourceColorSpace);

                if (separationColorSpace->isAll())
                {
                    // Map this color to all device colors
                    uint32_t colorCount = static_cast<uint32_t>(targetColorSpace->getColorComponentCount());
                    mapping.mapping.reserve(colorCount);

                    for (size_t i = 0; i < colorCount; ++i)
                    {
                        mapping.map(0, uint8_t(i));
                    }
                }
                else if (!separationColorSpace->isNone() && !separationColorSpace->getColorName().isEmpty())
                {
                    const QByteArray& colorName = separationColorSpace->getColorName();
                    const ColorInfo* info = getSpotColor(colorName);
                    if (info && info->active && targetPixelFormat.hasSpotColors() && info->spotColorIndex < targetPixelFormat.getSpotColorChannelCount())
                    {
                        mapping.map(0, uint8_t(targetPixelFormat.getSpotColorChannelIndexStart() + info->spotColorIndex));
                    }
                }

                break;
            }

            case PDFAbstractColorSpace::ColorSpace::DeviceN:
            {
                const PDFDeviceNColorSpace* deviceNColorSpace = dynamic_cast<const PDFDeviceNColorSpace*>(sourceColorSpace);

                if (!deviceNColorSpace->isNone())
                {
                    const PDFDeviceNColorSpace::Colorants& colorants = deviceNColorSpace->getColorants();
                    for (size_t i = 0; i < colorants.size(); ++i)
                    {
                        const PDFDeviceNColorSpace::ColorantInfo& colorantInfo = colorants[i];
                        const ColorInfo* info = getSpotColor(colorantInfo.name);

                        if (info && info->active && targetPixelFormat.hasSpotColors() && info->spotColorIndex < targetPixelFormat.getSpotColorChannelCount())
                        {
                            mapping.map(uint8_t(i), uint8_t(targetColorSpace->getColorComponentCount() + info->spotColorIndex));
                        }
                    }
                }

                break;
            }
        }
    }

    return mapping;
}

PDFPainterPathSampler::PDFPainterPathSampler(QPainterPath path, int samplesCount, PDFColorComponent defaultShape, QRect fillRect, bool precise) :
    m_defaultShape(defaultShape),
    m_samplesCount(qMax(samplesCount, 1)),
    m_path(qMove(path)),
    m_fillRect(fillRect),
    m_precise(precise)
{
    if (!precise)
    {
        m_fillPolygon = m_path.toFillPolygon();
        prepareScanLines();
    }
}

PDFColorComponent PDFPainterPathSampler::sample(QPoint point) const
{
    if (m_path.isEmpty() || !m_fillRect.contains(point))
    {
        return m_defaultShape;
    }

    if (!m_scanLineInfo.empty())
    {
        return sampleByScanLine(point);
    }

    const qreal coordX1 = point.x();
    const qreal coordX2 = coordX1 + 1.0;
    const qreal coordY1 = point.y();
    const qreal coordY2 = coordY1 + 1.0;

    const qreal centerX = (coordX1 + coordX2) * 0.5;
    const qreal centerY = (coordY1 + coordY2) * 0.5;

    const QPointF topLeft(coordX1, coordY1);
    const QPointF topRight(coordX2, coordY1);
    const QPointF bottomLeft(coordX1, coordY2);
    const QPointF bottomRight(coordX2, coordY2);

    if (m_samplesCount <= 1)
    {
        // Jakub Melka: Just one sample
        if (m_precise)
        {
            return m_path.contains(QPointF(centerX, centerY)) ? 1.0f : 0.0f;
        }
        else
        {
            return m_fillPolygon.contains(QPointF(centerX, centerY)) ? 1.0f : 0.0f;
        }
    }

    int cornerHits = 0;
    Qt::FillRule fillRule = m_path.fillRule();

    if (m_precise)
    {
        cornerHits += m_path.contains(topLeft) ? 1 : 0;
        cornerHits += m_path.contains(topRight) ? 1 : 0;
        cornerHits += m_path.contains(bottomLeft) ? 1 : 0;
        cornerHits += m_path.contains(bottomRight) ? 1 : 0;
    }
    else
    {
        cornerHits += m_fillPolygon.containsPoint(topLeft, fillRule) ? 1 : 0;
        cornerHits += m_fillPolygon.containsPoint(topRight, fillRule) ? 1 : 0;
        cornerHits += m_fillPolygon.containsPoint(bottomLeft, fillRule) ? 1 : 0;
        cornerHits += m_fillPolygon.containsPoint(bottomRight, fillRule) ? 1 : 0;
    }

    if (cornerHits == 4)
    {
        // Completely inside
        return 1.0;
    }

    if (cornerHits == 0)
    {
        // Completely outside
        return 0.0;
    }

    // Otherwise we must use regular sample grid
    const qreal offset = 1.0f / PDFColorComponent(m_samplesCount + 1);
    PDFColorComponent sampleValue = 0.0f;
    const PDFColorComponent sampleGain = 1.0f / PDFColorComponent(m_samplesCount * m_samplesCount);
    for (int ix = 0; ix < m_samplesCount; ++ix)
    {
        const qreal x = offset * (ix + 1) + coordX1;

        for (int iy = 0; iy < m_samplesCount; ++iy)
        {
            const qreal y = offset * (iy + 1) + coordY1;

            if (m_precise)
            {
                if (m_path.contains(QPointF(x, y)))
                {
                    sampleValue += sampleGain;
                }
            }
            else
            {
                if (m_fillPolygon.containsPoint(QPointF(x, y), fillRule))
                {
                    sampleValue += sampleGain;
                }
            }
        }
    }

    return sampleValue;
}

PDFColorComponent PDFPainterPathSampler::sampleByScanLine(QPoint point) const
{
    int scanLinePosition = point.y() - m_fillRect.y();

    size_t scanLineCountPerPixel = getScanLineCountPerPixel();
    size_t scanLineTopRow = scanLinePosition * scanLineCountPerPixel;
    size_t scanLineBottomRow = scanLineTopRow + scanLineCountPerPixel - 1;
    size_t scanLineGridRowTop = scanLineTopRow + 1;

    Qt::FillRule fillRule = m_path.fillRule();

    auto performSampling = [&](size_t scanLineIndex, PDFReal firstOrdinate, int sampleCount, PDFReal step, PDFReal gain)
    {
        ScanLineInfo info = m_scanLineInfo[scanLineIndex];
        auto it = std::next(m_scanLineSamples.cbegin(), info.indexStart);

        PDFReal ordinate = firstOrdinate;
        PDFColorComponent value = 0.0;
        auto ordinateIt = it;
        for (int i = 0; i < sampleCount; ++i)
        {
            while (std::next(ordinateIt)->x < ordinate)
            {
                ++ordinateIt;
            }

            int windingNumber = ordinateIt->windingNumber;

            const bool inside = (fillRule == Qt::WindingFill) ? windingNumber != 0 : windingNumber % 2 != 0;
            if (inside)
            {
                value += gain;
            }

            ordinate += step;
        }

        return value;
    };

    const qreal coordX1 = point.x();
    const PDFReal cornerValue = performSampling(scanLineTopRow, coordX1, 2, 1.0, 1.0) + performSampling(scanLineBottomRow, coordX1, 2, 1.0, 1.0);

    if (qFuzzyIsNull(4.0 - cornerValue))
    {
        // Whole inside
        return 1.0;
    }

    if (qFuzzyIsNull(cornerValue))
    {
        // Whole outside
        return 0.0;
    }

    const qreal offset = 1.0f / PDFColorComponent(m_samplesCount + 1);
    PDFColorComponent sampleValue = 0.0f;
    const PDFColorComponent sampleGain = 1.0f / PDFColorComponent(m_samplesCount * m_samplesCount);

    for (size_t i = 0; i < m_samplesCount; ++i)
    {
        sampleValue += performSampling(scanLineGridRowTop++, coordX1 + offset, m_samplesCount, offset, sampleGain);
    }

    return sampleValue;
}

size_t PDFPainterPathSampler::getScanLineCountPerPixel() const
{
    return m_samplesCount + 2;
}

void PDFPainterPathSampler::prepareScanLines()
{
    if (m_fillPolygon.isEmpty())
    {
        return;
    }

    for (int yOffset = m_fillRect.top(); yOffset <= m_fillRect.bottom(); ++yOffset)
    {
        const qreal coordY1 = yOffset;
        const qreal coordY2 = coordY1 + 1.0;

        // Top pixel line
        if (m_scanLineInfo.empty())
        {
            m_scanLineInfo.emplace_back(createScanLine(coordY1));
        }
        else
        {
            m_scanLineInfo.emplace_back(m_scanLineInfo.back());
        }

        // Sample grid
        const qreal offset = 1.0f / PDFColorComponent(m_samplesCount + 1);
        for (int iy = 0; iy < m_samplesCount; ++iy)
        {
            const qreal y = offset * (iy + 1) + coordY1;
            m_scanLineInfo.emplace_back(createScanLine(y));
        }

        // Bottom pixel line
        m_scanLineInfo.emplace_back(createScanLine(coordY2));
    }
}

PDFPainterPathSampler::ScanLineInfo PDFPainterPathSampler::createScanLine(qreal y)
{
    ScanLineInfo result;
    result.indexStart = m_scanLineSamples.size();

    // Add start item
    m_scanLineSamples.emplace_back(-std::numeric_limits<PDFReal>::infinity(), 0);

    // Traverse polygon, add sample for each polygon line, we must
    // also implicitly close last edge (if polygon is not closed)
    for (int i = 1; i < m_fillPolygon.size(); ++i)
    {
        createScanLineSample(m_fillPolygon[i - 1], m_fillPolygon[i], y);
    }

    if (m_fillPolygon.front() != m_fillPolygon.back())
    {
        createScanLineSample(m_fillPolygon.back(), m_fillPolygon.front(), y);
    }

    // Add end item
    m_scanLineSamples.emplace_back(+std::numeric_limits<PDFReal>::infinity(), 0);

    result.indexEnd = m_scanLineSamples.size();

    auto it = std::next(m_scanLineSamples.begin(), result.indexStart);
    auto itEnd = std::next(m_scanLineSamples.begin(), result.indexEnd);

    // Jakub Melka: now, sort the line samples and compute properly the winding number
    std::sort(it, itEnd);

    int currentWindingNumber = 0;
    for (; it != itEnd; ++it)
    {
        currentWindingNumber += it->windingNumber;
        it->windingNumber = currentWindingNumber;
    }

    return result;
}

void PDFPainterPathSampler::createScanLineSample(const QPointF& p1, const QPointF& p2, qreal y)
{
    PDFReal y1 = p1.y();
    PDFReal y2 = p2.y();

    if (qFuzzyIsNull(y2 - y1))
    {
        // Ignore horizontal lines
        return;
    }

    PDFReal x1 = p1.x();
    PDFReal x2 = p2.x();

    int windingNumber = 1;
    if (y2 < y1)
    {
        std::swap(y1, y2);
        std::swap(x1, x2);
        windingNumber = -1;
    }

    // Do we have intercept?
    if (y1 <= y && y < y2)
    {
        const PDFReal x = interpolate(y, y1, y2, x1, x2);
        m_scanLineSamples.emplace_back(x, windingNumber);
    }
}

void PDFDrawBuffer::markActiveColors(uint32_t activeColors)
{
    m_activeColors |= activeColors;
}

void PDFDrawBuffer::clear()
{
    if (!m_modifiedRect.isValid())
    {
        return;
    }

    for (int x = m_modifiedRect.left(); x <= m_modifiedRect.right(); ++x)
    {
        for (int y = m_modifiedRect.top(); y <= m_modifiedRect.bottom(); ++y)
        {
            PDFColorBuffer buffer = getPixel(x, y);
            std::fill(buffer.begin(), buffer.end(), 0.0f);
        }
    }

    m_activeColors = 0;
    m_containsFilling = false;
    m_containsStroking = false;
    m_modifiedRect = QRect();
}

void PDFDrawBuffer::modify(QRect rect, bool containsFilling, bool containsStroking)
{
    m_modifiedRect = m_modifiedRect.united(rect);
    m_containsFilling |= containsFilling;
    m_containsStroking |= containsStroking;
}

void PDFTransparencyRenderer::PDFTransparencyGroupPainterData::makeInitialBackdropTransparent()
{
    initialBackdrop.makeTransparent();
}

void PDFTransparencyRenderer::PDFTransparencyGroupPainterData::makeImmediateBackdropTransparent()
{
    immediateBackdrop.makeTransparent();
}

void PDFTransparencyRenderer::PDFTransparencyGroupPainterData::makeSoftMaskOpaque()
{
    softMask.makeOpaque();
}

}   // namespace pdf
