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

#include <QtTest>

#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_measure.h"
#include "pdfmeasure.h"
#include "pdfmeasurewriter.h"
#include "pdfoperationcontrol.h"

#include <QJsonDocument>
#include <QRegularExpression>

class MeasureTest : public QObject
{
    Q_OBJECT

private slots:
    void test_number_format_defaults();
    void test_number_format_decimal();
    void test_number_format_round_and_truncate();
    void test_number_format_fraction();
    void test_number_format_prefix_label();
    void test_number_format_array_feet_inches();
    void test_measure_invalid();
    void test_measure_distance_and_area();
    void test_measure_y_defaults_to_x();
    void test_measure_anisotropic();
    void test_viewport_parsing();
    void test_viewport_overlapping();
    void test_viewport_missing();
    void test_measure_units();

    // Package P7 of step 7c (plan of step 7 of phase 2, 4.8, 5.1, 8)
    void test_number_format_table268();
    void test_unit_labels();
    void test_scale_values();
    void test_scale_from_measure();
    void test_geometry_values();
    void test_geometry_points();
    void test_writer_distance();
    void test_writer_kinds();
    void test_writer_user_unit();
    void test_writer_tagged();
    void test_read_old_plugin_annotations();
    void test_tool_hostile_measurements();
    void test_tool_list_viewports();
    void test_tool_csv();
    void test_tool_add_certification();

    // Review RS7CCORE of step 7c
    void test_hostile_measure_texts();

    // The fix wave of step 7c (P2S7CFIXMEASURE)
    void test_writer_pdfa_opaque();
    void test_tool_points_outside_page();
    void test_read_many_vertices();
    void test_read_canceled();

private:
    /// Adds one A4 page (595 × 842 pt) with the given /UserUnit to the builder and returns its reference
    static pdf::PDFObjectReference addPage(pdf::PDFDocumentBuilder& builder, pdf::PDFReal userUnit);

    /// A rectilinear measure dictionary of a viewport: \p unitsPerPoint units \p label per user space unit
    static pdf::PDFObject createViewportMeasure(const QString& label, pdf::PDFReal unitsPerPoint, const char* subtype, const QString& ratio);

    /// The dictionary of an object of a document (nullptr = none)
    static const pdf::PDFDictionary* getDictionary(const pdf::PDFDocument& document, pdf::PDFObjectReference reference);

    /// Runs Measure on the document
    static pdf::PDFDocumentToolResult runMeasure(const pdf::PDFDocument& document, const QVariantMap& parameters);

private:
    /// Creates a number format dictionary. Items are given as pairs of the key
    /// and the already built object.
    static pdf::PDFObject createNumberFormat(const QString& unitLabel,
                                             pdf::PDFReal conversionFactor,
                                             const char* fractionDisplay = nullptr,
                                             pdf::PDFInteger denominator = -1,
                                             bool fixedDenominator = false,
                                             const char* order = nullptr,
                                             const QString* labelPrefix = nullptr,
                                             const QString* thousandSeparator = nullptr,
                                             const QString* decimalSeparator = nullptr);

    static pdf::PDFObject createArray(const std::vector<pdf::PDFObject>& objects);
};

pdf::PDFObject MeasureTest::createNumberFormat(const QString& unitLabel,
                                               pdf::PDFReal conversionFactor,
                                               const char* fractionDisplay,
                                               pdf::PDFInteger denominator,
                                               bool fixedDenominator,
                                               const char* order,
                                               const QString* labelPrefix,
                                               const QString* thousandSeparator,
                                               const QString* decimalSeparator)
{
    pdf::PDFObjectFactory factory;

    factory.beginDictionary();

    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("NumberFormat");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("U");
    factory << unitLabel;
    factory.endDictionaryItem();

    factory.beginDictionaryItem("C");
    factory << conversionFactor;
    factory.endDictionaryItem();

    if (fractionDisplay)
    {
        factory.beginDictionaryItem("F");
        factory << pdf::WrapName(fractionDisplay);
        factory.endDictionaryItem();
    }

    if (denominator > 0)
    {
        factory.beginDictionaryItem("D");
        factory << denominator;
        factory.endDictionaryItem();
    }

    if (fixedDenominator)
    {
        factory.beginDictionaryItem("FD");
        factory << true;
        factory.endDictionaryItem();
    }

    if (order)
    {
        factory.beginDictionaryItem("O");
        factory << pdf::WrapName(order);
        factory.endDictionaryItem();
    }

    if (labelPrefix)
    {
        factory.beginDictionaryItem("PS");
        factory << *labelPrefix;
        factory.endDictionaryItem();
    }

    if (thousandSeparator)
    {
        factory.beginDictionaryItem("RT");
        factory << *thousandSeparator;
        factory.endDictionaryItem();
    }

    if (decimalSeparator)
    {
        factory.beginDictionaryItem("RD");
        factory << *decimalSeparator;
        factory.endDictionaryItem();
    }

    factory.endDictionary();

    return factory.takeObject();
}

pdf::PDFObject MeasureTest::createArray(const std::vector<pdf::PDFObject>& objects)
{
    pdf::PDFObjectFactory factory;

    factory.beginArray();
    for (const pdf::PDFObject& object : objects)
    {
        factory << object;
    }
    factory.endArray();

    return factory.takeObject();
}

void MeasureTest::test_number_format_defaults()
{
    pdf::PDFObjectStorage storage;

    // Only the required entries are present, everything else must default
    pdf::PDFNumberFormat format = pdf::PDFNumberFormat::parse(&storage, createNumberFormat("mm", 1.0));

    QCOMPARE(format.getUnitLabel(), QString("mm"));
    QCOMPARE(format.getConversionFactor(), 1.0);
    QCOMPARE(format.getFractionDisplay(), pdf::PDFNumberFormat::FractionDisplay::Decimal);
    QCOMPARE(format.getDenominator(), pdf::PDFInteger(100));
    QCOMPARE(format.hasFixedDenominator(), false);
    QCOMPARE(format.getThousandSeparator(), QString(","));
    QCOMPARE(format.getDecimalSeparator(), QString("."));
    QCOMPARE(format.getOrder(), pdf::PDFNumberFormat::Order::Suffix);
}

void MeasureTest::test_number_format_decimal()
{
    pdf::PDFObjectStorage storage;

    // Denominator 100 means two decimal places
    std::vector<pdf::PDFNumberFormat> formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("mm", 1.0)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 1234.5), QString("1,234.50 mm"));

    // Denominator 1 means no decimal places at all. The value is rounded away
    // from zero, the same way as the round fraction display does it.
    formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("mm", 1.0, "D", 1)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 1234.4), QString("1,234 mm"));
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 1234.5), QString("1,235 mm"));
    QCOMPARE(pdf::PDFNumberFormat::format(formats, -1234.5), QString("-1,235 mm"));

    // Custom separators, as they are defined by the RT/RD entries
    const QString thousandSeparator(" ");
    const QString decimalSeparator(",");
    formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("m", 0.001, "D", 1000, false, nullptr, nullptr, &thousandSeparator, &decimalSeparator)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 1234567.0), QString("1 234,567 m"));

    // Negative values must keep the sign and must be grouped correctly
    formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("mm", 1.0, "D", 1)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, -12345.0), QString("-12,345 mm"));
}

void MeasureTest::test_number_format_round_and_truncate()
{
    pdf::PDFObjectStorage storage;

    std::vector<pdf::PDFNumberFormat> roundFormat = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("in", 1.0, "R")) };
    QCOMPARE(pdf::PDFNumberFormat::format(roundFormat, 2.6), QString("3 in"));
    QCOMPARE(pdf::PDFNumberFormat::format(roundFormat, 2.4), QString("2 in"));

    std::vector<pdf::PDFNumberFormat> truncateFormat = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("in", 1.0, "T")) };
    QCOMPARE(pdf::PDFNumberFormat::format(truncateFormat, 2.9), QString("2 in"));
    QCOMPARE(pdf::PDFNumberFormat::format(truncateFormat, -2.9), QString("-2 in"));
}

void MeasureTest::test_number_format_fraction()
{
    pdf::PDFObjectStorage storage;

    // 2.5 inches with denominator 16 is 2 8/16, which is reduced to 2 1/2
    std::vector<pdf::PDFNumberFormat> formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("in", 1.0, "F", 16)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 2.5), QString("2 1/2 in"));

    // With a fixed denominator the fraction must not be reduced
    formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("in", 1.0, "F", 16, true)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 2.5), QString("2 8/16 in"));

    // Whole value has no fractional part displayed
    formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("in", 1.0, "F", 16)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 3.0), QString("3 in"));

    // Value below one is displayed without the leading zero
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 0.25), QString("1/4 in"));

    // Rounding of the fraction reaches the whole unit
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 2.9999), QString("3 in"));
}

void MeasureTest::test_number_format_prefix_label()
{
    pdf::PDFObjectStorage storage;

    // ISO 32000-2, Table 268: SS (the text after the label) defaults to a single space (B208), so a prefix
    // label without SS is followed by a space
    const QString emptyPrefix;
    std::vector<pdf::PDFNumberFormat> formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("$", 1.0, "D", 100, false, "P", &emptyPrefix)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 12.5), QString("$ 12.50"));
}

void MeasureTest::test_number_format_array_feet_inches()
{
    pdf::PDFObjectStorage storage;

    // Feet and inches - the second element formats the fractional part
    // left over by the first one
    pdf::PDFObject array = createArray({ createNumberFormat("ft", 1.0, "T"),
                                         createNumberFormat("in", 12.0, "D", 1) });

    std::vector<pdf::PDFNumberFormat> formats = pdf::PDFNumberFormat::parseArray(&storage, array);
    QCOMPARE(formats.size(), size_t(2));

    // 3.5 ft is 3 ft and 6 in
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 3.5), QString("3 ft 6 in"));

    // A single dictionary instead of an array must be tolerated
    formats = pdf::PDFNumberFormat::parseArray(&storage, createNumberFormat("ft", 1.0, "D", 1));
    QCOMPARE(formats.size(), size_t(1));
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 3.0), QString("3 ft"));

    // Empty format array produces an empty string
    QCOMPARE(pdf::PDFNumberFormat::format(std::vector<pdf::PDFNumberFormat>(), 3.0), QString());
}

void MeasureTest::test_measure_invalid()
{
    pdf::PDFObjectStorage storage;

    pdf::PDFMeasure measure = pdf::PDFMeasure::parse(&storage, pdf::PDFObject());
    QCOMPARE(measure.isValid(), false);
    QCOMPARE(measure.getUnitsPerUserSpaceUnit(), 0.0);
    QCOMPARE(measure.formatDistance(100.0), QString());
    QCOMPARE(measure.formatArea(100.0), QString());
    QCOMPARE(measure.formatAngle(45.0), QString());
}

void MeasureTest::test_measure_distance_and_area()
{
    pdf::PDFObjectStorage storage;

    // Example based on the PDF specification - 1 in on the paper is 0.1 mi
    // in reality, so one point of the default user space is 0.00139 mi
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();

    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("Measure");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("RL");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("R");
    factory << QString("1 in = 0.1 mi");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("X");
    factory << createArray({ createNumberFormat("mi", 0.00139, "D", 100000) });
    factory.endDictionaryItem();

    factory.beginDictionaryItem("D");
    factory << createArray({ createNumberFormat("mi", 1.0, "D", 100) });
    factory.endDictionaryItem();

    factory.beginDictionaryItem("A");
    factory << createArray({ createNumberFormat("acres", 640.0, "D", 100) });
    factory.endDictionaryItem();

    factory.beginDictionaryItem("T");
    factory << createArray({ createNumberFormat(QString::fromUtf8("°"), 1.0, "D", 10) });
    factory.endDictionaryItem();

    factory.endDictionary();

    pdf::PDFMeasure measure = pdf::PDFMeasure::parse(&storage, factory.takeObject());

    QVERIFY(measure.isValid());
    QVERIFY(measure.isRectilinear());
    QCOMPARE(measure.getScaleRatio(), QString("1 in = 0.1 mi"));
    QCOMPARE(measure.getUnitLabel(), QString("mi"));
    QCOMPARE(measure.getUnitsPerUserSpaceUnit(), 0.00139);
    QCOMPARE(measure.getFactorYX(), 1.0);

    // 1000 points of the default user space are 1.39 mi
    QCOMPARE(measure.formatDistance(1000.0), QString("1.39 mi"));

    // Area conversion applies the x factor twice - 1000000 square points
    // are 1.9321 square miles, which is 1236.54 acres
    QCOMPARE(measure.formatArea(1000000.0), QString("1,236.54 acres"));

    // Angle is passed through in degrees
    QCOMPARE(measure.formatAngle(45.24), QString::fromUtf8("45.2 °"));
}

