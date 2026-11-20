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

#ifndef PDFMEASURE_H
#define PDFMEASURE_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QRectF>
#include <QString>

#include <utility>
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

    /// Most dictionaries of a number format array, which are read (Table 267, 268: the units of one quantity in
    /// descending order, such as "mi ft in"; the others are left out). A hostile array, which refers to one
    /// dictionary a million times, would repeat its text in every formatted value (review RS7CCORE of step 7c).
    static constexpr size_t MAXIMUM_ARRAY_LENGTH = 16;

    /// Longest text of a number format dictionary, which is read (/U, /RT, /RD, /PS, /SS of Table 268); a longer
    /// one is cut. A thousands separator is inserted after every third digit of a value, so a long one would
    /// multiply the text of a large value.
    static constexpr qsizetype MAXIMUM_TEXT_LENGTH = 64;

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
    /// accepted and is treated as an array with one element. At most
    /// MAXIMUM_ARRAY_LENGTH dictionaries are read (the first ones).
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
    QString m_labelSuffix = QString(" ");   ///< B208: Table 268 SS, default a single space as PS
    Order m_order = Order::Suffix;
};

/// Measure dictionary. It describes the scale of a drawing - the relation between
/// the measurements in the default user space and the real world quantities they
/// represent. See PDF 2.0 specification, chapter 12.9.
class PDF4QTLIBCORESHARED_EXPORT PDFMeasure
{
public:
    explicit PDFMeasure() = default;

    /// Longest scale ratio /R (Table 267), which is read; a longer one is cut (a hostile text shared by the
    /// measures of many annotations would be copied into each of them)
    static constexpr qsizetype MAXIMUM_RATIO_LENGTH = 256;

    enum class Subtype
    {
        Invalid,                        ///< Measure dictionary is missing or malformed
        RectilinearCoordinateSystem,    ///< Subtype RL
        GeospatialCoordinateSystem      ///< Subtype GEO, not interpreted by this class
    };

    /// Parses the measure dictionary. If the object is not a valid measure
    /// dictionary, then invalid measure is returned. Untrusted input: /R is cut
    /// to MAXIMUM_RATIO_LENGTH characters, /O keeps its first two numbers (the
    /// origin, Table 267), the number format arrays are limited by PDFNumberFormat.
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

    /// Most entries of a /VP array, which are read: the last ones (12.9.2: the last viewport containing a point
    /// is chosen). A hostile array of a million references to one viewport would be parsed whole for every
    /// measured point (review RS7CCORE of step 7c).
    static constexpr size_t MAXIMUM_VIEWPORT_COUNT = 1024;

    /// Longest /Name of a viewport (Table 265), which is read; a longer one is cut
    static constexpr qsizetype MAXIMUM_NAME_LENGTH = 256;

    /// Parses the array of viewport dictionaries, which is usually stored
    /// in the VP entry of the page dictionary. Viewports without a valid
    /// bounding box are skipped. Only the last MAXIMUM_VIEWPORT_COUNT entries
    /// of the array are read.
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

/// Quantity of a measurement (plan of step 7 of phase 2, 4.8): the three lists of units of the panel Units
/// (473:8597) and the three number format arrays of a measure dictionary (ISO 32000-2, Table 267: /D, /A, /T)
enum class PDFMeasureQuantity
{
    Length,     ///< Distances and perimeters (/D)
    Area,       ///< Areas (/A)
    Angle       ///< Angles (/T)
};

/// A unit of measurement (plan of step 7 of phase 2, 4.8; the units of the old DimensionsPlugin,
/// dimensionunits.cpp, moved to the core). A plain value.
class PDF4QTLIBCORESHARED_EXPORT PDFMeasureUnit
{
public:
    QByteArray id;                  ///< Stable untranslated id ("mm", "sqm", "deg"): stored (PDFMeasureScaleRepository), the values of MCP and of the column Unit of the CSV
    PDFReal factor = 0.0;           ///< Units in one point of the default user space (Length), in one square point (Area), in one degree (Angle)
    const char* symbol = nullptr;   ///< Symbol (QT_TRANSLATE_NOOP of the context pdf::PDFMeasureUnits, "mm", "sq. m", "°")
    const char* name = nullptr;     ///< Name of the menu of the units (473:8597, QT_TRANSLATE_NOOP of pdf::PDFMeasureUnits, "Millimeters")

