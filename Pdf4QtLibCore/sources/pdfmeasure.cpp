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

#include "pdfmeasure.h"
#include "pdfdocument.h"

#include <QStringList>
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

        // The texts are cut (MAXIMUM_TEXT_LENGTH): every formatted value repeats them
        result.m_unitLabel = loader.readTextStringFromDictionary(dictionary, "U", QString()).left(MAXIMUM_TEXT_LENGTH);
        result.m_conversionFactor = loader.readNumberFromDictionary(dictionary, "C", 1.0);

        constexpr std::array<std::pair<const char*, FractionDisplay>, 4> fractionDisplays = {
            std::pair<const char*, FractionDisplay>{ "D", FractionDisplay::Decimal },
            std::pair<const char*, FractionDisplay>{ "F", FractionDisplay::Fraction },
            std::pair<const char*, FractionDisplay>{ "R", FractionDisplay::Round },
            std::pair<const char*, FractionDisplay>{ "T", FractionDisplay::Truncate }
        };
        result.m_fractionDisplay = loader.readEnumByName(dictionary->get("F"), fractionDisplays.begin(), fractionDisplays.end(), FractionDisplay::Decimal);

        // B208, Table 268: the default of D is 100 (hundredths) for the decimal display, 16 for the fraction
        const PDFInteger defaultDenominator = (result.m_fractionDisplay == FractionDisplay::Fraction) ? 16 : 100;
        result.m_denominator = loader.readIntegerFromDictionary(dictionary, "D", defaultDenominator);
        if (result.m_denominator < 1)
        {
            result.m_denominator = defaultDenominator;
        }

        result.m_fixedDenominator = loader.readBooleanFromDictionary(dictionary, "FD", false);

        if (dictionary->hasKey("RT"))
        {
            result.m_thousandSeparator = loader.readTextStringFromDictionary(dictionary, "RT", QString()).left(MAXIMUM_TEXT_LENGTH);
        }
        if (dictionary->hasKey("RD"))
        {
            result.m_decimalSeparator = loader.readTextStringFromDictionary(dictionary, "RD", QString()).left(MAXIMUM_TEXT_LENGTH);
        }
        if (dictionary->hasKey("PS"))
        {
            result.m_labelPrefix = loader.readTextStringFromDictionary(dictionary, "PS", QString()).left(MAXIMUM_TEXT_LENGTH);
        }
        if (dictionary->hasKey("SS"))
        {
            result.m_labelSuffix = loader.readTextStringFromDictionary(dictionary, "SS", QString()).left(MAXIMUM_TEXT_LENGTH);
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
        // At most MAXIMUM_ARRAY_LENGTH dictionaries (a hostile array of many references to one of them)
        const PDFArray* array = object.getArray();
        const size_t count = qMin(array->getCount(), MAXIMUM_ARRAY_LENGTH);
        result.reserve(count);
        for (size_t i = 0; i < count; ++i)
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

        result.m_scaleRatio = loader.readTextStringFromDictionary(dictionary, "R", QString()).left(MAXIMUM_RATIO_LENGTH);
        result.m_x = PDFNumberFormat::parseArray(storage, dictionary->get("X"));
        result.m_y = PDFNumberFormat::parseArray(storage, dictionary->get("Y"));
        result.m_distance = PDFNumberFormat::parseArray(storage, dictionary->get("D"));
        result.m_area = PDFNumberFormat::parseArray(storage, dictionary->get("A"));
        result.m_angle = PDFNumberFormat::parseArray(storage, dictionary->get("T"));
        result.m_slope = PDFNumberFormat::parseArray(storage, dictionary->get("S"));
        result.m_origin = loader.readNumberArrayFromDictionary(dictionary, "O");
        if (result.m_origin.size() > 2)
        {
            // Table 267: the origin is an array of two numbers; a longer array is kept by none of the measures
            result.m_origin.resize(2);
            result.m_origin.shrink_to_fit();
        }

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
        // The last MAXIMUM_VIEWPORT_COUNT entries: the last viewport containing a point is chosen (12.9.2)
        const PDFArray* array = object.getArray();
        PDFDocumentDataLoaderDecorator loader(storage);
        const size_t count = array->getCount();
        const size_t first = count > MAXIMUM_VIEWPORT_COUNT ? count - MAXIMUM_VIEWPORT_COUNT : 0;
        result.reserve(count - first);

        for (size_t i = first; i < count; ++i)
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

            viewport.m_name = loader.readTextStringFromDictionary(dictionary, "Name", QString()).left(MAXIMUM_NAME_LENGTH);
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

// ------------------------------------------------------- Units and scales (step 7c of phase 2, 4.8)
//
// The units and the scales of the old DimensionsPlugin (dimensionunits.cpp) moved to the core; the plugin
// calls these classes until it is deleted in phase 3.

QString PDFMeasureUnit::getSymbol() const
{
    return symbol ? QCoreApplication::translate("pdf::PDFMeasureUnits", symbol) : QString();
}

QString PDFMeasureUnit::getDocumentSymbol() const
{
    return symbol ? QString::fromUtf8(symbol) : QString();
}

QString PDFMeasureUnit::getName() const
{
    return name ? QCoreApplication::translate("pdf::PDFMeasureUnits", name) : QString();
}

std::vector<PDFMeasureUnit> PDFMeasureUnits::getUnits(PDFMeasureQuantity quantity)
{
    // The tables of the class (derived from the inch and the millimeter exactly; the ids are those of the old
    // plugin, its stored values stay readable)
    std::vector<PDFMeasureUnit> units;
    auto addUnits = [&units](const auto& definitions)
    {
        for (const PDFMeasureUnitDefinition& definition : definitions)
        {
            units.push_back({ QByteArray(definition.id), definition.factor, definition.symbol, definition.name });
        }
    };

    switch (quantity)
    {
        case PDFMeasureQuantity::Length:
            addUnits(LENGTH_UNITS);
            break;
        case PDFMeasureQuantity::Area:
            addUnits(AREA_UNITS);
            break;
        case PDFMeasureQuantity::Angle:
            addUnits(ANGLE_UNITS);
            break;
    }
    return units;
}

PDFMeasureUnit PDFMeasureUnits::findUnit(PDFMeasureQuantity quantity, const QByteArray& id)
{
    for (const PDFMeasureUnit& unit : getUnits(quantity))
    {
        if (unit.id == id)
        {
            return unit;
        }
    }
    return PDFMeasureUnit();
}

PDFMeasureUnit PDFMeasureUnits::getDefaultUnit(PDFMeasureQuantity quantity)
{
    const std::vector<PDFMeasureUnit> units = getUnits(quantity);
    return units.empty() ? PDFMeasureUnit() : units.front();
}

PDFMeasureUnit PDFMeasureUnits::getInitialUnit(PDFMeasureQuantity quantity)
{
    switch (quantity)
    {
        case PDFMeasureQuantity::Length:
            return findUnit(quantity, QByteArray(INITIAL_LENGTH_UNIT));
        case PDFMeasureQuantity::Area:
            return findUnit(quantity, QByteArray(INITIAL_AREA_UNIT));
        case PDFMeasureQuantity::Angle:
            return findUnit(quantity, QByteArray(INITIAL_ANGLE_UNIT));
    }
    return PDFMeasureUnit();
}

PDFMeasureUnit PDFMeasureUnits::findUnitOrDefault(PDFMeasureQuantity quantity, const QByteArray& id)
{
    const PDFMeasureUnit unit = findUnit(quantity, id);
    return unit.isValid() ? unit : getDefaultUnit(quantity);
}

PDFMeasureUnit PDFMeasureUnits::findLengthUnitByLabel(const QString& label)
{
    const QString normalizedLabel = label.trimmed().toLower();
    if (normalizedLabel.isEmpty())
    {
        return PDFMeasureUnit();
    }

    // The labels of the number formats are not standardized (Table 268 only recommends "a universally
    // recognised abbreviation"): the abbreviations, the names in the American and the British spelling
    // and the marks of the foot and the inch (the aliases of the old plugin)
    static const std::vector<std::pair<const char*, QStringList>> aliases = {
        { "pt", { "pt", "pts", "point", "points" } },
        { "in", { "in", "ins", "inch", "inches", "\"" } },
        { "mm", { "mm", "millimeter", "millimeters", "millimetre", "millimetres" } },
        { "cm", { "cm", "centimeter", "centimeters", "centimetre", "centimetres" } },
        { "m", { "m", "meter", "meters", "metre", "metres" } },
        { "km", { "km", "kilometer", "kilometers", "kilometre", "kilometres" } },
        { "ft", { "ft", "foot", "feet", "'" } },
        { "yd", { "yd", "yds", "yard", "yards" } },
        { "mi", { "mi", "mile", "miles" } }
    };

    for (const auto& [id, labels] : aliases)
    {
        if (labels.contains(normalizedLabel))
        {
            return findUnit(PDFMeasureQuantity::Length, QByteArray(id));
        }
    }

    return PDFMeasureUnit();
}

QString PDFMeasureUnits::formatNumber(PDFReal value, int decimals)
{
    if (!std::isfinite(value))
    {
        return QString();
    }

    QString text = QString::number(value, 'f', qBound(0, decimals, 10));
    if (text.contains(QLatin1Char('.')))
    {
        while (text.endsWith(QLatin1Char('0')))
        {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.')))
        {
            text.chop(1);
        }
    }
    if (text == QLatin1String("-0"))
    {
        text = QStringLiteral("0");
    }
    return text;
}

PDFMeasureScale::PDFMeasureScale(PDFReal paperValue, QByteArray paperUnitId, PDFReal realValue, QByteArray realUnitId, QString name) :
    m_paperValue(paperValue),
    m_paperUnitId(std::move(paperUnitId)),
    m_realValue(realValue),
    m_realUnitId(std::move(realUnitId)),
    m_name(std::move(name))
{

}

bool PDFMeasureScale::isValid() const
{
    if (!std::isfinite(m_paperValue) || !std::isfinite(m_realValue) || m_paperValue <= 0.0 || m_realValue <= 0.0)
    {
        return false;
    }

    // Both units must really be known - findUnitOrDefault would hide an unknown one
    const PDFMeasureUnit paperUnit = PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, m_paperUnitId);
    const PDFMeasureUnit realUnit = PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, m_realUnitId);
    if (!paperUnit.isValid() || !realUnit.isValid())
    {
        return false;
    }

    // Both sides in points: the factor must be a usable positive number (1e-300 mm = 1e300 km is not)
    const PDFReal factor = (m_realValue / realUnit.factor) / (m_paperValue / paperUnit.factor);
    return std::isfinite(factor) && factor > 0.0;
}