void MeasureTest::test_measure_y_defaults_to_x()
{
    pdf::PDFObjectStorage storage;

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();

    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("RL");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("X");
    factory << createArray({ createNumberFormat("m", 0.5) });
    factory.endDictionaryItem();

    factory.endDictionary();

    pdf::PDFMeasure measure = pdf::PDFMeasure::parse(&storage, factory.takeObject());

    QVERIFY(measure.isValid());
    QCOMPARE(measure.getYFormat().size(), size_t(1));
    QCOMPARE(measure.getYFormat().front().getConversionFactor(), 0.5);

    // Distance format is missing, the x format must be used directly
    QCOMPARE(measure.formatDistance(10.0), QString("5.00 m"));
}

void MeasureTest::test_measure_anisotropic()
{
    pdf::PDFObjectStorage storage;

    // The x axis and the y axis use a different conversion and the units
    // of the y axis are twice as long as the units of the x axis
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();

    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("RL");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("X");
    factory << createArray({ createNumberFormat("m", 0.5) });
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Y");
    factory << createArray({ createNumberFormat("m", 0.25) });
    factory.endDictionaryItem();

    factory.beginDictionaryItem("CYX");
    factory << pdf::PDFReal(2.0);
    factory.endDictionaryItem();

    factory.beginDictionaryItem("D");
    factory << createArray({ createNumberFormat("m", 1.0) });
    factory.endDictionaryItem();

    factory.beginDictionaryItem("A");
    factory << createArray({ createNumberFormat("sq m", 1.0) });
    factory.endDictionaryItem();

    factory.endDictionary();

    pdf::PDFMeasure measure = pdf::PDFMeasure::parse(&storage, factory.takeObject());

    QVERIFY(measure.isValid());
    QVERIFY(measure.isAnisotropic());
    QCOMPARE(measure.getUnitsPerUserSpaceUnit(), 0.5);
    QCOMPARE(measure.getUnitsPerUserSpaceUnitY(), 0.25);
    QCOMPARE(measure.getFactorYX(), 2.0);

    // The area must use the factor of both axes including the CYX factor, so it
    // is consistent with the distances - the square of 10 x 10 units has sides
    // of 5 m and 5 m, so its area is 25 square meters
    QCOMPARE(measure.formatArea(100.0), QString("25.00 sq m"));

    // The vector overload converts both components separately and expresses
    // the y component in the units of the x axis - 10 units along the x axis
    // are 5 m, 10 units along the y axis are 10 * 0.25 * 2 = 5 m
    QCOMPARE(measure.formatDistance(QPointF(10.0, 10.0)), QString("7.07 m"));
    QCOMPARE(measure.formatDistance(QPointF(10.0, 0.0)), QString("5.00 m"));

    // The scalar overload uses the x axis only
    QCOMPARE(measure.formatDistance(10.0), QString("5.00 m"));

    // The CYX entry is meaningful only if the Y entry is present, so a measure
    // with the x axis only is isotropic even when a stray CYX is written in it
    pdf::PDFObjectFactory isotropicFactory;
    isotropicFactory.beginDictionary();
    isotropicFactory.beginDictionaryItem("X");
    isotropicFactory << createArray({ createNumberFormat("m", 0.5) });
    isotropicFactory.endDictionaryItem();
    isotropicFactory.beginDictionaryItem("CYX");
    isotropicFactory << pdf::PDFReal(2.0);
    isotropicFactory.endDictionaryItem();
    isotropicFactory.endDictionary();

    pdf::PDFMeasure isotropic = pdf::PDFMeasure::parse(&storage, isotropicFactory.takeObject());
    QVERIFY(!isotropic.isAnisotropic());
    QCOMPARE(isotropic.getFactorYX(), 1.0);
    QCOMPARE(isotropic.getUnitsPerUserSpaceUnitY(), 0.5);
}

void MeasureTest::test_viewport_parsing()
{
    pdf::PDFObjectStorage storage;

    pdf::PDFObjectFactory measureFactory;
    measureFactory.beginDictionary();
    measureFactory.beginDictionaryItem("Subtype");
    measureFactory << pdf::WrapName("RL");
    measureFactory.endDictionaryItem();
    measureFactory.beginDictionaryItem("X");
    measureFactory << createArray({ createNumberFormat("mm", 0.5) });
    measureFactory.endDictionaryItem();
    measureFactory.endDictionary();

    pdf::PDFObjectFactory factory;
    factory.beginArray();

    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("Viewport");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("BBox");
    factory << QRectF(0, 0, 100, 200);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Name");
    factory << QString("Plan");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Measure");
    factory << measureFactory.takeObject();
    factory.endDictionaryItem();
    factory.endDictionary();

    // Viewport without a bounding box must be skipped
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("Viewport");
    factory.endDictionaryItem();
    factory.endDictionary();

    factory.endArray();

    std::vector<pdf::PDFViewport> viewports = pdf::PDFViewport::parseViewports(&storage, factory.takeObject());

    QCOMPARE(viewports.size(), size_t(1));
    QCOMPARE(viewports.front().getName(), QString("Plan"));
    QCOMPARE(viewports.front().getBoundingBox(), QRectF(0, 0, 100, 200));
    QVERIFY(viewports.front().getMeasure().isValid());
    QCOMPARE(viewports.front().getMeasure().getUnitsPerUserSpaceUnit(), 0.5);

    QVERIFY(pdf::PDFViewport::findViewportForPoint(viewports, QPointF(50, 50)) != nullptr);
    QVERIFY(pdf::PDFViewport::findViewportForPoint(viewports, QPointF(500, 50)) == nullptr);
}

void MeasureTest::test_viewport_overlapping()
{
    pdf::PDFObjectStorage storage;

    auto createViewport = [this](const QRectF& boundingBox, const QString& name)
    {
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("BBox");
        factory << boundingBox;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Name");
        factory << name;
        factory.endDictionaryItem();
        factory.endDictionary();
        return factory.takeObject();
    };

    pdf::PDFObject array = createArray({ createViewport(QRectF(0, 0, 100, 100), "First"),
                                         createViewport(QRectF(0, 0, 100, 100), "Second") });

    std::vector<pdf::PDFViewport> viewports = pdf::PDFViewport::parseViewports(&storage, array);
    QCOMPARE(viewports.size(), size_t(2));

    // If the viewports overlap, the last one in the array takes precedence
    const pdf::PDFViewport* viewport = pdf::PDFViewport::findViewportForPoint(viewports, QPointF(50, 50));
    QVERIFY(viewport != nullptr);
    QCOMPARE(viewport->getName(), QString("Second"));
}

void MeasureTest::test_viewport_missing()
{
    pdf::PDFObjectStorage storage;

    // A page without the VP entry gives a null object, and a damaged document
    // can contain anything at all. Neither must be treated as an array.
    QVERIFY(pdf::PDFViewport::parseViewports(&storage, pdf::PDFObject()).empty());
    QVERIFY(pdf::PDFViewport::parseViewports(&storage, pdf::PDFObject::createInteger(1)).empty());
    QVERIFY(pdf::PDFViewport::parseViewports(&storage, pdf::PDFObject::createName(QByteArray("VP"))).empty());

    QVERIFY(pdf::PDFNumberFormat::parseArray(&storage, pdf::PDFObject()).empty());
    QVERIFY(pdf::PDFNumberFormat::parseArray(&storage, pdf::PDFObject::createInteger(1)).empty());

    // A measure, which is not a dictionary, must be invalid, not a crash
    QVERIFY(!pdf::PDFMeasure::parse(&storage, pdf::PDFObject::createInteger(1)).isValid());
}

void MeasureTest::test_measure_units()
{
    // The units of Measure (plan of step 7 of phase 2, 4.8; Figma 473:8597: 9 lengths, 11 areas, 3 angles) with
    // the ids of the old DimensionsPlugin; the factors are units per point of the default user space by the exact
    // definitions: 1 in = 72 pt (ISO 32000-2, 8.3.2.3), 1 in = 25.4 mm, 1 ft = 12 in, 1 yd = 36 in, 1 mi = 63 360 in,
    // 1 ha = 10 000 m², 1 acre = 43 560 ft², 1 rad = 180/π °, 1 gon = 0.9 °
    using pdf::PDFMeasureQuantity;
    using pdf::PDFMeasureUnits;
    auto ids = [](PDFMeasureQuantity quantity)
    {
        QList<QByteArray> result;
        for (const pdf::PDFMeasureUnit& unit : PDFMeasureUnits::getUnits(quantity))
        {
            result << unit.id;
        }
        return result;
    };
    QCOMPARE(ids(PDFMeasureQuantity::Length), QList<QByteArray>({ "pt", "in", "mm", "cm", "m", "ft", "yd", "km", "mi" }));
    QCOMPARE(ids(PDFMeasureQuantity::Area), QList<QByteArray>({ "sqpt", "sqin", "sqmm", "sqcm", "sqm", "sqft", "sqyd", "ha", "acre", "sqkm", "sqmi" }));
    QCOMPARE(ids(PDFMeasureQuantity::Angle), QList<QByteArray>({ "deg", "rad", "gon" }));

    auto factor = [](PDFMeasureQuantity quantity, const char* id) { return PDFMeasureUnits::findUnit(quantity, QByteArray(id)).factor; };
    QCOMPARE(factor(PDFMeasureQuantity::Length, "pt"), 1.0);
    QCOMPARE(factor(PDFMeasureQuantity::Length, "in"), 1.0 / 72.0);
    QCOMPARE(factor(PDFMeasureQuantity::Length, "mm"), 25.4 / 72.0);
    QCOMPARE(factor(PDFMeasureQuantity::Length, "cm"), 2.54 / 72.0);
    QCOMPARE(factor(PDFMeasureQuantity::Length, "m"), 0.0254 / 72.0);
    QCOMPARE(factor(PDFMeasureQuantity::Length, "km"), 0.0000254 / 72.0);
    QCOMPARE(factor(PDFMeasureQuantity::Length, "ft"), 1.0 / (72.0 * 12.0));
    QCOMPARE(factor(PDFMeasureQuantity::Length, "yd"), 1.0 / (72.0 * 36.0));
    QCOMPARE(factor(PDFMeasureQuantity::Length, "mi"), 1.0 / (72.0 * 63360.0));
    QCOMPARE(factor(PDFMeasureQuantity::Area, "sqmm"), (25.4 / 72.0) * (25.4 / 72.0));
    QCOMPARE(factor(PDFMeasureQuantity::Area, "sqm"), (0.0254 / 72.0) * (0.0254 / 72.0));
    QCOMPARE(factor(PDFMeasureQuantity::Area, "ha"), (0.0254 / 72.0) * (0.0254 / 72.0) / 10000.0);
    QCOMPARE(factor(PDFMeasureQuantity::Area, "acre"), (1.0 / 864.0) * (1.0 / 864.0) / 43560.0);
    QCOMPARE(factor(PDFMeasureQuantity::Angle, "deg"), 1.0);
    QCOMPARE(factor(PDFMeasureQuantity::Angle, "rad"), 3.14159265358979323846 / 180.0);
    QCOMPARE(factor(PDFMeasureQuantity::Angle, "gon"), 1.0 / 0.9);

    // 72 pt is one inch, 25.4 mm
    QCOMPARE(72.0 * factor(PDFMeasureQuantity::Length, "mm"), 25.4);

    // An unknown id (also of another quantity) is no unit; the default is the first one of the list (the old plugin)
    QVERIFY(!PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, QByteArray("sqm")).isValid());
    QVERIFY(!PDFMeasureUnits::findUnit(PDFMeasureQuantity::Area, QByteArray()).isValid());
    QVERIFY(!pdf::PDFMeasureUnit().isValid());
    QCOMPARE(PDFMeasureUnits::getDefaultUnit(PDFMeasureQuantity::Length).id, QByteArray("pt"));
    QCOMPARE(PDFMeasureUnits::getDefaultUnit(PDFMeasureQuantity::Area).id, QByteArray("sqpt"));
    QCOMPARE(PDFMeasureUnits::getDefaultUnit(PDFMeasureQuantity::Angle).id, QByteArray("deg"));
    QCOMPARE(PDFMeasureUnits::findUnit(PDFMeasureQuantity::Length, QByteArray("mm")).getSymbol(), QStringLiteral("mm"));
}

// ------------------------------------------------------- Package P7 of step 7c

pdf::PDFObjectReference MeasureTest::addPage(pdf::PDFDocumentBuilder& builder, pdf::PDFReal userUnit)
{
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595, 842));
    if (userUnit != 1.0)
    {
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("UserUnit");
        factory << userUnit;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(page, factory.takeObject());
    }
    return page;
}

pdf::PDFObject MeasureTest::createViewportMeasure(const QString& label, pdf::PDFReal unitsPerPoint, const char* subtype, const QString& ratio)
{
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("Measure");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName(subtype);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("R");
    factory << ratio;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("X");
    factory << createArray({ createNumberFormat(label, unitsPerPoint) });
    factory.endDictionaryItem();
    factory.beginDictionaryItem("D");
    factory << createArray({ createNumberFormat(label, 1.0) });
    factory.endDictionaryItem();
    factory.beginDictionaryItem("A");
    factory << createArray({ createNumberFormat(QString("sq. ") + label, 1.0) });
    factory.endDictionaryItem();
    factory.endDictionary();
    return factory.takeObject();
}

