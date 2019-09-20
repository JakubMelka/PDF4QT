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

#include "pdfcolorspaces.h"
#include "pdfobject.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfutils.h"
#include "pdfpattern.h"

namespace pdf
{

QColor PDFDeviceGrayColorSpace::getDefaultColor() const
{
    return QColor(Qt::black);
}

QColor PDFDeviceGrayColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    PDFColorComponent component = clip01(color[0]);

    QColor result(QColor::Rgb);
    result.setRgbF(component, component, component, 1.0);
    return result;
}

size_t PDFDeviceGrayColorSpace::getColorComponentCount() const
{
    return 1;
}

QColor PDFDeviceRGBColorSpace::getDefaultColor() const
{
    return QColor(Qt::black);
}

QColor PDFDeviceRGBColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    return fromRGB01({ color[0], color[1], color[2] });
}

size_t PDFDeviceRGBColorSpace::getColorComponentCount() const
{
    return 3;
}

QColor PDFDeviceCMYKColorSpace::getDefaultColor() const
{
    return QColor(Qt::black);
}

QColor PDFDeviceCMYKColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    PDFColorComponent c = clip01(color[0]);
    PDFColorComponent m = clip01(color[1]);
    PDFColorComponent y = clip01(color[2]);
    PDFColorComponent k = clip01(color[3]);

    QColor result(QColor::Cmyk);
    result.setCmykF(c, m, y, k, 1.0);
    return result;
}

size_t PDFDeviceCMYKColorSpace::getColorComponentCount() const
{
    return 4;
}

