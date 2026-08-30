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

#ifndef PDFMEASURE_H
#define PDFMEASURE_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QRectF>
#include <QString>

#include <vector>

namespace pdf
{
class PDFObjectStorage;

/// Number format dictionary. It describes, how a numerical value is converted
/// to a text string presented to the user, including the unit label, the number
/// of displayed digits and the separators. See PDF 2.0 specification,
/// chapter 12.9.2, table 271.
class PDF4QTLIBCORESHARED_EXPORT PDFNumberFormat
{
public:
    explicit PDFNumberFormat() = default;

    /// Manner, in which is the fractional part of the value displayed
    enum class FractionDisplay
    {
        Decimal,    ///< Decimal fraction, precision is given by the denominator
        Fraction,   ///< Common fraction with the denominator \p getDenominator
        Round,      ///< Round to the nearest integer
        Truncate    ///< Truncate the fractional part
    };

    /// Position of the unit label relative to the value
    enum class Order
    {
        Suffix,     ///< Label follows the value
        Prefix      ///< Label precedes the value
    };

    /// Parses the number format dictionary. If the object is not a valid
    /// number format dictionary, then default constructed (identity) format
    /// is returned.
    /// \param storage Object storage
    /// \param object Number format dictionary
    static PDFNumberFormat parse(const PDFObjectStorage* storage, PDFObject object);

    /// Parses an array of number format dictionaries. Single dictionary is also
    /// accepted and is treated as an array with one element.
    /// \param storage Object storage
    /// \param object Array of number format dictionaries
    static std::vector<PDFNumberFormat> parseArray(const PDFObjectStorage* storage, PDFObject object);

    /// Converts the value to a text string using the given number format array.
    /// The value is expected to be in the units, from which the first element
    /// of the array converts (see \p getConversionFactor). Each subsequent element
    /// of the array formats the fractional part left over by the preceding one,
    /// so arrays such as feet/inches are handled correctly. Empty array means
    /// no conversion is defined and an empty string is returned.
    /// \param formats Number format array
    /// \param value Value to be formatted
    static QString format(const std::vector<PDFNumberFormat>& formats, PDFReal value);

    /// Converts the value to a text string using this single number format.
    /// \param value Value to be formatted, in the units of the preceding array element
    /// \param isLastElement If false, then only the whole part is displayed
    QString formatValue(PDFReal value, bool isLastElement) const;

    const QString& getUnitLabel() const { return m_unitLabel; }
    PDFReal getConversionFactor() const { return m_conversionFactor; }
    FractionDisplay getFractionDisplay() const { return m_fractionDisplay; }
    PDFInteger getDenominator() const { return m_denominator; }
    bool hasFixedDenominator() const { return m_fixedDenominator; }
    const QString& getThousandSeparator() const { return m_thousandSeparator; }
    const QString& getDecimalSeparator() const { return m_decimalSeparator; }
    const QString& getLabelPrefix() const { return m_labelPrefix; }
    const QString& getLabelSuffix() const { return m_labelSuffix; }
    Order getOrder() const { return m_order; }

private:
    /// Returns count of the displayed decimal places derived from the denominator
    int getDecimalPlaces() const;

    /// Formats the number itself (without the unit label), applying
    /// the thousand and decimal separators
    QString formatNumber(PDFReal value, int decimalPlaces) const;

    /// Concatenates the number text and the unit label with respect to the order
    QString appendLabel(const QString& numberText) const;

    QString m_unitLabel;
    PDFReal m_conversionFactor = 1.0;
    FractionDisplay m_fractionDisplay = FractionDisplay::Decimal;
    PDFInteger m_denominator = 100;
    bool m_fixedDenominator = false;
    QString m_thousandSeparator = QString(",");
    QString m_decimalSeparator = QString(".");
    QString m_labelPrefix = QString(" ");
    QString m_labelSuffix;
    Order m_order = Order::Suffix;
};

/// Measure dictionary. It describes the scale of a drawing - the relation between
/// the measurements in the default user space and the real world quantities they
/// represent. See PDF 2.0 specification, chapter 12.9.
class PDF4QTLIBCORESHARED_EXPORT PDFMeasure
{
public:
    explicit PDFMeasure() = default;

    enum class Subtype
    {
        Invalid,                        ///< Measure dictionary is missing or malformed
        RectilinearCoordinateSystem,    ///< Subtype RL
        GeospatialCoordinateSystem      ///< Subtype GEO, not interpreted by this class
    };

    /// Parses the measure dictionary. If the object is not a valid measure
    /// dictionary, then invalid measure is returned.
    /// \param storage Object storage
    /// \param object Measure dictionary
    static PDFMeasure parse(const PDFObjectStorage* storage, PDFObject object);