const pdf::PDFDictionary* MeasureTest::getDictionary(const pdf::PDFDocument& document, pdf::PDFObjectReference reference)
{
    return document.getDictionaryFromObject(document.getObjectByReference(reference));
}

pdf::PDFDocumentToolResult MeasureTest::runMeasure(const pdf::PDFDocument& document, const QVariantMap& parameters)
{
    pdf::PDFDocumentToolMeasure tool;
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(), QStringLiteral("Floor_Plan.pdf"), true));
    return tool.run(context, parameters, nullptr, nullptr);
}

void MeasureTest::test_number_format_table268()
{
    // ISO 32000-2, Table 268 (B208): D defaults to 100 for F D and to 16 for F F; SS (after the label) to a
    // single space
    pdf::PDFObjectStorage storage;
    const pdf::PDFNumberFormat fraction = pdf::PDFNumberFormat::parse(&storage, createNumberFormat("in", 1.0, "F"));
    QCOMPARE(fraction.getDenominator(), pdf::PDFInteger(16));
    QCOMPARE(pdf::PDFNumberFormat::format({ fraction }, 2.0625), QString("2 1/16 in"));

    const pdf::PDFNumberFormat decimal = pdf::PDFNumberFormat::parse(&storage, createNumberFormat("in", 1.0, "D"));
    QCOMPARE(decimal.getDenominator(), pdf::PDFInteger(100));
    QCOMPARE(decimal.getLabelSuffix(), QString(" "));
    QCOMPARE(decimal.getLabelPrefix(), QString(" "));

    // An explicit empty SS removes the space after a prefix label
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("U");
    factory << QString("$");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("C");
    factory << 1.0;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("O");
    factory << pdf::WrapName("P");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("PS");
    factory << QString();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("SS");
    factory << QString();
    factory.endDictionaryItem();
    factory.endDictionary();
    QCOMPARE(pdf::PDFNumberFormat::format({ pdf::PDFNumberFormat::parse(&storage, factory.takeObject()) }, 12.5), QString("$12.50"));
}

void MeasureTest::test_unit_labels()
{
    using pdf::PDFMeasureUnits;

    // The labels of /U are not standardized: the abbreviations, the names and the marks of the foot and inch
    QCOMPARE(PDFMeasureUnits::findLengthUnitByLabel("mm").id, QByteArray("mm"));
    QCOMPARE(PDFMeasureUnits::findLengthUnitByLabel(" Feet ").id, QByteArray("ft"));
    QCOMPARE(PDFMeasureUnits::findLengthUnitByLabel("'").id, QByteArray("ft"));
    QCOMPARE(PDFMeasureUnits::findLengthUnitByLabel("\"").id, QByteArray("in"));
    QCOMPARE(PDFMeasureUnits::findLengthUnitByLabel("metres").id, QByteArray("m"));
    QCOMPARE(PDFMeasureUnits::findLengthUnitByLabel("Miles").id, QByteArray("mi"));
    QVERIFY(!PDFMeasureUnits::findLengthUnitByLabel("smoots").isValid());
    QVERIFY(!PDFMeasureUnits::findLengthUnitByLabel(QString()).isValid());

    // The units the frames show before a choice (471:5490: m, sq. m, °) and the names of 473:8597
    QCOMPARE(PDFMeasureUnits::getInitialUnit(pdf::PDFMeasureQuantity::Length).id, QByteArray("m"));
    QCOMPARE(PDFMeasureUnits::getInitialUnit(pdf::PDFMeasureQuantity::Area).id, QByteArray("sqm"));
    QCOMPARE(PDFMeasureUnits::getInitialUnit(pdf::PDFMeasureQuantity::Angle).id, QByteArray("deg"));
    QCOMPARE(PDFMeasureUnits::findUnit(pdf::PDFMeasureQuantity::Area, "ha").getName(), QString("Hectares"));

    // The numbers of the scales: the C locale, trailing zeros removed
    QCOMPARE(PDFMeasureUnits::formatNumber(50.0, 4), QString("50"));
    QCOMPARE(PDFMeasureUnits::formatNumber(0.125, 4), QString("0.125"));
    QCOMPARE(PDFMeasureUnits::formatNumber(11.20004, 4), QString("11.2"));
    QCOMPARE(PDFMeasureUnits::formatNumber(std::numeric_limits<double>::quiet_NaN(), 4), QString());
}

void MeasureTest::test_scale_values()
{
    using pdf::PDFMeasureScale;

    // 1 mm on the paper is 50 mm: the factor 50; the units of both sides may differ
    QVERIFY(qFuzzyCompare(PDFMeasureScale(1.0, "mm", 50.0, "mm").getScaleFactor(), 50.0));
    QVERIFY(qFuzzyCompare(PDFMeasureScale(1.0, "mm", 1.0, "m").getScaleFactor(), 1000.0));
    QVERIFY(qFuzzyCompare(PDFMeasureScale(1.0, "cm", 100.0, "m").getScaleFactor(), 10000.0));
    QVERIFY(qFuzzyCompare(PDFMeasureScale(0.125, "in", 1.0, "ft").getScaleFactor(), 96.0));
    QVERIFY(qFuzzyCompare(PDFMeasureScale(2.0, "mm", 1.0, "mm").getScaleFactor(), 0.5));

    // A scale of a zero, negative, infinite value or an unknown unit is no scale
    QVERIFY(!PDFMeasureScale().isValid());
    QVERIFY(!PDFMeasureScale(0.0, "mm", 1.0, "mm").isValid());
    QVERIFY(!PDFMeasureScale(1.0, "mm", -1.0, "mm").isValid());
    QVERIFY(!PDFMeasureScale(1.0, "mm", std::numeric_limits<double>::infinity(), "mm").isValid());
    QVERIFY(!PDFMeasureScale(1.0, "parsec", 1.0, "mm").isValid());
    QVERIFY(!PDFMeasureScale(1.0, "sqm", 1.0, "mm").isValid());
    QCOMPARE(PDFMeasureScale().getScaleFactor(), 0.0);
    QVERIFY(PDFMeasureScale::createIdentity().isValid());
    QCOMPARE(PDFMeasureScale::createIdentity().getScaleFactor(), 1.0);

    // The texts: the proportion, the ratio, /R in the C locale with the untranslated abbreviations (B183)
    QCOMPARE(PDFMeasureScale(1.0, "mm", 50.0, "mm").getProportionText(), QString("1:50"));
    QCOMPARE(PDFMeasureScale(1.0, "mm", 1.0, "m").getProportionText(), QString("1:1000"));
    QCOMPARE(PDFMeasureScale(2.0, "mm", 1.0, "mm").getProportionText(), QString("2:1"));
    QCOMPARE(PDFMeasureScale(1.0, "mm", 62.5, "mm").getProportionText(), QString("1:62.5"));
    QCOMPARE(PDFMeasureScale(1.0, "mm", 50.0, "mm").getDocumentRatioText(), QString("1 mm = 50 mm"));
    QCOMPARE(PDFMeasureScale(0.125, "in", 1.0, "ft").getDocumentRatioText(), QString("0.125 in = 1 ft"));
    QCOMPARE(PDFMeasureScale(0.125, "in", 1.0, "ft").getRatioText(), QString("0.125 in = 1 ft"));
    QCOMPARE(PDFMeasureScale().getDocumentRatioText(), QString());
    QVERIFY(PDFMeasureScale(1.0, "mm", 50.0, "mm").hasSameProportion(PDFMeasureScale(1.0, "cm", 50.0, "cm", "Other")));
    QVERIFY(!PDFMeasureScale(1.0, "mm", 50.0, "mm").hasSameProportion(PDFMeasureScale(1.0, "mm", 51.0, "mm")));

    // The 12 presets of 471:6943 in their order
    const std::vector<PDFMeasureScale> presets = PDFMeasureScale::getDefaultPresets();
    QCOMPARE(presets.size(), size_t(12));
    QCOMPARE(presets.front().getName(), QString("Real size (1:1)"));
    QCOMPARE(presets.front().getDescription(), QString("The drawing is in the real size"));
    QCOMPARE(presets[2].getProportionText(), QString("1:50"));
    QCOMPARE(presets[6].getProportionText(), QString("1:1000"));
    QCOMPARE(presets[7].getProportionText(), QString("1:10000"));
    QCOMPARE(presets[8].getProportionText(), QString("1:96"));
    QCOMPARE(presets[11].getProportionText(), QString("1:1200"));
    for (const PDFMeasureScale& preset : presets)
    {
        QVERIFY(preset.isValid());
    }
}

void MeasureTest::test_scale_from_measure()
{
    pdf::PDFObjectStorage storage;

    // ISO 32000-2, 12.9.2: /X converts a unit of the default user space; 1 unit = 100 × 25.4 / 72 mm is 1:100
    const pdf::PDFReal metric100 = 100.0 * 25.4 / 72.0;
    const pdf::PDFMeasureScale scale = pdf::PDFMeasureScale::createFromMeasure(pdf::PDFMeasure::parse(&storage, createViewportMeasure("mm", metric100, "RL", "1:100")), 1.0);
    QVERIFY(scale.isValid());
    QVERIFY(qFuzzyCompare(scale.getScaleFactor(), 100.0));
    QCOMPARE(scale.getPaperUnitId(), QByteArray("mm"));
    QCOMPARE(scale.getName(), QString("1:100"));

    // B209: a unit of the page with /UserUnit 2 is 2 points, so the factor per point is half
    const pdf::PDFMeasureScale scaleUserUnit = pdf::PDFMeasureScale::createFromMeasure(pdf::PDFMeasure::parse(&storage, createViewportMeasure("mm", metric100, "RL", "1:100")), 2.0);
    QVERIFY(qFuzzyCompare(scaleUserUnit.getScaleFactor(), 50.0));

    // Imperial units: the paper side is an inch (1 unit = 1/72 in = 8 ft: 1 in = 576 ft, 1:6912)
    const pdf::PDFMeasureScale feet = pdf::PDFMeasureScale::createFromMeasure(pdf::PDFMeasure::parse(&storage, createViewportMeasure("ft", 8.0, "RL", "1 in = 576 ft")), 1.0);
    QCOMPARE(feet.getPaperUnitId(), QByteArray("in"));
    QVERIFY(qFuzzyCompare(feet.getRealValue(), 576.0));
    QVERIFY(qFuzzyCompare(feet.getScaleFactor(), 6912.0));

    // A geospatial measure (FM-7), an unknown unit, no conversion: no scale
    QVERIFY(!pdf::PDFMeasureScale::createFromMeasure(pdf::PDFMeasure::parse(&storage, createViewportMeasure("mm", metric100, "GEO", "")), 1.0).isValid());
    QVERIFY(!pdf::PDFMeasureScale::createFromMeasure(pdf::PDFMeasure::parse(&storage, createViewportMeasure("smoots", 1.0, "RL", "")), 1.0).isValid());
    QVERIFY(!pdf::PDFMeasureScale::createFromMeasure(pdf::PDFMeasure::parse(&storage, createViewportMeasure("mm", 0.0, "RL", "")), 1.0).isValid());
    QVERIFY(!pdf::PDFMeasureScale::createFromMeasure(pdf::PDFMeasure(), 1.0).isValid());
}

