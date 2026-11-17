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

#ifndef PDFTRANSPARENCYRENDERER_H
#define PDFTRANSPARENCYRENDERER_H

#include "pdfglobal.h"
#include "pdfcolorspaces.h"
#include "pdfpagecontentprocessor.h"
#include "pdfconstants.h"
#include "pdfutils.h"
#include "pdfprogress.h"

#include <QHash>
#include <QImage>

#include <set>
#include <tuple>

namespace pdf
{
class PDFAnnotation;
class PDFOperationControl;

/// Pixel format, describes color channels, both process colors (for example,
/// R, G, B, Gray, C, M, Y, K) or spot colors. Also, describes, if pixel
/// has shape channel and opacity channel. Two auxiliary channels are possible,
/// shape channel and opacity channel. Shape channel defines the shape (so, for
/// example, if we draw a rectangle onto the bitmap, shape value is 1.0 inside the
/// rectangle and 0.0 outside the rectangle). PDF transparency processing requires
/// shape and opacity values separated for correct transparency processing.
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
    constexpr bool hasActiveColorMask() const { return m_flags & FLAG_HAS_ACTIVE_COLOR_MASK; }

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
    constexpr uint8_t getOpacityChannelIndex() const { return hasOpacityChannel() ? getProcessColorChannelCount() + getSpotColorChannelCount() + getShapeChannelCount() : INVALID_CHANNEL_INDEX; }

    /// Pixel format is valid, if we have at least one color channel
    /// (it doesn't matter, if it is process color, or spot color)
    constexpr bool isValid() const { return getChannelCount() > 0; }

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
    static constexpr PDFPixelFormat createFormatDefaultGray(uint8_t spotColors) { return createFormat(1, spotColors, true, false, false); }
    static constexpr PDFPixelFormat createFormatDefaultRGB(uint8_t spotColors) { return createFormat(3, spotColors, true, false, false); }
    static constexpr PDFPixelFormat createFormatDefaultCMYK(uint8_t spotColors) { return createFormat(4, spotColors, true, true, false); }

    static constexpr PDFPixelFormat removeProcessColors(PDFPixelFormat format) { return PDFPixelFormat(0, format.getSpotColorChannelCount(), format.getFlags()); }
    static constexpr PDFPixelFormat removeSpotColors(PDFPixelFormat format) { return PDFPixelFormat(format.getProcessColorChannelCount(), 0, format.getFlags()); }
    static constexpr PDFPixelFormat removeShapeAndOpacity(PDFPixelFormat format) { return PDFPixelFormat(format.getProcessColorChannelCount(), format.getSpotColorChannelCount(), format.hasProcessColorsSubtractive() ? FLAG_PROCESS_COLORS_SUBTRACTIVE : 0); }

    static constexpr uint32_t getAllColorsMask() { return 0xFFFFFFFFu; }

    static constexpr PDFPixelFormat createFormat(uint8_t processColors, uint8_t spotColors, bool withShapeAndOpacity, bool processColorSubtractive, bool hasActiveColorMask)
    {
        const uint8_t flags = (withShapeAndOpacity ? FLAG_HAS_SHAPE_CHANNEL + FLAG_HAS_OPACITY_CHANNEL : 0) +
                              (processColorSubtractive ? FLAG_PROCESS_COLORS_SUBTRACTIVE : 0) +
                              (hasActiveColorMask ? FLAG_HAS_ACTIVE_COLOR_MASK : 0);
        return PDFPixelFormat(processColors, spotColors, flags);
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
    constexpr static uint8_t FLAG_HAS_ACTIVE_COLOR_MASK = 0x08;

    uint8_t m_processColors = 0;
    uint8_t m_spotColors = 0;
    uint8_t m_flags = 0;
};

/// Represents float bitmap with arbitrary color channel count. Bitmap can also
/// have auxiliary channels, such as shape and opacity channels.
class PDF4QTLIBCORESHARED_EXPORT PDFFloatBitmap
{
public:
    explicit PDFFloatBitmap();
    explicit PDFFloatBitmap(size_t width, size_t height, PDFPixelFormat format);

    PDFFloatBitmap(const PDFFloatBitmap&) = default;
    PDFFloatBitmap(PDFFloatBitmap&&) = default;

    PDFFloatBitmap& operator=(const PDFFloatBitmap&) = default;
    PDFFloatBitmap& operator=(PDFFloatBitmap&&) = default;

    /// Returns buffer with pixel channels
    PDFColorBuffer getPixel(size_t x, size_t y);

    /// Returns constant buffer with pixel channels
    PDFConstColorBuffer getPixel(size_t x, size_t y) const;

    /// Returns buffer with all pixels
    PDFColorBuffer getPixels();

    /// Returns ink coverage
    PDFColorComponent getPixelInkCoverage(size_t x, size_t y) const;

    /// Returns ink coverage bitmap. Bitmap consists of one color channel,
    /// which consists of ink coverage.
    PDFFloatBitmap getInkCoverageBitmap() const;

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

    /// Fillss process color channels to a value, that corresponds to black,
    /// so 0.0 for additive colors, 1.0 for subtractive colors.
    void makeColorBlack();

    /// Fillss process color channels to a value, that corresponds to white,
    /// so 1.0 for additive colors, 0.0 for subtractive colors.
    void makeColorWhite();

    /// Returns index where given pixel starts in the data block
    /// \param x Horizontal coordinate of the pixel
    /// \param y Vertical coordinate of the pixel
    size_t getPixelIndex(size_t x, size_t y) const;

    /// Returns true, if bitmap has active color mask
    bool hasActiveColorMask() const { return !m_activeColorMask.empty(); }

