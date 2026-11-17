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

// Ink Coverage of the core (step 7 of phase 2, plan 4.5, 8; package P3a): the coverage in the process space
// (B196), the spot colors of every resource (B197), /None (B198), the settings and the cancellation of the
// calculator (B199), the box of the ratio (Q7.6, R7-8), the total (B163), a page, which cannot be rendered
// (Q7.9), the export, PdfTool ink-coverage (B179, B163) and info-inks. The expected values come from the
// specification: a page of 100 x 100 points at the resolution 100 has a pixel per point; a point is
// 25.4 / 72 mm; the area of an ink is weighted by its tint (K7.15).

#include "outputpreviewtestpdf.h"
#include "pdfconstants.h"
#include "pdfdocumenttool_inkcoverage.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "testlocaleguard.h"

#include <QElapsedTimer>
#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <atomic>
#include <map>

using namespace pdf;

/// A cancellation, which is set by the test
class InkCoverageTestControl : public PDFOperationControl
{
public:
    virtual bool isOperationCanceled() const override { return isCanceled.load(); }

    std::atomic<bool> isCanceled = false;
};

/// A cancellation, which comes after the operation has asked for it a given number of times (the user
/// presses Stop while the operation runs)
class InkCoverageTestCountingControl : public PDFOperationControl
{
public:
    explicit InkCoverageTestCountingControl(int allowedChecks) : m_allowedChecks(allowedChecks) { }

    virtual bool isOperationCanceled() const override { return ++m_checks > m_allowedChecks; }

private:
    int m_allowedChecks;
    mutable std::atomic<int> m_checks = 0;
};

class InkCoverageTest : public QObject
{
    Q_OBJECT

private slots:
    void tintWeightedQuarter();
    void totalCountsEachPageOnce();
    void spotInksOfEveryResource();
    void hostileSpotColors();
    void sharedColorSpacesParsedOnce();
    void pageGroupRgb();
    void unmeasuredPageAndRenderingIssue();
    void cancellationAndCollector();
    void exportFixedNames();
    void calculatorSettings();
    void pdfToolInkCoverage();
    void factsInCLocale();

private:
    static double areaMM(double width, double height) { return width * height * (25.4 / 72.0) * (25.4 / 72.0); }

    static PDFDocumentToolContext createContext(const OutputPreviewTestPdf& pdf, const QString& fileName = QString())
    {
        PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(pdf.read())), QByteArray(), fileName, false));
        context.rendering.cmsSettings = PDFCMSSettings();
        return context;
    }

    static QVariantMap createParameters(const QString& pages = QString(), int resolution = 100)
    {
        QVariantMap parameters;
        parameters.insert(PDFDocumentToolInkCoverage::PARAMETER_PAGES, pages);
        parameters.insert(PDFDocumentToolInkCoverage::PARAMETER_RESOLUTION, resolution);
        return parameters;
    }

    /// Index of an ink in "inks" of the data, -1 = none
    static qsizetype findInk(const QJsonObject& data, const QString& name)
    {
        const QJsonArray inks = data.value(PDFDocumentToolInkCoverage::DATA_INKS).toArray();
        for (qsizetype i = 0; i < inks.size(); ++i)
        {
            if (inks.at(i).toObject().value("name").toString() == name)
            {
                return i;
            }
        }
        return -1;
    }

    static double pageValue(const QJsonObject& data, int row, const char* key, qsizetype ink)
    {
        return data.value(PDFDocumentToolInkCoverage::DATA_PAGES).toArray().at(row).toObject().value(key).toArray().at(ink).toDouble();
    }
};

