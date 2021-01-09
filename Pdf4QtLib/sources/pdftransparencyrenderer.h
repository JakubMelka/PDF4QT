//    Copyright (C) 2021 Jakub Melka
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

#ifndef PDFTRANSPARENCYRENDERER_H
#define PDFTRANSPARENCYRENDERER_H

#include "pdfglobal.h"
#include "pdfcolorspaces.h"

namespace pdf
{

/// Pixel format, describes color channels, both process colors (for example,
/// R, G, B, Gray, C, M, Y, K) or spot colors. Also, describes, if pixel
/// has shape channel and opacity channel. Two auxiliary channels are possible,
/// shape channel and opacity channel. Shape channel defines the shape (so, for
/// example, if we draw a rectangle onto the bitmap, shape value is 1.0 inside the
/// rectangle and 0.0 outside the rectangle). PDF transparency processing requires
/// shape and opacity values separated.
class PDFPixelFormat
{
public:
    inline explicit constexpr PDFPixelFormat() = default;

    constexpr uint8_t INVALID_CHANNEL_INDEX = 0xFF;

    constexpr bool hasProcessColors() const { return m_processColors > 0; }
    constexpr bool hasSpotColors() const { return m_spotColors > 0; }
    constexpr bool hasShapeChannel() const { return m_flags & FLAG_HAS_SHAPE_CHANNEL; }
    constexpr bool hasOpacityChannel() const { return m_flags & FLAG_HAS_OPACITY_CHANNEL; }
    constexpr bool hasProcessColorsSubtractive() const { return m_flags & FLAG_PROCESS_COLORS_SUBTRACTIVE; }
    constexpr bool hasSpotColorsSubtractive() const { return true; }

    constexpr uint8_t getFlags() const { return m_flags; }
    constexpr uint8_t getMaximalColorChannelCount() const { return 32; }
    constexpr uint8_t getProcessColorChannelCount() const { return m_processColors; }
    constexpr uint8_t getSpotColorChannelCount() const { return m_spotColors; }
    constexpr uint8_t getColorChannelCount() const { return getProcessColorChannelCount() + getSpotColorChannelCount(); }
    constexpr uint8_t getShapeChannelCount() const { return hasShapeChannel() ? 1 : 0; }
    constexpr uint8_t getOpacityChannelCount() const { return hasOpacityChannel() ? 1 : 0; }
    constexpr uint8_t getAuxiliaryChannelCount() const { return getShapeChannelCount() + getOpacityChannelCount(); }
    constexpr uint8_t getChannelCount() const { return getColorChannelCount() + getAuxiliaryChannelCount(); }

    constexpr uint8_t getProcessColorChannelIndexStart() const { return hasProcessColors() ? 0 : INVALID_CHANNEL_INDEX; }
    constexpr uint8_t getProcessColorChannelIndexEnd() const { return hasProcessColors() ? getProcessColorChannelCount() : INVALID_CHANNEL_INDEX; }
    constexpr uint8_t getSpotColorChannelIndexStart() const { return hasSpotColors() ? getProcessColorChannelCount() : INVALID_CHANNEL_INDEX; }
    constexpr uint8_t getSpotColorChannelIndexEnd() const { return hasSpotColors() ? getSpotColorChannelIndexStart() + getSpotColorChannelCount() : INVALID_CHANNEL_INDEX; }
    constexpr uint8_t getShapeChannelIndex() const { return hasShapeChannel() ? getProcessColorChannelCount() + getSpotColorChannelCount() : INVALID_CHANNEL_INDEX; }
    constexpr uint8_t getOpacityChannelIndex() const { return hasShapeChannel() ? getProcessColorChannelCount() + getSpotColorChannelCount() + getShapeChannelCount() : INVALID_CHANNEL_INDEX; }

    /// Pixel format is valid, if we have at least one color channel
    /// (it doesn't matter, if it is process color, or spot color)
    constexpr bool isValid() const { return getColorChannelCount() > 0; }