QImage PDFAbstractColorSpace::getImage(const PDFImageData& imageData) const
{
    if (imageData.isValid())
    {
        switch (imageData.getMaskingType())
        {
            case PDFImageData::MaskingType::None:
            {
                QImage image(imageData.getWidth(), imageData.getHeight(), QImage::Format_RGB888);
                image.fill(QColor(Qt::white));

                unsigned int componentCount = imageData.getComponents();
                if (componentCount != getColorComponentCount())
                {
                    throw PDFParserException(PDFTranslationContext::tr("Invalid colors for color space. Color space has %1 colors. Provided color count is %4.").arg(getColorComponentCount()).arg(componentCount));
                }

                const std::vector<PDFReal>& decode = imageData.getDecode();
                if (!decode.empty() && decode.size() != componentCount * 2)
                {
                    throw PDFParserException(PDFTranslationContext::tr("Invalid size of the decoded array. Expected %1, actual %2.").arg(componentCount * 2).arg(decode.size()));
                }

                PDFBitReader reader(&imageData.getData(), imageData.getBitsPerComponent());

                PDFColor color;
                color.resize(componentCount);

                const double max = reader.max();
                const double coefficient = 1.0 / max;
                for (unsigned int i = 0, rowCount = imageData.getHeight(); i < rowCount; ++i)
                {
                    reader.seek(i * imageData.getStride());
                    unsigned char* outputLine = image.scanLine(i);

                    for (unsigned int j = 0; j < imageData.getWidth(); ++j)
                    {
                        for (unsigned int k = 0; k < componentCount; ++k)
                        {
                            PDFReal value = reader.read();

                            // Interpolate value, if it is not empty
                            if (!decode.empty())
                            {
                                color[k] = interpolate(value, 0.0, max, decode[2 * k], decode[2 * k + 1]);
                            }
                            else
                            {
                                color[k] = value * coefficient;
                            }
                        }

                        QColor transformedColor = getColor(color);
                        QRgb rgb = transformedColor.rgb();

                        *outputLine++ = qRed(rgb);
                        *outputLine++ = qGreen(rgb);
                        *outputLine++ = qBlue(rgb);
                    }
                }

                return image;
            }

            case PDFImageData::MaskingType::ColorKeyMasking:
            {
                QImage image(imageData.getWidth(), imageData.getHeight(), QImage::Format_RGBA8888);
                image.fill(QColor(Qt::transparent));

                unsigned int componentCount = imageData.getComponents();
                if (componentCount != getColorComponentCount())
                {
                    throw PDFParserException(PDFTranslationContext::tr("Invalid colors for color space. Color space has %1 colors. Provided color count is %4.").arg(getColorComponentCount()).arg(componentCount));
                }

                Q_ASSERT(componentCount > 0);
                const std::vector<PDFInteger>& colorKeyMask = imageData.getColorKeyMask();
                if (colorKeyMask.size() / 2 != componentCount)
                {
                    throw PDFParserException(PDFTranslationContext::tr("Invalid number of color components in color key mask. Expected %1, provided %2.").arg(2 * componentCount).arg(colorKeyMask.size()));
                }

                const std::vector<PDFReal>& decode = imageData.getDecode();
                if (!decode.empty() && decode.size() != componentCount * 2)
                {
                    throw PDFParserException(PDFTranslationContext::tr("Invalid size of the decoded array. Expected %1, actual %2.").arg(componentCount * 2).arg(decode.size()));
                }

                PDFBitReader reader(&imageData.getData(), imageData.getBitsPerComponent());

                PDFColor color;
                color.resize(componentCount);

                const double max = reader.max();
                const double coefficient = 1.0 / max;
                for (unsigned int i = 0, rowCount = imageData.getHeight(); i < rowCount; ++i)
                {
                    reader.seek(i * imageData.getStride());
                    unsigned char* outputLine = image.scanLine(i);

                    for (unsigned int j = 0; j < imageData.getWidth(); ++j)
                    {
                        // Number of masked-out colors
                        unsigned int maskedColors = 0;

                        for (unsigned int k = 0; k < componentCount; ++k)
                        {
                            PDFBitReader::Value value = reader.read();

                            // Interpolate value, if it is not empty
                            if (!decode.empty())
                            {
                                color[k] = interpolate(value, 0.0, max, decode[2 * k], decode[2 * k + 1]);
                            }
                            else
                            {
                                color[k] = value * coefficient;
                            }

                            Q_ASSERT(2 * k + 1 < colorKeyMask.size());
                            if (static_cast<std::decay<decltype(colorKeyMask)>::type::value_type>(value) >= colorKeyMask[2 * k] &&
                                static_cast<std::decay<decltype(colorKeyMask)>::type::value_type>(value) <= colorKeyMask[2 * k + 1])
                            {
                                ++maskedColors;
                            }
                        }

                        QColor transformedColor = getColor(color);
                        QRgb rgb = transformedColor.rgb();

                        *outputLine++ = qRed(rgb);
                        *outputLine++ = qGreen(rgb);
                        *outputLine++ = qBlue(rgb);
                        *outputLine++ = (maskedColors == componentCount) ? 0x00 : 0xFF;
                    }
                }

                return image;
            }

            default:
            {
                throw PDFRendererException(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Image masking not implemented!"));
            }
        }
    }

    return QImage();
}

QColor PDFAbstractColorSpace::getCheckedColor(const PDFColor& color) const
{
    if (getColorComponentCount() != color.size())
    {
        throw PDFParserException(PDFTranslationContext::tr("Invalid number of color components. Expected number is %1, actual number is %2.").arg(static_cast<int>(getColorComponentCount()), static_cast<int>(color.size())));
    }

    return getColor(color);
}

PDFColorSpacePointer PDFAbstractColorSpace::createColorSpace(const PDFDictionary* colorSpaceDictionary,
                                                             const PDFDocument* document,
                                                             const PDFObject& colorSpace)
{
    return createColorSpaceImpl(colorSpaceDictionary, document, colorSpace, COLOR_SPACE_MAX_LEVEL_OF_RECURSION);
}

PDFColorSpacePointer PDFAbstractColorSpace::createDeviceColorSpaceByName(const PDFDictionary* colorSpaceDictionary,
                                                                         const PDFDocument* document,
                                                                         const QByteArray& name)
{
    return createDeviceColorSpaceByNameImpl(colorSpaceDictionary, document, name, COLOR_SPACE_MAX_LEVEL_OF_RECURSION);
}

PDFColor PDFAbstractColorSpace::convertToColor(const std::vector<PDFReal>& components)
{
    PDFColor result;

    for (PDFReal component : components)
    {
        result.push_back(component);
    }

    return result;
}

bool PDFAbstractColorSpace::isColorEqual(const PDFColor& color1, const PDFColor& color2, PDFReal tolerance)
{
    const size_t size = color1.size();
    if (size != color2.size())
    {
        return false;
    }

    for (size_t i = 0; i < size; ++i)
    {
        if (std::fabs(color1[i] - color2[i]) > tolerance)
        {
            return false;
        }
    }

    return true;
}

PDFColor PDFAbstractColorSpace::mixColors(const PDFColor& color1, const PDFColor& color2, PDFReal ratio)
{
    const size_t size = color1.size();
    Q_ASSERT(size == color2.size());

    PDFColor result;
    result.resize(size);
    for (size_t i = 0; i < size; ++i)
    {
        result[i] = color1[i] * (1.0 - ratio) + color2[i] * ratio;
    }

    return result;
}

PDFColorSpacePointer PDFAbstractColorSpace::createColorSpaceImpl(const PDFDictionary* colorSpaceDictionary,
                                                                 const PDFDocument* document,
                                                                 const PDFObject& colorSpace,
                                                                 int recursion)
{
    if (--recursion <= 0)
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't load color space, because color space structure is too complex."));
    }

    if (colorSpace.isName())
    {
        return createDeviceColorSpaceByNameImpl(colorSpaceDictionary, document, colorSpace.getString(), recursion);
    }
    else if (colorSpace.isArray())
    {
        // First value of the array should be identification name, second value dictionary with parameters
        const PDFArray* array = colorSpace.getArray();
        size_t count = array->getCount();

        if (count > 0)
        {
            // Name of the color space
            const PDFObject& colorSpaceIdentifier = document->getObject(array->getItem(0));
            if (colorSpaceIdentifier.isName())
            {
                QByteArray name = colorSpaceIdentifier.getString();

                const PDFDictionary* dictionary = nullptr;
                const PDFStream* stream = nullptr;
                if (count > 1)
                {
                    const PDFObject& colorSpaceSettings = document->getObject(array->getItem(1));
                    if (colorSpaceSettings.isDictionary())
                    {
                        dictionary = colorSpaceSettings.getDictionary();
                    }
                    if (colorSpaceSettings.isStream())
                    {
                        stream = colorSpaceSettings.getStream();
                    }
                }

                if (name == COLOR_SPACE_NAME_PATTERN)
                {
                    return PDFColorSpacePointer(new PDFPatternColorSpace(std::make_shared<PDFInvalidPattern>()));
                }

                if (dictionary)
                {
                    if (name == COLOR_SPACE_NAME_CAL_GRAY)
                    {
                        return PDFCalGrayColorSpace::createCalGrayColorSpace(document, dictionary);
                    }
                    else if (name == COLOR_SPACE_NAME_CAL_RGB)
                    {
                        return PDFCalRGBColorSpace::createCalRGBColorSpace(document, dictionary);
                    }
                    else if (name == COLOR_SPACE_NAME_LAB)
                    {
                        return PDFLabColorSpace::createLabColorSpace(document, dictionary);
                    }
                }

                if (stream && name == COLOR_SPACE_NAME_ICCBASED)
                {
                    return PDFICCBasedColorSpace::createICCBasedColorSpace(colorSpaceDictionary, document, stream, recursion);
                }

                if (name == COLOR_SPACE_NAME_INDEXED && count == 4)
                {
                    return PDFIndexedColorSpace::createIndexedColorSpace(colorSpaceDictionary, document, array, recursion);
                }

                if (name == COLOR_SPACE_NAME_SEPARATION && count == 4)
                {
                    return PDFSeparationColorSpace::createSeparationColorSpace(colorSpaceDictionary, document, array, recursion);
                }

                if (name == COLOR_SPACE_NAME_DEVICE_N && count >= 4)
                {
                    return PDFDeviceNColorSpace::createDeviceNColorSpace(colorSpaceDictionary, document, array, recursion);
                }

                // Try to just load by standard way - we can have "standard" color space stored in array
                return createColorSpaceImpl(colorSpaceDictionary, document, colorSpaceIdentifier, recursion);
            }
        }
    }

    throw PDFParserException(PDFTranslationContext::tr("Invalid color space."));
    return PDFColorSpacePointer();
}