void InkCoverageTest::tintWeightedQuarter()
{
    // Plan 8: a rectangle of 50 % cyan on a quarter of the page is 12.5 % of the page; its area is the
    // quarter weighted by the tint (K7.15)
    OutputPreviewTestPdf pdf;
    pdf.addPage("0.5 0 0 0 k 0 0 50 50 re f");
    const PDFDocumentToolInkCoverage tool;
    const PDFDocumentToolResult result = tool.run(createContext(pdf), createParameters(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const qsizetype cyan = findInk(result.data, "Cyan");
    QCOMPARE(cyan, qsizetype(0));
    QVERIFY2(qAbs(pageValue(result.data, 0, "ratio", cyan) - 12.5) < 0.01, qPrintable(QString::number(pageValue(result.data, 0, "ratio", cyan))));
    QVERIFY(qAbs(pageValue(result.data, 0, "area", cyan) - 0.5 * areaMM(50, 50)) < 0.01);
    QCOMPARE(pageValue(result.data, 0, "area", findInk(result.data, "Black")), 0.0);
    QCOMPARE(result.data.value(PDFDocumentToolInkCoverage::DATA_BOX).toString(), QStringLiteral("crop"));
    QCOMPARE(qRound(result.data.value(PDFDocumentToolInkCoverage::DATA_PEAK).toObject().value("coverage").toDouble()), 50);
}

void InkCoverageTest::totalCountsEachPageOnce()
{
    // B163: the total is the sum of the pages, each page once (the old PdfTool counted the first page
    // of an ink twice); the ratio of the total is of the boxes of all pages
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 0 0 0 k 0 0 100 100 re f");
    pdf.addPage("1 0 0 0 k 0 0 50 100 re f");
    pdf.addPage("0 0 0 1 k 0 0 100 100 re f");
    const PDFDocumentToolInkCoverage tool;
    const PDFDocumentToolResult result = tool.run(createContext(pdf), createParameters("1-2"), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const QJsonObject total = result.data.value(PDFDocumentToolInkCoverage::DATA_TOTAL).toObject();
    QVERIFY2(qAbs(total.value("area").toArray().at(0).toDouble() - areaMM(150, 100)) < 0.01, qPrintable(QString::number(total.value("area").toArray().at(0).toDouble())));
    QVERIFY(qAbs(total.value("ratio").toArray().at(0).toDouble() - 75.0) < 0.01);
    QCOMPARE(total.value("area").toArray().at(3).toDouble(), 0.0);
    QCOMPARE(result.data.value(PDFDocumentToolInkCoverage::DATA_PAGE_COUNT).toInteger(), qint64(2));
    QCOMPARE(result.data.value(PDFDocumentToolInkCoverage::DATA_PAGES).toArray().at(1).toObject().value("page").toInteger(), qint64(2));
}

void InkCoverageTest::spotInksOfEveryResource()
{
    // B197: the spot inks of a form XObject, of the base of an Indexed space of an image and of a shading
    // are inks, not only the color spaces of the page; B198: the colorant /None of a DeviceN space is no ink
    OutputPreviewTestPdf pdf;
    const int form = pdf.add(OutputPreviewTestPdf::stream("/CSF cs 1 scn 0 0 50 100 re f",
                                                          "/Type /XObject /Subtype /Form /BBox [0 0 100 100] /Resources << /ColorSpace << /CSF " + OutputPreviewTestPdf::separation("FormSpot") + " >> >>"));
    const QByteArray indexedSpace = "[/Indexed " + OutputPreviewTestPdf::separation("IndexedSpot") + " 1 <00FF>]";
    const int image = pdf.add(OutputPreviewTestPdf::stream(QByteArray("\x01", 1), "/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace " + indexedSpace));
    // The tint transformation of the DeviceN space (7.10.5, a PostScript calculator): t1 t2 -> 0 t1 0 0
    const int deviceNFunction = pdf.add(OutputPreviewTestPdf::stream("{ pop 0 exch 0 0 }", "/FunctionType 4 /Domain [0 1 0 1] /Range [0 1 0 1 0 1 0 1]"));
    const QByteArray deviceNSpace = "[/DeviceN [/DeviceNSpot /None] /DeviceCMYK " + OutputPreviewTestPdf::ref(deviceNFunction) + "]";
    const QByteArray shading = "<< /ShadingType 2 /ColorSpace " + OutputPreviewTestPdf::separation("ShadingSpot") + " /Coords [0 0 100 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >> >>";
    pdf.addPage("/Fm1 Do q 50 0 0 50 50 50 cm /Im1 Do Q /CSN cs 1 1 scn 50 0 50 50 re f",
                "<< /XObject << /Fm1 " + OutputPreviewTestPdf::ref(form) + " /Im1 " + OutputPreviewTestPdf::ref(image) + " >> /ColorSpace << /CSN " + deviceNSpace + " >> /Shading << /Sh1 " + shading + " >> >>");
    const PDFDocumentToolInkCoverage tool;
    const PDFDocumentToolResult result = tool.run(createContext(pdf), createParameters(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    QStringList names;
    for (const QJsonValue& ink : result.data.value(PDFDocumentToolInkCoverage::DATA_INKS).toArray())
    {
        names << ink.toObject().value("name").toString();
    }
    QVERIFY2(names.contains("FormSpot") && names.contains("IndexedSpot") && names.contains("DeviceNSpot") && names.contains("ShadingSpot"), qPrintable(names.join(", ")));
    QVERIFY(!names.contains("None"));

    // The form paints its spot ink on the left half: half of the page
    QVERIFY(qAbs(pageValue(result.data, 0, "ratio", findInk(result.data, "FormSpot")) - 50.0) < 0.01);
    // The image of index 1 (tint 1.0) on the upper right quarter
    QVERIFY(qAbs(pageValue(result.data, 0, "ratio", findInk(result.data, "IndexedSpot")) - 25.0) < 0.01);
    // The DeviceN color 1 1 on the lower right quarter marks only its spot ink
    QVERIFY(qAbs(pageValue(result.data, 0, "ratio", findInk(result.data, "DeviceNSpot")) - 25.0) < 0.01);
}

void InkCoverageTest::hostileSpotColors()
{
    // Hostile input (RS7COLOR): one /ColorSpace dictionary of 200 000 Separation spaces, each of another spot
    // color (8.6.6.4; names of the same length). Every spot color is found once, the first 26 can be active
    // (476:11703), in a time proportional to them - one search in the list of the spot colors for each of them
    // was quadratic (2 x 10^10 comparisons, tens of seconds, and no Stop inside of one resource dictionary).
    // The alternate space is CIE-based: a device alternate makes the parser look for /DefaultCMYK in the large
    // dictionary for each space (a linear lookup of PDFDictionary, SECURITY_TODO.md 62).
    constexpr int SPOT_COUNT = 200000;
    auto createDocument = [](int count)
    {
        OutputPreviewTestPdf pdf;
        const int alternate = pdf.add("[/CalGray << /WhitePoint [0.9505 1 1.089] >>]");
        const int function = pdf.add("<< /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>");
        QByteArray colorSpaces;
        colorSpaces.reserve(count * 48);
        for (int i = 0; i < count; ++i)
        {
            const QByteArray number = QByteArray::number(i).rightJustified(7, '0');
            colorSpaces += "/C" + number + " [/Separation /S" + number + " " + OutputPreviewTestPdf::ref(alternate) + " " + OutputPreviewTestPdf::ref(function) + "]\n";
        }
        pdf.addPage("/C0000000 cs 1 scn 0 0 50 100 re f", "<< /ColorSpace << " + colorSpaces + " >> >>");
        return pdf.read();
    };

    {
        const PDFDocument document = createDocument(SPOT_COUNT);
        PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(&document);
        PDFInkMapper inkMapper(&cmsManager, &document);

        QElapsedTimer timer;
        timer.start();
        inkMapper.createSpotColors(true);
        const qint64 elapsed = timer.elapsed();

        QCOMPARE(inkMapper.getSpotColorCount(), size_t(SPOT_COUNT));
        QVERIFY(inkMapper.isSpotColorLimitReached());
        QCOMPARE(inkMapper.getActiveSpotColorCount(), size_t(PDFInkMapper::MAX_SPOT_COLOR_COMPONENTS));
        QCOMPARE(inkMapper.getSpotColors().back().name, QByteArray("S0199999"));
        QVERIFY(inkMapper.containsSpotColor("S0123456"));
        QVERIFY(!inkMapper.containsSpotColor("S9999999"));

        const qint64 limit = 10000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
        QVERIFY2(elapsed < limit, qPrintable(QStringLiteral("%1 ms").arg(elapsed)));
    }

    // Stop inside of one resource dictionary: the walk ends at the next color space, not after all of them
    constexpr int CANCELED_COUNT = 20000;
    const PDFDocument document = createDocument(CANCELED_COUNT);
    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    PDFInkMapper inkMapper(&cmsManager, &document);
    InkCoverageTestCountingControl control(1000);
    inkMapper.createSpotColors(true, { 0 }, &control);
    QVERIFY2(inkMapper.getSpotColorCount() < size_t(CANCELED_COUNT / 2), qPrintable(QString::number(inkMapper.getSpotColorCount())));
}

void InkCoverageTest::sharedColorSpacesParsedOnce()
{
    // RS7CORE M5: 50 pages, each with its own resource dictionary, share one /ColorSpace dictionary of 40
    // Separation spaces (8.6.6.4) and one /XObject dictionary of an image of another spot ink (8.9.5). Every
    // color space is parsed once - not once for each page (50 x 41 = 2050 parsings of the same spaces) - and
    // the spot colors are the same, in the same order, as of a document, which repeats the dictionaries
    // directly in each page (nothing is shared, every space of every page is parsed).
    constexpr int PAGE_COUNT = 50;
    constexpr int SPOT_COUNT = 40;
    QByteArray colorSpaces;
    QStringList expectedNames;
    for (int i = 0; i < SPOT_COUNT; ++i)
    {
        const QByteArray number = QByteArray::number(i).rightJustified(2, '0');
        colorSpaces += "/CS" + number + " " + OutputPreviewTestPdf::separation("Spot" + number, i % 2 ? "0 1 0 0" : "1 0 0 0") + "\n";
        expectedNames << QString::fromLatin1("Spot" + number);
    }
    expectedNames << QStringLiteral("ImageSpot");

    const QByteArray imageText = OutputPreviewTestPdf::stream(QByteArray("\xFF", 1), "/Type /XObject /Subtype /Image /Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace " +
                                                              OutputPreviewTestPdf::separation("ImageSpot", "0 0 1 0"));
    const QByteArray content = "/CS00 cs 1 scn 0 0 50 100 re f q 50 0 0 50 50 50 cm /Im1 Do Q";

    OutputPreviewTestPdf shared;
    const int colorSpaceDictionary = shared.add("<< " + colorSpaces + " >>");
    const int image = shared.add(imageText);
    const int xobjects = shared.add("<< /Im1 " + OutputPreviewTestPdf::ref(image) + " >>");
    OutputPreviewTestPdf repeated;
    for (int i = 0; i < PAGE_COUNT; ++i)
    {
        shared.addPage(content, "<< /ColorSpace " + OutputPreviewTestPdf::ref(colorSpaceDictionary) + " /XObject " + OutputPreviewTestPdf::ref(xobjects) + " >>");
        const int pageImage = repeated.add(imageText);
        repeated.addPage(content, "<< /ColorSpace << " + colorSpaces + " >> /XObject << /Im1 " + OutputPreviewTestPdf::ref(pageImage) + " >> >>");
    }

    auto getSpotColors = [](const PDFDocument& document, size_t* creationCount)
    {
        PDFCMSManager cmsManager(nullptr);
        cmsManager.setDocument(&document);
        PDFInkMapper inkMapper(&cmsManager, &document);
        inkMapper.createSpotColors(true);
        *creationCount = inkMapper.getColorSpaceCreationCount();

        QStringList names;
        QStringList colors;
        for (const PDFInkMapper::ColorInfo& info : inkMapper.getSpotColors())
        {
            names << QString::fromLatin1(info.name);
            colors << info.color.name(QColor::HexArgb);
        }
        return std::make_pair(names, colors);
    };

    const PDFDocument sharedDocument = shared.read();
    const PDFDocument repeatedDocument = repeated.read();
    size_t sharedCount = 0;
    size_t repeatedCount = 0;
    const auto sharedColors = getSpotColors(sharedDocument, &sharedCount);
    const auto repeatedColors = getSpotColors(repeatedDocument, &repeatedCount);

    QCOMPARE(sharedColors.first, expectedNames);
    QCOMPARE(sharedColors, repeatedColors);
    QCOMPARE(sharedCount, size_t(SPOT_COUNT + 1));
    QCOMPARE(repeatedCount, size_t(PAGE_COUNT * (SPOT_COUNT + 1)));

    // One dictionary of a hostile document used as the /ColorSpace and as the /XObject of the resources: both
    // kinds are scanned (the image of the dictionary brings its ink)
    OutputPreviewTestPdf both;
    const int bothImage = both.add(imageText);
    const int bothDictionary = both.add("<< /CS00 " + OutputPreviewTestPdf::separation("Spot00") + " /Im1 " + OutputPreviewTestPdf::ref(bothImage) + " >>");
    both.addPage(content, "<< /ColorSpace " + OutputPreviewTestPdf::ref(bothDictionary) + " /XObject " + OutputPreviewTestPdf::ref(bothDictionary) + " >>");
    size_t bothCount = 0;
    QCOMPARE(getSpotColors(both.read(), &bothCount).first, QStringList({ QStringLiteral("Spot00"), QStringLiteral("ImageSpot") }));
}

void InkCoverageTest::pageGroupRgb()
{
    // B196: the inks of a page with a page group of RGB are the inks of the process space, never "Red"
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 0 0 rg 0 0 100 100 re f", "<< >>", "/Group << /S /Transparency /CS /DeviceRGB >>");
    const PDFDocumentToolInkCoverage tool;
    const PDFDocumentToolResult result = tool.run(createContext(pdf), createParameters(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    QCOMPARE(findInk(result.data, "Red"), qsizetype(-1));
    QVERIFY(qAbs(pageValue(result.data, 0, "ratio", findInk(result.data, "Magenta")) - 100.0) < 0.01);
    QVERIFY(qAbs(pageValue(result.data, 0, "ratio", findInk(result.data, "Yellow")) - 100.0) < 0.01);
    QCOMPARE(pageValue(result.data, 0, "ratio", findInk(result.data, "Cyan")), 0.0);
}

void InkCoverageTest::unmeasuredPageAndRenderingIssue()
{
    // Owner answer Q7.9 (b): a page with an error of the rendering is measured with a warning, only a
    // page, which cannot be rendered at all (no area), is left out of the totals with its reason
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 0 0 0 k 0 0 100 100 re f");
    pdf.addPage("1 0 0 0 k 0 0 100 100 re f", "<< >>", QByteArray(), "[0 0 100 0]");
    const QByteArray garbage = "this is not a compressed image stream at all";
    const int image = pdf.add(OutputPreviewTestPdf::stream(garbage, "/Type /XObject /Subtype /Image /Width 16 /Height 16 /BitsPerComponent 8 /ColorSpace /DeviceRGB /Filter /FlateDecode"));
    pdf.addPage("1 0 0 0 k 0 0 50 100 re f q 50 0 0 50 50 0 cm /Im1 Do Q", "<< /XObject << /Im1 " + OutputPreviewTestPdf::ref(image) + " >> >>");

    const PDFDocumentToolInkCoverage tool;
    const PDFDocumentToolResult result = tool.run(createContext(pdf), createParameters(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const QJsonArray unmeasured = result.data.value(PDFDocumentToolInkCoverage::DATA_UNMEASURED_PAGES).toArray();
    QCOMPARE(unmeasured.size(), 1);
    QCOMPARE(unmeasured.at(0).toObject().value("page").toInteger(), qint64(2));
    QVERIFY(!unmeasured.at(0).toObject().value("message").toString().isEmpty());

    const QJsonArray issues = result.data.value(PDFDocumentToolInkCoverage::DATA_RENDERING_ISSUES).toArray();
    QVERIFY(!issues.isEmpty());
    QCOMPARE(issues.at(0).toObject().value("page").toInteger(), qint64(3));
    QVERIFY(!result.renderingIssues.empty());
    QCOMPARE(result.renderingIssues.front().pageIndex, PDFInteger(2));

    // Pages 1 and 3 in the total: the whole page and the half
    QCOMPARE(result.data.value(PDFDocumentToolInkCoverage::DATA_PAGE_COUNT).toInteger(), qint64(2));
    const QJsonObject total = result.data.value(PDFDocumentToolInkCoverage::DATA_TOTAL).toObject();
    QVERIFY(qAbs(total.value("area").toArray().at(0).toDouble() - areaMM(150, 100)) < 0.01);

    // No page, which can be rendered: the run fails with the reason
    OutputPreviewTestPdf empty;
    empty.addPage("1 0 0 0 k 0 0 100 100 re f", "<< >>", QByteArray(), "[0 0 100 0]");
    const PDFDocumentToolResult failed = tool.run(createContext(empty), createParameters(), nullptr, nullptr);
    QCOMPARE(failed.status, PDFDocumentToolResultStatus::Failed);
}

void InkCoverageTest::cancellationAndCollector()
{
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 0 0 0 k 0 0 100 100 re f");
    pdf.addPage("0 1 0 0 k 0 0 100 100 re f");
    const PDFDocumentToolContext context = createContext(pdf);

    // The collector has the rows of the finished pages in the order of the pages (478:11954)
    PDFInkCoverageCollector collector;
    const PDFDocumentToolResult result = PDFDocumentToolInkCoverage::measure(context, createParameters(), nullptr, nullptr, &collector);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(collector.getPageCount(), 2);
    QCOMPARE(collector.getInks().size(), 4);
    const QJsonArray pages = collector.getPages();
    QCOMPARE(pages.at(0).toObject().value("page").toInteger(), qint64(1));
    QCOMPARE(pages.at(1).toObject().value("page").toInteger(), qint64(2));
    QVERIFY(qAbs(pages.at(1).toObject().value("ratio").toArray().at(1).toDouble() - 100.0) < 0.01);

    // B199: a canceled run measures nothing and is Stopped
    InkCoverageTestControl control;
    control.isCanceled = true;
    PDFInkCoverageCollector canceledCollector;
    const PDFDocumentToolResult canceled = PDFDocumentToolInkCoverage::measure(context, createParameters(), &control, nullptr, &canceledCollector);
    QCOMPARE(canceled.status, PDFDocumentToolResultStatus::Stopped);
    QCOMPARE(canceledCollector.getPageCount(), 0);
}

void InkCoverageTest::exportFixedNames()
{
    // B179: the XML element names are fixed ("ink5-area"), the name of an ink is in the title; CSV is
    // one table with the Total row (RFC 4180, the C locale)
    OutputPreviewTestPdf pdf;
    // The name "PANTONE 151 C<&>" (7.3.5: the delimiters by #xx) is no valid XML name
    const QByteArray spotSpace = OutputPreviewTestPdf::separation("PANTONE#20151#20C#3C&#3E");
    pdf.addPage("/CS1 cs 1 scn 0 0 100 100 re f", "<< /ColorSpace << /CS1 " + spotSpace + " >> >>");
    pdf.addPage("/CS1 cs 1 scn 0 0 50 100 re f", "<< /ColorSpace << /CS1 " + spotSpace + " >> >>");
    const PDFDocumentToolInkCoverage tool;
    QVariantMap parameters = createParameters();
    parameters.insert(PDFDocumentToolInkCoverage::PARAMETER_FORMAT, QStringLiteral("xml"));
    const PDFDocumentToolResult result = tool.run(createContext(pdf, "Brochure.pdf"), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.outputs.size(), size_t(1));
    QCOMPARE(result.outputs.front().suggestedFileName.endsWith(".xml"), true);

    QXmlStreamReader reader(result.outputs.front().data);
    QStringList spotTexts;
    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("ink5-area"))
        {
            spotTexts << reader.readElementText();
        }
    }
    QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
    QCOMPARE(spotTexts.size(), 4);  // the header, two pages, the total
    QCOMPARE(spotTexts.at(0), QStringLiteral("PANTONE 151 C<&> [mm²]"));
    QCOMPARE(spotTexts.at(3), QString::number(areaMM(150, 100), 'f', 2));

    parameters.insert(PDFDocumentToolInkCoverage::PARAMETER_FORMAT, QStringLiteral("csv"));
    const PDFDocumentToolOutputFile csv = PDFDocumentToolInkCoverage::writeOutput(result.data, parameters, PDFDocumentToolTableWriter::Options(), "Brochure - ink coverage", nullptr);
    const QList<QByteArray> lines = csv.data.split('\n');
    QVERIFY(lines.size() >= 4);
    QVERIFY(lines.at(0).startsWith("Page;Cyan [mm"));
    QVERIFY(lines.at(3).startsWith("Total;"));
    QVERIFY(lines.at(3).contains(QString::number(areaMM(150, 100), 'f', 2).toLatin1()));
}

void InkCoverageTest::calculatorSettings()
{
    // B199: the calculator of the old plugin uses its settings (vector graphics off = nothing measured)
    // and its cancellation; B169: the ratio of the trim box
    OutputPreviewTestPdf pdf;
    pdf.addPage("1 0 0 0 k 0 0 120 120 re f", "<< >>", "/TrimBox [10 10 110 110]", "[0 0 120 120]");
    const PDFDocument document = pdf.read();

    PDFOptionalContentActivity optionalContentActivity(&document, OCUsage::Print, nullptr);
    PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(PDFCMSSettings());
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    const PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(&document), &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    iccProfileCache.setDocument(modifiedDocument);
    PDFInkMapper inkMapper(&cmsManager, &document);
    inkMapper.createSpotColors(true);

    PDFInkCoverageCalculator calculator(&document, &fontCache, &iccProfileCache, &cmsManager, &optionalContentActivity, &inkMapper, nullptr, PDFTransparencyRendererSettings());
    calculator.perform(QSize(120, 120), { 0 });
    const auto* coverage = calculator.getInkCoverage(0);
    const auto* cyan = PDFInkCoverageCalculator::findCoverageInfoByName(*coverage, "Cyan");
    QVERIFY(cyan);
    QVERIFY2(qAbs(cyan->ratio - 1.0) < 0.0001, qPrintable(QString::number(cyan->ratio)));
    QVERIFY(qAbs(cyan->coveredArea - areaMM(120, 120)) < 0.01);

    PDFTransparencyRendererSettings settings;
    settings.flags.setFlag(PDFTransparencyRendererSettings::DisplayVectorGraphics, false);
    PDFInkCoverageCalculator withoutVectors(&document, &fontCache, &iccProfileCache, &cmsManager, &optionalContentActivity, &inkMapper, nullptr, settings);
    withoutVectors.perform(QSize(120, 120), { 0 });
    QCOMPARE(PDFInkCoverageCalculator::findCoverageInfoByName(*withoutVectors.getInkCoverage(0), "Cyan")->coveredArea, 0.0f);

    InkCoverageTestControl control;
    control.isCanceled = true;
    PDFInkCoverageCalculator canceled(&document, &fontCache, &iccProfileCache, &cmsManager, &optionalContentActivity, &inkMapper, nullptr, PDFTransparencyRendererSettings());
    canceled.setOperationControl(&control);
    canceled.perform(QSize(120, 120), { 0 });
    QVERIFY(canceled.getInkCoverage(0)->empty());
}

void InkCoverageTest::pdfToolInkCoverage()
{
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    // PdfTool ink-coverage runs the tool: B163 (the total), B179 (valid XML with any ink name); info-inks
    // lists the spot ink of a form (B197) and no /None (B198)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    OutputPreviewTestPdf pdf;
    const int form = pdf.add(OutputPreviewTestPdf::stream("/CSF cs 1 scn 0 0 50 100 re f",
                                                          "/Type /XObject /Subtype /Form /BBox [0 0 100 100] /Resources << /ColorSpace << /CSF " + OutputPreviewTestPdf::separation("Spot#20A&B") + " >> >>"));
    const QByteArray resources = "<< /XObject << /Fm1 " + OutputPreviewTestPdf::ref(form) + " >> /ColorSpace << /CSN [/DeviceN [/None] /DeviceCMYK " + OutputPreviewTestPdf::tintFunction("0 0 0 1") + "] >> >>";
    // The spot ink on the left half of page 1 (the form), cyan on its right half and on the whole page 2:
    // the total of cyan is 150 x 100 points (the old PdfTool counted page 1 twice: 200 x 100)
    pdf.addPage("/Fm1 Do 1 0 0 0 k 50 0 50 100 re f", resources);
    pdf.addPage("1 0 0 0 k 0 0 100 100 re f", resources);
    const QString fileName = directory.filePath("inks.pdf");
    {
        QFile file(fileName);
        QVERIFY(file.open(QFile::WriteOnly));
        file.write(pdf.write());
    }

    auto runPdfTool = [](const QStringList& arguments, QByteArray* output)
    {
        QProcess process;
        process.setProgram(QStringLiteral(PDF4QT_TEST_PDFTOOL));
        process.setArguments(arguments);
        process.start();
        process.waitForFinished(120000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
        *output = process.readAllStandardOutput();
        return process.exitCode();
    };

    QByteArray output;
    QCOMPARE(runPdfTool({ "ink-coverage", "--console-format", "xml", fileName }, &output), 0);

    QXmlStreamReader reader(output);
    QStringList cyanTexts;
    QStringList spotTitles;
    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("ink1-area"))
        {
            cyanTexts << reader.readElementText();
        }
        else if (reader.isStartElement() && reader.name() == QLatin1String("ink5-area"))
        {
            spotTitles << reader.readElementText();
        }
    }
    QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
    QCOMPARE(cyanTexts.size(), 4);
    QCOMPARE(cyanTexts.at(3), QString::number(areaMM(150, 100), 'f', 2));
    QCOMPARE(spotTitles.value(0), QStringLiteral("Spot A&B [mm²]"));

    QCOMPARE(runPdfTool({ "info-inks", fileName }, &output), 0);
    QVERIFY(output.contains("Spot A&B"));
    QVERIFY(!output.contains("None"));
#endif
}

void InkCoverageTest::factsInCLocale()
{
    // The fix wave of step 7: the facts of the tool are machine-readable (MCP, PdfTool) - the numbers in the C
    // locale, whatever the default locale is: one page, the four process inks, the peak of 50 % cyan on page 1
    pdftest::TestLocaleGuard guard;
    QVERIFY(pdftest::TestLocaleGuard::isDefaultLocaleOtherThanC());

    OutputPreviewTestPdf pdf;
    pdf.addPage("0.5 0 0 0 k 0 0 50 50 re f");
    const PDFDocumentToolInkCoverage tool;
    const PDFDocumentToolResult result = tool.run(createContext(pdf), createParameters(), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    std::map<QString, QString> facts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    QCOMPARE(facts["pages"], QStringLiteral("1 page(s)"));
    QCOMPARE(facts["inks"], QStringLiteral("4 ink(s)"));
    QCOMPARE(facts["peak"], QStringLiteral("50 % (page 1)"));
}

QTEST_MAIN(InkCoverageTest)

#include "tst_inkcoveragetest.moc"