void MeasureTest::test_geometry_values()
{
    using pdf::PDFMeasureGeometry;
    using pdf::PDFMeasureKind;

    // Plan 8: a line of 72 pt (1 in = 25.4 mm) at 1:50 in millimeters is 25.4 × 50 = 1270 mm
    const std::vector<QPointF> line = { QPointF(10, 10), QPointF(82, 10) };
    const pdf::PDFReal length = PDFMeasureGeometry::getValue(PDFMeasureKind::Distance, line, 1.0);
    QCOMPARE(length, 72.0);
    const pdf::PDFMeasureUnit millimeter = pdf::PDFMeasureUnits::findUnit(pdf::PDFMeasureQuantity::Length, "mm");
    QVERIFY(qFuzzyCompare(PDFMeasureGeometry::convertValue(PDFMeasureKind::Distance, length, pdf::PDFMeasureScale(1.0, "mm", 50.0, "mm"), millimeter), 1270.0));

    // B209: a length × the /UserUnit of the page, an area × its square
    QCOMPARE(PDFMeasureGeometry::getValue(PDFMeasureKind::Distance, line, 2.0), 144.0);

    // The shoelace formula: a square of 72 pt is 1 sq. in; at 1:50 in square meters 1.27² m²
    const std::vector<QPointF> square = { QPointF(0, 0), QPointF(72, 0), QPointF(72, 72), QPointF(0, 72) };
    QCOMPARE(PDFMeasureGeometry::getValue(PDFMeasureKind::Area, square, 1.0), 5184.0);
    QCOMPARE(PDFMeasureGeometry::getValue(PDFMeasureKind::Area, square, 2.0), 4.0 * 5184.0);
    const pdf::PDFMeasureUnit squareInch = pdf::PDFMeasureUnits::findUnit(pdf::PDFMeasureQuantity::Area, "sqin");
    QVERIFY(qFuzzyCompare(PDFMeasureGeometry::convertValue(PDFMeasureKind::Area, 5184.0, pdf::PDFMeasureScale::createIdentity(), squareInch), 1.0));
    const pdf::PDFMeasureUnit squareMeter = pdf::PDFMeasureUnits::findUnit(pdf::PDFMeasureQuantity::Area, "sqm");
    QVERIFY(qFuzzyCompare(PDFMeasureGeometry::convertValue(PDFMeasureKind::Area, 5184.0, pdf::PDFMeasureScale(1.0, "mm", 50.0, "mm"), squareMeter), 1.27 * 1.27));

    // A clockwise polygon has the same area
    const std::vector<QPointF> clockwise = { QPointF(0, 0), QPointF(0, 72), QPointF(72, 72), QPointF(72, 0) };
    QCOMPARE(PDFMeasureGeometry::getPolygonArea(clockwise), 5184.0);

    // The perimeter of the closed square (the first point repeated)
    QCOMPARE(PDFMeasureGeometry::getValue(PDFMeasureKind::Perimeter, { QPointF(0, 0), QPointF(72, 0), QPointF(72, 72), QPointF(0, 72), QPointF(0, 0) }, 1.0), 288.0);

    // B181: the angle is the smaller one, in both orders of the arms; an angle has no scale
    QVERIFY(qFuzzyCompare(PDFMeasureGeometry::getAngle(QPointF(72, 0), QPointF(0, 0), QPointF(0, 72)), 90.0));
    QVERIFY(qFuzzyCompare(PDFMeasureGeometry::getAngle(QPointF(0, 72), QPointF(0, 0), QPointF(72, 0)), 90.0));
    QVERIFY(qFuzzyCompare(PDFMeasureGeometry::getAngle(QPointF(1, 0), QPointF(0, 0), QPointF(-1, -1)), 135.0));
    QVERIFY(qFuzzyCompare(PDFMeasureGeometry::getAngle(QPointF(-1, -1), QPointF(0, 0), QPointF(1, 0)), 135.0));
    QVERIFY(qFuzzyCompare(PDFMeasureGeometry::getAngle(QPointF(1, 0), QPointF(0, 0), QPointF(-1, 0)), 180.0));
    const pdf::PDFMeasureUnit radian = pdf::PDFMeasureUnits::findUnit(pdf::PDFMeasureQuantity::Angle, "rad");
    QVERIFY(qFuzzyCompare(PDFMeasureGeometry::convertValue(PDFMeasureKind::Angle, 180.0, pdf::PDFMeasureScale(1.0, "mm", 50.0, "mm"), radian), M_PI));

    // The texts of 471:5975
    QCOMPARE(PDFMeasureGeometry::getValueText(PDFMeasureKind::Area, "31.95", "sq. m"), QString("A = 31.95 sq. m"));
    QCOMPARE(PDFMeasureGeometry::getValueText(PDFMeasureKind::Perimeter, "12.56", "m"), QString("p = 12.56 m"));
    QCOMPARE(PDFMeasureGeometry::getValueText(PDFMeasureKind::Distance, "11.20", "m"), QString("11.20 m"));
}

void MeasureTest::test_geometry_points()
{
    using pdf::PDFMeasureGeometry;
    using pdf::PDFMeasureKind;

    // A perimeter is closed (Q7.5 b: a closed /PolyLine), an area is not (a /Polygon closes itself)
    const std::vector<QPointF> triangle = { QPointF(0, 0), QPointF(10, 0), QPointF(0, 10) };
    std::optional<std::vector<QPointF>> perimeter = PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Perimeter, triangle, 200);
    QVERIFY(perimeter);
    QCOMPARE(perimeter->size(), size_t(4));
    QCOMPARE(perimeter->back(), perimeter->front());
    std::optional<std::vector<QPointF>> area = PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Area, { QPointF(0, 0), QPointF(10, 0), QPointF(0, 10), QPointF(0, 0) }, 200);
    QVERIFY(area);
    QCOMPARE(area->size(), size_t(3));

    // Hostile points: not finite, a wrong count, fewer than 3 different corners, an arm of zero length, too many
    QVERIFY(!PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(std::numeric_limits<double>::infinity(), 0) }, 200));
    QVERIFY(!PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Distance, { QPointF(std::numeric_limits<double>::quiet_NaN(), 0), QPointF(1, 0) }, 200));
    QVERIFY(!PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Distance, { QPointF(0, 0) }, 200));
    QVERIFY(!PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Area, { QPointF(0, 0), QPointF(1, 1), QPointF(0, 0), QPointF(1, 1) }, 200));
    QVERIFY(!PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Angle, { QPointF(0, 0), QPointF(0, 0), QPointF(1, 1) }, 200));
    QVERIFY(!PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Angle, { QPointF(0, 0), QPointF(1, 1) }, 200));
    std::vector<QPointF> many;
    for (int i = 0; i < 201; ++i)
    {
        many.emplace_back(i, i % 2);
    }
    QVERIFY(!PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Area, many, 200));
    many.pop_back();
    QVERIFY(PDFMeasureGeometry::normalizePoints(PDFMeasureKind::Area, many, 200));

    // The drawing is complete: 2, 3, a closed shape
    QVERIFY(PDFMeasureGeometry::isComplete(PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(1, 0) }));
    QVERIFY(!PDFMeasureGeometry::isComplete(PDFMeasureKind::Area, triangle));
    QVERIFY(PDFMeasureGeometry::isComplete(PDFMeasureKind::Area, *perimeter));
}

void MeasureTest::test_writer_distance()
{
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 1.0);

    pdf::PDFMeasureDisplayUnits units;
    units.length = "mm";
    units.area = "sqm";
    units.angle = "deg";
    pdf::PDFMeasureAnnotationStyle style;
    style.author = "Tester";
    QString errorMessage;
    const pdf::PDFObjectReference annotation = pdf::PDFMeasureWriter::createAnnotation(&builder, page, pdf::PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) },
                                                                                       pdf::PDFMeasureScale(1.0, "mm", 50.0, "mm"), units, style, &errorMessage);
    QVERIFY2(annotation.isValid(), qPrintable(errorMessage));
    const pdf::PDFDocument document = builder.build();

    // Table 178: /Line with /IT /LineDimension; /Contents the value (the alternate text, ISO 14289-1 7.18.1)
    const pdf::PDFDictionary* dictionary = getDictionary(document, annotation);
    QVERIFY(dictionary);
    QCOMPARE(document.getObject(dictionary->get("Subtype")).getString(), QByteArray("Line"));
    QCOMPARE(document.getObject(dictionary->get("IT")).getString(), QByteArray("LineDimension"));
    pdf::PDFDocumentDataLoaderDecorator loader(&document);
    QCOMPARE(loader.readTextStringFromDictionary(dictionary, "Contents", QString()), QString("1270.00 mm"));
    QCOMPARE(loader.readTextStringFromDictionary(dictionary, "Subj", QString()), QString("Length"));
    QCOMPARE(loader.readTextStringFromDictionary(dictionary, "T", QString()), QString("Tester"));

    // Table 266, 267 (R7-12 e): RL with /R, /X, /D, /A, /T; /X converts the user space unit to mm at 1:50
    const pdf::PDFDictionary* measure = document.getDictionaryFromObject(dictionary->get("Measure"));
    QVERIFY(measure);
    QCOMPARE(document.getObject(measure->get("Subtype")).getString(), QByteArray("RL"));
    QCOMPARE(loader.readTextStringFromDictionary(measure, "R", QString()), QString("1 mm = 50 mm"));
    for (const char* key : { "X", "D", "A", "T" })
    {
        QVERIFY2(document.getObject(measure->get(key)).isArray(), key);
    }
    const pdf::PDFMeasure parsedMeasure = pdf::PDFMeasure::parse(&document.getStorage(), dictionary->get("Measure"));
    QVERIFY(qFuzzyCompare(parsedMeasure.getUnitsPerUserSpaceUnit(), 50.0 * 25.4 / 72.0));
    QCOMPARE(parsedMeasure.getUnitLabel(), QString("mm"));
    QCOMPARE(parsedMeasure.getAreaFormat().front().getUnitLabel(), QString("sq. m"));
    QVERIFY(qFuzzyCompare(parsedMeasure.getAreaFormat().front().getConversionFactor(), 1e-6));
    QCOMPARE(parsedMeasure.getAngleFormat().front().getUnitLabel(), QString("°"));
    QCOMPARE(parsedMeasure.getAngleFormat().front().getConversionFactor(), 1.0);

    // 12.5.5: an appearance stream (PDF/A), whose box holds the line
    const pdf::PDFDictionary* appearance = document.getDictionaryFromObject(dictionary->get("AP"));
    QVERIFY(appearance);
    const pdf::PDFObject normal = document.getObject(appearance->get("N"));
    QVERIFY(normal.isStream());
    const QRectF box = loader.readRectangle(normal.getStream()->getDictionary()->get("BBox"), QRectF());
    QVERIFY(box.contains(QPointF(100, 100)) && box.contains(QPointF(172, 100)));
    const QRectF rectangle = loader.readRectangle(dictionary->get("Rect"), QRectF());
    QVERIFY(rectangle.contains(box.adjusted(1, 1, -1, -1)));

    // The annotation of the page reads back as the measurement (the number format of its /Measure)
    QCOMPARE(document.getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));
    const std::optional<pdf::PDFMeasureAnnotationInfo> info = pdf::PDFMeasureAnnotationInfo::read(&document, 0, annotation, 200);
    QVERIFY(info);
    QCOMPARE(info->kind, pdf::PDFMeasureKind::Distance);
    QVERIFY(qFuzzyCompare(info->value, 1270.0));
    QCOMPARE(info->unitId, QByteArray("mm"));
    QCOMPARE(info->text, QString("1,270.00 mm"));
    QCOMPARE(info->ratio, QString("1 mm = 50 mm"));
    QVERIFY(info->hasMeasure);

    // Invalid inputs make no annotation: the scale, the points of the kind, a reference, which is no page
    pdf::PDFDocumentBuilder secondBuilder;
    const pdf::PDFObjectReference secondPage = addPage(secondBuilder, 1.0);
    QVERIFY(!pdf::PDFMeasureWriter::createAnnotation(&secondBuilder, secondPage, pdf::PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(1, 0) }, pdf::PDFMeasureScale(), units, style, &errorMessage).isValid());
    QVERIFY(!errorMessage.isEmpty());
    QVERIFY(!pdf::PDFMeasureWriter::createAnnotation(&secondBuilder, secondPage, pdf::PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(1, 0), QPointF(2, 0) }, pdf::PDFMeasureScale::createIdentity(), units, style, &errorMessage).isValid());
    QVERIFY(!pdf::PDFMeasureWriter::createAnnotation(&secondBuilder, secondBuilder.getCatalogReference(), pdf::PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(1, 0) }, pdf::PDFMeasureScale::createIdentity(), units, style, &errorMessage).isValid());
    QVERIFY(secondBuilder.build().getCatalog()->getPage(0)->getAnnotations().empty());
}