PDFColorSpacePointer PDFAbstractColorSpace::createDeviceColorSpaceByNameImpl(const PDFDictionary* colorSpaceDictionary,
                                                                             const PDFDocument* document,
                                                                             const QByteArray& name,
                                                                             int recursion)
{
    if (--recursion <= 0)
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't load color space, because color space structure is too complex."));
    }

    if (name == COLOR_SPACE_NAME_PATTERN)
    {
        return PDFColorSpacePointer(new PDFPatternColorSpace(std::make_shared<PDFInvalidPattern>()));
    }

    if (name == COLOR_SPACE_NAME_DEVICE_GRAY || name == COLOR_SPACE_NAME_ABBREVIATION_DEVICE_GRAY)
    {
        if (colorSpaceDictionary && colorSpaceDictionary->hasKey(COLOR_SPACE_NAME_DEFAULT_GRAY))
        {
            return createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(colorSpaceDictionary->get(COLOR_SPACE_NAME_DEFAULT_GRAY)), recursion);
        }
        else
        {
            return PDFColorSpacePointer(new PDFDeviceGrayColorSpace());
        }
    }
    else if (name == COLOR_SPACE_NAME_DEVICE_RGB || name == COLOR_SPACE_NAME_ABBREVIATION_DEVICE_RGB)
    {
        if (colorSpaceDictionary && colorSpaceDictionary->hasKey(COLOR_SPACE_NAME_DEFAULT_RGB))
        {
            return createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(colorSpaceDictionary->get(COLOR_SPACE_NAME_DEFAULT_RGB)), recursion);
        }
        else
        {
            return PDFColorSpacePointer(new PDFDeviceRGBColorSpace());
        }
    }
    else if (name == COLOR_SPACE_NAME_DEVICE_CMYK || name == COLOR_SPACE_NAME_ABBREVIATION_DEVICE_CMYK)
    {
        if (colorSpaceDictionary && colorSpaceDictionary->hasKey(COLOR_SPACE_NAME_DEFAULT_CMYK))
        {
            return createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(colorSpaceDictionary->get(COLOR_SPACE_NAME_DEFAULT_CMYK)), recursion);
        }
        else
        {
            return PDFColorSpacePointer(new PDFDeviceCMYKColorSpace());
        }
    }
    else if (colorSpaceDictionary && colorSpaceDictionary->hasKey(name))
    {
        return createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(colorSpaceDictionary->get(name)), recursion);
    }

    throw PDFParserException(PDFTranslationContext::tr("Invalid color space."));
    return PDFColorSpacePointer();
}

/// Conversion matrix from XYZ space to RGB space. Values are taken from this article:
/// https://en.wikipedia.org/wiki/SRGB#The_sRGB_transfer_function_.28.22gamma.22.29
static constexpr const PDFColorComponentMatrix<3, 3> matrixXYZtoRGB(
     3.2406f, -1.5372f, -0.4986f,
    -0.9689f,  1.8758f,  0.0415f,
     0.0557f, -0.2040f,  1.0570f
);

PDFColor3 PDFAbstractColorSpace::convertXYZtoRGB(const PDFColor3& xyzColor)
{
    return matrixXYZtoRGB * xyzColor;
}


QColor PDFXYZColorSpace::getDefaultColor() const
{
    PDFColor color;
    const size_t componentCount = getColorComponentCount();
    for (size_t i = 0; i < componentCount; ++i)
    {
        color.push_back(0.0f);
    }
    return getColor(color);
}

