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

#include "pdfmeasure.h"
#include "pdfdocument.h"

#include <QtMath>

#include <array>
#include <cmath>
#include <numeric>

namespace pdf
{

PDFNumberFormat PDFNumberFormat::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFNumberFormat result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        result.m_unitLabel = loader.readTextStringFromDictionary(dictionary, "U", QString());
        result.m_conversionFactor = loader.readNumberFromDictionary(dictionary, "C", 1.0);

        constexpr std::array<std::pair<const char*, FractionDisplay>, 4> fractionDisplays = {
            std::pair<const char*, FractionDisplay>{ "D", FractionDisplay::Decimal },
            std::pair<const char*, FractionDisplay>{ "F", FractionDisplay::Fraction },
            std::pair<const char*, FractionDisplay>{ "R", FractionDisplay::Round },
            std::pair<const char*, FractionDisplay>{ "T", FractionDisplay::Truncate }
        };
        result.m_fractionDisplay = loader.readEnumByName(dictionary->get("F"), fractionDisplays.begin(), fractionDisplays.end(), FractionDisplay::Decimal);

        result.m_denominator = loader.readIntegerFromDictionary(dictionary, "D", 100);
        if (result.m_denominator < 1)
        {
            result.m_denominator = 100;
        }

        result.m_fixedDenominator = loader.readBooleanFromDictionary(dictionary, "FD", false);

        if (dictionary->hasKey("RT"))
        {
            result.m_thousandSeparator = loader.readTextStringFromDictionary(dictionary, "RT", QString());
        }
        if (dictionary->hasKey("RD"))
        {
            result.m_decimalSeparator = loader.readTextStringFromDictionary(dictionary, "RD", QString());
        }
        if (dictionary->hasKey("PS"))
        {
            result.m_labelPrefix = loader.readTextStringFromDictionary(dictionary, "PS", QString());
        }
        if (dictionary->hasKey("SS"))
        {
            result.m_labelSuffix = loader.readTextStringFromDictionary(dictionary, "SS", QString());
        }

        constexpr std::array<std::pair<const char*, Order>, 2> orders = {
            std::pair<const char*, Order>{ "S", Order::Suffix },
            std::pair<const char*, Order>{ "P", Order::Prefix }
        };
        result.m_order = loader.readEnumByName(dictionary->get("O"), orders.begin(), orders.end(), Order::Suffix);
    }

    return result;
}

std::vector<PDFNumberFormat> PDFNumberFormat::parseArray(const PDFObjectStorage* storage, PDFObject object)
{
    std::vector<PDFNumberFormat> result;

    object = storage->getObject(object);

    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        result.reserve(array->getCount());
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            result.push_back(parse(storage, array->getItem(i)));
        }
    }
    else if (object.isDictionary())
    {
        // Jakub Melka: some producers write a single number format dictionary
        // instead of an array with one element. Be tolerant to this.
        result.push_back(parse(storage, object));
    }

    return result;
}

int PDFNumberFormat::getDecimalPlaces() const
{
    const int decimalPlaces = qRound(std::log10(double(m_denominator)));
    return qBound(0, decimalPlaces, 10);
}

QString PDFNumberFormat::formatNumber(PDFReal value, int decimalPlaces) const
{
    QString text = QString::number(value, 'f', decimalPlaces);

    QString sign;
    if (text.startsWith(QChar('-')))
    {
        sign = QString(QChar('-'));
        text = text.mid(1);
    }

    QString integerPart = text;
    QString fractionalPart;

    const qsizetype separatorPosition = text.indexOf(QChar('.'));
    if (separatorPosition != -1)
    {
        integerPart = text.left(separatorPosition);
        fractionalPart = text.mid(separatorPosition + 1);
    }

    if (!m_thousandSeparator.isEmpty())
    {
        for (qsizetype i = integerPart.size() - 3; i > 0; i -= 3)
        {
            integerPart.insert(i, m_thousandSeparator);
        }
    }

    if (!fractionalPart.isEmpty())
    {
        return sign + integerPart + m_decimalSeparator + fractionalPart;
    }

    return sign + integerPart;
}

QString PDFNumberFormat::appendLabel(const QString& numberText) const
{
    QString result;

    if (m_order == Order::Prefix)
    {
        result = m_labelPrefix + m_unitLabel + m_labelSuffix + numberText;
    }
    else
    {
        result = numberText + m_labelPrefix + m_unitLabel + m_labelSuffix;
    }

    return result.trimmed();
}