    bool isValid() const { return m_subtype != Subtype::Invalid; }
    bool isRectilinear() const { return m_subtype == Subtype::RectilinearCoordinateSystem; }

    Subtype getSubtype() const { return m_subtype; }

    /// Returns the scale ratio as a human readable text, for example "1 in = 0.1 mi"
    const QString& getScaleRatio() const { return m_scaleRatio; }

    const std::vector<PDFNumberFormat>& getXFormat() const { return m_x; }
    const std::vector<PDFNumberFormat>& getYFormat() const { return m_y; }
    const std::vector<PDFNumberFormat>& getDistanceFormat() const { return m_distance; }
    const std::vector<PDFNumberFormat>& getAreaFormat() const { return m_area; }
    const std::vector<PDFNumberFormat>& getAngleFormat() const { return m_angle; }
    const std::vector<PDFNumberFormat>& getSlopeFormat() const { return m_slope; }
    const std::vector<PDFReal>& getOrigin() const { return m_origin; }
    PDFReal getFactorYX() const { return m_factorYX; }

    /// Returns count of the measurement units per one unit of the default user
    /// space along the x axis. Returns zero, if the measure does not define it.
    PDFReal getUnitsPerUserSpaceUnit() const;

    /// Returns count of the measurement units per one unit of the default user
    /// space along the y axis. If the measure does not define the y axis
    /// separately, then the factor of the x axis is returned.
    PDFReal getUnitsPerUserSpaceUnitY() const;

    /// Returns true, if the coordinate system is anisotropic, i.e. the x axis
    /// and the y axis do not use the same conversion. Distances in such
    /// a coordinate system cannot be expressed by a single scale factor.
    bool isAnisotropic() const;

    /// Returns the label of the measurement unit along the x axis, for example "mi".
    /// Returns empty string, if the measure does not define it.
    QString getUnitLabel() const;

    /// Formats the distance given in the default user space units. The x axis
    /// conversion is used, so the result is correct only for an isotropic
    /// coordinate system - see \p isAnisotropic and the vector overload.
    /// \param userSpaceLength Length in the default user space units
    QString formatDistance(PDFReal userSpaceLength) const;

    /// Formats the distance given as a vector in the default user space. Both
    /// axes are converted separately and the y component is then expressed
    /// in the units of the x axis using the CYX factor, so anisotropic
    /// coordinate systems are handled correctly.
    /// \param userSpaceVector Vector in the default user space units
    QString formatDistance(const QPointF& userSpaceVector) const;

    /// Formats the area given in the square units of the default user space.
    /// The area is a product of a distance along the x axis and a distance
    /// along the y axis, so both conversions are applied.
    /// \param userSpaceArea Area in the square units of the default user space
    QString formatArea(PDFReal userSpaceArea) const;

    /// Formats the angle given in degrees
    /// \param degrees Angle in degrees
    QString formatAngle(PDFReal degrees) const;

private:
    Subtype m_subtype = Subtype::Invalid;
    QString m_scaleRatio;
    std::vector<PDFNumberFormat> m_x;
    std::vector<PDFNumberFormat> m_y;
    std::vector<PDFNumberFormat> m_distance;
    std::vector<PDFNumberFormat> m_area;
    std::vector<PDFNumberFormat> m_angle;
    std::vector<PDFNumberFormat> m_slope;
    std::vector<PDFReal> m_origin;
    PDFReal m_factorYX = 1.0;
};

/// Viewport dictionary. Viewport defines a region of the page, in which
/// a measure (scale) is valid. See PDF 2.0 specification, chapter 12.9.
class PDF4QTLIBCORESHARED_EXPORT PDFViewport
{
public:
    explicit PDFViewport() = default;

    /// Parses the array of viewport dictionaries, which is usually stored
    /// in the VP entry of the page dictionary. Viewports without a valid
    /// bounding box are skipped.
    /// \param storage Object storage
    /// \param object Array of viewport dictionaries
    static std::vector<PDFViewport> parseViewports(const PDFObjectStorage* storage, PDFObject object);

    /// Returns viewport, which applies to the given point, or nullptr, if no
    /// viewport contains the point. If the viewports overlap, then the last one
    /// in the array is used, as required by the specification.
    /// \param viewports Viewports
    /// \param point Point in the default user space
    static const PDFViewport* findViewportForPoint(const std::vector<PDFViewport>& viewports, const QPointF& point);

    const QRectF& getBoundingBox() const { return m_boundingBox; }
    const QString& getName() const { return m_name; }
    const PDFMeasure& getMeasure() const { return m_measure; }

private:
    QRectF m_boundingBox;
    QString m_name;
    PDFMeasure m_measure;
};

}   // namespace pdf

#endif // PDFMEASURE_H