PDFXYZColorSpace::PDFXYZColorSpace(PDFColor3 whitePoint) :
    m_whitePoint(whitePoint),
    m_correctionCoefficients()
{
    PDFColor3 mappedWhitePoint = convertXYZtoRGB(m_whitePoint);

    m_correctionCoefficients[0] = 1.0f / mappedWhitePoint[0];
    m_correctionCoefficients[1] = 1.0f / mappedWhitePoint[1];
    m_correctionCoefficients[2] = 1.0f / mappedWhitePoint[2];
}

PDFCalGrayColorSpace::PDFCalGrayColorSpace(PDFColor3 whitePoint, PDFColor3 blackPoint, PDFColorComponent gamma) :
    PDFXYZColorSpace(whitePoint),
    m_blackPoint(blackPoint),
    m_gamma(gamma)
{

}

QColor PDFCalGrayColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    const PDFColorComponent A = clip01(color[0]);
    const PDFColorComponent xyzColor = std::powf(A, m_gamma);
    const PDFColor3 xyzColorMultipliedByWhitePoint = colorMultiplyByFactor(m_whitePoint, xyzColor);
    const PDFColor3 rgb = convertXYZtoRGB(xyzColorMultipliedByWhitePoint);
    const PDFColor3 calibratedRGB = colorMultiplyByFactors(rgb, m_correctionCoefficients);
    return fromRGB01(calibratedRGB);
}

size_t PDFCalGrayColorSpace::getColorComponentCount() const
{
    return 1;
}

PDFColorSpacePointer PDFCalGrayColorSpace::createCalGrayColorSpace(const PDFDocument* document, const PDFDictionary* dictionary)
{
    // Standard D65 white point
    PDFColor3 whitePoint = { 0.9505f, 1.0000f, 1.0890f };
    PDFColor3 blackPoint = { 0, 0, 0 };
    PDFColorComponent gamma = 1.0f;

    PDFDocumentDataLoaderDecorator loader(document);
    loader.readNumberArrayFromDictionary(dictionary, CAL_WHITE_POINT, whitePoint.begin(), whitePoint.end());
    loader.readNumberArrayFromDictionary(dictionary, CAL_BLACK_POINT, blackPoint.begin(), blackPoint.end());
    gamma = loader.readNumberFromDictionary(dictionary, CAL_GAMMA, gamma);

    return PDFColorSpacePointer(new PDFCalGrayColorSpace(whitePoint, blackPoint, gamma));
}

PDFCalRGBColorSpace::PDFCalRGBColorSpace(PDFColor3 whitePoint, PDFColor3 blackPoint, PDFColor3 gamma, PDFColorComponentMatrix_3x3 matrix) :
    PDFXYZColorSpace(whitePoint),
    m_blackPoint(blackPoint),
    m_gamma(gamma),
    m_matrix(matrix)
{

}

QColor PDFCalRGBColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    const PDFColor3 ABC = clip01(PDFColor3{ color[0], color[1], color[2] });
    const PDFColor3 ABCwithGamma = colorPowerByFactors(ABC, m_gamma);
    const PDFColor3 XYZ = m_matrix * ABCwithGamma;
    const PDFColor3 rgb = convertXYZtoRGB(XYZ);
    const PDFColor3 calibratedRGB = colorMultiplyByFactors(rgb, m_correctionCoefficients);
    return fromRGB01(calibratedRGB);
}

size_t PDFCalRGBColorSpace::getColorComponentCount() const
{
    return 3;
}

PDFColorSpacePointer PDFCalRGBColorSpace::createCalRGBColorSpace(const PDFDocument* document, const PDFDictionary* dictionary)
{
    // Standard D65 white point
    PDFColor3 whitePoint = { 0.9505f, 1.0000f, 1.0890f };
    PDFColor3 blackPoint = { 0, 0, 0 };
    PDFColor3 gamma = { 1.0f, 1.0f, 1.0f };
    PDFColorComponentMatrix_3x3 matrix( 1, 0, 0,
                                        0, 1, 0,
                                        0, 0, 1 );

    PDFDocumentDataLoaderDecorator loader(document);
    loader.readNumberArrayFromDictionary(dictionary, CAL_WHITE_POINT, whitePoint.begin(), whitePoint.end());
    loader.readNumberArrayFromDictionary(dictionary, CAL_BLACK_POINT, blackPoint.begin(), blackPoint.end());
    loader.readNumberArrayFromDictionary(dictionary, CAL_GAMMA, gamma.begin(), gamma.end());
    loader.readNumberArrayFromDictionary(dictionary, CAL_MATRIX, matrix.begin(), matrix.end());

    return PDFColorSpacePointer(new PDFCalRGBColorSpace(whitePoint, blackPoint, gamma, matrix));
}

PDFLabColorSpace::PDFLabColorSpace(PDFColor3 whitePoint,
                                   PDFColor3 blackPoint,
                                   PDFColorComponent aMin,
                                   PDFColorComponent aMax,
                                   PDFColorComponent bMin,
                                   PDFColorComponent bMax) :
    PDFXYZColorSpace(whitePoint),
    m_blackPoint(blackPoint),
    m_aMin(aMin),
    m_aMax(aMax),
    m_bMin(bMin),
    m_bMax(bMax)
{

}

