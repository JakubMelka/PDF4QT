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

#include "dimensionunits.h"

#include "pdfmeasure.h"

#include <QLocale>
#include <QtMath>

#include <algorithm>
#include <utility>

namespace
{

// Conversion factors from a single point of the default user space
// to the given unit. Feet, yards and miles are derived from inches,
// so they are exact.
constexpr pdf::PDFReal POINT_TO_FOOT = pdf::PDF_POINT_TO_INCH / 12.0;
constexpr pdf::PDFReal POINT_TO_YARD = pdf::PDF_POINT_TO_INCH / 36.0;
constexpr pdf::PDFReal POINT_TO_MILE = pdf::PDF_POINT_TO_INCH / 63360.0;
constexpr pdf::PDFReal POINT_TO_CENTIMETER = pdf::PDF_POINT_TO_MM * 0.1;
constexpr pdf::PDFReal POINT_TO_METER = pdf::PDF_POINT_TO_MM * 0.001;
constexpr pdf::PDFReal POINT_TO_KILOMETER = pdf::PDF_POINT_TO_MM * 0.000001;

constexpr pdf::PDFReal square(pdf::PDFReal value)
{
    return value * value;
}

/// Formats the number so that it is readable - the trailing zeros
/// of the fractional part are removed
QString formatScaleNumber(pdf::PDFReal value)
{
    QLocale locale;
    QString text = locale.toString(value, 'f', 4);
    const QString decimalPoint = locale.decimalPoint();

    if (text.contains(decimalPoint))
    {
        while (text.endsWith(QChar('0')))
        {
            text.chop(1);
        }
        if (text.endsWith(decimalPoint))
        {
            text.chop(decimalPoint.size());
        }
    }

    return text;
}

/// Translates the symbol of a unit. The symbols have always been translated
/// in the context of DimensionTool, so the context is kept here, even though
/// the units themselves live in another class now.
QString translateUnit(const char* symbol)
{
    return QCoreApplication::translate("DimensionTool", symbol);
}

const DimensionUnit* findUnit(const DimensionUnits& units, const QByteArray& id)
{
    auto it = std::find_if(units.cbegin(), units.cend(), [&id](const DimensionUnit& unit) { return unit.id == id; });
    return it != units.cend() ? &*it : nullptr;
}

}   // namespace

DimensionUnits DimensionUnit::getLengthUnits()
{
    DimensionUnits units;

    units.emplace_back("pt", 1.0, translateUnit("pt"));
    units.emplace_back("in", pdf::PDF_POINT_TO_INCH, translateUnit("in"));
    units.emplace_back("mm", pdf::PDF_POINT_TO_MM, translateUnit("mm"));
    units.emplace_back("cm", POINT_TO_CENTIMETER, translateUnit("cm"));
    units.emplace_back("m", POINT_TO_METER, translateUnit("m"));
    units.emplace_back("ft", POINT_TO_FOOT, translateUnit("ft"));
    units.emplace_back("yd", POINT_TO_YARD, translateUnit("yd"));
    units.emplace_back("km", POINT_TO_KILOMETER, translateUnit("km"));
    units.emplace_back("mi", POINT_TO_MILE, translateUnit("mi"));

    return units;
}

DimensionUnits DimensionUnit::getAreaUnits()
{
    DimensionUnits units;

    units.emplace_back("sqpt", 1.0, translateUnit("sq. pt"));
    units.emplace_back("sqin", square(pdf::PDF_POINT_TO_INCH), translateUnit("sq. in"));
    units.emplace_back("sqmm", square(pdf::PDF_POINT_TO_MM), translateUnit("sq. mm"));
    units.emplace_back("sqcm", square(POINT_TO_CENTIMETER), translateUnit("sq. cm"));
    units.emplace_back("sqm", square(POINT_TO_METER), translateUnit("sq. m"));
    units.emplace_back("sqft", square(POINT_TO_FOOT), translateUnit("sq. ft"));
    units.emplace_back("sqyd", square(POINT_TO_YARD), translateUnit("sq. yd"));
    units.emplace_back("ha", square(POINT_TO_METER) / 10000.0, translateUnit("ha"));
    units.emplace_back("acre", square(POINT_TO_FOOT) / 43560.0, translateUnit("acre"));
    units.emplace_back("sqkm", square(POINT_TO_KILOMETER), translateUnit("sq. km"));
    units.emplace_back("sqmi", square(POINT_TO_MILE), translateUnit("sq. mi"));

    return units;
}

