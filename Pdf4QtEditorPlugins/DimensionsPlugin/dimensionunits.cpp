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

#include "dimensionunits.h"

#include "pdfmeasure.h"

// The units, the scales and their computations live in the core (pdf::PDFMeasureUnits, pdf::PDFMeasureScale;
// plan of step 7 of phase 2, 4.8): the plugin converts its values to them until it is deleted in phase 3.

DimensionUnits DimensionUnit::getUnits(pdf::PDFMeasureQuantity quantity)
{
    DimensionUnits units;
    for (const pdf::PDFMeasureUnit& unit : pdf::PDFMeasureUnits::getUnits(quantity))
    {
        units.emplace_back(unit.id, unit.factor, unit.getSymbol());
    }
    return units;
}

DimensionUnit DimensionUnit::getUnit(pdf::PDFMeasureQuantity quantity, const QByteArray& id)
{
    // An unknown identifier gives the default unit (the first one)
    const pdf::PDFMeasureUnit unit = pdf::PDFMeasureUnits::findUnitOrDefault(quantity, id);
    return DimensionUnit(unit.id, unit.factor, unit.getSymbol());
}

DimensionUnits DimensionUnit::getLengthUnits()
{
    return getUnits(pdf::PDFMeasureQuantity::Length);
}

DimensionUnits DimensionUnit::getAreaUnits()
{
    return getUnits(pdf::PDFMeasureQuantity::Area);
}

DimensionUnits DimensionUnit::getAngleUnits()
{
    return getUnits(pdf::PDFMeasureQuantity::Angle);
}

DimensionUnit DimensionUnit::getLengthUnit(const QByteArray& id)
{
    return getUnit(pdf::PDFMeasureQuantity::Length, id);
}

DimensionUnit DimensionUnit::getAreaUnit(const QByteArray& id)
{
    return getUnit(pdf::PDFMeasureQuantity::Area, id);
}

DimensionUnit DimensionUnit::getAngleUnit(const QByteArray& id)
{
    return getUnit(pdf::PDFMeasureQuantity::Angle, id);
}

DimensionUnit DimensionUnit::findLengthUnitByLabel(const QString& label)
{
    const pdf::PDFMeasureUnit unit = pdf::PDFMeasureUnits::findLengthUnitByLabel(label);
    return unit.isValid() ? DimensionUnit(unit.id, unit.factor, unit.getSymbol()) : DimensionUnit();
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

pdf::PDFMeasureScale DimensionScale::toMeasureScale() const
{
    pdf::PDFMeasureScale scale(m_paperValue, m_paperUnitId, m_realValue, m_realUnitId, m_name);
    scale.setDescription(m_description);
    return scale;
}

DimensionScale DimensionScale::fromMeasureScale(const pdf::PDFMeasureScale& scale)
{
    if (!scale.isValid())
    {
        return DimensionScale();
    }
    return DimensionScale(scale.getPaperValue(), scale.getPaperUnitId(), scale.getRealValue(), scale.getRealUnitId(), scale.getName(), scale.getDescription());
}

bool DimensionScale::isValid() const
{
    return toMeasureScale().isValid();
}

pdf::PDFReal DimensionScale::getScaleFactor() const
{
    // The plugin draws an invalid scale as 1:1
    return isValid() ? toMeasureScale().getScaleFactor() : 1.0;
}

QString DimensionScale::getRatioText() const
{
    return toMeasureScale().getRatioText();
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
    return fromMeasureScale(pdf::PDFMeasureScale::createIdentity());
}

DimensionScale DimensionScale::createFromMeasure(const pdf::PDFMeasure& measure, pdf::PDFReal userUnit)
{
    // The measured lengths of the plugin are in points (× /UserUnit), the core removes the /UserUnit
    return fromMeasureScale(pdf::PDFMeasureScale::createFromMeasure(measure, userUnit));
}

std::vector<DimensionScale> DimensionScale::getDefaultPresets()
{
    std::vector<DimensionScale> presets;
    for (const pdf::PDFMeasureScale& preset : pdf::PDFMeasureScale::getDefaultPresets())
    {
        presets.push_back(fromMeasureScale(preset));
    }
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