QColor PDFLabColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    const PDFColorComponent LStar = qBound(0.0f, color[0], 100.0f);
    const PDFColorComponent aStar = qBound(m_aMin, color[1], m_aMax);
    const PDFColorComponent bStar = qBound(m_bMin, color[2], m_bMax);

    const PDFColorComponent param1 = (LStar + 16.0f) / 116.0f;
    const PDFColorComponent param2 = aStar / 500.0f;
    const PDFColorComponent param3 = bStar / 200.0f;

    const PDFColorComponent L = param1 + param2;
    const PDFColorComponent M = param1;
    const PDFColorComponent N = param1 - param3;

    auto g = [](PDFColorComponent x) -> PDFColorComponent
    {
        if (x >= 6.0f / 29.0f)
        {
            return x * x * x;
        }
        else
        {
            return (108.0f / 841.0f) * (x - 4.0f / 29.0f);
        }
    };

    const PDFColorComponent gL = g(L);
    const PDFColorComponent gM = g(M);
    const PDFColorComponent gN = g(N);
    const PDFColor3 gLMN = { gL, gM, gN };

    const PDFColor3 XYZ = colorMultiplyByFactors(m_whitePoint, gLMN);
    const PDFColor3 rgb = convertXYZtoRGB(XYZ);
    const PDFColor3 calibratedRGB = colorMultiplyByFactors(rgb, m_correctionCoefficients);
    return fromRGB01(calibratedRGB);
}

size_t PDFLabColorSpace::getColorComponentCount() const
{
    return 3;
}

PDFColorSpacePointer PDFLabColorSpace::createLabColorSpace(const PDFDocument* document, const PDFDictionary* dictionary)
{
    // Standard D65 white point
    PDFColor3 whitePoint = { 0.9505f, 1.0000f, 1.0890f };
    PDFColor3 blackPoint = { 0, 0, 0 };

    static_assert(std::numeric_limits<PDFColorComponent>::has_infinity, "Fix this code!");
    const PDFColorComponent infPos = std::numeric_limits<PDFColorComponent>::infinity();
    const PDFColorComponent infNeg = -std::numeric_limits<PDFColorComponent>::infinity();
    std::array<PDFColorComponent, 4> minMax = { infNeg, infPos, infNeg, infPos };

    PDFDocumentDataLoaderDecorator loader(document);
    loader.readNumberArrayFromDictionary(dictionary, CAL_WHITE_POINT, whitePoint.begin(), whitePoint.end());
    loader.readNumberArrayFromDictionary(dictionary, CAL_BLACK_POINT, blackPoint.begin(), blackPoint.end());
    loader.readNumberArrayFromDictionary(dictionary, CAL_RANGE, minMax.begin(), minMax.end());

    return PDFColorSpacePointer(new PDFLabColorSpace(whitePoint, blackPoint, minMax[0], minMax[1], minMax[2], minMax[3]));
}

PDFICCBasedColorSpace::PDFICCBasedColorSpace(PDFColorSpacePointer alternateColorSpace, Ranges range) :
    m_alternateColorSpace(qMove(alternateColorSpace)),
    m_range(range)
{

}

QColor PDFICCBasedColorSpace::getDefaultColor() const
{
    PDFColor color;
    const size_t componentCount = getColorComponentCount();
    for (size_t i = 0; i < componentCount; ++i)
    {
        color.push_back(0.0f);
    }
    return getColor(color);
}

QColor PDFICCBasedColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    size_t colorComponentCount = getColorComponentCount();

    // Clip color values by range
    PDFColor clippedColor = color;
    for (size_t i = 0; i < colorComponentCount; ++i)
    {
        const size_t imin = 2 * i + 0;
        const size_t imax = 2 * i + 1;
        clippedColor[i] = qBound(m_range[imin], clippedColor[i], m_range[imax]);
    }

    return m_alternateColorSpace->getColor(clippedColor);
}

size_t PDFICCBasedColorSpace::getColorComponentCount() const
{
    return m_alternateColorSpace->getColorComponentCount();
}

PDFColorSpacePointer PDFICCBasedColorSpace::createICCBasedColorSpace(const PDFDictionary* colorSpaceDictionary,
                                                                     const PDFDocument* document,
                                                                     const PDFStream* stream,
                                                                     int recursion)
{
    // First, try to load alternate color space, if it is present
    const PDFDictionary* dictionary = stream->getDictionary();

    PDFDocumentDataLoaderDecorator loader(document);
    PDFColorSpacePointer alternateColorSpace;
    if (dictionary->hasKey(ICCBASED_ALTERNATE))
    {
        alternateColorSpace = PDFAbstractColorSpace::createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(dictionary->get(ICCBASED_ALTERNATE)), recursion);
    }
    else
    {
        // Determine color space from parameter N, which determines number of components
        const PDFInteger N = loader.readIntegerFromDictionary(dictionary, ICCBASED_N, 0);

        switch (N)
        {
            case 1:
            {
                alternateColorSpace = PDFAbstractColorSpace::createColorSpaceImpl(colorSpaceDictionary, document, PDFObject::createName(std::make_shared<PDFString>(std::move(QByteArray(COLOR_SPACE_NAME_DEVICE_GRAY)))), recursion);
                break;
            }

            case 3:
            {
                alternateColorSpace = PDFAbstractColorSpace::createColorSpaceImpl(colorSpaceDictionary, document, PDFObject::createName(std::make_shared<PDFString>(std::move(QByteArray(COLOR_SPACE_NAME_DEVICE_RGB)))), recursion);
                break;
            }

            case 4:
            {
                alternateColorSpace = PDFAbstractColorSpace::createColorSpaceImpl(colorSpaceDictionary, document, PDFObject::createName(std::make_shared<PDFString>(std::move(QByteArray(COLOR_SPACE_NAME_DEVICE_CMYK)))), recursion);
                break;
            }

            default:
            {
                throw PDFParserException(PDFTranslationContext::tr("Can't determine alternate color space for ICC based profile. Number of components is %1.").arg(N));
                break;
            }
        }
    }

    if (!alternateColorSpace)
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't determine alternate color space for ICC based profile."));
    }

    Ranges ranges = { 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f };
    static_assert(ranges.size() == 8, "Fix initialization above!");

    const size_t components = alternateColorSpace->getColorComponentCount();
    const size_t rangeSize = 2 * components;

    if (rangeSize > ranges.size())
    {
        throw PDFParserException(PDFTranslationContext::tr("Too much color components for ICC based profile."));
    }

    auto itStart = ranges.begin();
    auto itEnd = std::next(itStart, rangeSize);
    loader.readNumberArrayFromDictionary(dictionary, ICCBASED_RANGE, itStart, itEnd);

    return PDFColorSpacePointer(new PDFICCBasedColorSpace(qMove(alternateColorSpace), ranges));
}