QString PDFNumberFormat::formatValue(PDFReal value, bool isLastElement) const
{
    if (!isLastElement)
    {
        // Only the whole part is displayed, the fractional part is passed
        // to the next element of the number format array
        return appendLabel(formatNumber(std::trunc(value), 0));
    }

    switch (m_fractionDisplay)
    {
        case FractionDisplay::Decimal:
            return appendLabel(formatNumber(value, getDecimalPlaces()));

        case FractionDisplay::Round:
            return appendLabel(formatNumber(std::round(value), 0));

        case FractionDisplay::Truncate:
            return appendLabel(formatNumber(std::trunc(value), 0));

        case FractionDisplay::Fraction:
        {
            const PDFReal absoluteValue = qAbs(value);
            const PDFReal wholePart = std::floor(absoluteValue);

            PDFInteger denominator = m_denominator;
            PDFInteger numerator = PDFInteger(std::llround((absoluteValue - wholePart) * PDFReal(denominator)));

            if (numerator >= denominator)
            {
                // Rounding of the fractional part reached the whole unit
                return appendLabel(formatNumber(value < 0.0 ? -(wholePart + 1.0) : (wholePart + 1.0), 0));
            }

            if (numerator > 0 && !m_fixedDenominator)
            {
                const PDFInteger divisor = std::gcd(numerator, denominator);
                numerator /= divisor;
                denominator /= divisor;
            }

            QString text = formatNumber(value < 0.0 ? -wholePart : wholePart, 0);

            if (numerator > 0)
            {
                if (wholePart > 0.0)
                {
                    text = QString("%1 %2/%3").arg(text).arg(numerator).arg(denominator);
                }
                else
                {
                    // Do not display a leading zero in front of the fraction
                    text = QString("%1%2/%3").arg(value < 0.0 ? QString(QChar('-')) : QString()).arg(numerator).arg(denominator);
                }
            }

            return appendLabel(text);
        }

        default:
            break;
    }

    return appendLabel(formatNumber(value, getDecimalPlaces()));
}

QString PDFNumberFormat::format(const std::vector<PDFNumberFormat>& formats, PDFReal value)
{
    if (formats.empty())
    {
        return QString();
    }

    QStringList parts;
    PDFReal currentValue = value;

    for (size_t i = 0; i < formats.size(); ++i)
    {
        const PDFNumberFormat& numberFormat = formats[i];
        currentValue = currentValue * numberFormat.getConversionFactor();

        const bool isLastElement = (i == formats.size() - 1);
        parts << numberFormat.formatValue(currentValue, isLastElement);

        if (!isLastElement)
        {
            // The fractional part is converted by the next element of the array
            currentValue = currentValue - std::trunc(currentValue);
        }
    }

    return parts.join(QChar(' '));
}

PDFMeasure PDFMeasure::parse(const PDFObjectStorage* storage, PDFObject object)
{
    PDFMeasure result;

    if (const PDFDictionary* dictionary = storage->getDictionaryFromObject(object))
    {
        PDFDocumentDataLoaderDecorator loader(storage);

        constexpr std::array<std::pair<const char*, Subtype>, 2> subtypes = {
            std::pair<const char*, Subtype>{ "RL", Subtype::RectilinearCoordinateSystem },
            std::pair<const char*, Subtype>{ "GEO", Subtype::GeospatialCoordinateSystem }
        };

        // Rectilinear coordinate system is the default subtype
        result.m_subtype = loader.readEnumByName(dictionary->get("Subtype"), subtypes.begin(), subtypes.end(), Subtype::RectilinearCoordinateSystem);

        result.m_scaleRatio = loader.readTextStringFromDictionary(dictionary, "R", QString());
        result.m_x = PDFNumberFormat::parseArray(storage, dictionary->get("X"));
        result.m_y = PDFNumberFormat::parseArray(storage, dictionary->get("Y"));
        result.m_distance = PDFNumberFormat::parseArray(storage, dictionary->get("D"));
        result.m_area = PDFNumberFormat::parseArray(storage, dictionary->get("A"));
        result.m_angle = PDFNumberFormat::parseArray(storage, dictionary->get("T"));
        result.m_slope = PDFNumberFormat::parseArray(storage, dictionary->get("S"));
        result.m_origin = loader.readNumberArrayFromDictionary(dictionary, "O");

        if (!result.m_y.empty())
        {
            // The CYX entry is meaningful only if the Y entry is present
            result.m_factorYX = loader.readNumberFromDictionary(dictionary, "CYX", 1.0);
        }
        else
        {
            // If the Y entry is missing, then the X entry applies to both axes
            result.m_y = result.m_x;
        }
    }

    return result;
}

