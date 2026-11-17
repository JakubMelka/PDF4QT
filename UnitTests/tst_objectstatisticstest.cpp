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

#include "pdfdocumenttool_statistics.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "mergetestpdf.h"
#include "testlocaleguard.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <map>
#include <set>

using namespace pdf;

/// Helpers of the tests of Document Statistics (plan of step 7 of phase 2, 4.2, 8)
class ObjectStatisticsTestHelper
{
public:
    ObjectStatisticsTestHelper() = delete;

    using Classifier = PDFObjectClassifier;

    /// A damaged file: the objects of a page, but startxref points nowhere (ISO 32000-2, 7.5.5) - the permissive
    /// reader restores the document by scanning the file
    static QByteArray createDamagedPdf()
    {
        return "%PDF-1.7\n"
               "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
               "2 0 obj\n<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\nendobj\n"
               "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>\nendobj\n"
               "trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n99999999\n%%EOF\n";
    }

    /// The document of the classes: every class of the classifier with known members (ISO 32000-2 7.7.3
    /// pages, Table 31 /Contents, 7.8.3 resources with an inherited /Resources, 8.9.5 a soft mask, 8.10 a
    /// cycle of forms, 9.6.4 a Type 3 font in a tiling pattern sharing a form, 12.6 actions without /Type
    /// with a /Next cycle, 12.3.3 an outline action, 7.5.7 / 7.5.8 the file structure) and the B190 /Contents
    /// 99999 0 R
    static mergetest::MergeTestPdf createClassesPdf()
    {
        using Pdf = mergetest::MergeTestPdf;
        Pdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R /OpenAction 20 0 R /Outlines 30 0 R >>");
        pdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /Resources 10 0 R >>");
        pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents [5 0 R 6 0 R] /Annots [7 0 R] >>");
        pdf.set(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 99999 0 R /Resources 10 0 R >>");
        pdf.set(5, Pdf::stream("q Q"));
        pdf.set(6, Pdf::stream("q Q"));
        pdf.set(7, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A 21 0 R /P 3 0 R >>");
        pdf.set(10, "<< /Font << /F1 11 0 R >> /XObject << /Im1 12 0 R /Fm1 13 0 R >> /ExtGState << /G1 14 0 R >> /ColorSpace << /CS1 [/ICCBased 15 0 R] >> "
                    "/Shading << /Sh1 16 0 R >> /Pattern << /P1 17 0 R >> >>");
        pdf.set(11, "<< /Type /Font /Subtype /TrueType /BaseFont /X /FontDescriptor 18 0 R >>");
        pdf.set(12, Pdf::stream(QByteArray(1, '\x80'), "/Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray /SMask 22 0 R"));
        pdf.set(13, Pdf::stream("/Fm2 Do", "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << /XObject << /Fm2 23 0 R >> >>"));
        pdf.set(14, "<< /Type /ExtGState /CA 0.5 >>");
        pdf.set(15, Pdf::stream("icc", "/N 1"));
        pdf.set(16, "<< /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 1 0] /Function 24 0 R >>");
        pdf.set(17, Pdf::stream("0 0 1 1 re f", "/PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 1 1] /XStep 1 /YStep 1 /Resources << /Font << /T3 25 0 R >> >>"));
        pdf.set(18, "<< /Type /FontDescriptor /FontName /X /Flags 32 /FontFile2 19 0 R >>");
        pdf.set(19, Pdf::stream("font"));
        pdf.set(20, "<< /S /JavaScript /JS 27 0 R /Next 28 0 R >>");
        pdf.set(21, "<< /Type /Action /S /URI /URI (http://example.com) >>");
        pdf.set(22, Pdf::stream(QByteArray(1, '\xff'), "/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray"));
        pdf.set(23, Pdf::stream("/Fm1 Do", "/Subtype /Form /BBox [0 0 1 1] /Resources << /XObject << /Fm1 13 0 R >> /ExtGState << /G2 43 0 R >> >>"));
        pdf.set(24, "<< /FunctionType 2 /Domain [0 1] /N 1 >>");
        pdf.set(25, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1 1] /FontMatrix [1 0 0 1 0 0] /CharProcs << /a 26 0 R >> /Encoding << /Differences [97 /a] >> "
                    "/FirstChar 97 /LastChar 97 /Widths [1] /Resources << /XObject << /Fm3 13 0 R >> >> >>");
        pdf.set(26, Pdf::stream("1 0 0 0 1 1 d1"));
        pdf.set(27, Pdf::stream("app.alert(1);"));
        pdf.set(28, "<< /S /Named /N /NextPage /Next 20 0 R >>");
        pdf.set(29, "<< /S /GoTo /D [3 0 R /Fit] >>");
        pdf.set(30, "<< /Type /Outlines /First 31 0 R /Last 31 0 R /Count 1 >>");
        pdf.set(31, "<< /Title (A) /Parent 30 0 R /A 29 0 R >>");
        pdf.set(40, Pdf::stream("50 0 << >>", "/Type /ObjStm /N 1 /First 5"));
        pdf.set(41, Pdf::stream(QByteArray(3, '\0'), "/Type /XRef /Size 1 /W [1 1 1]"));
        pdf.set(42, "<< /Foo 1 >>");
        pdf.set(43, "<< /Type /ExtGState /LW 2 >>");
        return pdf;
    }

    static std::set<int> numbers(const std::vector<PDFObjectReference>& references)
    {
        std::set<int> result;
        for (const PDFObjectReference reference : references)
        {
            result.insert(int(reference.objectNumber));
        }
        return result;
    }

    /// The objects of the document, whose class is \p type
    static std::set<int> members(const PDFDocument& document, const Classifier& classifier, Classifier::Type type)
    {
        std::set<int> result;
        const PDFObjectStorage::PDFObjects& objects = document.getStorage().getObjects();
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const PDFObjectReference reference(PDFInteger(i), objects[i].generation);
            if (!objects[i].object.isNull() && classifier.getClass(reference) == type)
            {
                result.insert(int(i));
            }
        }
        return result;
    }

