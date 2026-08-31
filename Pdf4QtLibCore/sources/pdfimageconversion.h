// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFIMAGECONVERSION_H
#define PDFIMAGECONVERSION_H

#include "pdfglobal.h"

#include <QImage>

#include <vector>

namespace pdf
{

/// This class facilitates various image conversions,
/// including transforming colored images into monochromatic (or bitonal) formats.
class PDF4QTLIBCORESHARED_EXPORT PDFImageConversion
{
public:
    PDFImageConversion();

    enum class ConversionMethod
    {
        Automatic,  ///< The threshold is determined automatically using an algorithm.
        Manual,     ///< The threshold is manually provided by the user.
        Adaptive,   ///< Adaptive local thresholding.
        Dither      ///< Error diffusion dither.
    };

    /// Specifies, how the alpha channel of the source image is treated
    /// during the conversion to the bitonal format.
    enum class AlphaMode
    {
        Ignore,     ///< Alpha channel is ignored, color samples are thresholded as they are.
        Composite   ///< Transparent pixels are composited onto the white background before
                    ///< the conversion, so they enter the threshold calculation as a blank
                    ///< paper instead of their meaningless color samples, and they are
                    ///< always converted to the white color. The transparency itself is not
                    ///< lost - it can be obtained as a bitonal mask using the function
                    ///< \p getConvertedAlphaMask.
    };

    /// Sets the image to be converted using the specified conversion method.
    /// This operation resets any previously converted image and the automatic threshold,
    /// thereby erasing all prior image data.
    /// \param image The image to be set for conversion.
    void setImage(QImage image);

    /// Sets the method for image conversion. Multiple methods are available
    /// for selection, including adaptive thresholding and dithering. If the
    /// manual method is chosen, an appropriate threshold must also be set by
    /// the user.
    /// \param method The conversion method to be used.
    void setConversionMethod(ConversionMethod method);

    /// Sets the manual threshold value. When a non-manual (e.g., automatic) conversion
    /// method is in use, this function will retain the manual threshold settings,
    /// but the conversion will utilize an automatically calculated threshold for the image.
    /// The manually set threshold is preserved and not overwritten. Therefore, if the
    /// manual conversion method is later selected, the previously established manual
    /// threshold will be applied.
    /// \param threshold The manual threshold value to be set.
    void setThreshold(int threshold);

    /// Sets the mode, in which the alpha channel of the source image is treated.
    /// Images, which do not have an alpha channel, are not affected by this setting.
    /// \param alphaMode Alpha mode to be set.
    void setAlphaMode(AlphaMode alphaMode);

    /// Returns the mode, in which the alpha channel of the source image is treated.
    AlphaMode getAlphaMode() const { return m_alphaMode; }

    /// This method converts the image into a bitonal (monochromatic) format. If
    /// the automatic threshold calculation is enabled, it executes Otsu's 1D algorithm
    /// to determine the threshold. When the manual conversion method is selected,
    /// the automatic threshold calculation is bypassed, and the predefined manual threshold
    /// value is utilized instead. Adaptive and dithered conversions use their respective
    /// algorithms and may ignore the global threshold. This method returns true if the
    /// conversion is successful, and false otherwise. The alpha channel of the source
    /// image is handled according to the selected alpha mode. \sa setAlphaMode
    bool convert();

    /// Returns the threshold used in image conversion. If the automatic conversion method is
    /// selected, this function should be called only after executing the convert() method;
    /// otherwise, it may return invalid data. The automatic threshold calculation is
    /// performed within the convert() method.
    /// \returns The threshold value used in image conversion.
    int getThreshold() const;

    /// Returns the converted image. This method should only be called after
    /// the convert() method has been executed, and additionally, only if the
    /// convert() method returns true. If these conditions are not met, the result
    /// is undefined.
    QImage getConvertedImage() const;

    /// Returns the transparency of the source image as a bitonal mask, in which
    /// a set sample means an opaque pixel and a cleared sample means a transparent
    /// pixel. A null image is returned, when the source image is fully opaque, or
    /// when the alpha mode is set to \p AlphaMode::Ignore. This method should be
    /// called only after a successful call of the convert() method. The mask can be
    /// used as a soft mask of the converted image, so the transparency of the source
    /// image is not lost during the conversion.
    QImage getConvertedAlphaMask() const;

    /// Returns true, if the converted image has a transparency mask.
    /// \sa getConvertedAlphaMask
    bool hasConvertedAlphaMask() const { return !m_convertedAlphaMask.isNull(); }

private:
    /// Decomposes the source image (with the transparent pixels composited onto the
    /// white background) into the lightness buffer, the grayscale buffer and the
    /// opacity buffer. All conversion algorithms then work with these buffers instead
    /// of the source image.
    void prepareSourceImage();

    int calculateOtsu1DThreshold() const;
    QImage convertThresholded(int threshold) const;
    QImage convertAdaptive() const;
    QImage convertDithered(int threshold) const;

    /// Returns true, if the pixel at a given index is treated as a transparent one
    bool isTransparent(size_t index) const { return !m_opacity.empty() && m_opacity[index] < OPACITY_THRESHOLD; }

    /// Creates a black bitonal image of the size of the source image
    QImage createBitonalImage() const;

    /// Sets the sample of the bitonal image to the white color. Samples of
    /// the image created by \p createBitonalImage are black by default.
    static void setWhiteSample(QImage& image, int x, int y);

    static constexpr int DEFAULT_THRESHOLD = 128;
    static constexpr int ADAPTIVE_WINDOW_RADIUS = 8;
    static constexpr int ADAPTIVE_OFFSET = 8;
    static constexpr int OPACITY_THRESHOLD = 128;

    QImage m_image;
    QImage m_convertedImage;
    QImage m_convertedAlphaMask;
    ConversionMethod m_conversionMethod = ConversionMethod::Automatic;
    AlphaMode m_alphaMode = AlphaMode::Composite;
    int m_automaticThreshold = DEFAULT_THRESHOLD;
    int m_manualThreshold = DEFAULT_THRESHOLD;

    int m_width = 0;
    int m_height = 0;

    /// Lightness (HSL color model) of the source image pixels, transparent pixels
    /// are composited onto the white background (when the alpha mode is
    /// \p AlphaMode::Composite). It is used by the global thresholding.
    std::vector<unsigned char> m_lightness;

    /// Grayscale (luminance) values of the same pixels. Adaptive thresholding and
    /// dithering historically operate on the grayscale representation of the image,
    /// not on the lightness, so both representations are kept.
    std::vector<unsigned char> m_gray;

    /// Alpha channel of the source image. This buffer is empty, when the source
    /// image is fully opaque, or when the alpha channel is being ignored.
    std::vector<unsigned char> m_opacity;
};

}   // namespace pdf

#endif // PDFIMAGECONVERSION_H
