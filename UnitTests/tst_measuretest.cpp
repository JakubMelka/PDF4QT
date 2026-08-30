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

#include <QtTest>

#include "pdfdocumentbuilder.h"
#include "pdfmeasure.h"

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

    // Denominator 1 means no decimal places at all
    formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("mm", 1.0, "D", 1)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 1234.5), QString("1,234 mm"));

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

    const QString emptyPrefix;
    std::vector<pdf::PDFNumberFormat> formats = { pdf::PDFNumberFormat::parse(&storage, createNumberFormat("$", 1.0, "D", 100, false, "P", &emptyPrefix)) };
    QCOMPARE(pdf::PDFNumberFormat::format(formats, 12.5), QString("$12.50"));
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

QTEST_APPLESS_MAIN(MeasureTest)

#include "tst_measuretest.moc"