PDFIndexedColorSpace::PDFIndexedColorSpace(PDFColorSpacePointer baseColorSpace, QByteArray&& colors, int maxValue) :
    m_baseColorSpace(qMove(baseColorSpace)),
    m_colors(qMove(colors)),
    m_maxValue(maxValue)
{

}

QColor PDFIndexedColorSpace::getDefaultColor() const
{
    return getColor(PDFColor(0.0f));
}

QColor PDFIndexedColorSpace::getColor(const PDFColor& color) const
{
    // Indexed color space value must have exactly one component!
    Q_ASSERT(color.size() == 1);

    const int colorIndex = qBound(MIN_VALUE, static_cast<int>(color[0]), m_maxValue);
    const int componentCount = static_cast<int>(m_baseColorSpace->getColorComponentCount());
    const int byteOffset = colorIndex * componentCount;

    // We must point into the array. Check first and last component.
    Q_ASSERT(byteOffset + componentCount - 1 < m_colors.size());

    PDFColor decodedColor;
    const char* bytePointer = m_colors.constData() + byteOffset;

    for (int i = 0; i < componentCount; ++i)
    {
        const unsigned char value = *bytePointer++;
        const PDFColorComponent component = static_cast<PDFColorComponent>(value) / 255.0f;
        decodedColor.push_back(component);
    }

    return m_baseColorSpace->getColor(decodedColor);
}

size_t PDFIndexedColorSpace::getColorComponentCount() const
{
    return 1;
}

QImage PDFIndexedColorSpace::getImage(const PDFImageData& imageData) const
{
    if (imageData.isValid())
    {
        QImage image(imageData.getWidth(), imageData.getHeight(), QImage::Format_RGB888);
        image.fill(QColor(Qt::white));

        unsigned int componentCount = imageData.getComponents();
        PDFBitReader reader(&imageData.getData(), imageData.getBitsPerComponent());

        if (componentCount != getColorComponentCount())
        {
            throw PDFParserException(PDFTranslationContext::tr("Invalid colors for indexed color space. Color space has %1 colors. Provided color count is %4.").arg(getColorComponentCount()).arg(componentCount));
        }

        Q_ASSERT(componentCount == 1);

        PDFColor color;
        color.resize(1);

        for (unsigned int i = 0, rowCount = imageData.getHeight(); i < rowCount; ++i)
        {
            reader.seek(i * imageData.getStride());
            unsigned char* outputLine = image.scanLine(i);

            for (unsigned int j = 0; j < imageData.getWidth(); ++j)
            {
                PDFBitReader::Value index = reader.read();
                color[0] = index;

                QColor transformedColor = getColor(color);
                QRgb rgb = transformedColor.rgb();

                *outputLine++ = qRed(rgb);
                *outputLine++ = qGreen(rgb);
                *outputLine++ = qBlue(rgb);
            }
        }

        return image;
    }

    return QImage();
}

PDFColorSpacePointer PDFIndexedColorSpace::createIndexedColorSpace(const PDFDictionary* colorSpaceDictionary,
                                                                   const PDFDocument* document,
                                                                   const PDFArray* array,
                                                                   int recursion)
{
    Q_ASSERT(array->getCount() == 4);

    // Read base color space
    PDFColorSpacePointer baseColorSpace = PDFAbstractColorSpace::createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(array->getItem(1)), recursion);

    if (!baseColorSpace)
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't determine base color space for indexed color space."));
    }

    // Read maximum value
    PDFDocumentDataLoaderDecorator loader(document);
    const int maxValue = qBound<int>(MIN_VALUE, loader.readInteger(array->getItem(2), 0), MAX_VALUE);

    // Read stream/byte string with corresponding color values
    QByteArray colors;
    const PDFObject& colorDataObject = document->getObject(array->getItem(3));

    if (colorDataObject.isString())
    {
        colors = colorDataObject.getString();
    }
    else if (colorDataObject.isStream())
    {
        colors = document->getDecodedStream(colorDataObject.getStream());
    }

    // Check, if we have enough colors
    const int colorCount = maxValue - MIN_VALUE + 1;
    const int componentCount = static_cast<int>(baseColorSpace->getColorComponentCount());
    const int byteCount = colorCount * componentCount;
    if (byteCount != colors.size())
    {
        throw PDFParserException(PDFTranslationContext::tr("Invalid colors for indexed color space. Color space has %1 colors, %2 color components and must have %3 size. Provided size is %4.").arg(colorCount).arg(componentCount).arg(byteCount).arg(colors.size()));
    }

    return PDFColorSpacePointer(new PDFIndexedColorSpace(qMove(baseColorSpace), qMove(colors), maxValue));
}

