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

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include "dimensionsettings.h"
#include "dimensionunits.h"

#include "pdfdocumentbuilder.h"
#include "pdfmeasure.h"

class DimensionsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void test_unit_conversion_factors();
    void test_scale_factor();
    void test_scale_validity();
    void test_scale_serialization();
    void test_scale_from_measure();
    void test_scale_from_measure_user_unit();
    void test_scale_from_measure_rejected();
    void test_settings_persistence();
    void test_presets_persistence();
    void test_presets_can_be_emptied();
    void test_document_scale_by_path();
    void test_document_scale_by_permanent_id();
    void test_document_scale_path_has_priority();
    void test_document_identity_canonical_path();

private:
    /// Creates a measure dictionary with a single number format on each axis
    /// \param unitLabel Unit label of the x axis
    /// \param factorX Count of units per one default user space unit along the x axis
    /// \param factorY Count of units per one default user space unit along the y axis, negative means no Y entry
    /// \param factorYX Value of the CYX entry
    static pdf::PDFObject createMeasure(const QString& unitLabel,
                                        pdf::PDFReal factorX,
                                        pdf::PDFReal factorY = -1.0,
                                        pdf::PDFReal factorYX = 1.0);

    static pdf::PDFObject createNumberFormat(const QString& unitLabel, pdf::PDFReal conversionFactor);

    /// Creates a file in the settings directory and returns its canonical path
    /// \param fileName Name of the file
    QString createFile(const QString& fileName);

    QTemporaryDir m_settingsDirectory;
    pdf::PDFObjectStorage m_storage;
};

pdf::PDFObject DimensionsTest::createNumberFormat(const QString& unitLabel, pdf::PDFReal conversionFactor)
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
    factory.endDictionary();

    return factory.takeObject();
}

pdf::PDFObject DimensionsTest::createMeasure(const QString& unitLabel,
                                             pdf::PDFReal factorX,
                                             pdf::PDFReal factorY,
                                             pdf::PDFReal factorYX)
{
    pdf::PDFObjectFactory factory;

    factory.beginDictionary();

    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("Measure");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("RL");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("R");
    factory << QString("1 mm = 1 mm");
    factory.endDictionaryItem();

    factory.beginDictionaryItem("X");
    factory.beginArray();
    factory << createNumberFormat(unitLabel, factorX);
    factory.endArray();
    factory.endDictionaryItem();

    if (factorY > 0.0)
    {
        factory.beginDictionaryItem("Y");
        factory.beginArray();
        factory << createNumberFormat(unitLabel, factorY);
        factory.endArray();
        factory.endDictionaryItem();
    }

    factory.beginDictionaryItem("CYX");
    factory << factorYX;
    factory.endDictionaryItem();

    factory.endDictionary();

    return factory.takeObject();
}

QString DimensionsTest::createFile(const QString& fileName)
{
    const QString path = m_settingsDirectory.filePath(fileName);

    QFile file(path);
    if (file.open(QFile::WriteOnly))
    {
        file.write("%PDF-1.7\n");
        file.close();
    }

    return QFileInfo(path).canonicalFilePath();
}

void DimensionsTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());

    // The settings must not touch the real settings of the user
    QCoreApplication::setOrganizationName("PDF4QT_UnitTests");
    QCoreApplication::setApplicationName("DimensionsTest");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void DimensionsTest::test_unit_conversion_factors()
{
    // One point of the default user space is 1/72 inch, so the imperial units
    // must be derived from the inch by division (see issue #424)
    const DimensionUnit foot = DimensionUnit::getLengthUnit("ft");
    const DimensionUnit yard = DimensionUnit::getLengthUnit("yd");
    const DimensionUnit inch = DimensionUnit::getLengthUnit("in");

    QCOMPARE(foot.id, QByteArray("ft"));
    QCOMPARE(yard.id, QByteArray("yd"));

    // 1 foot is 864 points, 1 yard is 2592 points
    QVERIFY(qFuzzyCompare(1.0 / foot.scale, 864.0));
    QVERIFY(qFuzzyCompare(1.0 / yard.scale, 2592.0));
    QVERIFY(qFuzzyCompare(inch.scale / foot.scale, 12.0));

    // Millimeters must be consistent with the inch as well
    const DimensionUnit millimeter = DimensionUnit::getLengthUnit("mm");
    QVERIFY(qFuzzyCompare(millimeter.scale / inch.scale, 25.4));

    // Unknown identifier falls back to the default unit
    QCOMPARE(DimensionUnit::getLengthUnit("unknown").id, DimensionUnit::getLengthUnits().front().id);
}