void MeasureTest::test_writer_kinds()
{
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 1.0);
    pdf::PDFMeasureDisplayUnits units;
    units.length = "pt";
    units.area = "sqpt";
    units.angle = "deg";
    const pdf::PDFMeasureAnnotationStyle style;
    const pdf::PDFMeasureScale identity = pdf::PDFMeasureScale::createIdentity();
    const std::vector<QPointF> square = { QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(0, 100) };

    const pdf::PDFObjectReference perimeter = pdf::PDFMeasureWriter::createAnnotation(&builder, page, pdf::PDFMeasureKind::Perimeter, square, identity, units, style, nullptr);
    const pdf::PDFObjectReference area = pdf::PDFMeasureWriter::createAnnotation(&builder, page, pdf::PDFMeasureKind::Area, square, identity, units, style, nullptr);
    const pdf::PDFObjectReference angle = pdf::PDFMeasureWriter::createAnnotation(&builder, page, pdf::PDFMeasureKind::Angle, { QPointF(100, 0), QPointF(0, 0), QPointF(0, 100) }, identity, units, style, nullptr);
    QVERIFY(perimeter.isValid() && area.isValid() && angle.isValid());
    const pdf::PDFDocument document = builder.build();
    pdf::PDFDocumentDataLoaderDecorator loader(&document);

    // Q7.5 (b): a perimeter is a closed /PolyLine /IT /PolyLineDimension (the last vertex is the first)
    const pdf::PDFDictionary* perimeterDictionary = getDictionary(document, perimeter);
    QCOMPARE(document.getObject(perimeterDictionary->get("Subtype")).getString(), QByteArray("PolyLine"));
    QCOMPARE(document.getObject(perimeterDictionary->get("IT")).getString(), QByteArray("PolyLineDimension"));
    const std::vector<pdf::PDFReal> perimeterVertices = loader.readNumberArrayFromDictionary(perimeterDictionary, "Vertices");
    QCOMPARE(perimeterVertices.size(), size_t(10));
    QCOMPARE(perimeterVertices[8], perimeterVertices[0]);
    QCOMPARE(perimeterVertices[9], perimeterVertices[1]);

    // Table 181: an area is a /Polygon /IT /PolygonDimension (not closed by a repeated vertex)
    const pdf::PDFDictionary* areaDictionary = getDictionary(document, area);
    QCOMPARE(document.getObject(areaDictionary->get("Subtype")).getString(), QByteArray("Polygon"));
    QCOMPARE(document.getObject(areaDictionary->get("IT")).getString(), QByteArray("PolygonDimension"));
    QCOMPARE(loader.readNumberArrayFromDictionary(areaDictionary, "Vertices").size(), size_t(8));

    // Q7.5 (b): an angle is a /PolyLine without /IT, /Subj Angle, /Measure with /T
    const pdf::PDFDictionary* angleDictionary = getDictionary(document, angle);
    QCOMPARE(document.getObject(angleDictionary->get("Subtype")).getString(), QByteArray("PolyLine"));
    QVERIFY(!angleDictionary->hasKey("IT"));
    QCOMPARE(loader.readTextStringFromDictionary(angleDictionary, "Subj", QString()), QString("Angle"));
    QCOMPARE(loader.readTextStringFromDictionary(angleDictionary, "Contents", QString()), QString("90.00 °"));
    QVERIFY(document.getObject(document.getDictionaryFromObject(angleDictionary->get("Measure"))->get("T")).isArray());

    // The three read back with their kinds and values
    const std::vector<pdf::PDFMeasureAnnotationInfo> infos = pdf::PDFMeasureAnnotationInfo::readPages(&document, { 0 }, 100, 200, nullptr, nullptr);
    QCOMPARE(infos.size(), size_t(3));
    QCOMPARE(infos[0].kind, pdf::PDFMeasureKind::Perimeter);
    QVERIFY(qFuzzyCompare(infos[0].value, 400.0));
    QCOMPARE(infos[1].kind, pdf::PDFMeasureKind::Area);
    QVERIFY(qFuzzyCompare(infos[1].value, 10000.0));
    QCOMPARE(infos[1].unitId, QByteArray("sqpt"));
    QCOMPARE(infos[2].kind, pdf::PDFMeasureKind::Angle);
    QVERIFY(qFuzzyCompare(infos[2].value, 90.0));
    QCOMPARE(infos[2].unitId, QByteArray("deg"));

    // A limit of the listed annotations says so
    bool isTruncated = false;
    QCOMPARE(pdf::PDFMeasureAnnotationInfo::readPages(&document, { 0 }, 2, 200, &isTruncated, nullptr).size(), size_t(2));
    QVERIFY(isTruncated);
}

void MeasureTest::test_writer_user_unit()
{
    // B209: /X of a page with /UserUnit 2 converts its unit (2 points): 72 units are 144 pt = 50.8 mm
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 2.0);
    pdf::PDFMeasureDisplayUnits units;
    units.length = "mm";
    const pdf::PDFObjectReference annotation = pdf::PDFMeasureWriter::createAnnotation(&builder, page, pdf::PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(72, 0) },
                                                                                       pdf::PDFMeasureScale::createIdentity(), units, pdf::PDFMeasureAnnotationStyle(), nullptr);
    QVERIFY(annotation.isValid());

    // A hostile /UserUnit (Table 31 range) counts as 1
    pdf::PDFDocumentBuilder hostileBuilder;
    const pdf::PDFObjectReference hostilePage = addPage(hostileBuilder, 1e12);
    const pdf::PDFObjectReference hostileAnnotation = pdf::PDFMeasureWriter::createAnnotation(&hostileBuilder, hostilePage, pdf::PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(72, 0) },
                                                                                              pdf::PDFMeasureScale::createIdentity(), units, pdf::PDFMeasureAnnotationStyle(), nullptr);

    const pdf::PDFDocument document = builder.build();
    const pdf::PDFDictionary* dictionary = getDictionary(document, annotation);
    pdf::PDFDocumentDataLoaderDecorator loader(&document);
    QCOMPARE(loader.readTextStringFromDictionary(dictionary, "Contents", QString()), QString("50.80 mm"));
    QVERIFY(qFuzzyCompare(pdf::PDFMeasure::parse(&document.getStorage(), dictionary->get("Measure")).getUnitsPerUserSpaceUnit(), 2.0 * 25.4 / 72.0));
    const std::optional<pdf::PDFMeasureAnnotationInfo> info = pdf::PDFMeasureAnnotationInfo::read(&document, 0, annotation, 200);
    QVERIFY(info && qFuzzyCompare(info->value, 50.8));

    const pdf::PDFDocument hostileDocument = hostileBuilder.build();
    QVERIFY(qFuzzyCompare(pdf::PDFMeasure::parse(&hostileDocument.getStorage(), getDictionary(hostileDocument, hostileAnnotation)->get("Measure")).getUnitsPerUserSpaceUnit(), 25.4 / 72.0));
}

void MeasureTest::test_writer_tagged()
{
    // ISO 14289-1, 7.18.1: in a tagged document the annotation is the content of an /Annot element (an /OBJR
    // of it, ISO 32000-2, 14.7.4.4), its /StructParent a new key of /ParentTree (14.7.5.4)
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 1.0);
    const pdf::PDFObjectReference root = builder.addObject(pdf::PDFObject::createNull());

    pdf::PDFObjectFactory element;
    element.beginDictionary();
    element.beginDictionaryItem("Type");
    element << pdf::WrapName("StructElem");
    element.endDictionaryItem();
    element.beginDictionaryItem("S");
    element << pdf::WrapName("Document");
    element.endDictionaryItem();
    element.beginDictionaryItem("P");
    element << root;
    element.endDictionaryItem();
    element.endDictionary();
    const pdf::PDFObjectReference documentElement = builder.addObject(element.takeObject());

    pdf::PDFObjectFactory rootFactory;
    rootFactory.beginDictionary();
    rootFactory.beginDictionaryItem("Type");
    rootFactory << pdf::WrapName("StructTreeRoot");
    rootFactory.endDictionaryItem();
    rootFactory.beginDictionaryItem("K");
    rootFactory << documentElement;
    rootFactory.endDictionaryItem();
    rootFactory.beginDictionaryItem("ParentTree");
    rootFactory.beginDictionary();
    rootFactory.beginDictionaryItem("Nums");
    rootFactory.beginArray();
    rootFactory << pdf::PDFInteger(0);
    rootFactory << documentElement;
    rootFactory.endArray();
    rootFactory.endDictionaryItem();
    rootFactory.endDictionary();
    rootFactory.endDictionaryItem();
    rootFactory.beginDictionaryItem("ParentTreeNextKey");
    rootFactory << pdf::PDFInteger(1);
    rootFactory.endDictionaryItem();
    rootFactory.endDictionary();
    builder.setObject(root, rootFactory.takeObject());

    pdf::PDFObjectFactory catalog;
    catalog.beginDictionary();
    catalog.beginDictionaryItem("StructTreeRoot");
    catalog << root;
    catalog.endDictionaryItem();
    catalog.endDictionary();
    builder.mergeTo(builder.getCatalogReference(), catalog.takeObject());

    const pdf::PDFObjectReference annotation = pdf::PDFMeasureWriter::createAnnotation(&builder, page, pdf::PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(72, 0) },
                                                                                       pdf::PDFMeasureScale::createIdentity(), pdf::PDFMeasureDisplayUnits(), pdf::PDFMeasureAnnotationStyle(), nullptr);
    QVERIFY(annotation.isValid());
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFDictionary* annotationDictionary = getDictionary(document, annotation);
    const pdf::PDFObject structParent = document.getObject(annotationDictionary->get("StructParent"));
    QVERIFY(structParent.isInt());
    QCOMPARE(structParent.getInteger(), pdf::PDFInteger(1));

    const pdf::PDFDictionary* documentDictionary = getDictionary(document, documentElement);
    const pdf::PDFObject kids = document.getObject(documentDictionary->get("K"));
    QVERIFY(kids.isArray());
    QCOMPARE(kids.getArray()->getCount(), size_t(1));
    const pdf::PDFDictionary* annotElement = document.getDictionaryFromObject(kids.getArray()->getItem(0));
    QVERIFY(annotElement);
    QCOMPARE(document.getObject(annotElement->get("S")).getString(), QByteArray("Annot"));
    const pdf::PDFDictionary* objectReference = document.getDictionaryFromObject(annotElement->get("K"));
    QVERIFY(objectReference);
    QCOMPARE(document.getObject(objectReference->get("Type")).getString(), QByteArray("OBJR"));
    QCOMPARE(objectReference->get("Obj").getReference(), annotation);
    QCOMPARE(objectReference->get("Pg").getReference(), page);

    const pdf::PDFDictionary* rootDictionary = getDictionary(document, root);
    QCOMPARE(document.getObject(rootDictionary->get("ParentTreeNextKey")).getInteger(), pdf::PDFInteger(2));
    const pdf::PDFDictionary* parentTree = document.getDictionaryFromObject(rootDictionary->get("ParentTree"));
    const pdf::PDFObject nums = document.getObject(parentTree->get("Nums"));
    QCOMPARE(nums.getArray()->getCount(), size_t(4));
    QCOMPARE(document.getObject(nums.getArray()->getItem(2)).getInteger(), pdf::PDFInteger(1));

    // Review RS7CCORE L5, ISO 14289-1 7.18.3: a page with annotations of a tagged document has /Tabs /S (the order
    // of the structure, Table 31 of ISO 32000-2)
    const pdf::PDFObject tabs = document.getObject(getDictionary(document, page)->get("Tabs"));
    QVERIFY(tabs.isName());
    QCOMPARE(tabs.getString(), QByteArray("S"));

    // An untagged document gets no structure (the test of the other writes: no /StructParent) and its page keeps
    // its tab order (no /Tabs: the order of /Annots)
    pdf::PDFDocumentBuilder plainBuilder;
    const pdf::PDFObjectReference plainPage = addPage(plainBuilder, 1.0);
    const pdf::PDFObjectReference plainAnnotation = pdf::PDFMeasureWriter::createAnnotation(&plainBuilder, plainPage, pdf::PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(72, 0) },
                                                                                            pdf::PDFMeasureScale::createIdentity(), pdf::PDFMeasureDisplayUnits(), pdf::PDFMeasureAnnotationStyle(), nullptr);
    const pdf::PDFDocument plainDocument = plainBuilder.build();
    QVERIFY(!getDictionary(plainDocument, plainAnnotation)->hasKey("StructParent"));
    QVERIFY(!getDictionary(plainDocument, plainPage)->hasKey("Tabs"));
}

void MeasureTest::test_read_old_plugin_annotations()
{
    // The old plugin wrote a perimeter as /Polygon /PolygonDimension and an angle as /PolyLine
    // /PolyLineDimension (B171), its /Subj said which; another producer writes an open /PolyLineDimension
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 1.0);
    auto setIntent = [&builder](pdf::PDFObjectReference annotation, const char* intent)
    {
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("IT");
        factory << pdf::WrapName(intent);
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(annotation, factory.takeObject());
    };

    const QPolygonF square({ QPointF(0, 0), QPointF(72, 0), QPointF(72, 72), QPointF(0, 72) });
    const pdf::PDFObjectReference oldPerimeter = builder.createAnnotationPolygon(page, square, 1.0, QColor(), Qt::black, "Author", "Perimeter", "p = 1 in");
    setIntent(oldPerimeter, "PolygonDimension");
    const pdf::PDFObjectReference oldAngle = builder.createAnnotationPolyline(page, QPolygonF({ QPointF(72, 0), QPointF(0, 0), QPointF(0, 72) }), 1.0, QColor(), Qt::black, "Author", "Angle", "90 °",
                                                                             pdf::AnnotationLineEnding::None, pdf::AnnotationLineEnding::None);
    setIntent(oldAngle, "PolyLineDimension");
    const pdf::PDFObjectReference openLine = builder.createAnnotationPolyline(page, QPolygonF({ QPointF(0, 0), QPointF(30, 40), QPointF(30, 100) }), 1.0, QColor(), Qt::black, "Author", "Path", "110 pt",
                                                                              pdf::AnnotationLineEnding::None, pdf::AnnotationLineEnding::None);
    setIntent(openLine, "PolyLineDimension");
    const pdf::PDFObjectReference plainPolygon = builder.createAnnotationPolygon(page, square, 1.0, QColor(), Qt::black, "Author", "Shape", "A shape");
    Q_UNUSED(plainPolygon);
    const pdf::PDFDocument document = builder.build();

    const std::vector<pdf::PDFMeasureAnnotationInfo> infos = pdf::PDFMeasureAnnotationInfo::readPages(&document, { 0 }, 100, 200, nullptr, nullptr);
    QCOMPARE(infos.size(), size_t(3));
    QCOMPARE(infos[0].kind, pdf::PDFMeasureKind::Perimeter);
    QCOMPARE(infos[0].value, 288.0);
    QCOMPARE(infos[0].text, QString("p = 1 in"));
    QVERIFY(!infos[0].hasMeasure);
    QCOMPARE(infos[1].kind, pdf::PDFMeasureKind::Angle);
    QVERIFY(qFuzzyCompare(infos[1].value, 90.0));
    QCOMPARE(infos[2].kind, pdf::PDFMeasureKind::Distance);
    QCOMPARE(infos[2].value, 110.0);
}