    /// Get color mask for given pixel. This function must be called
    /// only on bitmaps, which use active color mask.
    /// \param x Horizontal coordinate of the pixel
    /// \param y Vertical coordinate of the pixel
    uint32_t getPixelActiveColorMask(size_t x, size_t y) const;

    /// Marks active color channels for given pixel as active
    /// \param x Horizontal coordinate of the pixel
    /// \param y Vertical coordinate of the pixel
    /// \param activeColorMask Active color mask
    void markPixelActiveColorMask(size_t x, size_t y, uint32_t activeColorMask);

    /// Sets active color channels for given pixel
    /// \param x Horizontal coordinate of the pixel
    /// \param y Vertical coordinate of the pixel
    /// \param activeColorMask Active color mask
    void setPixelActiveColorMask(size_t x, size_t y, uint32_t activeColorMask);

    /// Sets all colors as active
    void setAllColorActive();

    /// Sets all colors as inactive
    void setAllColorInactive();

    /// Sets color activity to all pixels
    /// \param mask Color activity
    void setColorActivity(uint32_t mask);

    /// Returns gray image created from color channel. If color channel
    /// is invalid, then empty image is returned.
    /// \param channelIndex Channel index
    QImage getChannelImage(uint8_t channelIndex) const;

    /// Extract process colors into another bitmap
    PDFFloatBitmap extractProcessColors() const;

    /// Extract spot channel
    /// \param channel Channel
    PDFFloatBitmap extractSpotChannel(uint8_t channel) const;

    /// Extract opacity channel. If bitmap doesn't have opacity channel,
    /// opaque opacity bitmap of same size is returned.
    PDFFloatBitmap extractOpacityChannel() const;

    /// Extract luminosity channel as opacity channel. Bitmap should have
    /// 1 (gray), 3 (red, green, blue) or 4 (cyan, magenta, yellow, black)
    /// process colors. Otherwise opaque bitmap of same size is returned.
    PDFFloatBitmap extractLuminosityChannel() const;

    /// Copies channel from source bitmap to target channel in this bitmap
    /// \param sourceBitmap Source bitmap
    /// \param channelFrom Source channel
    /// \param channelTo Target channel
    void copyChannel(const PDFFloatBitmap& sourceBitmap, uint8_t channelFrom, uint8_t channelTo);

    /// Resize the bitmap using given transformation mode. Fast transformation mode
    /// uses nearest neighbor mapping, smooth transformation mode uses weighted
    /// averaging algorithm.
    /// \param mode Transformation mode
    PDFFloatBitmap resize(size_t width, size_t height, Qt::TransformationMode mode) const;

    enum class OverprintMode
    {
        NoOveprint,         ///< No oveprint performed
        Overprint_Mode_0,   ///< Overprint performed (either backdrop or source color is selected)
        Overprint_Mode_1,   ///< Overprint performed (only nonzero source color is selected, otherwise backdrop)
    };

    /// Performs bitmap blending, pixel format of source and target must be the same.
    /// Blending algorithm uses the one from chapter 11.4.8 in the PDF 2.0 specification.
    /// Bitmap size must be equal for all three bitmaps (source, target and soft mask).
    /// Oveprinting is also handled. You can specify a mask with active color channels.
    /// If n-th bit in \p activeColorChannels variable is 1, then color channel is active;
    /// otherwise backdrop color is selected (if overprint is active).
    /// \param source Source bitmap
    /// \param target Target bitmap
    /// \param backdrop Backdrop
    /// \param initialBackdrop Initial backdrop
    /// \param softMask Soft mask
    /// \param alphaIsShape Both soft mask and constant alpha are shapes and not opacity?
    /// \param constantAlpha Constant alpha, can mean shape or opacity
    /// \param mode Blend mode
    /// \param overprintMode Overprint mode
    /// \param blendRegion Blend region
    static void blend(const PDFFloatBitmap& source,
                      PDFFloatBitmap& target,
                      const PDFFloatBitmap& backdrop,
                      const PDFFloatBitmap& initialBackdrop,
                      const PDFFloatBitmap& softMask,
                      bool alphaIsShape,
                      PDFColorComponent constantAlpha,
                      BlendMode mode,
                      bool knockoutGroup,
                      OverprintMode overprintMode,
                      QRect blendRegion);

    /// Blends converted spot colors, which are in \p convertedSpotColors bitmap.
    /// Process colors must match.
    /// \param convertedSpotColors Bitmap with converted spot colors
    void blendConvertedSpots(const PDFFloatBitmap& convertedSpotColors);

    void fillProcessColorChannels(PDFColorComponent value);
    void fillChannel(size_t channel, PDFColorComponent value);

    /// Creates opaque soft mask of given size
    /// \param width Width
    /// \param height Height
    static PDFFloatBitmap createOpaqueSoftMask(size_t width, size_t height);

private:
    PDFPixelFormat m_format;
    std::size_t m_width;
    std::size_t m_height;
    std::size_t m_pixelSize;
    std::vector<PDFColorComponent> m_data;
    std::vector<uint32_t> m_activeColorMask;
};

/// Float bitmap with color space
class PDF4QTLIBCORESHARED_EXPORT PDFFloatBitmapWithColorSpace : public PDFFloatBitmap
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

/// Ink mapping
struct PDFInkMapping
{
    inline bool isValid() const { return !mapping.empty(); }
    inline void reserve(size_t size) { mapping.reserve(size); }
    inline void map(uint8_t source, uint8_t target) { mapping.emplace_back(Mapping{ source, target, Pass}); activeChannels |= (static_cast<uint32_t>(1u) << target); }

    enum Type
    {
        Pass
    };