PDFSeparationColorSpace::PDFSeparationColorSpace(QByteArray&& colorName, PDFColorSpacePointer alternateColorSpace, PDFFunctionPtr tintTransform) :
    m_colorName(qMove(colorName)),
    m_alternateColorSpace(qMove(alternateColorSpace)),
    m_tintTransform(qMove(tintTransform))
{

}

QColor PDFSeparationColorSpace::getDefaultColor() const
{
    return getColor(PDFColor(0.0f));
}

QColor PDFSeparationColorSpace::getColor(const PDFColor& color) const
{
    // Separation color space value must have exactly one component!
    Q_ASSERT(color.size() == 1);

    // Input value
    double tint = color.back();

    // Output values
    std::vector<double> outputColor;
    outputColor.resize(m_alternateColorSpace->getColorComponentCount(), 0.0);
    PDFFunction::FunctionResult result = m_tintTransform->apply(&tint, &tint + 1, outputColor.data(), outputColor.data() + outputColor.size());

    if (result)
    {
        PDFColor color;
        std::for_each(outputColor.cbegin(), outputColor.cend(), [&color](double value) { color.push_back(static_cast<float>(value)); });
        return m_alternateColorSpace->getColor(color);
    }
    else
    {
        // Return invalid color
        return QColor();
    }
}

size_t PDFSeparationColorSpace::getColorComponentCount() const
{
    return 1;
}

PDFColorSpacePointer PDFSeparationColorSpace::createSeparationColorSpace(const PDFDictionary* colorSpaceDictionary,
                                                                         const PDFDocument* document,
                                                                         const PDFArray* array,
                                                                         int recursion)
{
    Q_ASSERT(array->getCount() == 4);

    // Read color name
    const PDFObject& colorNameObject = document->getObject(array->getItem(1));
    if (!colorNameObject.isName())
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't determine color name for separation color space."));
    }
    QByteArray colorName = colorNameObject.getString();

    // Read alternate color space
    PDFColorSpacePointer alternateColorSpace = PDFAbstractColorSpace::createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(array->getItem(2)), recursion);
    if (!alternateColorSpace)
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't determine alternate color space for separation color space."));
    }

    PDFFunctionPtr tintTransform = PDFFunction::createFunction(document, array->getItem(3));
    if (!tintTransform)
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't determine tint transform for separation color space."));
    }

    return PDFColorSpacePointer(new PDFSeparationColorSpace(qMove(colorName), qMove(alternateColorSpace), qMove(tintTransform)));
}

const unsigned char* PDFImageData::getRow(unsigned int rowIndex) const
{
    const unsigned char* data = reinterpret_cast<const unsigned char*>(m_data.constData());

    Q_ASSERT(rowIndex < m_height);
    return data + (rowIndex * m_stride);
}

QColor PDFPatternColorSpace::getDefaultColor() const
{
    return QColor(Qt::transparent);
}

QColor PDFPatternColorSpace::getColor(const PDFColor& color) const
{
    Q_UNUSED(color);
    throw PDFParserException(PDFTranslationContext::tr("Pattern doesn't have defined uniform color."));
}

size_t PDFPatternColorSpace::getColorComponentCount() const
{
    return 0;
}

PDFDeviceNColorSpace::PDFDeviceNColorSpace(PDFDeviceNColorSpace::Type type,
                                           PDFDeviceNColorSpace::Colorants&& colorants,
                                           PDFColorSpacePointer alternateColorSpace,
                                           PDFColorSpacePointer processColorSpace,
                                           PDFFunctionPtr tintTransform,
                                           std::vector<QByteArray>&& colorantsPrintingOrder,
                                           std::vector<QByteArray> processColorSpaceComponents) :
    m_type(type),
    m_colorants(qMove(colorants)),
    m_alternateColorSpace(qMove(alternateColorSpace)),
    m_processColorSpace(qMove(processColorSpace)),
    m_tintTransform(qMove(tintTransform)),
    m_colorantsPrintingOrder(qMove(colorantsPrintingOrder)),
    m_processColorSpaceComponents(qMove(processColorSpaceComponents))
{

}

QColor PDFDeviceNColorSpace::getDefaultColor() const
{
    PDFColor color;
    color.resize(getColorComponentCount());
    return getColor(color);
}

QColor PDFDeviceNColorSpace::getColor(const PDFColor& color) const
{
    // Input values
    std::vector<double> inputColor(color.size(), 0.0);
    for (size_t i = 0, count = inputColor.size(); i < count; ++i)
    {
        inputColor[i] = color[i];
    }

    // Output values
    std::vector<double> outputColor;
    outputColor.resize(m_alternateColorSpace->getColorComponentCount(), 0.0);
    PDFFunction::FunctionResult result = m_tintTransform->apply(inputColor.data(), inputColor.data() + inputColor.size(), outputColor.data(), outputColor.data() + outputColor.size());

    if (result)
    {
        PDFColor color;
        std::for_each(outputColor.cbegin(), outputColor.cend(), [&color](double value) { color.push_back(static_cast<float>(value)); });
        return m_alternateColorSpace->getColor(color);
    }
    else
    {
        // Return invalid color
        return QColor();
    }
}