    static qint64 getTimeLimit()
    {
        bool ok = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &ok);
        return qint64(5000) * ((ok && factor > 0) ? factor : 1);
    }

    static QString name(const char* text) { return QString::fromLatin1(text); }

    /// The row of \p id in \p key of the data
    static QJsonObject row(const QJsonObject& data, const char* key, const char* rowKey, const QString& id)
    {
        for (const QJsonValue& value : data.value(name(key)).toArray())
        {
            if (value.toObject().value(name(rowKey)).toString() == id)
            {
                return value.toObject();
            }
        }
        return QJsonObject();
    }

    class Canceled : public PDFOperationControl
    {
    public:
        virtual bool isOperationCanceled() const override { return true; }
    };
};

class ObjectStatisticsTest : public QObject
{
    Q_OBJECT

private slots:
    void classifierRoots();
    void classifierClasses();
    void classifierHostile();
    void classifierLimits();
    void classifierSharedResources();
    void classifierCanceled();
    void valueCounts();
    void toolData();
    void toolRunAndExport();
    void emptyDocument();
    void repairedWhenOpened();
    void factsInCLocale();
    void pdfToolRepairedWhenOpened();
};

void ObjectStatisticsTest::classifierRoots()
{
    // The roots of the classes: the lists of the views of Object Inspector (482:15054)
    using H = ObjectStatisticsTestHelper;
    using C = PDFObjectClassifier;
    const PDFDocument document = H::createClassesPdf().read();
    C classifier;
    QVERIFY(classifier.classify(&document, nullptr));
    QVERIFY(classifier.isComplete());

    QCOMPARE(H::numbers(classifier.getObjectsByType(C::Page)), std::set<int>({ 3, 4 }));
    // Table 31: an array of content streams; B190: 99999 0 R is no object and is never marked
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::ContentStream)), std::set<int>({ 5, 6 }));
    QVERIFY(!classifier.hasObject(PDFObjectReference(99999, 0)));
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::Annotation)), std::set<int>({ 7 }));
    // B185: the image 12 has no /Type; the soft mask 22 is no resource (no root)
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::Image)), std::set<int>({ 12 }));
    // The cycle of the forms 13 -> 23 -> 13 and the form 13 shared by the page and the Type 3 font
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::Form)), std::set<int>({ 13, 23 }));
    // The resources of an inherited /Resources (page 3), of a tiling pattern (the Type 3 font 25)
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::Font)), std::set<int>({ 11, 25 }));
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::Pattern)), std::set<int>({ 17 }));
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::Shading)), std::set<int>({ 16 }));
    // B167: the resources of a form (43 only there), not only of the page
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::GraphicState)), std::set<int>({ 14, 43 }));
    // A direct color space array is no indirect object: no root
    QVERIFY(classifier.getObjectsByType(C::ColorSpace).empty());
    QVERIFY(!classifier.hasType(C::ColorSpace));
    // B205: /OpenAction 20 and its /Next 28 (a cycle back to 20) without /Type, the link action 21, the
    // outline action 29
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::Action)), std::set<int>({ 20, 21, 28, 29 }));
    // B206: the object stream and the cross-reference stream
    QCOMPARE(H::numbers(classifier.getObjectsByType(C::FileStructure)), std::set<int>({ 40, 41 }));
    QCOMPARE(classifier.getObjectCountByType(C::Action), qint64(4));
}