    /// A known unit (a default constructed unit is invalid)
    bool isValid() const { return !id.isEmpty() && factor > 0.0; }

    /// The symbol in the language of the user interface (empty for an invalid unit)
    QString getSymbol() const;

    /// The untranslated symbol ("sq. m"): the label /U of a number format of a document (Table 268), which other
    /// readers show - never the language of the user (B183)
    QString getDocumentSymbol() const;

    /// The name in the language of the user interface ("Millimeters"; empty for an invalid unit)
    QString getName() const;
};

/// A row of the tables of PDFMeasureUnits: compile-time values (the titles of the options of the parameters
/// of the tool Measure are literals, PDFDocumentToolText)
class PDFMeasureUnitDefinition
{
public:
    const char* id;         ///< PDFMeasureUnit::id
    PDFReal factor;         ///< PDFMeasureUnit::factor
    const char* symbol;     ///< PDFMeasureUnit::symbol
    const char* name;       ///< PDFMeasureUnit::name
};

/// The units of the measurements (plan of step 7 of phase 2, 4.8, 473:8597): 9 lengths (pt, in, mm, cm, m,
/// ft, yd, km, mi), 11 areas (sq. pt, sq. in, sq. mm, sq. cm, sq. m, sq. ft, sq. yd, ha, acre, sq. km, sq. mi)
/// and 3 angles (°, rad, gon) - the lists and ids of the old plugin, so its stored values stay readable.
/// Feet, yards and miles derive from the inch exactly (1 in = 72 pt, 1 in = 25.4 mm, 1 ft = 12 in, 1 yd = 36 in,
/// 1 mi = 63 360 in; 1 ha = 10 000 m², 1 acre = 43 560 ft²; 1 rad = 180/π °, 1 gon = 0.9 °). Reentrant.
///
/// The skeleton of step 7c implements the lists (the values of the parameters of PDFDocumentToolMeasure);
/// package P7 owns the class and adds what the tool needs.
class PDF4QTLIBCORESHARED_EXPORT PDFMeasureUnits
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFMeasureUnits)

