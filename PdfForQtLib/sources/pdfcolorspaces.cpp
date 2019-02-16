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
#include "pdfparser.h"

namespace pdf
{

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

QColor PDFDeviceRGBColorSpace::getColor(const PDFColor& color) const
{
    Q_ASSERT(color.size() == getColorComponentCount());

    return fromRGB01({ color[0], color[1], color[2] });
}

size_t PDFDeviceRGBColorSpace::getColorComponentCount() const
{
    return 3;
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

PDFColorSpacePointer PDFAbstractColorSpace::createColorSpace(const PDFDictionary* colorSpaceDictionary,
                                                                              const PDFDocument* document,
                                                                              const PDFObject& colorSpace)
{
    return createColorSpaceImpl(colorSpaceDictionary, document, colorSpace, COLOR_SPACE_MAX_LEVEL_OF_RECURSION);
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
        QByteArray name = colorSpace.getString();

        if (name == COLOR_SPACE_NAME_DEVICE_GRAY || name == COLOR_SPACE_NAME_ABBREVIATION_DEVICE_GRAY)
        {
            if (colorSpaceDictionary->hasKey(COLOR_SPACE_NAME_DEFAULT_GRAY))
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
            if (colorSpaceDictionary->hasKey(COLOR_SPACE_NAME_DEFAULT_RGB))
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
            if (colorSpaceDictionary->hasKey(COLOR_SPACE_NAME_DEFAULT_CMYK))
            {
                return createColorSpaceImpl(colorSpaceDictionary, document, document->getObject(colorSpaceDictionary->get(COLOR_SPACE_NAME_DEFAULT_CMYK)), recursion);
            }
            else
            {
                return PDFColorSpacePointer(new PDFDeviceCMYKColorSpace());
            }
        }
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

                // Try to just load by standard way - we can have "standard" color space stored in array
                return createColorSpaceImpl(colorSpaceDictionary, document, colorSpaceIdentifier, recursion);
            }
        }
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

PDFCalGrayColorSpace::PDFCalGrayColorSpace(PDFColor3 whitePoint, PDFColor3 blackPoint, PDFColorComponent gamma) :
    m_whitePoint(whitePoint),
    m_blackPoint(blackPoint),
    m_gamma(gamma),
    m_correctionCoefficients()
{
    PDFColor3 mappedWhitePoint = convertXYZtoRGB(m_whitePoint);

    m_correctionCoefficients[0] = 1.0f / mappedWhitePoint[0];
    m_correctionCoefficients[1] = 1.0f / mappedWhitePoint[1];
    m_correctionCoefficients[2] = 1.0f / mappedWhitePoint[2];
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
    m_whitePoint(whitePoint),
    m_blackPoint(blackPoint),
    m_gamma(gamma),
    m_matrix(matrix),
    m_correctionCoefficients()
{
    PDFColor3 mappedWhitePoint = convertXYZtoRGB(m_whitePoint);

    m_correctionCoefficients[0] = 1.0f / mappedWhitePoint[0];
    m_correctionCoefficients[1] = 1.0f / mappedWhitePoint[1];
    m_correctionCoefficients[2] = 1.0f / mappedWhitePoint[2];
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
    m_whitePoint(whitePoint),
    m_blackPoint(blackPoint),
    m_aMin(aMin),
    m_aMax(aMax),
    m_bMin(bMin),
    m_bMax(bMax),
    m_correctionCoefficients()
{
    PDFColor3 mappedWhitePoint = convertXYZtoRGB(m_whitePoint);

    m_correctionCoefficients[0] = 1.0f / mappedWhitePoint[0];
    m_correctionCoefficients[1] = 1.0f / mappedWhitePoint[1];
    m_correctionCoefficients[2] = 1.0f / mappedWhitePoint[2];
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

}   // namespace pdf