    struct Mapping
    {
        uint8_t source = 0;
        uint8_t target = 0;
        Type type = Pass;
    };

    std::vector<Mapping> mapping;
    uint32_t activeChannels = 0;
};

/// Ink mapper for mapping device inks (device colors) and spot inks (spot colors).
class PDF4QTLIBCORESHARED_EXPORT PDFInkMapper
{
public:
    explicit PDFInkMapper(const PDFCMSManager* manager, const PDFDocument* document);

    struct ColorInfo
    {
        QByteArray name;
        QString textName;
        uint32_t spotColorIndex = 0; ///< Index of this spot color
        uint32_t colorSpaceIndex = 0; ///< Index into DeviceN color space (index of colorant)
        PDFColorSpacePointer colorSpace;
        bool canBeActive = false; ///< Can spot color be activated?
        bool active = false; ///< Is spot color active?
        bool isSpot = true;
        QColor color; ///< Spot/process color transformed to RGB color space
        PDFAbstractColorSpace::ColorSpace colorSpaceType = PDFAbstractColorSpace::ColorSpace::Invalid;
    };

    static constexpr const uint32_t MAX_COLOR_COMPONENTS = PDF_MAX_COLOR_COMPONENTS;
    static constexpr const uint32_t MAX_DEVICE_COLOR_COMPONENTS = 4;
    static constexpr const uint32_t MAX_SPOT_COLOR_COMPONENTS = MAX_COLOR_COMPONENTS - MAX_DEVICE_COLOR_COMPONENTS - 2;

    /// Returns a vector of separations correspoding to the process colors
    /// and spot colors. Only active spot colors are added. Only 1, 3 and 4
    /// process colors are supported.
    /// \param processColorCount Process color count
    /// \param withSpots Add active spot colors?
    std::vector<ColorInfo> getSeparations(uint32_t processColorCount, bool withSpots = true) const;

    /// Scan document for spot colors and fills color info (all pages, see the overload)
    /// \param activate Set spot colors active?
    void createSpotColors(bool activate);

    /// Scans the pages for spot colors and fills color info (plan of step 7 of phase 2, B197,
    /// B198): every resource reached from the pages by PDFResourceWalk - the pages, their forms,
    /// the tiling patterns, the Type 3 fonts, the groups of the soft masks and the appearance
    /// streams of the printed annotations (/F Print, not Hidden; ISO 32000-2, 12.5.3) - and in it
    /// the color spaces of /ColorSpace, of the images of /XObject, of /Shading and of the shading
    /// patterns of /Pattern, with the base of an Indexed space and the underlying space of an
    /// uncolored pattern space. A colorant /None of a DeviceN space (8.6.6.5) and /All are no
    /// inks. The first MAX_SPOT_COLOR_COMPONENTS spot colors in the order of the walk can be
    /// active (isSpotColorLimitReached).
    /// \param activate Set spot colors active?
    /// \param pageIndices The pages (indices from 0)
    /// \param control Cancellation of the walk (optional; a canceled walk gives the spot colors found so far)
    void createSpotColors(bool activate, const std::vector<PDFInteger>& pageIndices, const PDFOperationControl* control);

    /// Number of the spot colors found (also those, which cannot be active)
    size_t getSpotColorCount() const { return m_spotColors.size(); }

    /// Returns true, if more spot colors were found, than can be active (MAX_SPOT_COLOR_COMPONENTS;
    /// Output Preview 476:11703)
    bool isSpotColorLimitReached() const { return m_spotColors.size() > MAX_SPOT_COLOR_COMPONENTS; }

    /// All spot colors found, in their order (also the inactive ones)
    const std::vector<ColorInfo>& getSpotColors() const { return m_spotColors; }

    /// Number of the color space objects, which the last createSpotColors has parsed (a color space
    /// shared by the resources of many pages is parsed once; for the tests and the diagnostics)
    size_t getColorSpaceCreationCount() const { return m_colorSpaceCreationCount; }

    /// Returns true, if mapper contains given spot color
    /// \param colorName Color name
    bool containsSpotColor(const QByteArray& colorName) const;

    /// Returns true, if mapper contains given process color
    /// \param colorName Color name
    bool containsProcessColor(const QByteArray& colorName) const;

    /// Returns number of active spot colors
    size_t getActiveSpotColorCount() const { return m_activeSpotColors; }

    /// Returns spot color information (or nullptr, if spot color is not present)
    /// \param colorName Color name
    const ColorInfo* getSpotColor(const QByteArray& colorName) const;

    /// Returns process color information (or nullptr, if process color is not present)
    /// \param colorName Color name
    const ColorInfo* getProcessColor(const QByteArray& colorName) const;

    /// Returns process color information (or nullptr, if process color is not present or is inactive)
    /// \param colorName Color name
    /// \param colorSpace Color space (actively used color space)
    const ColorInfo* getActiveProcessColor(const QByteArray& colorName, PDFAbstractColorSpace::ColorSpace colorSpace) const;

    /// Returns active spot color with given index. If index
    /// of the spot color is invalid, or no active spot color
    /// is found, then nullptr is returned.
    /// \param index Active color index
    const ColorInfo* getActiveSpotColor(size_t index) const;

    /// Activates / deactivates spot colors
    /// \param active Make spot colors active?
    void setSpotColorsActive(bool active);