public:
    PDFMeasureUnits() = delete;

    /// The lengths, units per point of the default user space (1 in = 72 pt, ISO 32000-2, 8.3.2.3)
    static constexpr PDFMeasureUnitDefinition LENGTH_UNITS[] =
    {
        { "pt", 1.0, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "pt"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Points") },
        { "in", PDF_POINT_TO_INCH, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "in"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Inches") },
        { "mm", PDF_POINT_TO_MM, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "mm"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Millimeters") },
        { "cm", PDF_POINT_TO_MM * 0.1, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "cm"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Centimeters") },
        { "m", PDF_POINT_TO_MM * 0.001, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "m"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Meters") },
        { "ft", PDF_POINT_TO_INCH / 12.0, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "ft"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Feet") },
        { "yd", PDF_POINT_TO_INCH / 36.0, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "yd"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Yards") },
        { "km", PDF_POINT_TO_MM * 0.000001, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "km"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Kilometers") },
        { "mi", PDF_POINT_TO_INCH / 63360.0, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "mi"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Miles") }
    };

    /// The areas, units per square point
    static constexpr PDFMeasureUnitDefinition AREA_UNITS[] =
    {
        { "sqpt", 1.0, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "sq. pt"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Square points") },
        { "sqin", PDF_POINT_TO_INCH * PDF_POINT_TO_INCH, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "sq. in"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Square inches") },
        { "sqmm", PDF_POINT_TO_MM * PDF_POINT_TO_MM, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "sq. mm"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Square millimeters") },
        { "sqcm", (PDF_POINT_TO_MM * 0.1) * (PDF_POINT_TO_MM * 0.1), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "sq. cm"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Square centimeters") },
        { "sqm", (PDF_POINT_TO_MM * 0.001) * (PDF_POINT_TO_MM * 0.001), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "sq. m"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Square meters") },
        { "sqft", (PDF_POINT_TO_INCH / 12.0) * (PDF_POINT_TO_INCH / 12.0), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "sq. ft"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Square feet") },
        { "sqyd", (PDF_POINT_TO_INCH / 36.0) * (PDF_POINT_TO_INCH / 36.0), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "sq. yd"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Square yards") },
        { "ha", (PDF_POINT_TO_MM * 0.001) * (PDF_POINT_TO_MM * 0.001) / 10000.0, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "ha"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Hectares") },
        { "acre", (PDF_POINT_TO_INCH / 12.0) * (PDF_POINT_TO_INCH / 12.0) / 43560.0, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "acre"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Acres") },
        { "sqkm", (PDF_POINT_TO_MM * 0.000001) * (PDF_POINT_TO_MM * 0.000001), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "sq. km"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Square kilometers") },
        { "sqmi", (PDF_POINT_TO_INCH / 63360.0) * (PDF_POINT_TO_INCH / 63360.0), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "sq. mi"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Square miles") }
    };

    /// The angles, units per degree
    static constexpr PDFMeasureUnitDefinition ANGLE_UNITS[] =
    {
        { "deg", 1.0, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "°"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Degrees") },
        { "rad", 3.14159265358979323846 / 180.0, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "rad"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Radians") },
        { "gon", 1.0 / 0.9, QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "gon"), QT_TRANSLATE_NOOP("pdf::PDFMeasureUnits", "Gradians") }
    };

    /// The units of the tool Measure, which the user has not chosen yet (the panel Units of the frames 471:5490
    /// … 473:91785: "m", "sq. m", "°" - the defaults of the parameters lengthUnit, areaUnit, angleUnit)
    static constexpr const char* INITIAL_LENGTH_UNIT = "m";
    static constexpr const char* INITIAL_AREA_UNIT = "sqm";
    static constexpr const char* INITIAL_ANGLE_UNIT = "deg";

    /// The units of a quantity in the order of the panel Units (473:8597)
    static std::vector<PDFMeasureUnit> getUnits(PDFMeasureQuantity quantity);

    /// The unit of the id, an invalid unit for an unknown id
    static PDFMeasureUnit findUnit(PDFMeasureQuantity quantity, const QByteArray& id);

    /// The unit used, when none is chosen: the first one of its list (the default of the old plugin: pt,
    /// sq. pt, °; the panel of the new tool remembers the last choice - the last values of the parameters)
    static PDFMeasureUnit getDefaultUnit(PDFMeasureQuantity quantity);

    /// The unit of the panel Units before the user chooses one (INITIAL_*_UNIT: the frames show "m", "sq. m", "°")
    static PDFMeasureUnit getInitialUnit(PDFMeasureQuantity quantity);

    /// The unit of the id, the default unit of its quantity for an empty or unknown id (getDefaultUnit)
    static PDFMeasureUnit findUnitOrDefault(PDFMeasureQuantity quantity, const QByteArray& id);

    /// The length unit of the label of a number format of a document (/U of Table 268: "mm", "in", "ft",
    /// "feet", "mi" …), an invalid unit when the label is not recognized (the labels are not standardized:
    /// the abbreviations, the singular and plural names in the American and the British spelling, ' and ")
    static PDFMeasureUnit findLengthUnitByLabel(const QString& label);

    /// The text of a number of a scale or a document in the C locale: at most \p decimals places, the trailing
    /// zeros removed ("50", "0.125", "11.2"); an empty text for a value that is not finite
    static QString formatNumber(PDFReal value, int decimals);
};

/// Scale of a drawing (plan of step 7 of phase 2, 4.8; DimensionScale of the old plugin moved to the core): an
/// equality of a distance on the paper and the real distance it stands for, "1 mm = 50 mm" for 1:50, with an
/// optional name of a preset ("Metric 1:50", 471:6943). A default constructed scale is invalid (no scale is
/// known); createIdentity is 1:1. A plain value.
///
/// The scale lives only in the application (owner answer Q7.4: PDFMeasureScaleRepository, never written into
/// the document as /VP); an annotation written by PDFMeasureWriter carries it in its own /Measure dictionary.
///
/// The description is a text of the user interface only (the built-in "The drawing is in the real size" of
/// 471:6943, the field Description of 473:7860); it is not a part of the scale (operator== ignores it). The
/// application stores it with a preset and with the default scale (PDFMeasureScaleRepository).
class PDF4QTLIBCORESHARED_EXPORT PDFMeasureScale
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFMeasureScale)

public:
    /// Most places of a value of a scale (the fields of 473:7024 show 4)
    static constexpr int VALUE_DECIMALS = 4;

    /// Most places of the number of a proportion ("1:50", "1:62.5")
    static constexpr int PROPORTION_DECIMALS = 2;

    /// Longest name taken from /R of a document (createFromMeasure; a hostile /R is cut)
    static constexpr int MAXIMUM_DOCUMENT_NAME_LENGTH = 256;

    PDFMeasureScale() = default;
    explicit PDFMeasureScale(PDFReal paperValue, QByteArray paperUnitId, PDFReal realValue, QByteArray realUnitId, QString name = QString());

    /// Both values are positive finite numbers and both units are known length units
    bool isValid() const;

    /// The real length of one length on the paper, both in the same unit: 50 for 1:50 (areas by its square);
    /// 0 for an invalid scale
    PDFReal getScaleFactor() const;

    PDFReal getPaperValue() const { return m_paperValue; }
    const QByteArray& getPaperUnitId() const { return m_paperUnitId; }
    PDFReal getRealValue() const { return m_realValue; }
    const QByteArray& getRealUnitId() const { return m_realUnitId; }
    const QString& getName() const { return m_name; }
    void setName(QString name) { m_name = std::move(name); }
    const QString& getDescription() const { return m_description; }
    void setDescription(QString description) { m_description = std::move(description); }

    /// The same proportion (getScaleFactor equal to 1e-9 relative), whatever the units and the names: the
    /// check of a preset in the menu Scale (471:6943)
    bool hasSameProportion(const PDFMeasureScale& other) const;

    /// The proportion in the C locale ("1:50", "1:62.5", "2:1" for an enlargement; "1:1"); empty for an invalid
    /// scale. The application formats the numbers by PDFAppLocale (getProportion).
    QString getProportionText() const;

    /// The two numbers of the proportion: (1, 50) for 1:50, (2, 1) for 2:1; (0, 0) for an invalid scale
    std::pair<PDFReal, PDFReal> getProportion() const;

    /// The ratio for the user interface ("1 mm = 50 mm"); the numbers are formatted by the caller's locale
    /// in the application (PDFAppLocale, O17) - this text uses the C locale
    QString getRatioText() const;

    /// The /R text of a measure dictionary (Table 267): the C locale and the untranslated unit labels, never
    /// the locale of the user (B183)
    QString getDocumentRatioText() const;

    /// The name, or the ratio text of an unnamed scale
    QString getDisplayName() const;

    /// The scale 1:1 (1 mm = 1 mm)
    static PDFMeasureScale createIdentity();

    /// The scale of a measure dictionary of a viewport of a page (ISO 32000-2, 12.9.2): an invalid scale for an
    /// anisotropic or a geospatial measure and for an unknown unit. \p userUnit is the effective /UserUnit of
    /// the page (B209), which the measured lengths already contain.
    static PDFMeasureScale createFromMeasure(const PDFMeasure& measure, PDFReal userUnit);

    /// The presets offered before the user edits the list (the 12 presets of 471:6943: Real size, Metric 1:20
    /// … 1:10000, Architectural, Engineering)
    static std::vector<PDFMeasureScale> getDefaultPresets();

    bool operator==(const PDFMeasureScale& other) const;
    bool operator!=(const PDFMeasureScale& other) const { return !(*this == other); }

private:
    PDFReal m_paperValue = 0.0;
    QByteArray m_paperUnitId;
    PDFReal m_realValue = 0.0;
    QByteArray m_realUnitId;
    QString m_name;
    QString m_description;
};

}   // namespace pdf

#endif // PDFMEASURE_H