void DimensionsTest::test_scale_factor()
{
    // Drawing in the scale 1:50 - a millimeter on the paper is 50 millimeters
    // in the reality
    QVERIFY(qFuzzyCompare(DimensionScale(1.0, "mm", 50.0, "mm").getScaleFactor(), 50.0));

    // The units of both sides may differ
    QVERIFY(qFuzzyCompare(DimensionScale(1.0, "mm", 1.0, "m").getScaleFactor(), 1000.0));
    QVERIFY(qFuzzyCompare(DimensionScale(1.0, "cm", 100.0, "m").getScaleFactor(), 10000.0));

    // Architectural scale 1/8" = 1'
    QVERIFY(qFuzzyCompare(DimensionScale(0.125, "in", 1.0, "ft").getScaleFactor(), 96.0));

    // Enlarged drawings must be possible as well
    QVERIFY(qFuzzyCompare(DimensionScale(2.0, "mm", 1.0, "mm").getScaleFactor(), 0.5));

    QVERIFY(qFuzzyCompare(DimensionScale::createIdentity().getScaleFactor(), 1.0));
}

void DimensionsTest::test_scale_validity()
{
    // A default constructed scale is the "no scale is known" value returned
    // from the whole plugin, it must never behave as the identity scale
    QVERIFY(!DimensionScale().isValid());
    QVERIFY(DimensionScale() != DimensionScale::createIdentity());
    QVERIFY(DimensionScale::createIdentity().isValid());

    // Unknown units are not accepted
    QVERIFY(!DimensionScale(1.0, "parsec", 1.0, "mm").isValid());
    QVERIFY(!DimensionScale(1.0, "mm", 1.0, "parsec").isValid());

    // Zero or negative values are not accepted
    QVERIFY(!DimensionScale(0.0, "mm", 1.0, "mm").isValid());
    QVERIFY(!DimensionScale(1.0, "mm", -1.0, "mm").isValid());
}

void DimensionsTest::test_scale_serialization()
{
    DimensionScale scale(1.0, "mm", 50.0, "mm", QString("Site plan; \"main\""), QString("Description, with a comma"));

    DimensionScale restored = DimensionScale::fromStringList(scale.toStringList());

    QVERIFY(restored.isValid());
    QCOMPARE(restored, scale);
    QCOMPARE(restored.getName(), scale.getName());
    QCOMPARE(restored.getDescription(), scale.getDescription());
    QVERIFY(qFuzzyCompare(restored.getScaleFactor(), 50.0));

    // Malformed data must not produce a valid scale
    QVERIFY(!DimensionScale::fromStringList(QStringList()).isValid());
    QVERIFY(!DimensionScale::fromStringList(QStringList() << "x" << "mm" << "1" << "mm").isValid());
    QVERIFY(!DimensionScale::fromStringList(QStringList() << "1" << "nonsense" << "1" << "mm").isValid());
}

void DimensionsTest::test_scale_from_measure()
{
    // One default user space unit is 0.5 mm, so the drawing is reduced
    const pdf::PDFMeasure measure = pdf::PDFMeasure::parse(&m_storage, createMeasure("mm", 0.5));
    QVERIFY(measure.isValid());
    QVERIFY(!measure.isAnisotropic());

    const DimensionScale scale = DimensionScale::createFromMeasure(measure, 1.0);
    QVERIFY(scale.isValid());

    // A length of 100 points is 50 mm, which is 50 / (100 * PDF_POINT_TO_MM)
    // times the length on the paper
    const pdf::PDFReal expectedFactor = 0.5 / pdf::PDF_POINT_TO_MM;
    QVERIFY(qFuzzyCompare(scale.getScaleFactor(), expectedFactor));

    // The scale ratio of the document is offered as the name of the scale
    QCOMPARE(scale.getName(), QString("1 mm = 1 mm"));
}

void DimensionsTest::test_scale_from_measure_user_unit()
{
    // Measured lengths are multiplied by the UserUnit of the page before the
    // scale is applied, so the scale must compensate it. Otherwise a page with
    // UserUnit 2 would measure twice the real length (see issue #424).
    const pdf::PDFMeasure measure = pdf::PDFMeasure::parse(&m_storage, createMeasure("mm", 1.0));

    const DimensionScale scaleWithoutUserUnit = DimensionScale::createFromMeasure(measure, 1.0);
    const DimensionScale scaleWithUserUnit = DimensionScale::createFromMeasure(measure, 2.0);

    QVERIFY(scaleWithoutUserUnit.isValid());
    QVERIFY(scaleWithUserUnit.isValid());
    QVERIFY(qFuzzyCompare(scaleWithoutUserUnit.getScaleFactor(), 2.0 * scaleWithUserUnit.getScaleFactor()));

    // A line of 10 coordinate units on a page with UserUnit 2 is measured as
    // 20 points and must still be 10 mm
    const pdf::PDFReal measuredValue = 10.0 * 2.0;
    const pdf::PDFReal displayedValue = measuredValue * scaleWithUserUnit.getScaleFactor() * DimensionUnit::getLengthUnit("mm").scale;
    QVERIFY(qFuzzyCompare(displayedValue, 10.0));
}