    /// Creates color mapping from source color space to the target color space.
    /// If mapping  cannot be created, then invalid mapping is returned. Target
    /// color space must be blending color space and must correspond to active
    /// blending space, if used when painting.
    /// \param sourceColorSpace Source color space
    /// \param targetColorSpace Target color space
    /// \param targetPixelFormat
    PDFInkMapping createMapping(const PDFAbstractColorSpace* sourceColorSpace,
                                const PDFAbstractColorSpace* targetColorSpace,
                                PDFPixelFormat targetPixelFormat) const;

private:
    /// What one createSpotColors has already scanned (RS7CORE M5 of step 7 of phase 2: the resources of
    /// N pages sharing one /ColorSpace dictionary of M spaces parsed N x M color spaces, each with its tint
    /// transformation). A key consists of the /ColorSpace dictionary of the resources (it resolves the
    /// names and the default spaces) and of the identity of the object in the document: a reference, the
    /// address of a direct array, dictionary or stream (the document is not changed while the mapper
    /// scans it), a name. Scanning a key again adds no ink (a spot color is added once, by its name, and
    /// the parsing does not depend on anything else), so a skipped key gives the same spot colors in the
    /// same order. Each set holds at most MAXIMUM_KEYS keys; over it, the objects are scanned again.
    class SpotColorScan
    {
    public:
        static constexpr size_t MAXIMUM_KEYS = size_t(1) << 20;

        /// The kinds of the dictionaries of the resources, which are scanned (one dictionary of a hostile
        /// document can be its /ColorSpace and its /XObject at once)
        enum class Container
        {
            ColorSpace,
            XObject,
            Shading,
            Pattern
        };

        /// True for the first call with \p kind, \p colorSpaceDictionary and \p container (a dictionary of
        /// the resources of the kind), which is then remembered
        bool isNewContainer(Container kind, const PDFDictionary* colorSpaceDictionary, const PDFDictionary* container);

        /// True for the first call with \p colorSpaceDictionary and \p colorSpaceObject, which is then remembered
        bool isNewColorSpace(const PDFDictionary* colorSpaceDictionary, const PDFObject& colorSpaceObject);

    private:
        using ColorSpaceKey = std::tuple<quintptr, PDFObjectReference, quintptr, QByteArray>;

        std::set<std::tuple<Container, quintptr, quintptr>> m_containers;
        std::set<ColorSpaceKey> m_colorSpaces;
    };

    /// Adds the inks of a color space (Separation, the colorants of DeviceN; the base of Indexed,
    /// the underlying space of a Pattern space); every other color space has no spot color
    void addSpotColors(const PDFColorSpacePointer& colorSpace, const PDFCMS* cms);

    /// Adds the inks of a color space object of a resource dictionary (a name or an array);
    /// an invalid one is skipped, a scanned one too (\p scan)
    /// \param colorSpaceDictionary The /ColorSpace dictionary of the resources (names of the spaces)
    /// \param colorSpaceObject The color space object
    /// \param scan What the run has already scanned
    void addSpotColors(const PDFDictionary* colorSpaceDictionary, const PDFObject& colorSpaceObject, const PDFCMS* cms, SpotColorScan* scan);

    /// Adds a spot color at the end of m_spotColors and to m_spotColorIndices
    void addSpotColor(ColorInfo info);

    const PDFCMSManager* m_cmsManager;
    const PDFDocument* m_document;
    std::vector<ColorInfo> m_spotColors;
    QHash<QByteArray, size_t> m_spotColorIndices;  ///< Index of a spot color of m_spotColors by its name (a hostile document has any number of spot colors: no linear search)
    std::vector<ColorInfo> m_deviceColors; ///< Device color space separations
    size_t m_activeSpotColors = 0;
    size_t m_colorSpaceCreationCount = 0;  ///< Color space objects parsed by the last createSpotColors
};

/// Represents draw buffer, into which is current graphics drawn
class PDFDrawBuffer : public PDFFloatBitmap
{
public:
    using PDFFloatBitmap::PDFFloatBitmap;

    /// Clears the draw buffer
    void clear();

    /// Marks given area as modified
    /// \param rect Modified area
    /// \param containsFilling The area is filled
    /// \param containsStroking The area is stroked
    /// \param isNonZeroOverprintAllowed The painted object may use the nonzero overprint mode
    ///        (ISO 32000-2, 8.6.7, 11.7.4.3: the current color of a DeviceCMYK color space in a
    ///        DeviceCMYK group, never an image or a shading; B211 of step 7 of phase 2)
    void modify(QRect rect, bool containsFilling, bool containsStroking, bool isNonZeroOverprintAllowed);

    /// Returns true, if draw buffer is modified and needs to be flushed
    bool isModified() const { return m_modifiedRect.isValid(); }

    /// Returns modified rectangle
    QRect getModifiedRect() const { return m_modifiedRect; }

    bool isContainsFilling() const { return m_containsFilling; }
    bool isContainsStroking() const { return m_containsStroking; }

    /// Every object in the buffer may use the nonzero overprint mode (overprint mode 1)
    bool isNonZeroOverprintAllowed() const { return m_isNonZeroOverprintAllowed; }

private:
    bool m_containsFilling = false;
    bool m_containsStroking = false;
    bool m_isNonZeroOverprintAllowed = false;
    QRect m_modifiedRect;
};

struct PDFTransparencyRendererSettings
{
    /// Tile size used for tile-based rendering.
    QSize tileSize = QSize(256, 256);

    /// Threshold for turning on painter path
    /// multithreaded painting. When number of potential
    /// pixels of painter path is greater than this constant,
    /// and MultithreadedPathSampler flag is turned on,
    /// multithreaded painting is performed.
    int multithreadingPathSampleThreshold = 128;

    /// Maximal number of steps performed in numerical algorithm
    /// used when some shadings are being sampled.
    int shadingAlgorithmLimit = 64;

    enum Flag
    {
        None               = 0x0000,

        /// Reserved for backward compatibility. Path sampler is no longer used.
        PrecisePathSampler          = 0x0001,