PDFReal PDFMeasureScale::getScaleFactor() const
{
    if (!isValid())
    {
        return 0.0;
    }

    // Both sides of the equality are converted to points, so the factor has no unit
    const PDFReal paperLength = m_paperValue / PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, m_paperUnitId).factor;
    const PDFReal realLength = m_realValue / PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, m_realUnitId).factor;
    return realLength / paperLength;
}

bool PDFMeasureScale::hasSameProportion(const PDFMeasureScale& other) const
{
    const PDFReal factor = getScaleFactor();
    const PDFReal otherFactor = other.getScaleFactor();
    if (factor <= 0.0 || otherFactor <= 0.0)
    {
        return false;
    }
    return qAbs(factor - otherFactor) <= 1e-9 * qMax(factor, otherFactor);
}

std::pair<PDFReal, PDFReal> PDFMeasureScale::getProportion() const
{
    const PDFReal factor = getScaleFactor();
    if (factor <= 0.0)
    {
        return { 0.0, 0.0 };
    }

    // A reduction is 1:n (a plan), an enlargement n:1 (a detail)
    return factor >= 1.0 ? std::make_pair(1.0, factor) : std::make_pair(1.0 / factor, 1.0);
}

QString PDFMeasureScale::getProportionText() const
{
    const std::pair<PDFReal, PDFReal> proportion = getProportion();
    if (proportion.first <= 0.0)
    {
        return QString();
    }
    return QStringLiteral("%1:%2").arg(PDFMeasureUnits::formatNumber(proportion.first, PROPORTION_DECIMALS),
                                       PDFMeasureUnits::formatNumber(proportion.second, PROPORTION_DECIMALS));
}