void MeasureTest::test_tool_hostile_measurements()
{
    pdf::PDFDocumentBuilder builder;
    addPage(builder, 1.0);
    const pdf::PDFDocument document = builder.build();

    auto run = [&document](const QString& measurements)
    {
        QVariantMap parameters;
        parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MODE, QString("list"));
        parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MEASUREMENTS, measurements);
        return runMeasure(document, parameters);
    };

    // Untrusted JSON of MCP: every wrong value is refused with its reason, nothing runs
    const QStringList hostile = {
        "not json",
        "{\"page\": 1}",
        "[1]",
        "[{\"page\": 0, \"kind\": \"distance\", \"points\": [[0, 0], [1, 0]]}]",
        "[{\"page\": 2, \"kind\": \"distance\", \"points\": [[0, 0], [1, 0]]}]",
        "[{\"page\": 1.5, \"kind\": \"distance\", \"points\": [[0, 0], [1, 0]]}]",
        "[{\"page\": 1, \"kind\": \"volume\", \"points\": [[0, 0], [1, 0]]}]",
        "[{\"page\": 1, \"kind\": \"distance\", \"points\": [[0, \"a\"], [1, 0]]}]",
        "[{\"page\": 1, \"kind\": \"distance\", \"points\": [[0, 0, 0], [1, 0]]}]",
        "[{\"page\": 1, \"kind\": \"area\", \"points\": [[0, 0], [0, 0], [1, 1]]}]",
        "[{\"page\": 1, \"kind\": \"angle\", \"points\": [[0, 0], [0, 0], [1, 1]]}]",
        "[{\"page\": 1, \"kind\": \"distance\", \"points\": [[0, 0], [1, 0]], \"scale\": {\"paperValue\": 1, \"paperUnit\": \"parsec\", \"realValue\": 1, \"realUnit\": \"mm\"}}]",
        "[{\"page\": 1, \"kind\": \"distance\", \"points\": [[0, 0], [1, 0]], \"scale\": {\"paperValue\": 0, \"paperUnit\": \"mm\", \"realValue\": 1, \"realUnit\": \"mm\"}}]",
        "[{\"page\": 1, \"kind\": \"distance\", \"points\": [[0, 0], [1, 0]], \"scale\": 5}]"
    };
    for (const QString& measurements : hostile)
    {
        const pdf::PDFDocumentToolResult result = run(measurements);
        QVERIFY2(result.status == pdf::PDFDocumentToolResultStatus::Failed, qPrintable(measurements));
        QVERIFY(!result.message.isEmpty());
    }

    // The counts: at most 500 measurements, 200 points (plan 6.2)
    QJsonArray tooMany;
    for (int i = 0; i < pdf::PDFDocumentToolMeasure::MAXIMUM_MEASUREMENT_COUNT + 1; ++i)
    {
        tooMany.append(QJsonObject({ { "page", 1 }, { "kind", "distance" }, { "points", QJsonArray({ QJsonArray({ 0, 0 }), QJsonArray({ 1, 0 }) }) } }));
    }
    QVERIFY(run(QString::fromUtf8(QJsonDocument(tooMany).toJson())).status == pdf::PDFDocumentToolResultStatus::Failed);
    tooMany.removeLast();
    QVERIFY(run(QString::fromUtf8(QJsonDocument(tooMany).toJson())).isSuccess());

    QJsonArray points;
    for (int i = 0; i < pdf::PDFDocumentToolMeasure::MAXIMUM_POINT_COUNT + 1; ++i)
    {
        points.append(QJsonArray({ i, i % 2 }));
    }
    QVERIFY(run(QString::fromUtf8(QJsonDocument(QJsonArray({ QJsonObject({ { "page", 1 }, { "kind", "area" }, { "points", points } }) })).toJson())).status == pdf::PDFDocumentToolResultStatus::Failed);
}

void MeasureTest::test_tool_list_viewports()
{
    // ISO 32000-2, 12.9.2: the last viewport of /VP containing the first point gives the scale; a /GEO one is
    // ignored (FM-7); a point outside of the viewports takes the scale of the measurement, else 1:1
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 1.0);
    auto viewport = [](const QRectF& box, const QString& name, const pdf::PDFObject& measure)
    {
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("Viewport");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("BBox");
        factory << box;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Name");
        factory << name;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Measure");
        factory << measure;
        factory.endDictionaryItem();
        factory.endDictionary();
        return factory.takeObject();
    };
    pdf::PDFObjectFactory pageUpdate;
    pageUpdate.beginDictionary();
    pageUpdate.beginDictionaryItem("VP");
    pageUpdate << createArray({ viewport(QRectF(0, 0, 300, 300), "Plan", createViewportMeasure("mm", 100.0 * 25.4 / 72.0, "RL", "1:100")),
                                viewport(QRectF(0, 0, 100, 100), "Map", createViewportMeasure("mm", 1.0, "GEO", "")),
                                viewport(QRectF(200, 200, 200, 200), "Detail", createViewportMeasure("mm", 20.0 * 25.4 / 72.0, "RL", "1:20")) });
    pageUpdate.endDictionaryItem();
    pageUpdate.endDictionary();
    builder.mergeTo(page, pageUpdate.takeObject());
    const pdf::PDFDocument document = builder.build();

    const QString measurements = "["
                                 "{\"page\": 1, \"kind\": \"distance\", \"points\": [[250, 250], [322, 250]]},"
                                 "{\"page\": 1, \"kind\": \"distance\", \"points\": [[50, 50], [122, 50]]},"
                                 "{\"page\": 1, \"kind\": \"distance\", \"points\": [[500, 500], [572, 500]], \"scale\": {\"paperValue\": 1, \"paperUnit\": \"mm\", \"realValue\": 50, \"realUnit\": \"mm\"}},"
                                 "{\"page\": 1, \"kind\": \"distance\", \"points\": [[500, 600], [572, 600]]}"
                                 "]";
    QVariantMap parameters;
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MODE, QString("list"));
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MEASUREMENTS, measurements);
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_LENGTH_UNIT, QString("mm"));
    const pdf::PDFDocumentToolResult result = runMeasure(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const QJsonArray rows = result.data.value(pdf::PDFDocumentToolMeasure::DATA_MEASUREMENTS).toArray();
    QCOMPARE(rows.size(), 4);
    const pdf::PDFReal expected[] = { 25.4 * 20.0, 25.4 * 100.0, 25.4 * 50.0, 25.4 };
    const char* viewports[] = { "Detail", "Plan", "", "" };
    for (int i = 0; i < 4; ++i)
    {
        const QJsonObject row = rows.at(i).toObject();
        QVERIFY2(qAbs(row.value("value").toDouble() - expected[i]) < 1e-9, qPrintable(QString::number(i)));
        QCOMPARE(row.value("unit").toString(), QString("mm"));
        QCOMPARE(row.value("viewport").toString(), QString(viewports[i]));
        QCOMPARE(row.value("source").toString(), QString("new"));
    }
    QCOMPARE(rows.at(0).toObject().value("text").toString(), QString("508.00 mm"));

    // The viewports of the page with their subtypes (a /GEO one is listed)
    const QJsonArray viewportRows = result.data.value(pdf::PDFDocumentToolMeasure::DATA_VIEWPORTS).toArray();
    QCOMPARE(viewportRows.size(), 3);
    QCOMPARE(viewportRows.at(1).toObject().value("subtype").toString(), QString("GEO"));
    QCOMPARE(viewportRows.at(2).toObject().value("name").toString(), QString("Detail"));
    QVERIFY(!result.data.value(pdf::PDFDocumentToolMeasure::DATA_TRUNCATED).toBool());
}

void MeasureTest::test_tool_csv()
{
    // An existing measurement annotation (1:10, 72 pt = 254 mm) with a hostile /R and a new measurement
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 1.0);
    pdf::PDFMeasureDisplayUnits units;
    units.length = "mm";
    const pdf::PDFObjectReference annotation = pdf::PDFMeasureWriter::createAnnotation(&builder, page, pdf::PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) },
                                                                                       pdf::PDFMeasureScale(1.0, "mm", 10.0, "mm"), units, pdf::PDFMeasureAnnotationStyle(), nullptr);
    QVERIFY(annotation.isValid());
    const pdf::PDFDictionary* annotationDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(annotation));
    pdf::PDFDictionaryBuilder measure(*builder.getDictionaryFromObject(annotationDictionary->get("Measure")));
    measure.setEntry(pdf::PDFInplaceOrMemoryString("R"), pdf::PDFObject::createString(QByteArray("=1+1")));
    pdf::PDFDictionaryBuilder updatedAnnotation(*annotationDictionary);
    updatedAnnotation.setEntry(pdf::PDFInplaceOrMemoryString("Measure"), pdf::PDFObject::createDictionary(std::move(measure)));
    builder.setObject(annotation, pdf::PDFObject::createDictionary(std::move(updatedAnnotation)));
    const pdf::PDFDocument document = builder.build();

    QVariantMap parameters;
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MODE, QString("csv"));
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_LENGTH_UNIT, QString("mm"));
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MEASUREMENTS, QString("[{\"page\": 1, \"kind\": \"distance\", \"points\": [[0, 0], [72, 0]], "
                                                                                   "\"scale\": {\"paperValue\": 1, \"paperUnit\": \"mm\", \"realValue\": 10, \"realUnit\": \"mm\"}}]"));
    const pdf::PDFDocumentToolResult result = runMeasure(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.outputs.size(), size_t(1));
    QCOMPARE(result.outputs.front().suggestedFileName, QString("Floor_Plan - measurements.csv"));

    // Q7.20, 473:91038: Page; Type; Value; Unit; Text; Scale - a semicolon, a dot, UTF-8 without a BOM, RFC 4180
    // CRLF; the existing annotation first on its page; a text of the document is escaped against formulas (B182)
    const QString csv = QString::fromUtf8(result.outputs.front().data);
    const QStringList lines = csv.split("\r\n", Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines[0], QString("Page;Type;Value;Unit;Text;Scale"));
    QCOMPARE(lines[1], QString("1;Length;254.0000;mm;254.00 mm;'=1+1"));
    QCOMPARE(lines[2], QString("1;Length;254.0000;mm;254.00 mm;1 mm = 10 mm"));
    QVERIFY(!result.outputs.front().data.startsWith("\xEF\xBB\xBF"));

    // The export of the GUI writes the same table of the data without a run
    QString errorMessage;
    const pdf::PDFDocumentToolOutputFile output = pdf::PDFDocumentToolMeasure::writeOutput(result.data, QVariantMap(), pdf::PDFDocumentToolTableWriter::Options(), "Plan", &errorMessage);
    QCOMPARE(output.suggestedFileName, QString("Plan.csv"));
    QCOMPARE(output.data, result.outputs.front().data);
}