DimensionUnits DimensionUnit::getAngleUnits()
{
    DimensionUnits units;

    units.emplace_back("deg", 1.0, translateUnit("°"));
    units.emplace_back("rad", qDegreesToRadians(1.0), translateUnit("rad"));
    units.emplace_back("gon", 1.0 / 0.9, translateUnit("gon"));

    return units;
}

DimensionUnit DimensionUnit::getLengthUnit(const QByteArray& id)
{
    DimensionUnits units = getLengthUnits();

    if (const DimensionUnit* unit = findUnit(units, id))
    {
        return *unit;
    }

    return units.front();
}

DimensionUnit DimensionUnit::getAreaUnit(const QByteArray& id)
{
    DimensionUnits units = getAreaUnits();

    if (const DimensionUnit* unit = findUnit(units, id))
    {
        return *unit;
    }

    return units.front();
}

DimensionUnit DimensionUnit::getAngleUnit(const QByteArray& id)
{
    DimensionUnits units = getAngleUnits();

    if (const DimensionUnit* unit = findUnit(units, id))
    {
        return *unit;
    }

    return units.front();
}

DimensionUnit DimensionUnit::findLengthUnitByLabel(const QString& label)
{
    const QString normalizedLabel = label.trimmed().toLower();

    if (normalizedLabel.isEmpty())
    {
        return DimensionUnit();
    }

    // Labels used by the measure dictionaries are not standardized, so the most
    // common spellings of the units are recognized here
    static const std::vector<std::pair<QByteArray, QStringList>> aliases = {
        { QByteArray("pt"), { "pt", "pts", "point", "points" } },
        { QByteArray("in"), { "in", "ins", "inch", "inches", "\"" } },
        { QByteArray("mm"), { "mm", "millimeter", "millimeters", "millimetre", "millimetres" } },
        { QByteArray("cm"), { "cm", "centimeter", "centimeters", "centimetre", "centimetres" } },
        { QByteArray("m"), { "m", "meter", "meters", "metre", "metres" } },
        { QByteArray("km"), { "km", "kilometer", "kilometers", "kilometre", "kilometres" } },
        { QByteArray("ft"), { "ft", "foot", "feet", "'" } },
        { QByteArray("yd"), { "yd", "yds", "yard", "yards" } },
        { QByteArray("mi"), { "mi", "mile", "miles" } }
    };

    for (const auto& alias : aliases)
    {
        if (alias.second.contains(normalizedLabel))
        {
            return getLengthUnit(alias.first);
        }
    }

    return DimensionUnit();
}

DimensionScale::DimensionScale(pdf::PDFReal paperValue,
                               QByteArray paperUnitId,
                               pdf::PDFReal realValue,
                               QByteArray realUnitId,
                               QString name,
                               QString description) :
    m_paperValue(paperValue),
    m_paperUnitId(qMove(paperUnitId)),
    m_realValue(realValue),
    m_realUnitId(qMove(realUnitId)),
    m_name(qMove(name)),
    m_description(qMove(description))
{

}

bool DimensionScale::isValid() const
{
    if (m_paperValue <= 0.0 || m_realValue <= 0.0)
    {
        return false;
    }

    // Both units must be really known - getLengthUnit falls back to the default
    // unit for unknown identifiers, so it cannot be used for the check
    const DimensionUnits units = DimensionUnit::getLengthUnits();
    return findUnit(units, m_paperUnitId) && findUnit(units, m_realUnitId);
}

pdf::PDFReal DimensionScale::getScaleFactor() const
{
    if (!isValid())
    {
        return 1.0;
    }

    // Both sides of the equation are converted to the default user space units,
    // so the resulting factor is dimensionless
    const pdf::PDFReal paperLength = m_paperValue / getPaperUnit().scale;
    const pdf::PDFReal realLength = m_realValue / getRealUnit().scale;

    if (qFuzzyIsNull(paperLength))
    {
        return 1.0;
    }

    return realLength / paperLength;
}

QString DimensionScale::getRatioText() const
{
    return tr("%1 %2 = %3 %4").arg(formatScaleNumber(m_paperValue),
                                   getPaperUnit().symbol,
                                   formatScaleNumber(m_realValue),
                                   getRealUnit().symbol);
}

QString DimensionScale::getDisplayName() const
{
    if (!m_name.isEmpty())
    {
        return m_name;
    }

    return getRatioText();
}