        /// Use multithreading when rasterized path/image/shading tiles are processed?
        MultithreadedPathSampler    = 0x0002,

        /// When using CMYK process color space, transfer spot
        /// colors to the CMYK color space.
        SeparationSimulation        = 0x0004,

        /// Use active color mask (so we can clear channels,
        /// which are not active)
        ActiveColorMask             = 0x0008,

        /// Use smooth image transform, if it is possible. For
        /// images, which doesn't have Interpolate set to true,
        /// fast image transformation is used.
        SmoothImageTransformation   = 0x0010,

        /// Display images (if this flag is false, images aren't processed)
        DisplayImages               = 0x0020,

        /// Display text (if this flag is false, text isnn't processed)
        DisplayText                 = 0x0040,

        /// Display vector graphics (if this flag is false, vector graphics isn't processed)
        DisplayVectorGraphics       = 0x0080,

        /// Display shading patterns (if this flag is false, shading patterns aren't processed)
        DisplayShadings             = 0x0100,

        /// Display tiling patterns (if this flag is false, tiling patterns aren't processed)
        DisplayTilingPatterns       = 0x0200,

        /// Saves process image before it is transformed into device space
        /// and before separation simulation is applied. Active color mask
        /// is still applied to this image (unless OriginalProcessImageWithoutMask).
        /// The image is in the process color space (setProcessColorSpace): a page
        /// group of another color space (/Group /CS /DeviceRGB) is converted to it
        /// first, as the output device does (ISO 32000-2, 11.4.7; B196 of step 7 of
        /// phase 2) - the inks are the inks of the process color space, never the
        /// components of the group.
        SaveOriginalProcessImage    = 0x0400,

        /// The original process image (SaveOriginalProcessImage) is saved before the
        /// active color mask clears the inactive inks: the inks keep their values,
        /// only the device image hides them (Output Preview: the area of an ink does
        /// not depend on whether it is shown)
        OriginalProcessImageWithoutMask = 0x0800,
    };

    Q_DECLARE_FLAGS(Flags, Flag)

    /// Flags
    Flags flags = static_cast<Flags>(DisplayImages | DisplayText | DisplayVectorGraphics | DisplayShadings | DisplayTilingPatterns);

    /// Active color mask
    uint32_t activeColorMask = PDFPixelFormat::getAllColorsMask();
};

/// Renders PDF pages with transparency, using 32-bit floating point precision.
/// Both device color space and blending color space can be defined. It implements
/// page blending space and device blending space. So, painted graphics is being
/// blended to the page blending space, and then converted to the device blending
/// space.
class PDF4QTLIBCORESHARED_EXPORT PDFTransparencyRenderer : public PDFPageContentProcessor
{
private:
    using BaseClass = PDFPageContentProcessor;

public:
    PDFTransparencyRenderer(const PDFPage* page,
                            const PDFDocument* document,
                            const PDFFontCache* fontCache,
                            const PDFICCProfileCache* iccProfileCache,
                            const PDFCMS* cms,
                            const PDFOptionalContentActivity* optionalContentActivity,
                            const PDFInkMapper* inkMapper,
                            PDFTransparencyRendererSettings settings,
                            QTransform pagePointToDevicePointMatrix);

    /// Sets device color space. This is final color space, to which
    /// is painted page transformed.
    /// \param colorSpace Color space
    void setDeviceColorSpace(PDFColorSpacePointer colorSpace);

    /// Sets process color space. This color space is used for blending
    /// and intermediate results. If page has transparency group, then
    /// blending color space from transparency group is used.
    /// \param colorSpace Color space
    void setProcessColorSpace(PDFColorSpacePointer colorSpace);

    /// Starts painting on the device. This function must be called before page
    /// content stream is being processed (and must be called exactly once).
    void beginPaint(QSize pixelSize);

    /// Finishes painting on the device. This function must be called after page
    /// content stream is processed and all result graphics is being drawn. Page
    /// transparency group collapses nad contents are draw onto device transparency
    /// group.
    const PDFFloatBitmap& endPaint();

    /// This function should be called only after call to \p endPaint. After painting,
    /// when it is finished, the result float image can be converted to QImage with
    /// this function, but only, if the float image is RGB. If error occurs, empty
    /// image is returned. Also, result image can be painted onto opaque paper
    /// with paper color \p paperColor.
    /// \param use16bit Produce 16-bit image instead of standard 8-bit
    /// \param usePaper Blend image with opaque paper, with color \p paperColor
    /// \param paperColor Paper color
    QImage toImage(bool use16Bit, bool usePaper, const PDFRGB& paperColor) const;

    /// Clear color buffer with given color (this affects all process colors). If a number
    /// of process colors are different from a number of colors in color, then error is triggered,
    /// and most min(process color count, colors in color) process color channels are filled
    /// with color.
    /// \param color Color
    void clearColor(const PDFColor& color);

    /// Returns original process bitmap, before it is transformed into device space,
    /// and before separation simulation is being processed. Active color mask is still
    /// applied to this image (unless OriginalProcessImageWithoutMask). It is in the
    /// process color space (see SaveOriginalProcessImage).
    PDFFloatBitmapWithColorSpace getOriginalProcessBitmap() const { return m_originalProcessBitmap; }

