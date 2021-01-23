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
#include "pdfpagecontentprocessor.h"

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

    constexpr static uint8_t INVALID_CHANNEL_INDEX = 0xFF;

    constexpr bool operator==(const PDFPixelFormat&) const = default;
    constexpr bool operator!=(const PDFPixelFormat&) const = default;

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
    constexpr uint8_t getColorChannelIndexStart() const { return (hasProcessColors() || hasSpotColors()) ? 0 : INVALID_CHANNEL_INDEX; }
    constexpr uint8_t getColorChannelIndexEnd() const { return (hasProcessColors() || hasSpotColors()) ? (m_processColors + m_spotColors) : INVALID_CHANNEL_INDEX; }
    constexpr uint8_t getShapeChannelIndex() const { return hasShapeChannel() ? getProcessColorChannelCount() + getSpotColorChannelCount() : INVALID_CHANNEL_INDEX; }
    constexpr uint8_t getOpacityChannelIndex() const { return hasShapeChannel() ? getProcessColorChannelCount() + getSpotColorChannelCount() + getShapeChannelCount() : INVALID_CHANNEL_INDEX; }

    /// Pixel format is valid, if we have at least one color channel
    /// (it doesn't matter, if it is process color, or spot color)
    constexpr bool isValid() const { return getColorChannelCount() > 0; }

    inline void setProcessColors(const uint8_t& processColors) { m_processColors = processColors; }
    inline void setSpotColors(const uint8_t& spotColors) { m_spotColors = spotColors; }
    inline void setProcessColorsSubtractive(bool subtractive)
    {
        if (subtractive)
        {
            m_flags |= FLAG_PROCESS_COLORS_SUBTRACTIVE;
        }
        else
        {
            m_flags &= ~FLAG_PROCESS_COLORS_SUBTRACTIVE;
        }
    }

    static constexpr PDFPixelFormat createOpacityMask() { return PDFPixelFormat(0, 0, FLAG_HAS_OPACITY_CHANNEL); }
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

    constexpr static uint8_t FLAG_HAS_SHAPE_CHANNEL = 0x01;
    constexpr static uint8_t FLAG_HAS_OPACITY_CHANNEL = 0x02;
    constexpr static uint8_t FLAG_PROCESS_COLORS_SUBTRACTIVE = 0x04;

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

    /// Returns buffer with pixel channels
    PDFColorBuffer getPixel(size_t x, size_t y);

    /// Returns buffer with all pixels
    PDFColorBuffer getPixels();

    const PDFColorComponent* begin() const;
    const PDFColorComponent* end() const;

    PDFColorComponent* begin();
    PDFColorComponent* end();

    size_t getWidth() const { return m_width; }
    size_t getHeight() const { return m_height; }
    size_t getPixelSize() const { return m_pixelSize; }
    PDFPixelFormat getPixelFormat() const { return m_format; }

    /// Fills both shape and opacity channel with zero value.
    /// If bitmap doesn't have shape/opacity channel, nothing happens.
    void makeTransparent();

    /// Fills both shape and opacity channel with 1.0 value.
    /// If bitmap doesn't have shape/opacity channel, nothing happens.
    void makeOpaque();

    /// Returns index where given pixel starts in the data block
    /// \param x Horizontal coordinate of the pixel
    /// \param y Vertical coordinate of the pixel
    size_t getPixelIndex(size_t x, size_t y) const;

    /// Extract process colors into another bitmap
    PDFFloatBitmap extractProcessColors();

    /// Performs bitmap blending, pixel format of source and target must be the same.
    /// Blending algorithm uses the one from chapter 11.4.8 in the PDF 2.0 specification.
    /// Bitmap size must be equal for all three bitmaps (source, target and soft mask).
    /// \param source Source bitmap
    /// \param target Target bitmap
    /// \param backdrop Backdrop
    /// \param initialBackdrop Initial backdrop
    /// \param softMask Soft mask
    /// \param alphaIsShape Both soft mask and constant alpha are shapes and not opacity?
    /// \param constantAlpha Constant alpha, can mean shape or opacity
    /// \param mode Blend mode
    void blend(const PDFFloatBitmap& source,
               PDFFloatBitmap& target,
               const PDFFloatBitmap& backdrop,
               const PDFFloatBitmap& initialBackdrop,
               PDFFloatBitmap& softMask,
               bool alphaIsShape,
               PDFColorComponent constantAlpha,
               BlendMode mode);