QStringList DimensionScale::toStringList() const
{
    return QStringList() << QString::number(m_paperValue, 'g', 12)
                         << QString::fromLatin1(m_paperUnitId)
                         << QString::number(m_realValue, 'g', 12)
                         << QString::fromLatin1(m_realUnitId)
                         << m_name
                         << m_description;
}

DimensionScale DimensionScale::fromStringList(const QStringList& data)
{
    if (data.size() < 4)
    {
        return DimensionScale();
    }

    bool isPaperValueOk = false;
    bool isRealValueOk = false;

    const pdf::PDFReal paperValue = data[0].toDouble(&isPaperValueOk);
    const pdf::PDFReal realValue = data[2].toDouble(&isRealValueOk);

    if (!isPaperValueOk || !isRealValueOk)
    {
        return DimensionScale();
    }

    DimensionScale scale(paperValue,
                         data[1].toLatin1(),
                         realValue,
                         data[3].toLatin1(),
                         data.size() > 4 ? data[4] : QString(),
                         data.size() > 5 ? data[5] : QString());

    if (!scale.isValid())
    {
        return DimensionScale();
    }

    return scale;
}

DimensionScale DimensionScale::createIdentity()
{
    return DimensionScale(1.0, "mm", 1.0, "mm");
}

DimensionScale DimensionScale::createFromMeasure(const pdf::PDFMeasure& measure, pdf::PDFReal userUnit)
{
    if (!measure.isValid() || !measure.isRectilinear())
    {
        return DimensionScale();
    }

    if (measure.isAnisotropic())
    {
        // The x axis and the y axis use a different conversion, such a coordinate
        // system cannot be expressed by a single scale factor
        return DimensionScale();
    }

    const pdf::PDFReal unitsPerPoint = measure.getUnitsPerUserSpaceUnit();
    const DimensionUnit unit = DimensionUnit::findLengthUnitByLabel(measure.getUnitLabel());

    if (unitsPerPoint <= 0.0 || userUnit <= 0.0 || !unit.isValid())
    {
        // Either the measure does not define the conversion at all, or it uses
        // a unit we are not able to interpret
        return DimensionScale();
    }

    // Measured lengths are multiplied by the UserUnit of the page before the scale
    // is applied, while the measure dictionary converts the default user space
    // units. The UserUnit factor is therefore removed here.
    return DimensionScale(1.0, "pt", unitsPerPoint / userUnit, unit.id, measure.getScaleRatio());
}

std::vector<DimensionScale> DimensionScale::getDefaultPresets()
{
    std::vector<DimensionScale> presets;

    presets.emplace_back(1.0, "mm", 1.0, "mm", tr("Real size (1:1)"), tr("The drawing is in the real size"));
    presets.emplace_back(1.0, "mm", 20.0, "mm", tr("Metric 1:20"), QString());
    presets.emplace_back(1.0, "mm", 50.0, "mm", tr("Metric 1:50"), QString());
    presets.emplace_back(1.0, "mm", 100.0, "mm", tr("Metric 1:100"), QString());
    presets.emplace_back(1.0, "mm", 200.0, "mm", tr("Metric 1:200"), QString());
    presets.emplace_back(1.0, "mm", 500.0, "mm", tr("Metric 1:500"), QString());
    presets.emplace_back(1.0, "mm", 1.0, "m", tr("Metric 1:1000"), QString());
    presets.emplace_back(1.0, "cm", 100.0, "m", tr("Metric 1:10000"), QString());
    presets.emplace_back(0.125, "in", 1.0, "ft", tr("Architectural 1/8\" = 1'"), QString());
    presets.emplace_back(0.25, "in", 1.0, "ft", tr("Architectural 1/4\" = 1'"), QString());
    presets.emplace_back(1.0, "in", 10.0, "ft", tr("Engineering 1\" = 10'"), QString());
    presets.emplace_back(1.0, "in", 100.0, "ft", tr("Engineering 1\" = 100'"), QString());

    return presets;
}

bool DimensionScale::operator==(const DimensionScale& other) const
{
    return qFuzzyCompare(m_paperValue, other.m_paperValue) &&
           m_paperUnitId == other.m_paperUnitId &&
           qFuzzyCompare(m_realValue, other.m_realValue) &&
           m_realUnitId == other.m_realUnitId &&
           m_name == other.m_name &&
           m_description == other.m_description;
}