    /// Paints the annotations, which are printed with the page, over the painted contents (the
    /// output of a printer: ISO 32000-2, 12.5.3 - the flag Print, not Hidden; Table 167; a reply
    /// /IRT is not painted, as by PDFAnnotationManager; optional content of /OC by the activity of
    /// the renderer): the normal appearance stream of the current appearance state, placed by the
    /// algorithm of 12.5.5 (the box of the form mapped onto /Rect; NoRotate keeps the annotation
    /// upright). Call it after processContents and before endPaint. An annotation without an
    /// appearance stream is not painted (a warning). Every appearance is painted with the initial
    /// graphics state of the page (nothing the contents left at their top level - a matrix, a
    /// clip, an alpha - applies to it).
    /// \returns The errors of the annotations (the errors, which the processing of their content
    ///          streams reports through reportRenderError; see processPrintedAnnotations in the .cpp)
    QList<PDFRenderError> processPrintedAnnotations();

    virtual void reportRenderError(RenderErrorType type, QString message) override;
    virtual void reportRenderErrorOnce(RenderErrorType type, QString message) override;

    virtual bool isContentKindSuppressed(ContentKind kind) const override;
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override;
    virtual bool performPathPaintingUsingShading(const QPainterPath& path, bool stroke, bool fill, const PDFShadingPattern* shadingPattern) override;
    virtual void performFinishPathPainting() override;
    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule) override;
    virtual void performUpdateGraphicsState(const PDFPageContentProcessorState& state) override;
    virtual void performSaveGraphicState(ProcessOrder order) override;
    virtual void performRestoreGraphicState(ProcessOrder order) override;
    virtual void performBeginTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup) override;
    virtual void performEndTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup) override;
    virtual void performTextBegin(ProcessOrder order) override;
    virtual void performTextEnd(ProcessOrder order) override;
    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference) override;
    virtual void performImagePainting(const QImage& image) override;
    virtual void performMeshPainting(const PDFMesh& mesh) override;