void MeasureTest::test_tool_add_certification()
{
    // 12.8.2.2, Table 257: a certification of P = 1 or 2 allows no new annotation, P = 3 does
    auto createDocument = [](int certification)
    {
        pdf::PDFDocumentBuilder builder;
        addPage(builder, 1.0);
        if (certification > 0)
        {
            pdf::PDFObjectFactory signature;
            signature.beginDictionary();
            signature.beginDictionaryItem("Type");
            signature << pdf::WrapName("Sig");
            signature.endDictionaryItem();
            signature.beginDictionaryItem("Reference");
            signature.beginArray();
            signature.beginDictionary();
            signature.beginDictionaryItem("TransformMethod");
            signature << pdf::WrapName("DocMDP");
            signature.endDictionaryItem();
            signature.beginDictionaryItem("TransformParams");
            signature.beginDictionary();
            signature.beginDictionaryItem("P");
            signature << pdf::PDFInteger(certification);
            signature.endDictionaryItem();
            signature.endDictionary();
            signature.endDictionaryItem();
            signature.endDictionary();
            signature.endArray();
            signature.endDictionaryItem();
            signature.endDictionary();
            const pdf::PDFObjectReference signatureReference = builder.addObject(signature.takeObject());

            pdf::PDFObjectFactory catalog;
            catalog.beginDictionary();
            catalog.beginDictionaryItem("Perms");
            catalog.beginDictionary();
            catalog.beginDictionaryItem("DocMDP");
            catalog << signatureReference;
            catalog.endDictionaryItem();
            catalog.endDictionary();
            catalog.endDictionaryItem();
            catalog.endDictionary();
            builder.mergeTo(builder.getCatalogReference(), catalog.takeObject());
        }
        return builder.build();
    };

    QVariantMap parameters;
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MODE, QString("add"));
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MEASUREMENTS, QString("[{\"page\": 1, \"kind\": \"distance\", \"points\": [[0, 0], [72, 0]]},"
                                                                                   "{\"page\": 1, \"kind\": \"angle\", \"points\": [[72, 0], [0, 0], [0, 72]]}]"));

    {
        pdf::PDFDocumentToolMeasure definitionTool;
        QString definitionError;
        QVERIFY2(pdf::PDFDocumentToolParameters::isValid(definitionTool.getParameters(), &definitionError), qPrintable(definitionError));
    }

    for (int certification : { 1, 2 })
    {
        const pdf::PDFDocument document = createDocument(certification);
        QVERIFY(!pdf::PDFDocumentToolMeasure::isAnnotationAllowedByCertification(&document));
        QVERIFY(runMeasure(document, parameters).status == pdf::PDFDocumentToolResultStatus::Failed);
    }

    for (int certification : { 0, 3 })
    {
        const pdf::PDFDocument document = createDocument(certification);
        QVERIFY(pdf::PDFDocumentToolMeasure::isAnnotationAllowedByCertification(&document));
        const pdf::PDFDocumentToolResult result = runMeasure(document, parameters);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(result.data.value(pdf::PDFDocumentToolMeasure::DATA_ADDED).toInt(), 2);
        QVERIFY(result.data.value(pdf::PDFDocumentToolMeasure::DATA_FAILED).toArray().isEmpty());
        QCOMPARE(result.documents.size(), size_t(1));
        QCOMPARE(result.documents.front().document->getCatalog()->getPage(0)->getAnnotations().size(), size_t(2));
        QVERIFY(result.documents.front().flags.testFlag(pdf::PDFModifiedDocument::Annotation));
    }

    // The kinds of the tool: add = ChangeDocument with bit 6, list = Analysis without (Q7.2)
    pdf::PDFDocumentToolMeasure tool;
    QVERIFY(tool.getResultKind(parameters) == pdf::PDFDocumentToolResultKind::ChangeDocument);
    const std::vector<pdf::PDFSecurityHandler::Permission> permissions = tool.getRequiredPermissions(parameters);
    QVERIFY(std::find(permissions.begin(), permissions.end(), pdf::PDFSecurityHandler::Permission::ModifyInteractiveItems) != permissions.end());
    QVariantMap listParameters;
    listParameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MODE, QString("list"));
    QVERIFY(tool.getResultKind(listParameters) == pdf::PDFDocumentToolResultKind::Analysis);

    // Nothing to add is refused
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MEASUREMENTS, QString());
    QVERIFY(runMeasure(createDocument(0), parameters).status == pdf::PDFDocumentToolResultStatus::Failed);
}

void MeasureTest::test_hostile_measure_texts()
{
    // Review RS7CCORE of step 7c: the texts of a document are untrusted and an object may be referred to many times.
    // One number format with a long label (Table 268 /U, a universally recognised abbreviation) referred to by every
    // element of /D (Table 267: a few units in descending order), a long thousands separator, a long /R, an /O of
    // many numbers (Table 267: two numbers), a long /Contents and /T shared by the annotations and a /VP of many
    // references to one viewport would be copied into every annotation, every formatted value and every point
    // looked up (a file of megabytes, texts of gigabytes). Measure reads all of them when it opens.
    using pdf::PDFObjectFactory;

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 1.0);

    const QString longText(2000, QChar('x'));
    const QString longSeparator(500, QChar(','));
    const pdf::PDFObjectReference format = builder.addObject(createNumberFormat(longText, 1.0, nullptr, -1, false, nullptr, nullptr, &longSeparator));
    auto createTextObject = [](const QString& text)
    {
        PDFObjectFactory factory;
        factory << text;
        return factory.takeObject();
    };
    const pdf::PDFObjectReference sharedText = builder.addObject(createTextObject(longText));

    PDFObjectFactory measureFactory;
    measureFactory.beginDictionary();
    measureFactory.beginDictionaryItem("Type");
    measureFactory << pdf::WrapName("Measure");
    measureFactory.endDictionaryItem();
    measureFactory.beginDictionaryItem("Subtype");
    measureFactory << pdf::WrapName("RL");
    measureFactory.endDictionaryItem();
    measureFactory.beginDictionaryItem("R");
    measureFactory << longText;
    measureFactory.endDictionaryItem();
    measureFactory.beginDictionaryItem("X");
    measureFactory << createArray({ pdf::PDFObject::createReference(format) });
    measureFactory.endDictionaryItem();
    measureFactory.beginDictionaryItem("D");
    measureFactory << createArray(std::vector<pdf::PDFObject>(100, pdf::PDFObject::createReference(format)));
    measureFactory.endDictionaryItem();
    measureFactory.beginDictionaryItem("A");
    measureFactory << createArray({ pdf::PDFObject::createReference(format) });
    measureFactory.endDictionaryItem();
    measureFactory.beginDictionaryItem("O");
    measureFactory << createArray(std::vector<pdf::PDFObject>(1000, pdf::PDFObject::createReal(1.0)));
    measureFactory.endDictionaryItem();
    measureFactory.endDictionary();
    const pdf::PDFObjectReference measure = builder.addObject(measureFactory.takeObject());

    // A distance with the shared measure (its text by /D) and a distance without a measure (its text /Contents)
    auto addDistance = [&](bool hasMeasure)
    {
        const pdf::PDFObjectReference annotation = builder.createAnnotationLine(page, QRectF(0, 0, 72, 10), QPointF(0, 5), QPointF(72, 5), 1.0, Qt::black, Qt::black, "Author", "Length", "1 in",
                                                                                pdf::AnnotationLineEnding::Butt, pdf::AnnotationLineEnding::Butt);
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("IT");
        factory << pdf::WrapName("LineDimension");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("T");
        factory << sharedText;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Contents");
        factory << sharedText;
        factory.endDictionaryItem();
        if (hasMeasure)
        {
            factory.beginDictionaryItem("Measure");
            factory << measure;
            factory.endDictionaryItem();
        }
        factory.endDictionary();
        builder.mergeTo(annotation, factory.takeObject());
    };
    addDistance(true);
    addDistance(false);

    // /VP: 1 100 references to one viewport of a long name, then the last viewport
    auto createViewport = [&](const QString& name)
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("BBox");
        factory << QRectF(0, 0, 100, 100);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Name");
        factory << name;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Measure");
        factory << createViewportMeasure("mm", 25.4 / 72.0, "RL", "1:1");
        factory.endDictionaryItem();
        factory.endDictionary();
        return factory.takeObject();
    };
    std::vector<pdf::PDFObject> viewports(1100, pdf::PDFObject::createReference(builder.addObject(createViewport(longText))));
    viewports.push_back(createViewport("Last"));
    PDFObjectFactory pageUpdate;
    pageUpdate.beginDictionary();
    pageUpdate.beginDictionaryItem("VP");
    pageUpdate << createArray(viewports);
    pageUpdate.endDictionaryItem();
    pageUpdate.endDictionary();
    builder.mergeTo(page, pageUpdate.takeObject());
    const pdf::PDFDocument document = builder.build();
    const pdf::PDFObjectStorage* storage = &document.getStorage();

    // The measure: the first dictionaries of an array, cut texts, the two numbers of the origin
    const pdf::PDFMeasure parsedMeasure = pdf::PDFMeasure::parse(storage, pdf::PDFObject::createReference(measure));
    QCOMPARE(parsedMeasure.getDistanceFormat().size(), pdf::PDFNumberFormat::MAXIMUM_ARRAY_LENGTH);
    QCOMPARE(parsedMeasure.getUnitLabel(), longText.left(pdf::PDFNumberFormat::MAXIMUM_TEXT_LENGTH));
    QCOMPARE(parsedMeasure.getXFormat().front().getThousandSeparator(), longSeparator.left(pdf::PDFNumberFormat::MAXIMUM_TEXT_LENGTH));
    QCOMPARE(parsedMeasure.getScaleRatio(), longText.left(pdf::PDFMeasure::MAXIMUM_RATIO_LENGTH));
    QCOMPARE(parsedMeasure.getOrigin(), std::vector<pdf::PDFReal>({ 1.0, 1.0 }));

    // The annotations: the value by the geometry (72 pt, C 1), the kept texts cut
    const std::vector<pdf::PDFMeasureAnnotationInfo> infos = pdf::PDFMeasureAnnotationInfo::readPages(&document, { 0 }, 100, 200, nullptr, nullptr);
    QCOMPARE(infos.size(), size_t(2));
    QVERIFY(infos[0].hasMeasure);
    QCOMPARE(infos[0].value, 72.0);
    QVERIFY(infos[0].text.startsWith(QStringLiteral("72 xxxx")));
    QVERIFY(!infos[1].hasMeasure);
    QCOMPARE(infos[1].text, longText.left(pdf::PDFMeasureAnnotationInfo::MAXIMUM_TEXT_LENGTH));
    for (const pdf::PDFMeasureAnnotationInfo& info : infos)
    {
        QVERIFY(info.text.size() <= pdf::PDFMeasureAnnotationInfo::MAXIMUM_TEXT_LENGTH);
        QCOMPARE(info.author, longText.left(pdf::PDFMeasureAnnotationInfo::MAXIMUM_TEXT_LENGTH));
        QVERIFY(info.ratio.size() <= pdf::PDFMeasure::MAXIMUM_RATIO_LENGTH);
        QVERIFY(info.unitLabel.size() <= pdf::PDFNumberFormat::MAXIMUM_TEXT_LENGTH);
    }

    // The viewports: the last ones of /VP (12.9.2: the last one containing a point wins), their names cut
    const std::vector<pdf::PDFViewport> parsedViewports = pdf::PDFViewport::parseViewports(storage, document.getCatalog()->getPage(0)->getViewports(storage));
    QCOMPARE(parsedViewports.size(), pdf::PDFViewport::MAXIMUM_VIEWPORT_COUNT);
    QCOMPARE(parsedViewports.back().getName(), QStringLiteral("Last"));
    QCOMPARE(parsedViewports.front().getName(), longText.left(pdf::PDFViewport::MAXIMUM_NAME_LENGTH));
    QCOMPARE(pdf::PDFViewport::findViewportForPoint(parsedViewports, QPointF(50, 50))->getName(), QStringLiteral("Last"));
}

/// A control of an operation, which is canceled from its start (the reading of a closed tab)
class MeasureTestCanceledControl : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCanceled() const override { return true; }
};