void ObjectStatisticsTest::classifierClasses()
{
    // Every object in exactly one class, the class of the root it is used for (480:14219); the back links
    // /Parent and /P are not followed (the page tree 2 and the outline stay Other)
    using H = ObjectStatisticsTestHelper;
    using C = PDFObjectClassifier;
    const PDFDocument document = H::createClassesPdf().read();
    C classifier;
    QVERIFY(classifier.classify(&document, nullptr));

    QCOMPARE(H::members(document, classifier, C::Page), std::set<int>({ 3, 4, 10 }));
    QCOMPARE(H::members(document, classifier, C::ContentStream), std::set<int>({ 5, 6 }));
    QCOMPARE(H::members(document, classifier, C::Annotation), std::set<int>({ 7 }));
    QCOMPARE(H::members(document, classifier, C::Image), std::set<int>({ 12, 22 }));
    QCOMPARE(H::members(document, classifier, C::Form), std::set<int>({ 13, 23 }));
    QCOMPARE(H::members(document, classifier, C::Font), std::set<int>({ 11, 18, 19, 25, 26 }));
    QCOMPARE(H::members(document, classifier, C::Pattern), std::set<int>({ 17 }));
    QCOMPARE(H::members(document, classifier, C::Shading), std::set<int>({ 16, 24 }));
    QCOMPARE(H::members(document, classifier, C::ColorSpace), std::set<int>({ 15 }));
    QCOMPARE(H::members(document, classifier, C::GraphicState), std::set<int>({ 14, 43 }));
    QCOMPARE(H::members(document, classifier, C::Action), std::set<int>({ 20, 21, 27, 28, 29 }));
    QCOMPARE(H::members(document, classifier, C::FileStructure), std::set<int>({ 40, 41 }));
    QCOMPARE(H::members(document, classifier, C::None), std::set<int>({ 1, 2, 30, 31, 42 }));

    // The statistics: the counts of the classes and their object bytes (the serialized objects, B186)
    const C::Statistics statistics = classifier.calculateStatistics(&document, nullptr);
    QVERIFY(!statistics.isCanceled);
    qint64 total = 0;
    for (const C::Type type : C::getClasses())
    {
        const std::set<int> objects = H::members(document, classifier, type);
        qint64 bytes = 0;
        for (const int number : objects)
        {
            bytes += PDFDocumentWriter::getObjectSize(&document, PDFObjectReference(number, 0));
        }
        QCOMPARE(statistics.statistics.at(type).count.load(), qint64(objects.size()));
        QCOMPARE(statistics.statistics.at(type).bytes.load(), bytes);
        total += qint64(objects.size());
    }
    QCOMPARE(total, qint64(33));
}

void ObjectStatisticsTest::classifierHostile()
{
    // Hostile graphs: a chain of 10 000 actions by /Next (12.6.2) from /OpenAction - every one an action, no
    // depth of a path; a chain of dictionaries, each listing the next one twice (2^64 paths), under a font -
    // each object once: both within a time limit
    using H = ObjectStatisticsTestHelper;
    using C = PDFObjectClassifier;
    constexpr int ACTIONS = 10000;
    constexpr int LEVELS = 64;

    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R /OpenAction 100 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] /Resources << /Font << /F 10 0 R >> >> >>");
    pdf.set(10, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Shared 11 0 R >>");
    for (int i = 0; i < LEVELS; ++i)
    {
        const QByteArray next = QByteArray::number(12 + i) + " 0 R";
        pdf.set(11 + i, "<< /A " + next + " /B " + next + " >>");
    }
    pdf.set(11 + LEVELS, "<< >>");
    for (int i = 0; i < ACTIONS; ++i)
    {
        pdf.set(100 + i, i + 1 < ACTIONS ? "<< /S /Named /N /NextPage /Next " + QByteArray::number(101 + i) + " 0 R >>" : QByteArray("<< /S /Named /N /NextPage >>"));
    }
    const PDFDocument document = pdf.read();

    QElapsedTimer timer;
    timer.start();
    C classifier;
    QVERIFY(classifier.classify(&document, nullptr));
    QVERIFY(timer.elapsed() < H::getTimeLimit());
    QVERIFY(classifier.isComplete());
    QCOMPARE(classifier.getObjectCountByType(C::Action), qint64(ACTIONS));
    QCOMPARE(classifier.getClass(PDFObjectReference(100 + ACTIONS - 1, 0)), C::Action);

    // The shared chain: every link a Font (reached from the font), counted once
    std::set<int> expected = { 10 };
    for (int i = 0; i <= LEVELS; ++i)
    {
        expected.insert(11 + i);
    }
    QCOMPARE(H::members(document, classifier, C::Font), expected);

    const C::Statistics statistics = classifier.calculateStatistics(&document, nullptr);
    QCOMPARE(statistics.statistics.at(C::Font).count.load(), qint64(LEVELS + 2));
    QVERIFY(timer.elapsed() < H::getTimeLimit());
}