PDFReal PDFMeasure::getUnitsPerUserSpaceUnit() const
{
    if (m_x.empty())
    {
        return 0.0;
    }

    return m_x.front().getConversionFactor();
}

QString PDFMeasure::getUnitLabel() const
{
    if (m_x.empty())
    {
        return QString();
    }

    return m_x.front().getUnitLabel();
}

PDFReal PDFMeasure::getUnitsPerUserSpaceUnitY() const
{
    if (m_y.empty())
    {
        return getUnitsPerUserSpaceUnit();
    }

    return m_y.front().getConversionFactor();
}

bool PDFMeasure::isAnisotropic() const
{
    return !qFuzzyCompare(getUnitsPerUserSpaceUnit(), getUnitsPerUserSpaceUnitY()) || !qFuzzyCompare(m_factorYX, 1.0);
}

QString PDFMeasure::formatDistance(PDFReal userSpaceLength) const
{
    if (m_distance.empty())
    {
        // Distance format is not defined, use the format of the x axis,
        // which converts directly from the default user space units
        return PDFNumberFormat::format(m_x, userSpaceLength);
    }

    return PDFNumberFormat::format(m_distance, userSpaceLength * getUnitsPerUserSpaceUnit());
}

QString PDFMeasure::formatDistance(const QPointF& userSpaceVector) const
{
    // Both components are converted by the factor of their own axis, the y
    // component is then expressed in the units of the x axis, as it is required
    // by the CYX entry
    const PDFReal x = userSpaceVector.x() * getUnitsPerUserSpaceUnit();
    const PDFReal y = userSpaceVector.y() * getUnitsPerUserSpaceUnitY() * m_factorYX;
    const PDFReal length = std::hypot(x, y);

    if (m_distance.empty())
    {
        // Distance format is not defined, the x axis format has to be used. It
        // converts from the default user space units, so the conversion, which
        // was already performed, must be reverted first.
        const PDFReal factor = getUnitsPerUserSpaceUnit();
        return PDFNumberFormat::format(m_x, qFuzzyIsNull(factor) ? length : length / factor);
    }

    return PDFNumberFormat::format(m_distance, length);
}

QString PDFMeasure::formatArea(PDFReal userSpaceArea) const
{
    // The area is a product of a distance along the x axis and a distance along
    // the y axis. The distance along the y axis is expressed in the units of
    // the x axis by the CYX factor, exactly as it is done in formatDistance,
    // so the resulting area is in the square units of the x axis.
    return PDFNumberFormat::format(m_area, userSpaceArea * getUnitsPerUserSpaceUnit() * getUnitsPerUserSpaceUnitY() * m_factorYX);
}

QString PDFMeasure::formatAngle(PDFReal degrees) const
{
    return PDFNumberFormat::format(m_angle, degrees);
}

std::vector<PDFViewport> PDFViewport::parseViewports(const PDFObjectStorage* storage, PDFObject object)
{
    std::vector<PDFViewport> result;

    object = storage->getObject(object);

    if (object.isArray())
    {
        const PDFArray* array = object.getArray();
        PDFDocumentDataLoaderDecorator loader(storage);
        result.reserve(array->getCount());

        for (size_t i = 0; i < array->getCount(); ++i)
        {
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(array->getItem(i));
            if (!dictionary)
            {
                continue;
            }

            PDFViewport viewport;
            viewport.m_boundingBox = loader.readRectangle(dictionary->get("BBox"), QRectF());

            if (!viewport.m_boundingBox.isValid())
            {
                // Bounding box is required, viewport without it is meaningless
                continue;
            }

            viewport.m_name = loader.readTextStringFromDictionary(dictionary, "Name", QString());
            viewport.m_measure = PDFMeasure::parse(storage, dictionary->get("Measure"));
            result.push_back(qMove(viewport));
        }
    }

    return result;
}

const PDFViewport* PDFViewport::findViewportForPoint(const std::vector<PDFViewport>& viewports, const QPointF& point)
{
    // If the viewports overlap, then the last one in the array takes precedence
    for (auto it = viewports.rbegin(); it != viewports.rend(); ++it)
    {
        if (it->getBoundingBox().contains(point))
        {
            return &*it;
        }
    }

    return nullptr;
}

}   // namespace pdf