void MeasureTest::test_writer_pdfa_opaque()
{
    // Owner 2026-11-19 (review RS7CCORE M3 "Neprůhledně"): PDF/A-1 allows no transparency (ISO 19005-1, 6.4: /CA and
    // /ca of an ExtGState 1.0, no soft mask). A document declaring PDF/A gets measurement annotations without any
    // transparency - the label colors composited over white (black 10 % = gray 0.9), an area as its outline only (no
    // /IC, no fill) - and keeps its declaration; a plain document keeps the look of 471:5490 (black 10 %, the area
    // filled)
    static const QByteArray xmp = "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
                                  "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                                  "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\">"
                                  "<pdfaid:part>1</pdfaid:part><pdfaid:conformance>B</pdfaid:conformance>"
                                  "</rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";

    QVariantMap parameters;
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MODE, QString("add"));
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MEASUREMENTS, QString("[{\"page\": 1, \"kind\": \"distance\", \"points\": [[100, 100], [172, 100]]},"
                                                                                   "{\"page\": 1, \"kind\": \"area\", \"points\": [[100, 200], [300, 200], [300, 400], [100, 400]]}]"));

    for (const bool isPdfA : { true, false })
    {
        pdf::PDFDocumentBuilder builder;
        addPage(builder, 1.0);
        if (isPdfA)
        {
            builder.setCatalogMetadata(xmp);
        }
        const pdf::PDFDocument document = builder.build();
        QCOMPARE(pdf::PDFDocumentTool::getConformanceDeclarations(&document).contains(QStringLiteral("PDF/A")), isPdfA);

        const pdf::PDFDocumentToolResult result = runMeasure(document, parameters);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(result.documents.size(), size_t(1));
        const pdf::PDFDocument& changed = *result.documents.front().document;

        // The declaration stays
        QCOMPARE(pdf::PDFDocumentTool::getConformanceDeclarations(&changed).contains(QStringLiteral("PDF/A")), isPdfA);

        const std::vector<pdf::PDFObjectReference> annotations = changed.getCatalog()->getPage(0)->getAnnotations();
        QCOMPARE(annotations.size(), size_t(2));
        bool isTransparent = false;
        bool isAreaFilled = false;
        bool hasOpaqueGrayLabel = false;
        for (const pdf::PDFObjectReference& reference : annotations)
        {
            const pdf::PDFDictionary* annotation = getDictionary(changed, reference);
            QVERIFY(annotation);
            const bool isArea = changed.getObject(annotation->get("Subtype")).getString() == "Polygon";

            // The dictionary of the annotation (Table 166 /CA, Table 174 /ca)
            for (const char* key : { "CA", "ca" })
            {
                const pdf::PDFObject value = changed.getObject(annotation->get(key));
                isTransparent = isTransparent || (value.isReal() && value.getReal() < 1.0) || (value.isInt() && value.getInteger() < 1);
            }
            isAreaFilled = isAreaFilled || (isArea && annotation->hasKey("IC"));

            // The appearance stream: its graphics states (Table 57 /CA, /ca, /SMask) and the colors it fills with
            const pdf::PDFDictionary* appearances = changed.getDictionaryFromObject(annotation->get("AP"));
            QVERIFY(appearances);
            const pdf::PDFObject normal = changed.getObject(appearances->get("N"));
            QVERIFY(normal.isStream());
            const pdf::PDFDictionary* resources = changed.getDictionaryFromObject(normal.getStream()->getDictionary()->get("Resources"));
            const pdf::PDFDictionary* states = resources ? changed.getDictionaryFromObject(resources->get("ExtGState")) : nullptr;
            for (size_t i = 0; states && i < states->getCount(); ++i)
            {
                const pdf::PDFDictionary* state = changed.getDictionaryFromObject(states->getValue(i));
                QVERIFY(state);
                for (const char* key : { "CA", "ca" })
                {
                    const pdf::PDFObject value = changed.getObject(state->get(key));
                    isTransparent = isTransparent || (value.isReal() && value.getReal() < 1.0) || (value.isInt() && value.getInteger() < 1);
                }
                const pdf::PDFObject softMask = changed.getObject(state->get("SMask"));
                isTransparent = isTransparent || (!softMask.isNull() && !(softMask.isName() && softMask.getString() == "None"));
            }

            const QString content = QString::fromLatin1(changed.getDecodedStream(normal.getStream()));
            if (isArea)
            {
                // The path of the polygon (its first corner 100 200, ISO 32000-2, 8.5.3: the painting operator after
                // the path): S strokes only, f, f*, B, B*, b, b* fill too
                const QRegularExpressionMatch path = QRegularExpression(QStringLiteral("(?:^|\\s)100(?:\\.0+)? 200(?:\\.0+)? m[^a-zA-Z]*(?:l[^a-zA-Z]*)*h\\s+([a-zA-Z*]+)")).match(content);
                QVERIFY2(path.hasMatch(), qPrintable(content.left(500)));
                const QString paint = path.captured(1);
                isAreaFilled = isAreaFilled || paint.contains(QLatin1Char('f')) || paint.contains(QLatin1Char('B')) || paint.contains(QLatin1Char('b'));
            }
            const QRegularExpression fillColor(QStringLiteral("([0-9.]+)\\s+([0-9.]+)\\s+([0-9.]+)\\s+(rg|sc|scn)\\b"));
            for (QRegularExpressionMatchIterator it = fillColor.globalMatch(content); it.hasNext();)
            {
                const QRegularExpressionMatch match = it.next();
                const double red = match.captured(1).toDouble();
                const double green = match.captured(2).toDouble();
                const double blue = match.captured(3).toDouble();
                hasOpaqueGrayLabel = hasOpaqueGrayLabel || (qAbs(red - 0.9) <= 1.0 / 255.0 && qAbs(green - 0.9) <= 1.0 / 255.0 && qAbs(blue - 0.9) <= 1.0 / 255.0);
            }
        }

        QCOMPARE(isTransparent, !isPdfA);
        QCOMPARE(isAreaFilled, !isPdfA);
        QCOMPARE(hasOpaqueGrayLabel, isPdfA);
    }

    // The composition over white (ISO 19005-1, 6.4: the color as it looks on the paper): black 10 % is gray 0.9,
    // an opaque color stays, a document without PDF/A keeps the alpha
    const QColor gray = pdf::PDFMeasureAnnotationStyle::getWrittenColor(QColor(0, 0, 0, 26), true);
    QCOMPARE(gray.alpha(), 255);
    QVERIFY(qAbs(gray.redF() - (1.0 - 26.0 / 255.0)) <= 1.0 / 255.0);
    QCOMPARE(pdf::PDFMeasureAnnotationStyle::getWrittenColor(QColor(255, 0, 0, 128), true), QColor(255, 127, 127));
    QCOMPARE(pdf::PDFMeasureAnnotationStyle::getWrittenColor(QColor(0, 0, 255), true), QColor(0, 0, 255));
    QCOMPARE(pdf::PDFMeasureAnnotationStyle::getWrittenColor(QColor(0, 0, 0, 26), false), QColor(0, 0, 0, 26));
}

void MeasureTest::test_tool_points_outside_page()
{
    // Review RS7CCORE L4: a measurement of MCP lies on its page - a point outside of the MediaBox of its page (ISO
    // 32000-2, 14.11.2: the boundaries of the physical medium) is refused with the number of the measurement; the
    // edges belong to the page
    pdf::PDFDocumentBuilder builder;
    addPage(builder, 1.0);
    const pdf::PDFDocument document = builder.build();

    auto run = [&document](const QString& measurements)
    {
        QVariantMap parameters;
        parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MODE, QString("add"));
        parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MEASUREMENTS, measurements);
        return runMeasure(document, parameters);
    };

    const QString inside = "{\"page\": 1, \"kind\": \"distance\", \"points\": [[0, 0], [595, 842]]}";
    QVERIFY2(run("[" + inside + "]").isSuccess(), "the corners of the MediaBox lie on the page");

    const QStringList outside = { "{\"page\": 1, \"kind\": \"distance\", \"points\": [[100, 100], [596, 100]]}",
                                  "{\"page\": 1, \"kind\": \"distance\", \"points\": [[-0.5, 100], [100, 100]]}",
                                  "{\"page\": 1, \"kind\": \"area\", \"points\": [[100, 100], [200, 100], [200, 843]]}",
                                  "{\"page\": 1, \"kind\": \"angle\", \"points\": [[100, -1], [100, 100], [200, 100]]}" };
    for (const QString& measurement : outside)
    {
        const pdf::PDFDocumentToolResult result = run("[" + inside + "," + measurement + "]");
        QVERIFY2(result.status == pdf::PDFDocumentToolResultStatus::Failed, qPrintable(measurement));
        QVERIFY2(result.message.startsWith(QStringLiteral("Measurement 2: ")), qPrintable(result.message));
        QVERIFY2(result.message.contains(QStringLiteral("outside of the page")), qPrintable(result.message));
        QVERIFY(result.documents.empty());
    }
}

void MeasureTest::test_read_many_vertices()
{
    // Review RS7CCORE L6: the measurement annotations of other producers (and of the old plugin) may have more vertices
    // than the tool writes (200): they are read up to PDFMeasureAnnotationInfo::MAXIMUM_READ_POINT_COUNT; all of them
    // together at most MAXIMUM_TOTAL_POINT_COUNT - one /Vertices array shared by many annotations is copied into each
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 1.0);

    auto createVertices = [](int count)
    {
        // A closed zigzag along the page: 2 numbers per vertex
        std::vector<pdf::PDFObject> numbers;
        for (int i = 0; i < count; ++i)
        {
            numbers.push_back(pdf::PDFObject::createReal(10.0 + 500.0 * i / count));
            numbers.push_back(pdf::PDFObject::createReal(i % 2 == 0 ? 10.0 : 20.0));
        }
        return createArray(numbers);
    };
    auto createPolygon = [&builder, page](const pdf::PDFObject& vertices)
    {
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("Annot");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << pdf::WrapName("Polygon");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Rect");
        factory << QRectF(0, 0, 595, 842);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("IT");
        factory << pdf::WrapName("PolygonDimension");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("P");
        factory << page;
        factory.endDictionaryItem();
        factory.endDictionary();
        pdf::PDFDictionaryBuilder dictionary(*factory.takeObject().getDictionary());
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Vertices"), pdf::PDFObject(vertices));
        return builder.addObject(pdf::PDFObject::createDictionary(std::move(dictionary)));
    };

    // 500 vertices (more than the 200 of the tool) are read; 10 001 are not (more than the reading limit)
    std::vector<pdf::PDFObject> annotations;
    annotations.push_back(pdf::PDFObject::createReference(createPolygon(createVertices(500))));
    annotations.push_back(pdf::PDFObject::createReference(createPolygon(createVertices(pdf::PDFMeasureAnnotationInfo::MAXIMUM_READ_POINT_COUNT + 1))));

    // One array of 10 000 vertices shared by 100 annotations: after the 500 vertices 99 of them fit into the budget of
    // 1 000 000 vertices ((1 000 000 - 500) / 10 000), the 100th is truncated
    const pdf::PDFObjectReference sharedVertices = builder.addObject(createVertices(pdf::PDFMeasureAnnotationInfo::MAXIMUM_READ_POINT_COUNT));
    const size_t sharedCount = (pdf::PDFMeasureAnnotationInfo::MAXIMUM_TOTAL_POINT_COUNT - 500) / size_t(pdf::PDFMeasureAnnotationInfo::MAXIMUM_READ_POINT_COUNT);
    QCOMPARE(sharedCount, size_t(99));
    for (size_t i = 0; i < sharedCount + 1; ++i)
    {
        annotations.push_back(pdf::PDFObject::createReference(createPolygon(pdf::PDFObject::createReference(sharedVertices))));
    }

    pdf::PDFObjectFactory pageUpdate;
    pageUpdate.beginDictionary();
    pageUpdate.beginDictionaryItem("Annots");
    pageUpdate << createArray(annotations);
    pageUpdate.endDictionaryItem();
    pageUpdate.endDictionary();
    builder.mergeTo(page, pageUpdate.takeObject());
    const pdf::PDFDocument document = builder.build();

    bool isTruncated = false;
    const std::vector<pdf::PDFMeasureAnnotationInfo> infos = pdf::PDFMeasureAnnotationInfo::readPages(&document, { 0 }, 100000, pdf::PDFMeasureAnnotationInfo::MAXIMUM_READ_POINT_COUNT,
                                                                                                       &isTruncated, nullptr);
    QCOMPARE(infos.size(), 1 + sharedCount);
    QCOMPARE(infos.front().kind, pdf::PDFMeasureKind::Area);
    QCOMPARE(infos.front().points.size(), size_t(500));
    QCOMPARE(infos.back().points.size(), size_t(pdf::PDFMeasureAnnotationInfo::MAXIMUM_READ_POINT_COUNT));
    QVERIFY(isTruncated);
    size_t pointCount = 0;
    for (const pdf::PDFMeasureAnnotationInfo& info : infos)
    {
        pointCount += info.points.size();
    }
    QVERIFY(pointCount <= pdf::PDFMeasureAnnotationInfo::MAXIMUM_TOTAL_POINT_COUNT);

    // The list of the tool has the annotation of 500 vertices (the old plugin listed it)
    QVariantMap parameters;
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MODE, QString("list"));
    const pdf::PDFDocumentToolResult result = runMeasure(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QJsonArray rows = result.data.value(pdf::PDFDocumentToolMeasure::DATA_MEASUREMENTS).toArray();
    QVERIFY(!rows.isEmpty());
    QCOMPARE(rows.at(0).toObject().value("kind").toString(), QString("area"));
    QVERIFY(result.data.value(pdf::PDFDocumentToolMeasure::DATA_TRUNCATED).toBool());
}

void MeasureTest::test_read_canceled()
{
    // Review RS7CCORE L9: the reading of the annotations stops by its control - a canceled list is Stopped, never a list
    // of fewer measurements
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = addPage(builder, 1.0);
    QVERIFY(pdf::PDFMeasureWriter::createAnnotation(&builder, page, pdf::PDFMeasureKind::Distance, { QPointF(0, 0), QPointF(72, 0) }, pdf::PDFMeasureScale::createIdentity(),
                                                    pdf::PDFMeasureDisplayUnits(), pdf::PDFMeasureAnnotationStyle(), nullptr).isValid());
    const pdf::PDFDocument document = builder.build();
    const MeasureTestCanceledControl canceled;

    QCOMPARE(pdf::PDFMeasureAnnotationInfo::readPages(&document, { 0 }, 100, 200, nullptr, nullptr).size(), size_t(1));
    QVERIFY(pdf::PDFMeasureAnnotationInfo::readPages(&document, { 0 }, 100, 200, nullptr, &canceled).empty());
    const QString rows = QString::fromLatin1(pdf::PDFDocumentToolMeasure::DATA_MEASUREMENTS);
    QCOMPARE(pdf::PDFDocumentToolMeasure::createData(&document, { 0 }, { }, pdf::PDFMeasureDisplayUnits(), nullptr).value(rows).toArray().size(), 1);
    QVERIFY(pdf::PDFDocumentToolMeasure::createData(&document, { 0 }, { }, pdf::PDFMeasureDisplayUnits(), &canceled).value(rows).toArray().isEmpty());

    pdf::PDFDocumentToolMeasure tool;
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), QByteArray(),
                                                                                                                              QStringLiteral("Floor_Plan.pdf"), true));
    QVariantMap parameters;
    parameters.insert(pdf::PDFDocumentToolMeasure::PARAMETER_MODE, QString("list"));
    QVERIFY(tool.run(context, parameters, nullptr, nullptr).isSuccess());
    QCOMPARE(tool.run(context, parameters, &canceled, nullptr).status, pdf::PDFDocumentToolResultStatus::Stopped);
}

// QTEST_MAIN: the labels of the measurement annotations are text converted to curves (QFont)
QTEST_MAIN(MeasureTest)

#include "tst_measuretest.moc"