void ObjectStatisticsTest::classifierLimits()
{
    // A chain deeper than the depth of a walk (PDFObjectWalk::DEFAULT_MAXIMUM_DEPTH 256): the object at the
    // distance 256 from the font is still a Font, the next one is not read - the classification says so
    using C = PDFObjectClassifier;
    constexpr int LENGTH = 300;
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] /Resources << /Font << /F 10 0 R >> >> >>");
    for (int i = 0; i <= LENGTH; ++i)
    {
        pdf.set(10 + i, i < LENGTH ? "<< /Chain " + QByteArray::number(11 + i) + " 0 R >>" : QByteArray("<< >>"));
    }
    const PDFDocument document = pdf.read();

    C classifier;
    QVERIFY(classifier.classify(&document, nullptr));
    QVERIFY(!classifier.isComplete());
    QCOMPARE(classifier.getClass(PDFObjectReference(10 + PDFObjectWalk::DEFAULT_MAXIMUM_DEPTH, 0)), C::Font);
    QCOMPARE(classifier.getClass(PDFObjectReference(10 + PDFObjectWalk::DEFAULT_MAXIMUM_DEPTH + 1, 0)), C::None);

    // The data of the tool say it ("Not all objects were classified")
    bool isCanceled = true;
    const QJsonObject data = PDFDocumentToolStatistics::createData(&document, -1, false, nullptr, &isCanceled);
    QVERIFY(!isCanceled);
    QCOMPARE(data.value(QStringLiteral("isComplete")).toBool(), false);
}

void ObjectStatisticsTest::classifierSharedResources()
{
    // Review RS7CORE: 10 000 forms of a page share one resource dictionary of 10 000 images (7.8.3: a
    // resource dictionary may be shared). Each form, each image is a root of its class once - the dictionary
    // read by every form made 100 million roots (gigabytes and seconds of the walk over them).
    using C = PDFObjectClassifier;
    using H = ObjectStatisticsTestHelper;
    constexpr int count = 10000;
    constexpr int firstForm = 10;
    constexpr int firstImage = firstForm + count;
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10] /Resources 4 0 R >>");
    QByteArray forms;
    QByteArray images;
    for (int i = 0; i < count; ++i)
    {
        forms += "/F" + QByteArray::number(i) + " " + QByteArray::number(firstForm + i) + " 0 R ";
        images += "/I" + QByteArray::number(i) + " " + QByteArray::number(firstImage + i) + " 0 R ";
        pdf.set(firstForm + i, mergetest::MergeTestPdf::stream("q Q", "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources 5 0 R"));
        pdf.set(firstImage + i, mergetest::MergeTestPdf::stream(QByteArray(1, '\0'), "/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray"));
    }
    pdf.set(4, "<< /XObject << " + forms + " >> >>");
    pdf.set(5, "<< /XObject << " + images + " >> >>");
    const PDFDocument document = pdf.read();

    QElapsedTimer timer;
    timer.start();
    C classifier;
    QVERIFY(classifier.classify(&document, nullptr));
    QVERIFY(timer.elapsed() < H::getTimeLimit());
    QVERIFY(classifier.isComplete());
    QCOMPARE(classifier.getObjectCountByType(C::Form), qint64(count));
    QCOMPARE(classifier.getObjectCountByType(C::Image), qint64(count));
    QCOMPARE(classifier.getClass(PDFObjectReference(firstForm, 0)), C::Form);
    QCOMPARE(classifier.getClass(PDFObjectReference(firstImage + count - 1, 0)), C::Image);
}