private:
    void fillChannel(size_t channel, PDFColorComponent value);

    PDFPixelFormat m_format;
    std::size_t m_width;
    std::size_t m_height;
    std::size_t m_pixelSize;
    std::vector<PDFColorComponent> m_data;
};

/// Float bitmap with color space
class PDFFloatBitmapWithColorSpace : public PDFFloatBitmap
{
public:
    explicit PDFFloatBitmapWithColorSpace();
    explicit PDFFloatBitmapWithColorSpace(size_t width, size_t height, PDFPixelFormat format);
    explicit PDFFloatBitmapWithColorSpace(size_t width, size_t height, PDFPixelFormat format, PDFColorSpacePointer blendColorSpace);

    PDFColorSpacePointer getColorSpace() const;
    void setColorSpace(const PDFColorSpacePointer& colorSpace);

    /// Converts bitmap to target color space
    /// \param cms Color management system
    /// \param targetColorSpace Target color space
    void convertToColorSpace(const PDFCMS* cms,
                             RenderingIntent intent,
                             const PDFColorSpacePointer& targetColorSpace,
                             PDFRenderErrorReporter* reporter);

private:
    PDFColorSpacePointer m_colorSpace;
};

/// Renders PDF pages with transparency, using 32-bit floating point precision.
/// Both device color space and blending color space can be defined. It implements
/// page blending space and device blending space. So, painted graphics is being
/// blended to the page blending space, and then converted to the device blending
/// space.
class PDFTransparencyRenderer : public PDFPageContentProcessor
{
private:
    using BaseClass = PDFPageContentProcessor;

public:
    PDFTransparencyRenderer(const PDFPage* page,
                            const PDFDocument* document,
                            const PDFFontCache* fontCache,
                            const PDFCMS* cms,
                            const PDFOptionalContentActivity* optionalContentActivity,
                            QMatrix pagePointToDevicePointMatrix);

    void setDeviceColorSpace(PDFColorSpacePointer colorSpace);

    /// Starts painting on the device. This function must be called before page
    /// content stream is being processed (and must be called exactly once).
    void beginPaint();

    /// Finishes painting on the device. This function must be called after page
    /// content stream is processed and all result graphics is being drawn. Page
    /// transparency group collapses nad contents are draw onto device transparency
    /// group.
    const PDFFloatBitmap& endPaint();

    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override;
    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule) override;
    virtual void performUpdateGraphicsState(const PDFPageContentProcessorState& state) override;
    virtual void performSaveGraphicState(ProcessOrder order) override;
    virtual void performRestoreGraphicState(ProcessOrder order) override;
    virtual void performBeginTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup) override;
    virtual void performEndTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup) override;

private:

    struct PDFTransparencyGroupPainterData
    {
        PDFTransparencyGroup group;
        bool alphaIsShape = false;
        PDFReal alphaStroke = 1.0;
        PDFReal alphaFill = 1.0;
        BlendMode blendMode = BlendMode::Normal;
        BlackPointCompensationMode blackPointCompensationMode = BlackPointCompensationMode::Default;
        RenderingIntent renderingIntent = RenderingIntent::RelativeColorimetric;
        PDFFloatBitmapWithColorSpace initialBackdrop;   ///< Initial backdrop
        PDFFloatBitmapWithColorSpace immediateBackdrop; ///< Immediate backdrop
        PDFFloatBitmap softMask; ///< Soft mask for this group
        PDFColorSpacePointer blendColorSpace;
    };

    void removeInitialBackdrop();

    PDFFloatBitmapWithColorSpace* getInitialBackdrop();
    PDFFloatBitmapWithColorSpace* getImmediateBackdrop();
    PDFFloatBitmapWithColorSpace* getBackdrop();
    const PDFColorSpacePointer& getBlendColorSpace() const;

    bool isTransparencyGroupIsolated() const;
    bool isTransparencyGroupKnockout() const;

    PDFColorSpacePointer m_deviceColorSpace;    ///< Device color space (color space for final result)
    PDFColorSpacePointer m_processColorSpace;   ///< Process color space (color space, in which is page graphic's blended)
    std::unique_ptr<PDFTransparencyGroupGuard> m_pageTransparencyGroupGuard;
    std::vector<PDFTransparencyGroupPainterData> m_transparencyGroupDataStack;
    bool m_active;
};

}   // namespace pdf

#endif // PDFTRANSPARENCYRENDERER_H