    inline void setProcessColors(const uint8_t& processColors) { m_processColors = processColors; }
    inline void setSpotColors(const uint8_t& spotColors) { m_spotColors = spotColors; }

    static constexpr PDFPixelFormat createFormatDefaultGray(uint8_t spotColors) { return createFormat(1, spotColors, true, false); }
    static constexpr PDFPixelFormat createFormatDefaultRGB(uint8_t spotColors) { return createFormat(3, spotColors, true, false); }
    static constexpr PDFPixelFormat createFormatDefaultCMYK(uint8_t spotColors) { return createFormat(4, spotColors, true, true); }

    static constexpr PDFPixelFormat removeProcessColors(PDFPixelFormat format) { return PDFPixelFormat(0, format.getSpotColorChannelCount(), format.getFlags()); }
    static constexpr PDFPixelFormat removeSpotColors(PDFPixelFormat format) { return PDFPixelFormat(format.getProcessColorChannelCount(), 0, format.getFlags()); }
    static constexpr PDFPixelFormat removeShapeAndOpacity(PDFPixelFormat format) { return PDFPixelFormat(format.getProcessColorChannelCount(), format.getSpotColorChannelCount(), format.hasProcessColorsSubtractive() ? FLAG_PROCESS_COLORS_SUBTRACTIVE : 0); }

    static constexpr PDFPixelFormat createFormat(uint8_t processColors, uint8_t spotColors, bool withShapeAndOpacity, bool processColorSubtractive)
    {
        return PDFPixelFormat(processColors, spotColors, (withShapeAndOpacity ? FLAG_HAS_SHAPE_CHANNEL + FLAG_HAS_OPACITY_CHANNEL : 0) + (processColorSubtractive ? FLAG_PROCESS_COLORS_SUBTRACTIVE : 0));
    }

    /// Calculates bitmap data length required to store bitmapt with given pixel format.
    /// \param width Bitmap width
    /// \param height Bitmap height
    size_t calculateBitmapDataLength(size_t width, size_t height) const { return width * height * size_t(getChannelCount()); }

private:
    inline explicit constexpr PDFPixelFormat(uint8_t processColors, uint8_t spotColors, uint8_t flags) :
        m_processColors(processColors),
        m_spotColors(spotColors),
        m_flags(flags)
    {

    }

    constexpr uint8_t FLAG_HAS_SHAPE_CHANNEL = 0x01;
    constexpr uint8_t FLAG_HAS_OPACITY_CHANNEL = 0x02;
    constexpr uint8_t FLAG_PROCESS_COLORS_SUBTRACTIVE = 0x04;

    uint8_t m_processColors = 0;
    uint8_t m_spotColors = 0;
    uint8_t m_flags = 0;
};

/// Represents float bitmap with arbitrary color channel count. Bitmap can also
/// have auxiliary channels, such as shape and opacity channels.
class PDFFloatBitmap
{
public:
    explicit PDFFloatBitmap();
    explicit PDFFloatBitmap(size_t width, size_t height, PDFPixelFormat format);

    /// Returns buffer with pixel
    PDFColorBuffer getPixel(size_t x, size_t y);

    const PDFColorComponent* begin() const;
    const PDFColorComponent* end() const;

    PDFColorComponent* begin();
    PDFColorComponent* end();

    /// Returns index where given pixel starts in the data block
    /// \param x Horizontal coordinate of the pixel
    /// \param y Vertical coordinate of the pixel
    size_t getPixelIndex(size_t x, size_t y) const;

private:
    PDFPixelFormat m_format;
    std::size_t m_width;
    std::size_t m_height;
    std::size_t m_pixelSize;
    std::vector<PDFColorComponent> m_data;
};

class PDFTransparencyRenderer
{
public:
    PDFTransparencyRenderer();
};

}   // namespace pdf

#endif // PDFTRANSPARENCYRENDERER_H