void ObjectStatisticsTest::classifierCanceled()
{
    using H = ObjectStatisticsTestHelper;
    const PDFDocument document = H::createClassesPdf().read();
    H::Canceled canceled;

    PDFObjectClassifier classifier;
    QVERIFY(!classifier.classify(&document, &canceled));
    QVERIFY(!classifier.isComplete());

    bool isCanceled = false;
    QVERIFY(PDFDocumentToolStatistics::createData(&document, 100, false, &canceled, &isCanceled).isEmpty());
    QVERIFY(isCanceled);

    const PDFDocumentToolStatistics tool;
    PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(document)), QByteArray(), QString(), true));
    QCOMPARE(tool.run(context, QVariantMap(), &canceled, nullptr).status, PDFDocumentToolResultStatus::Stopped);
}

void ObjectStatisticsTest::valueCounts()
{
    // "Every value in the document is counted by its PDF type" (480:14555): the objects, the entries of the
    // dictionaries (of a stream too; the stream counts as a Stream), the items of the arrays; the free
    // entries 0, 5 and 6 of the cross-reference table are no values
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 10 10.5] /X true /Y null /S (s) >>");
    pdf.set(4, mergetest::MergeTestPdf::stream("q Q"));
    pdf.set(7, "[1 /N]");
    const PDFDocument document = pdf.read();

    PDFObjectClassifier classifier;
    classifier.classify(&document);
    const PDFObjectClassifier::Statistics statistics = classifier.calculateStatistics(&document);
    auto count = [&statistics](PDFObject::Type type) { return statistics.objectCountByType[size_t(type)]; };
    QCOMPARE(count(PDFObject::Type::Dictionary), qint64(3));
    QCOMPARE(count(PDFObject::Type::Stream), qint64(1));
    QCOMPARE(count(PDFObject::Type::Name), qint64(4));
    QCOMPARE(count(PDFObject::Type::Reference), qint64(3));
    QCOMPARE(count(PDFObject::Type::Array), qint64(3));
    QCOMPARE(count(PDFObject::Type::Int), qint64(1 + 3 + 1 + 1));
    QCOMPARE(count(PDFObject::Type::Real), qint64(1));
    QCOMPARE(count(PDFObject::Type::Bool), qint64(1));
    QCOMPARE(count(PDFObject::Type::Null), qint64(1));
    QCOMPARE(count(PDFObject::Type::String), qint64(1));
}

void ObjectStatisticsTest::toolData()
{
    // The data of the result (pdfdocumenttool_statistics.h): the classes by their bytes, the types by their
    // count, the shares of the object bytes and of the values
    using H = ObjectStatisticsTestHelper;
    using T = PDFDocumentToolStatistics;
    const PDFDocument document = H::createClassesPdf().read();

    bool isCanceled = true;
    const QJsonObject data = T::createData(&document, 12345, false, nullptr, &isCanceled);
    QVERIFY(!isCanceled);
    QCOMPARE(data.value(H::name(T::DATA_OBJECT_COUNT)).toInteger(), qint64(33));
    QCOMPARE(data.value(H::name(T::DATA_PAGE_COUNT)).toInteger(), qint64(2));
    QCOMPARE(data.value(H::name(T::DATA_FILE_SIZE)).toInteger(), qint64(12345));
    QCOMPARE(data.value(H::name(T::DATA_UNSAVED)).toBool(), false);
    QCOMPARE(data.value(H::name(T::DATA_COMPLETE)).toBool(), true);
    QCOMPARE(data.value(H::name(T::DATA_VERSION)).toString(), QStringLiteral("1.7"));

    const QJsonArray classes = data.value(H::name(T::DATA_CLASSES)).toArray();
    QCOMPARE(classes.size(), 13);
    qint64 previous = (std::numeric_limits<qint64>::max)();
    qint64 total = 0;
    for (const QJsonValue& value : classes)
    {
        const qint64 bytes = value.toObject().value(H::name(T::ROW_BYTES)).toInteger();
        QVERIFY(bytes <= previous);
        previous = bytes;
        total += bytes;
    }
    QCOMPARE(data.value(H::name(T::DATA_TOTAL_BYTES)).toInteger(), total);

    const QJsonObject fonts = H::row(data, T::DATA_CLASSES, T::ROW_ID, QStringLiteral("font"));
    QCOMPARE(fonts.value(H::name(T::ROW_COUNT)).toInteger(), qint64(5));
    QCOMPARE(fonts.value(H::name(T::ROW_SHARE)).toDouble(), 100.0 * double(fonts.value(H::name(T::ROW_BYTES)).toInteger()) / double(total));
    QCOMPARE(H::row(data, T::DATA_CLASSES, T::ROW_ID, QStringLiteral("file-structure")).value(H::name(T::ROW_COUNT)).toInteger(), qint64(2));
    QCOMPARE(H::row(data, T::DATA_CLASSES, T::ROW_ID, QStringLiteral("other")).value(H::name(T::ROW_COUNT)).toInteger(), qint64(5));
    QCOMPARE(T::getClassTitle(QStringLiteral("content-stream")), QStringLiteral("Content Stream"));
    QCOMPARE(T::getClassTitle(QStringLiteral("file-structure")), QStringLiteral("File Structure"));

    // The types by their count, the largest first; the shares of all values
    const QJsonArray types = data.value(H::name(T::DATA_TYPES)).toArray();
    qint64 valueCount = 0;
    previous = (std::numeric_limits<qint64>::max)();
    for (const QJsonValue& value : types)
    {
        const qint64 count = value.toObject().value(H::name(T::ROW_COUNT)).toInteger();
        QVERIFY(count <= previous && count > 0);
        previous = count;
        valueCount += count;
    }
    QCOMPARE(data.value(H::name(T::DATA_VALUE_COUNT)).toInteger(), valueCount);
    const QJsonObject dictionaries = H::row(data, T::DATA_TYPES, T::ROW_TYPE, QStringLiteral("dictionary"));
    QCOMPARE(dictionaries.value(H::name(T::ROW_SHARE)).toDouble(), 100.0 * double(dictionaries.value(H::name(T::ROW_COUNT)).toInteger()) / double(valueCount));

    // FD-2: a document with unsaved changes has no size of the file
    const QJsonObject unsaved = T::createData(&document, -1, false, nullptr, &isCanceled);
    QCOMPARE(unsaved.value(H::name(T::DATA_FILE_SIZE)).toInteger(), qint64(-1));
    QCOMPARE(unsaved.value(H::name(T::DATA_UNSAVED)).toBool(), true);
}