private:

    struct PDFRasterMask;

    PDFReal getShapeStroking() const;
    PDFReal getOpacityStroking() const;
    PDFReal getShapeFilling() const;
    PDFReal getOpacityFilling() const;
    QSize getEffectiveTileSize() const;
    PDFRasterMask rasterizePathToMask(const QPainterPath& path, const QRect& rect, PDFColorComponent defaultValue) const;

    struct PDFTransparencySoftMaskImpl : public QSharedData
    {
        PDFTransparencySoftMaskImpl() = default;
        PDFTransparencySoftMaskImpl(bool isOpaque, PDFFloatBitmap softMask) :
            isOpaque(isOpaque),
            softMask(qMove(softMask))
        {

        }

        bool isOpaque = false;
        PDFFloatBitmap softMask;
    };

    class PDFTransparencySoftMask
    {
    public:
        PDFTransparencySoftMask() { m_data = new PDFTransparencySoftMaskImpl; }
        PDFTransparencySoftMask(bool isOpaque, PDFFloatBitmap softMask) { m_data = new PDFTransparencySoftMaskImpl(isOpaque, qMove(softMask)); }

        bool isOpaque() const { return m_data->isOpaque; }
        const PDFFloatBitmap* getSoftMask() const { return &m_data->softMask; }

        void makeOpaque();

    private:
        QSharedDataPointer<PDFTransparencySoftMaskImpl> m_data;
    };

    struct PDFTransparencyGroupPainterData
    {
        void makeInitialBackdropTransparent();
        void makeImmediateBackdropTransparent();

        PDFTransparencyGroup group;
        bool alphaIsShape = false;
        PDFReal alphaStroke = 1.0;
        PDFReal alphaFill = 1.0;
        BlendMode blendMode = BlendMode::Normal;
        BlackPointCompensationMode blackPointCompensationMode = BlackPointCompensationMode::Default;
        RenderingIntent renderingIntent = RenderingIntent::RelativeColorimetric;
        PDFFloatBitmapWithColorSpace initialBackdrop;   ///< Initial backdrop
        PDFFloatBitmapWithColorSpace immediateBackdrop; ///< Immediate backdrop
        PDFTransparencySoftMask softMask; ///< Soft mask for this group
        PDFColorSpacePointer blendColorSpace;
        bool filterColorsUsingMask = false;
        uint32_t activeColorMask = PDFPixelFormat::getAllColorsMask();
        bool transformSpotsToDevice = false;
        bool saveOriginalImage = false;
        bool isPageGroup = false;           ///< The page group (11.4.7)
    };

    struct PDFTransparencyPainterState
    {
        QPainterPath clipPath; ///< Clipping path in device state coordinates
        PDFTransparencySoftMask softMask;
    };

    struct PDFMappedColor
    {
        PDFColor mappedColor;
        uint32_t activeChannels = 0;
    };

    struct PDFRasterMask
    {
        QRect rect;
        PDFColorComponent defaultValue = 0.0f;
        std::vector<uint8_t> alphaCoverage; ///< 0..255 per pixel, row-major

        bool isConstant() const { return alphaCoverage.empty(); }
        PDFColorComponent sample(int x, int y) const
        {
            if (isConstant() || !rect.contains(x, y))
            {
                return defaultValue;
            }

            const int width = rect.width();
            const int index = (y - rect.top()) * width + (x - rect.left());
            Q_ASSERT(index >= 0 && index < int(alphaCoverage.size()));
            return alphaCoverage[index] * (1.0f / 255.0f);
        }
    };

    void invalidateCachedItems();
    void removeInitialBackdrop();

    void fillMappedColorUsingMapping(const PDFPixelFormat pixelFormat,
                                     PDFMappedColor& result,
                                     const PDFInkMapping& inkMapping,
                                     const PDFColor& sourceColor);

    PDFMappedColor createMappedColor(const PDFColor& sourceColor,
                                     const PDFAbstractColorSpace* sourceColorSpace);

    /// Converts image in some other color space to the blend color space,
    /// so pixel format is equal to the draw buffer's pixel format and active
    /// colors are set.
    /// \param image Image
    PDFFloatBitmapWithColorSpace convertImageToBlendSpace(const PDFFloatBitmapWithColorSpace& image);

    /// Converts RGB bitmap to the image.
    QImage toImageImpl(const PDFFloatBitmapWithColorSpace& floatImage, bool use16Bit) const;

    PDFFloatBitmapWithColorSpace* getInitialBackdrop();
    PDFFloatBitmapWithColorSpace* getImmediateBackdrop();
    PDFFloatBitmapWithColorSpace* getBackdrop();
    const PDFFloatBitmapWithColorSpace* getInitialBackdrop() const;
    const PDFFloatBitmapWithColorSpace* getImmediateBackdrop() const;
    const PDFFloatBitmapWithColorSpace* getBackdrop() const;
    const PDFColorSpacePointer& getBlendColorSpace() const;

    PDFTransparencyPainterState* getPainterState() { return &m_painterStateStack.top(); }

    bool isTransparencyGroupIsolated() const;
    bool isTransparencyGroupKnockout() const;

    const PDFMappedColor& getMappedStrokeColor();
    const PDFMappedColor& getMappedFillColor();

    PDFMappedColor getMappedStrokeColorImpl();
    PDFMappedColor getMappedFillColorImpl();

    /// Returns painting rectangle (i.e. rectangle, which has topleft coordinate 0,0
    /// and has width/height equal to bitmap width/height)
    QRect getPaintRect() const;

    /// Returns fill area from fill rectangle
    /// \param fillRect Fill rectangle
    QRect getActualFillRect(const QRectF& fillRect) const;

    /// Flushes draw buffer
    void flushDrawBuffer();

    /// The nonzero overprint mode may apply to an object painted by the current color of
    /// \p sourceColorSpace (ISO 32000-2, 8.6.7, 11.7.4.3, Table 146): the source and the group
    /// color space are both DeviceCMYK. The caller never asks it for an image or a shading.
    bool isNonZeroOverprintAllowed(const PDFAbstractColorSpace* sourceColorSpace) const;

    /// The color space is /None (a Separation /None, a DeviceN of /None colorants, an Indexed space
    /// of such a base): painting in it has no effect on the page (ISO 32000-2, 8.6.6.4, 8.6.6.5)
    static bool isNoneColorSpace(const PDFAbstractColorSpace* colorSpace);

    /// Paints one annotation of processPrintedAnnotations (its appearance stream)
    void processPrintedAnnotation(const PDFAnnotation* annotation, const PDFStream* formStream);

    /// Returns true, if multithreaded painter path sampling should be used
    /// for a given fill rectangle.
    /// \param fillRect Fill rectangle
    /// \returns true, if multithreading should be used
    bool isMultithreadedPathSamplingUsed(QRect fillRect) const;

    /// Performs sampling of single pixel. Sampled pixel is painted
    /// into the draw buffer.
    /// \param shape Constant shape value
    /// \param opacity Constant opacity value
    /// \param x Horizontal coordinate of the pixel
    /// \param y Vertical coordinate of the pixel
    /// \param shapeChannel Shape channel (draw buffer)
    /// \param opacityChannel Opacity channel (draw buffer)
    /// \param colorChannelStart Color channel start (draw buffer)
    /// \param colorChannelEnd Color channel end (draw buffer)
    /// \param fillColor Fill color
    /// \param clipMask Clipping mask
    /// \param pathMask Path mask
    void performPixelSampling(const PDFReal shape,
                              const PDFReal opacity,
                              const uint8_t shapeChannel,
                              const uint8_t opacityChannel,
                              const uint8_t colorChannelStart,
                              const uint8_t colorChannelEnd,
                              int x,
                              int y,
                              const PDFMappedColor& fillColor,
                              const PDFRasterMask& clipMask,
                              const PDFRasterMask& pathMask);

    /// Performs fragment fill from texture. Sampled pixel is painted
    /// into the draw buffer.
    /// \param shape Constant shape value
    /// \param opacity Constant opacity value
    /// \param shapeChannel Shape channel (draw buffer)
    /// \param opacityChannel Opacity channel (draw buffer)
    /// \param colorChannelStart Color channel start (draw buffer)
    /// \param colorChannelEnd Color channel end (draw buffer)
    /// \param x Horizontal coordinate of the fragment pixel
    /// \param y Vertical coordinate of the fragment pixel
    /// \param worldToTextureMatrix World to texture matrix
    /// \param texture Texture
    /// \param clipMask Clipping mask
    void performFillFragmentFromTexture(const PDFReal shape,
                                        const PDFReal opacity,
                                        const uint8_t shapeChannel,
                                        const uint8_t opacityChannel,
                                        const uint8_t colorChannelStart,
                                        const uint8_t colorChannelEnd,
                                        int x,
                                        int y,
                                        const QTransform& worldToTextureMatrix,
                                        const PDFFloatBitmap& texture,
                                        const PDFRasterMask& clipMask);

    /// Collapses spot colors to device colors
    /// \param data Bitmap with data
    void collapseSpotColorsToDeviceColors(PDFFloatBitmapWithColorSpace& bitmap);

    /// Transforms image to float image in actual blending color space,
    /// with marked colors. Function for internal use only.
    /// \param sourceImage
    PDFFloatBitmapWithColorSpace getImage(const PDFImage& sourceImage);

    /// Transforms colored image to float image in actual blending color space,
    /// with marked colors. Function for internal use only.
    /// \param sourceImage
    PDFFloatBitmapWithColorSpace getColoredImage(const PDFImage& sourceImage);

    /// Create soft mask from image data. Image data must contain proper soft
    /// mask, otherwise function will throw exception.
    /// \param imageData Soft mask data
    PDFFloatBitmap getAlphaMaskFromSoftMask(const PDFImageData& softMask);

    /// Processes soft mask and sets it as active
    /// \param softMask Soft mask
    void processSoftMask(const PDFDictionary* softMask);

    static void createOpaqueBitmap(PDFFloatBitmap& bitmap);
    static void createPaperBitmap(PDFFloatBitmap& bitmap, const PDFRGB& paperColor);
    static void createOpaqueSoftMask(PDFFloatBitmap& softMask, size_t width, size_t height) { softMask = PDFFloatBitmap::createOpaqueSoftMask(width, height); }

    PDFColorSpacePointer m_deviceColorSpace;    ///< Device color space (color space for final result)
    PDFColorSpacePointer m_processColorSpace;   ///< Process color space (color space, in which is page graphic's blended)
    std::unique_ptr<PDFTransparencyGroupGuard> m_pageTransparencyGroupGuard;
    std::unique_ptr<PDFTransparencyGroupGuard> m_textTransparencyGroupGuard;
    std::vector<PDFTransparencyGroupPainterData> m_transparencyGroupDataStack;
    std::stack<PDFTransparencyPainterState> m_painterStateStack;
    const PDFInkMapper* m_inkMapper;
    bool m_active;
    PDFCachedItem<PDFMappedColor> m_mappedStrokeColor;
    PDFCachedItem<PDFMappedColor> m_mappedFillColor;
    PDFTransparencyRendererSettings m_settings;
    PDFDrawBuffer m_drawBuffer;
    PDFFloatBitmapWithColorSpace m_originalProcessBitmap;
    PDFPageContentProcessorState m_initialGraphicState;     ///< The graphics state at beginPaint (processPrintedAnnotations)
    QList<PDFRenderError>* m_annotationErrors = nullptr;    ///< Errors of processPrintedAnnotations, while it runs
};