void DimensionsTest::test_scale_from_measure_rejected()
{
    // Unit label, which cannot be interpreted
    const pdf::PDFMeasure unknownUnit = pdf::PDFMeasure::parse(&m_storage, createMeasure("smoots", 1.0));
    QVERIFY(!DimensionScale::createFromMeasure(unknownUnit, 1.0).isValid());

    // Anisotropic coordinate system cannot be expressed by a single scale factor
    const pdf::PDFMeasure anisotropic = pdf::PDFMeasure::parse(&m_storage, createMeasure("mm", 1.0, 2.0));
    QVERIFY(anisotropic.isAnisotropic());
    QVERIFY(!DimensionScale::createFromMeasure(anisotropic, 1.0).isValid());

    // The CYX factor makes the coordinate system anisotropic as well
    const pdf::PDFMeasure withFactorYX = pdf::PDFMeasure::parse(&m_storage, createMeasure("mm", 1.0, 1.0, 2.0));
    QVERIFY(withFactorYX.isAnisotropic());
    QVERIFY(!DimensionScale::createFromMeasure(withFactorYX, 1.0).isValid());

    // Invalid measure
    QVERIFY(!DimensionScale::createFromMeasure(pdf::PDFMeasure(), 1.0).isValid());
}

void DimensionsTest::test_settings_persistence()
{
    {
        pdfplugin::DimensionsSettingsStorage storage;
        storage.getSettings().lengthUnit = DimensionUnit::getLengthUnit("m");
        storage.getSettings().areaUnit = DimensionUnit::getAreaUnit("sqm");
        storage.getSettings().defaultScale = DimensionScale(1.0, "mm", 100.0, "mm", QString("Default"));
        storage.getSettings().storageMode = pdfplugin::DimensionsPluginSettings::StorageMode::Annotations;
        storage.getSettings().isScaleStoredPerDocument = false;
        storage.save();
    }

    pdfplugin::DimensionsSettingsStorage storage;
    storage.load();

    QCOMPARE(storage.getSettings().lengthUnit.id, QByteArray("m"));
    QCOMPARE(storage.getSettings().areaUnit.id, QByteArray("sqm"));
    QCOMPARE(storage.getSettings().storageMode, pdfplugin::DimensionsPluginSettings::StorageMode::Annotations);
    QCOMPARE(storage.getSettings().isScaleStoredPerDocument, false);
    QVERIFY(qFuzzyCompare(storage.getSettings().defaultScale.getScaleFactor(), 100.0));
    QCOMPARE(storage.getSettings().defaultScale.getName(), QString("Default"));
}

void DimensionsTest::test_presets_persistence()
{
    {
        pdfplugin::DimensionsSettingsStorage storage;
        storage.setPresets(std::vector<DimensionScale>());
        storage.addPreset(DimensionScale(1.0, "mm", 25.0, "mm", QString("Mine")));

        // A preset with the same name replaces the original one
        storage.addPreset(DimensionScale(1.0, "mm", 75.0, "mm", QString("Mine")));

        // An unnamed preset is not stored at all
        storage.addPreset(DimensionScale(1.0, "mm", 10.0, "mm"));

        QCOMPARE(storage.getPresets().size(), size_t(1));
        storage.save();
    }

    pdfplugin::DimensionsSettingsStorage storage;
    storage.load();

    QCOMPARE(storage.getPresets().size(), size_t(1));
    QCOMPARE(storage.getPresets().front().getName(), QString("Mine"));
    QVERIFY(qFuzzyCompare(storage.getPresets().front().getScaleFactor(), 75.0));
}

void DimensionsTest::test_presets_can_be_emptied()
{
    // The user is allowed to delete all the presets and the empty list must
    // survive the restart of the application
    {
        pdfplugin::DimensionsSettingsStorage storage;
        storage.setPresets(std::vector<DimensionScale>());
        storage.save();
    }

    pdfplugin::DimensionsSettingsStorage storage;
    storage.load();

    QVERIFY(storage.getPresets().empty());
}