void ObjectStatisticsTest::toolRunAndExport()
{
    using H = ObjectStatisticsTestHelper;
    using T = PDFDocumentToolStatistics;
    const QByteArray bytes = H::createClassesPdf().write();
    const PDFDocumentPointer document(new PDFDocument(mergetest::MergeTestPdf::readBytes(bytes)));
    const T tool;

    // The file of the document: its size (FD-2); with unsaved changes none
    const PDFDocumentToolContext saved = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(document, bytes, QStringLiteral("C:/work/Report.pdf"), false));
    const PDFDocumentToolResult result = tool.run(saved, QVariantMap(), nullptr, nullptr);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.data.value(H::name(T::DATA_FILE_SIZE)).toInteger(), qint64(bytes.size()));
    QVERIFY(result.outputs.empty());

    const PDFDocumentToolContext unsaved = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(document, bytes, QStringLiteral("C:/work/Report.pdf"), true));
    QCOMPARE(tool.run(unsaved, QVariantMap(), nullptr, nullptr).data.value(H::name(T::DATA_FILE_SIZE)).toInteger(), qint64(-1));

    // K7.20: CSV is the table of the view - a header and a row per class (RFC 4180, ';', CRLF)
    QVariantMap csv;
    csv.insert(H::name(T::PARAMETER_FORMAT), QStringLiteral("csv"));
    const PDFDocumentToolResult csvResult = tool.run(saved, csv, nullptr, nullptr);
    QVERIFY(csvResult.isSuccess());
    QCOMPARE(csvResult.outputs.size(), size_t(1));
    QCOMPARE(csvResult.outputs.front().suggestedFileName, QStringLiteral("Report - statistics.csv"));
    QStringList lines = QString::fromUtf8(csvResult.outputs.front().data).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 1 + 13);
    QCOMPARE(lines.front(), QStringLiteral("Class;Share (%);Count;Size (bytes)"));
    QVERIFY(lines.contains(QStringLiteral("File Structure;%1;2;%2").arg(QString::number(H::row(result.data, T::DATA_CLASSES, T::ROW_ID, QStringLiteral("file-structure")).value(H::name(T::ROW_SHARE)).toDouble(), 'f', 2))
                                                                        .arg(H::row(result.data, T::DATA_CLASSES, T::ROW_ID, QStringLiteral("file-structure")).value(H::name(T::ROW_BYTES)).toInteger())));

    csv.insert(H::name(T::PARAMETER_VIEW), QStringLiteral("type"));
    lines = QString::fromUtf8(tool.run(saved, csv, nullptr, nullptr).outputs.front().data).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    QCOMPARE(lines.front(), QStringLiteral("Type;Share (%);Count"));
    QCOMPARE(lines.size(), 1 + int(result.data.value(H::name(T::DATA_TYPES)).toArray().size()));

    // Text, HTML and XML: both tables; the export of the data never runs the tool again
    QVariantMap text;
    text.insert(H::name(T::PARAMETER_FORMAT), QStringLiteral("text"));
    const PDFDocumentToolOutputFile textFile = T::writeOutput(result.data, text, PDFDocumentToolTableWriter::Options(), QStringLiteral("Report - statistics"), nullptr);
    const QString textContent = QString::fromUtf8(textFile.data);
    QCOMPARE(textFile.suggestedFileName, QStringLiteral("Report - statistics.txt"));
    QVERIFY(textContent.contains(QStringLiteral("Statistics by object function")));
    QVERIFY(textContent.contains(QStringLiteral("Statistics by object type")));
    QVERIFY(textContent.contains(QStringLiteral("File size: %1 bytes").arg(bytes.size())));

    QVariantMap xml;
    xml.insert(H::name(T::PARAMETER_FORMAT), QStringLiteral("xml"));
    const QByteArray xmlData = T::writeOutput(result.data, xml, PDFDocumentToolTableWriter::Options(), QStringLiteral("x"), nullptr).data;
    QVERIFY(xmlData.contains("byFunction"));
    QVERIFY(xmlData.contains("byType"));

    QString errorMessage;
    QVERIFY(T::writeOutput(result.data, QVariantMap(), PDFDocumentToolTableWriter::Options(), QStringLiteral("x"), &errorMessage).data.isEmpty());
    QVERIFY(!errorMessage.isEmpty());
}