/// Ink coverage calculator. Calculates ink coverage for a given
/// page range. Calculates ink coverage of both cmyk colors and spot colors.
/// A page is measured by PDFOutputPreviewAnalyzer::renderPage (the one implementation of
/// Output Preview, Ink Coverage and this calculator; plan of step 7 of phase 2, 4.5): in the
/// process color space (B196), with the flags of the settings given to the constructor (B199),
/// the area weighted by the tint for the whole page and the ratio over the box of the page
/// (TrimBox, else CropBox, clipped by MediaBox - numerator and denominator of the same box,
/// owner answer Q7.6, R7-8, B169). The old Editor plugin uses it.
class PDF4QTLIBCORESHARED_EXPORT PDFInkCoverageCalculator
{
public:
    PDFInkCoverageCalculator(const PDFDocument* document,
                             const PDFFontCache* fontCache,
                             const PDFICCProfileCache* iccProfileCache,
                             const PDFCMSManager* cmsManager,
                             const PDFOptionalContentActivity* optionalContentActivity,
                             const PDFInkMapper* inkMapper,
                             PDFProgressSink* progress,
                             PDFTransparencyRendererSettings settings);

    struct InkCoverageChannelInfo
    {
        QByteArray name;
        QString textName;
        bool isSpot = true;
        QColor color;
        PDFColorComponent coveredArea = 0.0f;
        PDFColorComponent ratio = 0.0f;
    };

    /// Perform ink coverage calculations on given pages. Results are stored
    /// in this object. Page images are rendered using \p size resolution,
    /// and in this resolution, ink coverage is calculated.
    /// \param size Resolution size (for ink coverage calculation)
    /// \param pages Page indices
    void perform(QSize size, const std::vector<PDFInteger>& pages);

    /// Sets the cancellation of perform (B199): a canceled page has no result. The object must
    /// outlive perform; nullptr = none.
    void setOperationControl(const PDFOperationControl* control) { m_control = control; }

    /// Clear all calculated ink coverage results
    void clear();

    /// Clears calculated ink coverage for all pages. If ink coverage is not
    /// calculated for given page index, empty vector is returned.
    /// \param pageIndex Page index
    const std::vector<InkCoverageChannelInfo>* getInkCoverage(PDFInteger pageIndex) const;

    /// Find coverage info in vector by colorant name. If coverage info with a given colorant
    /// name is not found, then nullptr is returned.
    /// \param infos Vector of coverage info
    /// \param name Colornant name
    /// \returns Info for a given colorant name, or nullptr, if it is not found
    static const InkCoverageChannelInfo* findCoverageInfoByName(const std::vector<InkCoverageChannelInfo>& infos, const QByteArray& name);

    /// Find coverage info in vector by colorant name. If coverage info with a given colorant
    /// name is not found, then nullptr is returned.
    /// \param infos Vector of coverage info
    /// \param name Colornant name
    /// \returns Info for a given colorant name, or nullptr, if it is not found
    static InkCoverageChannelInfo* findCoverageInfoByName(std::vector<InkCoverageChannelInfo>& infos, const QByteArray& name);

private:
    const PDFDocument* m_document;
    const PDFFontCache* m_fontCache;
    const PDFICCProfileCache* m_iccProfileCache;
    const PDFCMSManager* m_cmsManager;
    const PDFOptionalContentActivity* m_optionalContentActivity;
    const PDFInkMapper* m_inkMapper;
    PDFProgressSink* m_progress;
    PDFTransparencyRendererSettings m_settings;
    const PDFOperationControl* m_control = nullptr;

    QMutex m_mutex;
    std::map<pdf::PDFInteger, std::vector<InkCoverageChannelInfo>> m_inkCoverageResults;
};

}   // namespace pdf

#endif // PDFTRANSPARENCYRENDERER_H