QString PDFMeasureScale::getRatioText() const
{
    if (!isValid())
    {
        return QString();
    }

    return tr("%1 %2 = %3 %4", "a scale: 1 mm = 50 mm").arg(PDFMeasureUnits::formatNumber(m_paperValue, VALUE_DECIMALS),
                                                            PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, m_paperUnitId).getSymbol(),
                                                            PDFMeasureUnits::formatNumber(m_realValue, VALUE_DECIMALS),
                                                            PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, m_realUnitId).getSymbol());
}

QString PDFMeasureScale::getDocumentRatioText() const
{
    if (!isValid())
    {
        return QString();
    }

    // B183: /R is read by other readers - the C locale and the untranslated abbreviations (Table 267:
    // "Universally recognised unit abbreviations should be used"), "1 mm = 50 mm", "0.25 in = 1 ft"
    return QStringLiteral("%1 %2 = %3 %4").arg(PDFMeasureUnits::formatNumber(m_paperValue, VALUE_DECIMALS),
                                               PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, m_paperUnitId).getDocumentSymbol(),
                                               PDFMeasureUnits::formatNumber(m_realValue, VALUE_DECIMALS),
                                               PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, m_realUnitId).getDocumentSymbol());
}

QString PDFMeasureScale::getDisplayName() const
{
    return m_name.isEmpty() ? getRatioText() : m_name;
}