size_t PDFDeviceNColorSpace::getColorComponentCount() const
{
    return m_colorants.size();
}

PDFColorSpacePointer PDFDeviceNColorSpace::createDeviceNColorSpace(const PDFDictionary* colorSpaceDictionary,
                                                                   const PDFDocument* document,
                                                                   const PDFArray* array,
                                                                   int recursion)
{
    Q_ASSERT(array->getCount() >= 4);

    PDFDocumentDataLoaderDecorator loader(document);
    std::vector<QByteArray> colorantNames = loader.readNameArray(array->getItem(1));

    if (colorantNames.empty())
    {
        throw PDFParserException(PDFTranslationContext::tr("Invalid colorants for DeviceN color space."));
    }

    std::vector<ColorantInfo> colorants;
    colorants.resize(colorantNames.size());
    for (size_t i = 0; i < colorantNames.size(); ++i)
    {
        colorants[i].name = qMove(colorantNames[i]);
    }

    // Read alternate color space
    PDFColorSpacePointer alternateColorSpace = PDFAbstractColorSpace::createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(array->getItem(2)), recursion);
    if (!alternateColorSpace)
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't determine alternate color space for DeviceN color space."));
    }

    PDFFunctionPtr tintTransform = PDFFunction::createFunction(document, array->getItem(3));
    if (!tintTransform)
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't determine tint transform for DeviceN color space."));
    }

    Type type = Type::DeviceN;
    std::vector<QByteArray> colorantsPrintingOrder;
    PDFColorSpacePointer processColorSpace;
    std::vector<QByteArray> processColorSpaceComponents;

    // Now, check, if we have attributes, and if yes, then read them
    if (array->getCount() == 5)
    {
        const PDFObject& object = document->getObject(array->getItem(4));
        if (object.isDictionary())
        {
            const PDFDictionary* attributesDictionary = object.getDictionary();
            QByteArray subtype = loader.readNameFromDictionary(attributesDictionary, "Subtype");
            if (subtype == "NChannel")
            {
                type = Type::NChannel;
            }

            const PDFObject& colorantsObject = document->getObject(attributesDictionary->get("Colorants"));
            if (colorantsObject.isDictionary())
            {
                const PDFDictionary* colorantsDictionary = colorantsObject.getDictionary();

                // Separation color spaces for each colorant
                for (ColorantInfo& colorantInfo : colorants)
                {
                    if (colorantsDictionary->hasKey(colorantInfo.name))
                    {
                        colorantInfo.separationColorSpace = PDFAbstractColorSpace::createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(colorantsDictionary->get(colorantInfo.name)), recursion);
                    }
                }
            }

            const PDFObject& mixingHints = document->getObject(attributesDictionary->get("MixingHints"));
            if (mixingHints.isDictionary())
            {
                const PDFDictionary* mixingHintsDictionary = mixingHints.getDictionary();

                // Printing order
                colorantsPrintingOrder = loader.readNameArray(mixingHintsDictionary->get("PrintingOrder"));

                // Solidities
                const PDFObject& solidityObject = document->getObject(mixingHintsDictionary->get("Solidites"));
                if (solidityObject.isDictionary())
                {
                    const PDFDictionary* solidityDictionary = solidityObject.getDictionary();
                    const PDFReal defaultSolidity = loader.readNumberFromDictionary(solidityDictionary, "Default", 0.0);
                    for (ColorantInfo& colorantInfo : colorants)
                    {
                        colorantInfo.solidity = loader.readNumberFromDictionary(solidityDictionary, colorantInfo.name, defaultSolidity);
                    }
                }

                // Dot gain
                const PDFObject& dotGainObject = document->getObject(mixingHintsDictionary->get("DotGain"));
                if (dotGainObject.isDictionary())
                {
                    const PDFDictionary* dotGainDictionary = dotGainObject.getDictionary();
                    for (ColorantInfo& colorantInfo : colorants)
                    {
                        const PDFObject& dotGainFunctionObject = document->getObject(dotGainDictionary->get(colorantInfo.name));
                        if (!dotGainFunctionObject.isNull())
                        {
                            colorantInfo.dotGain = PDFFunction::createFunction(document, dotGainFunctionObject);
                        }
                    }
                }
            }

            // Process
            const PDFObject& processObject = document->getObject(attributesDictionary->get("Process"));
            if (processObject.isDictionary())
            {
                const PDFDictionary* processDictionary = processObject.getDictionary();
                const PDFObject& processColorSpaceObject = document->getObject(processDictionary->get("ColorSpace"));
                if (!processColorSpaceObject.isNull())
                {
                    processColorSpace = PDFAbstractColorSpace::createColorSpaceImpl(colorSpaceDictionary, document, processColorSpaceObject, recursion);
                    processColorSpaceComponents = loader.readNameArrayFromDictionary(processDictionary, "Components");
                }
            }
        }
    }

    return PDFColorSpacePointer(new PDFDeviceNColorSpace(type, qMove(colorants), qMove(alternateColorSpace), qMove(processColorSpace), qMove(tintTransform), qMove(colorantsPrintingOrder), qMove(processColorSpaceComponents)));
}

}   // namespace pdf