void DimensionsTest::test_document_scale_by_path()
{
    pdfplugin::DocumentIdentity firstDocument;
    firstDocument.filePath = "/documents/first.pdf";

    pdfplugin::DocumentIdentity secondDocument;
    secondDocument.filePath = "/documents/second.pdf";

    pdfplugin::DimensionsSettingsStorage storage;
    storage.setDocumentScale(firstDocument, DimensionScale(1.0, "mm", 100.0, "mm"));

    QVERIFY(qFuzzyCompare(storage.getDocumentScale(firstDocument).getScaleFactor(), 100.0));

    // A document, which was never calibrated, must not inherit the scale
    // of another document (see issue #424)
    QVERIFY(!storage.getDocumentScale(secondDocument).isValid());
    QVERIFY(!storage.getDocumentScale(pdfplugin::DocumentIdentity()).isValid());
}

void DimensionsTest::test_document_scale_by_permanent_id()
{
    // A renamed file has to be recognized by its permanent identifier
    const QString originalPath = createFile("plan.pdf");
    const QString renamedPath = m_settingsDirectory.filePath("plan_renamed.pdf");

    pdfplugin::DocumentIdentity original;
    original.filePath = originalPath;
    original.permanentId = "abcdef";

    pdfplugin::DimensionsSettingsStorage storage;
    storage.setDocumentScale(original, DimensionScale(1.0, "mm", 50.0, "mm"));

    QVERIFY(QFile::rename(originalPath, renamedPath));

    pdfplugin::DocumentIdentity renamed;
    renamed.filePath = renamedPath;
    renamed.permanentId = "abcdef";

    QVERIFY(qFuzzyCompare(storage.getDocumentScale(renamed).getScaleFactor(), 50.0));

    // Storing the scale under the new path must move the existing record
    // instead of creating a second one
    storage.setDocumentScale(renamed, DimensionScale(1.0, "mm", 20.0, "mm"));
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(renamed).getScaleFactor(), 20.0));

    // A document without a file is identified by the identifier only
    pdfplugin::DocumentIdentity withoutPath;
    withoutPath.permanentId = "abcdef";
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(withoutPath).getScaleFactor(), 20.0));

    // A different identifier must not match anything
    pdfplugin::DocumentIdentity other;
    other.permanentId = "123456";
    QVERIFY(!storage.getDocumentScale(other).isValid());
}

void DimensionsTest::test_document_scale_path_has_priority()
{
    // Two different files may share the permanent identifier, when one of them
    // was created as a copy of the other one. As long as both files exist, they
    // must not share the calibrated scale.
    pdfplugin::DocumentIdentity first;
    first.filePath = createFile("a.pdf");
    first.permanentId = "shared";

    pdfplugin::DocumentIdentity second;
    second.filePath = createFile("b.pdf");
    second.permanentId = "shared";

    pdfplugin::DimensionsSettingsStorage storage;
    storage.setDocumentScale(first, DimensionScale(1.0, "mm", 100.0, "mm"));

    // The copy has not been calibrated yet and the original still exists,
    // so it must not silently use the scale of the original
    QVERIFY(!storage.getDocumentScale(second).isValid());

    storage.setDocumentScale(second, DimensionScale(1.0, "mm", 200.0, "mm"));

    QVERIFY(qFuzzyCompare(storage.getDocumentScale(first).getScaleFactor(), 100.0));
    QVERIFY(qFuzzyCompare(storage.getDocumentScale(second).getScaleFactor(), 200.0));
}

void DimensionsTest::test_document_identity_canonical_path()
{
    const QString fileName = m_settingsDirectory.filePath("document.pdf");
    QVERIFY(!createFile("document.pdf").isEmpty());

    // The path is canonicalized, so the same file opened through a different
    // spelling of the path is recognized as the same document
    const pdfplugin::DocumentIdentity direct = pdfplugin::DocumentIdentity::create(nullptr, fileName);
    const pdfplugin::DocumentIdentity indirect = pdfplugin::DocumentIdentity::create(nullptr, m_settingsDirectory.filePath("./document.pdf"));

    QVERIFY(direct.isValid());
    QCOMPARE(direct, indirect);
    QVERIFY(direct.permanentId.isEmpty());

    // A document, which has neither a file name nor an identifier, cannot
    // be identified
    QVERIFY(!pdfplugin::DocumentIdentity::create(nullptr, QString()).isValid());
}

QTEST_MAIN(DimensionsTest)

#include "tst_dimensionstest.moc"