void ObjectStatisticsTest::emptyDocument()
{
    // A document without objects and pages: no rows, no division by zero (B186)
    using H = ObjectStatisticsTestHelper;
    using T = PDFDocumentToolStatistics;
    const PDFDocument document;
    bool isCanceled = true;
    const QJsonObject data = T::createData(&document, 0, false, nullptr, &isCanceled);
    QVERIFY(!isCanceled);
    QVERIFY(data.value(H::name(T::DATA_CLASSES)).toArray().isEmpty());
    QVERIFY(data.value(H::name(T::DATA_TYPES)).toArray().isEmpty());
    QCOMPARE(data.value(H::name(T::DATA_TOTAL_BYTES)).toInteger(), qint64(0));
    QCOMPARE(data.value(H::name(T::DATA_PAGE_COUNT)).toInteger(), qint64(0));

    QVariantMap text;
    text.insert(H::name(T::PARAMETER_FORMAT), QStringLiteral("text"));
    const QString content = QString::fromUtf8(T::writeOutput(data, text, PDFDocumentToolTableWriter::Options(), QStringLiteral("x"), nullptr).data);
    QVERIFY(!content.contains(QStringLiteral("nan"), Qt::CaseInsensitive));
    QVERIFY(content.contains(QStringLiteral("Objects: 0")));
}