PDFMeasureScale PDFMeasureScale::createIdentity()
{
    return PDFMeasureScale(1.0, "mm", 1.0, "mm");
}

PDFMeasureScale PDFMeasureScale::createFromMeasure(const PDFMeasure& measure, PDFReal userUnit)
{
    // ISO 32000-2, 12.9.2 (Table 266, 267): only a rectilinear measure has one factor of the lengths; a
    // geospatial one is ignored (FM-7, NOTES.txt), an anisotropic one has no single factor
    if (!measure.isValid() || !measure.isRectilinear() || measure.isAnisotropic())
    {
        return PDFMeasureScale();
    }

    const PDFReal unitsPerUserSpaceUnit = measure.getUnitsPerUserSpaceUnit();
    const PDFMeasureUnit unit = PDFMeasureUnits::findLengthUnitByLabel(measure.getUnitLabel());
    if (!std::isfinite(unitsPerUserSpaceUnit) || unitsPerUserSpaceUnit <= 0.0 || !std::isfinite(userUnit) || userUnit <= 0.0 || !unit.isValid())
    {
        return PDFMeasureScale();
    }

    // /X converts the units of the default user space of the page, which are /UserUnit points (8.3.2.3, B209);
    // the measured lengths of the tool are in points, so the factor of one point is X / UserUnit. The paper
    // side is a millimeter for the metric units, an inch for the imperial ones, a point for points.
    const PDFReal realUnitsPerPoint = unitsPerUserSpaceUnit / userUnit;
    QByteArray paperUnitId = "mm";
    if (unit.id == "in" || unit.id == "ft" || unit.id == "yd" || unit.id == "mi")
    {
        paperUnitId = "in";
    }
    else if (unit.id == "pt")
    {
        paperUnitId = "pt";
    }
    const PDFMeasureUnit paperUnit = PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, paperUnitId);
    PDFMeasureScale scale(1.0, paperUnitId, realUnitsPerPoint / paperUnit.factor, unit.id, measure.getScaleRatio().left(PDFMeasureScale::MAXIMUM_DOCUMENT_NAME_LENGTH));
    return scale.isValid() ? scale : PDFMeasureScale();
}

std::vector<PDFMeasureScale> PDFMeasureScale::getDefaultPresets()
{
    // The 12 presets of 471:6943 (the presets of the old plugin)
    std::vector<PDFMeasureScale> presets;
    presets.emplace_back(1.0, "mm", 1.0, "mm", tr("Real size (1:1)"));
    presets.back().setDescription(tr("The drawing is in the real size"));
    presets.emplace_back(1.0, "mm", 20.0, "mm", tr("Metric 1:20"));
    presets.emplace_back(1.0, "mm", 50.0, "mm", tr("Metric 1:50"));
    presets.emplace_back(1.0, "mm", 100.0, "mm", tr("Metric 1:100"));
    presets.emplace_back(1.0, "mm", 200.0, "mm", tr("Metric 1:200"));
    presets.emplace_back(1.0, "mm", 500.0, "mm", tr("Metric 1:500"));
    presets.emplace_back(1.0, "mm", 1.0, "m", tr("Metric 1:1000"));
    presets.emplace_back(1.0, "cm", 100.0, "m", tr("Metric 1:10000"));
    presets.emplace_back(0.125, "in", 1.0, "ft", tr("Architectural 1/8\" = 1'"));
    presets.emplace_back(0.25, "in", 1.0, "ft", tr("Architectural 1/4\" = 1'"));
    presets.emplace_back(1.0, "in", 10.0, "ft", tr("Engineering 1\" = 10'"));
    presets.emplace_back(1.0, "in", 100.0, "ft", tr("Engineering 1\" = 100'"));
    return presets;
}

bool PDFMeasureScale::operator==(const PDFMeasureScale& other) const
{
    return m_paperValue == other.m_paperValue && m_paperUnitId == other.m_paperUnitId && m_realValue == other.m_realValue &&
           m_realUnitId == other.m_realUnitId && m_name == other.m_name;
}

}   // namespace pdf