void ObjectStatisticsTest::repairedWhenOpened()
{
    // Plan 4.2 (FD-7): a damaged file, which the reader restored when it was opened (its startxref points
    // nowhere, ISO 32000-2, 7.5.5), is "Repaired when opened" in the data, the facts and the export; the fact
    // comes from the input of the tool (the restoration the shell knows), a clean file has none of it
    using T = PDFDocumentToolStatistics;
    const T tool;
    QVariantMap text;
    text.insert(QString::fromLatin1(T::PARAMETER_FORMAT), QStringLiteral("text"));

    const QByteArray damagedBytes = ObjectStatisticsTestHelper::createDamagedPdf();
    PDFDocumentReader reader(nullptr, nullptr, true, false);
    const PDFDocumentPointer damaged(new PDFDocument(reader.readFromBuffer(damagedBytes)));
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);
    QVERIFY(reader.isDocumentRestored());

    PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(damaged, damagedBytes, QStringLiteral("Damaged.pdf"), false));
    context.documents.front().isRestored = reader.isDocumentRestored();
    const PDFDocumentToolResult result = tool.run(context, text, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QString::fromLatin1(T::DATA_REPAIRED)).toBool(), true);
    QVERIFY(std::any_of(result.facts.cbegin(), result.facts.cend(), [](const PDFDocumentToolFact& fact) { return fact.id == QLatin1String("repaired") && fact.title == QLatin1String("Repaired when opened"); }));
    QCOMPARE(result.outputs.size(), size_t(1));
    QVERIFY(QString::fromUtf8(result.outputs.front().data).contains(QStringLiteral("Repaired when opened")));

    // A clean file of the same objects
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>");
    const QByteArray cleanBytes = pdf.write();
    PDFDocumentReader cleanReader(nullptr, nullptr, true, false);
    const PDFDocumentPointer clean(new PDFDocument(cleanReader.readFromBuffer(cleanBytes)));
    QVERIFY(!cleanReader.isDocumentRestored());
    const PDFDocumentToolContext cleanContext = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(clean, cleanBytes, QStringLiteral("Clean.pdf"), false));
    const PDFDocumentToolResult cleanResult = tool.run(cleanContext, text, nullptr, nullptr);
    QVERIFY2(cleanResult.isSuccess(), qPrintable(cleanResult.message));
    QCOMPARE(cleanResult.data.value(QString::fromLatin1(T::DATA_REPAIRED)).toBool(true), false);
    QVERIFY(std::none_of(cleanResult.facts.cbegin(), cleanResult.facts.cend(), [](const PDFDocumentToolFact& fact) { return fact.id == QLatin1String("repaired"); }));
    QVERIFY(!QString::fromUtf8(cleanResult.outputs.front().data).contains(QStringLiteral("Repaired when opened")));
}

void ObjectStatisticsTest::factsInCLocale()
{
    // The fix wave of step 7: the facts are machine-readable (MCP) - the C locale, whatever the default locale
    // is; the 33 objects of the document of the classes (toolData), its object bytes and the size of its file
    using H = ObjectStatisticsTestHelper;
    using T = PDFDocumentToolStatistics;
    pdftest::TestLocaleGuard guard;
    QVERIFY(pdftest::TestLocaleGuard::isDefaultLocaleOtherThanC());

    const QByteArray bytes = H::createClassesPdf().write();
    const PDFDocumentPointer document(new PDFDocument(mergetest::MergeTestPdf::readBytes(bytes)));
    const PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(document, bytes, QStringLiteral("Report.pdf"), false));
    const PDFDocumentToolResult result = T().run(context, QVariantMap(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    std::map<QString, QString> facts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    const qint64 totalBytes = result.data.value(H::name(T::DATA_TOTAL_BYTES)).toInteger();
    QVERIFY(totalBytes > 1024 && bytes.size() > 1024);
    QCOMPARE(facts["objects"], QStringLiteral("33"));
    QCOMPARE(facts["objectBytes"], QLocale::c().formattedDataSize(totalBytes, 1, QLocale::DataSizeTraditionalFormat));
    QCOMPARE(facts["fileSize"], QLocale::c().formattedDataSize(bytes.size(), 1, QLocale::DataSizeTraditionalFormat));
    QVERIFY(facts["fileSize"].contains(QLatin1Char('.')));
}

void ObjectStatisticsTest::pdfToolRepairedWhenOpened()
{
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    // PdfTool statistics (plan 4.2, 4.10): the damaged file is restored by the permissive reader of PdfTool and
    // is "Repaired when opened"; a clean file is not
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto writeFile = [&directory](const QString& name, const QByteArray& data)
    {
        const QString fileName = directory.filePath(name);
        QFile file(fileName);
        if (file.open(QFile::WriteOnly))
        {
            file.write(data);
        }
        return fileName;
    };
    auto runStatistics = [](const QString& fileName, QByteArray* output)
    {
        QProcess process;
        process.setProgram(QStringLiteral(PDF4QT_TEST_PDFTOOL));
        process.setArguments({ QStringLiteral("statistics"), fileName });
        process.start();
        process.waitForFinished(120000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
        *output = process.readAllStandardOutput();
        return process.exitCode();
    };

    QByteArray output;
    QCOMPARE(runStatistics(writeFile(QStringLiteral("Damaged.pdf"), ObjectStatisticsTestHelper::createDamagedPdf()), &output), 0);
    QVERIFY2(output.contains("Repaired when opened"), output.constData());

    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 100 100 ] >>");
    QCOMPARE(runStatistics(writeFile(QStringLiteral("Clean.pdf"), pdf.write()), &output), 0);
    QVERIFY(output.contains("Statistics by object function"));
    QVERIFY(!output.contains("Repaired when opened"));
#endif
}

QTEST_MAIN(ObjectStatisticsTest)

#include "tst_objectstatisticstest.moc"
