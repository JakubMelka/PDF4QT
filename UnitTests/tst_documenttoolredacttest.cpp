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

// Redact Content of the core and the check of a redacted copy (step 6 of phase 2, plan 4.3, 6.8, 9;
// package P3): the interface (plan 10.3, the defaults of Figma 437:12794); the check of a copy finds
// what survives (R6-5: the pixels of an image under the fill, B138; a vector or a mesh under the fill;
// text; the structure, R6-6, R6-14) and warns of the kept title (Q6.10) and the soft masks (Q6.19);
// the runs of the tool (the marks, the areas and the search of MCP, the whole pages, the rule of Merge
// R6-9, the layers R6-15) are verified copies.

#include "pdfdocumenttool_redact.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttextindex.h"
#include "pdfredactionverifier.h"
#include "pdfdocumentreader.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfexception.h"
#include "pdfimage.h"
#include "pdfconstants.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfpagecontentprocessor.h"

#include "mergetestpdf.h"

#include <QtTest>

using namespace pdf;

class DocumentToolRedactTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void interface();
    void verifierNeverPassesUnchecked();
    void verifierFindsImagePixelsUnderFill();
    void verifierFindsVectorUnderFill();
    void verifierFindsFillOutsideArea();
    void verifierFindsMeshUnderFill();
    void verifierFindsText();
    void verifierFindsStructure_data();
    void verifierFindsStructure();
    void verifierWordsInKeptTexts();
    void verifierSoftMaskWarning();
    void runMarksIsVerifiedCopy_data();
    void runMarksIsVerifiedCopy();
    void runRestrictedDocumentNeedsOwner();
    void runAreasOfMcp();
    void runSearch();
    void runWholePages();
    void runNothingMarked();
    void runLayers();
    void runKeptTitleWarning();
    void givenParameters();
    void mappedParameters();

    // Step 6 of phase 2, integration P2S6INTCORE
    void searchAcrossLineBreaksRule();
    void explicitMaskBitsPerComponent();
    void type3GlyphBoxes();

    // Step 6 of phase 2, fix wave P2S6FIXCORE
    void areasLimitFitsAreaCount();
    void areasStayInPlaceOnAnyPageSize_data();
    void areasStayInPlaceOnAnyPageSize();
    void verifierFindsInvisibleTextInForm();

private:
    /// A document of one page 200 x 200 of the content (an array of streams, when there are more)
    /// and the resources; the further objects (from 10) by \p objects
    static mergetest::MergeTestPdf createPdf(const QByteArrayList& contents, const QByteArray& resources, const std::map<int, QByteArray>& objects = { },
                                             const QByteArray& pageEntries = QByteArray(), const QByteArray& catalogEntries = QByteArray());

    /// A run of the tool on the document
    static PDFDocumentToolResult run(const PDFDocument& document, const QVariantMap& parameters, bool isOwnerPasswordVerified = false,
                                     std::optional<std::map<PDFObjectReference, OCState>> layerStates = std::nullopt);

    /// The area (50, 50) - (150, 150) of page 1
    static std::vector<PDFRedactionVerifier::Area> getCenterArea();

    /// The options of a check with a black fill
    static PDFRedactionVerifier::Options getBlackFill();

    /// The Type 3 font of glyphs A and B (filled squares of their em, 9.6.4) as object 10
    static std::map<int, QByteArray> getType3Font();

    /// Samples 100 x 100 DeviceRGB of red, the pixels x, y in [from, to) black
    static QByteArray createImageData(int from, int to);
};

mergetest::MergeTestPdf DocumentToolRedactTest::createPdf(const QByteArrayList& contents, const QByteArray& resources, const std::map<int, QByteArray>& objects,
                                                         const QByteArray& pageEntries, const QByteArray& catalogEntries)
{
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R " + catalogEntries + " >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    QByteArray contentReferences;
    for (qsizetype i = 0; i < contents.size(); ++i)
    {
        contentReferences += QByteArray::number(4 + i) + " 0 R ";
    }
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources " + resources + " /Contents [" + contentReferences + "] " + pageEntries + " >>");
    pdf.set(4, mergetest::MergeTestPdf::stream(contents.value(0)));
    if (contents.size() > 1)
    {
        pdf.set(5, mergetest::MergeTestPdf::stream(contents.value(1)));
    }
    for (const auto& [number, text] : objects)
    {
        pdf.set(number, text);
    }
    return pdf;
}

PDFDocumentToolResult DocumentToolRedactTest::run(const PDFDocument& document, const QVariantMap& parameters, bool isOwnerPasswordVerified,
                                                  std::optional<std::map<PDFObjectReference, OCState>> layerStates)
{
    PDFDocumentToolInputDocument input = PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(document)), QByteArray(), QStringLiteral("C:/folder/report.pdf"), false);
    input.isOwnerPasswordVerified = isOwnerPasswordVerified;
    PDFDocumentToolContext context = PDFDocumentToolContext::create(std::move(input));
    context.rendering.layerStates = std::move(layerStates);
    const PDFDocumentToolRedact tool;
    return tool.run(context, parameters, nullptr, nullptr);
}

std::vector<PDFRedactionVerifier::Area> DocumentToolRedactTest::getCenterArea()
{
    QPainterPath path;
    path.addRect(QRectF(50, 50, 100, 100));
    return { { 0, path } };
}

PDFRedactionVerifier::Options DocumentToolRedactTest::getBlackFill()
{
    PDFRedactionVerifier::Options options;
    options.fillColor = QColor(Qt::black);
    return options;
}

std::map<int, QByteArray> DocumentToolRedactTest::getType3Font()
{
    const QByteArray glyph = mergetest::MergeTestPdf::stream("1000 0 0 0 1000 1000 d1 0 0 1000 1000 re f");
    const QByteArray toUnicode = mergetest::MergeTestPdf::stream("/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def "
                                                                 "/CMapName /Adobe-Identity-UCS def /CMapType 2 def 1 begincodespacerange <00> <FF> endcodespacerange "
                                                                 "2 beginbfchar <41> <0041> <42> <0042> endbfchar endcmap CMapName currentdict /CMap defineresource pop end end");
    return { { 10, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /A 11 0 R /B 12 0 R >> "
                   "/Encoding << /Type /Encoding /Differences [65 /A /B] >> /FirstChar 65 /LastChar 66 /Widths [1000 1000] /Resources << >> /ToUnicode 13 0 R >>" },
             { 11, glyph },
             { 12, glyph },
             { 13, toUnicode } };
}

QByteArray DocumentToolRedactTest::createImageData(int from, int to)
{
    QByteArray data;
    for (int y = 0; y < 100; ++y)
    {
        for (int x = 0; x < 100; ++x)
        {
            const bool isFilled = x >= from && x < to && y >= from && y < to;
            data.append(char(isFilled ? 0 : 255));
            data.append(char(0));
            data.append(char(0));
        }
    }
    return data;
}

void DocumentToolRedactTest::interface()
{
    const pdf::PDFDocumentToolRedact tool;
    QCOMPARE(tool.getId(), QStringLiteral("protect.redact"));
    QVERIFY(tool.getInfo());
    QVERIFY(pdf::PDFDocumentToolParameters::isValid(tool.getParameters()));

    // B139: a new document of the pages of the input, not encrypted (the rule of Merge)
    QVERIFY(tool.isNewDocumentOfInputs(QVariantMap()));

    // 437:12794: the fill black, the title and the metadata kept, the outline not; no whole page by default
    const QVariantMap defaults = pdf::PDFDocumentToolParameters::getDefaultValues(tool.getParameters());
    QCOMPARE(defaults.value(QStringLiteral("fillColor")).value<QColor>(), QColor(0, 0, 0));
    QCOMPARE(defaults.value(QStringLiteral("noFill")).toBool(), false);
    QCOMPARE(defaults.value(QStringLiteral("keepTitle")).toBool(), true);
    QCOMPARE(defaults.value(QStringLiteral("keepMetadata")).toBool(), true);
    QCOMPARE(defaults.value(QStringLiteral("keepOutline")).toBool(), false);
    QCOMPARE(defaults.value(QStringLiteral("useMarks")).toBool(), true);
    const std::optional<pdf::PDFDocumentToolPageSelection> pages = pdf::PDFDocumentToolPageSelection::fromVariant(defaults.value(QStringLiteral("pages")), nullptr);
    QVERIFY(pages && pages->isNone);

    // 6.5: MCP gives the areas, the search and the pages
    const QJsonObject properties = pdf::PDFDocumentToolParameters::toJsonSchema(tool.getParameters()).value(QStringLiteral("properties")).toObject();
    for (const char* name : { "useMarks", "areas", "search", "searchRegex", "searchCaseSensitive", "searchWholeWords", "searchAcrossLineBreaks", "pages", "fillColor", "noFill",
                              "keepTitle", "keepMetadata", "keepOutline" })
    {
        QVERIFY2(properties.contains(QString::fromLatin1(name)), name);
    }
}

void DocumentToolRedactTest::verifierNeverPassesUnchecked()
{
    // Owner answer Q6.9: a copy, which cannot be checked, never passes
    const pdf::PDFRedactionVerifier::Result result = pdf::PDFRedactionVerifier::verify(nullptr, { }, QByteArray(), pdf::PDFRedactionVerifier::Options(), nullptr, nullptr);
    QVERIFY(!result.isVerified());

    const PDFDocument source = createPdf({ "0 0 1 rg 0 0 200 200 re f" }, "<< >>").read();
    QVERIFY(!PDFRedactionVerifier::verify(&source, getCenterArea(), QByteArray("not a PDF"), getBlackFill(), nullptr, nullptr).isVerified());
}

void DocumentToolRedactTest::verifierFindsImagePixelsUnderFill()
{
    // R6-5, B138: the copy of the old redaction - the red image under the black fill (the last
    // content stream). The rendered page shows the fill, so only the page without the fill and the
    // pixels of the image show what stays. The copy with the pixels of the area black (as B138 fills
    // them, the area grown by a pixel) passes.
    const PDFDocument source = createPdf({ "q 200 0 0 200 0 0 cm /Im1 Do Q" }, "<< /XObject << /Im1 10 0 R >> >>",
                                         { { 10, mergetest::MergeTestPdf::stream(createImageData(0, 0), "/Type /XObject /Subtype /Image /Width 100 /Height 100 /ColorSpace /DeviceRGB /BitsPerComponent 8") } }).read();
    const QByteArray fill = "q 0 0 0 rg 50 50 m 150 50 l 150 150 l 50 150 l h f Q";

    for (const bool isFilled : { false, true })
    {
        const QByteArray image = isFilled ? createImageData(24, 76) : createImageData(0, 0);
        const QByteArray copy = createPdf({ "q 200 0 0 200 0 0 cm /Im1 Do Q", fill }, "<< /XObject << /Im1 10 0 R >> >>",
                                          { { 10, mergetest::MergeTestPdf::stream(image, "/Type /XObject /Subtype /Image /Width 100 /Height 100 /ColorSpace /DeviceRGB /BitsPerComponent 8") } }).write();
        const PDFRedactionVerifier::Result result = PDFRedactionVerifier::verify(&source, getCenterArea(), copy, getBlackFill(), nullptr, nullptr);
        QCOMPARE(result.isVerified(), isFilled);
        if (!isFilled)
        {
            QCOMPARE(result.errors.size(), size_t(1));
            QCOMPARE(result.errors.front().pageIndex, PDFInteger(0));
            QVERIFY(result.errors.front().message.contains(QStringLiteral("page 1")));
        }
    }

    // No fill: the red image hidden under an image of white pixels (the paper of an area without a
    // fill) - the rendered page shows the paper, the pixels of the hidden image show the content
    QByteArray white;
    for (int i = 0; i < 100 * 100; ++i)
    {
        white.append(QByteArray::fromHex("FFFFFF"));
    }
    const QByteArray hidden = createPdf({ "q 200 0 0 200 0 0 cm /Im1 Do /Im2 Do Q" }, "<< /XObject << /Im1 10 0 R /Im2 11 0 R >> >>",
                                        { { 10, mergetest::MergeTestPdf::stream(createImageData(0, 0), "/Type /XObject /Subtype /Image /Width 100 /Height 100 /ColorSpace /DeviceRGB /BitsPerComponent 8") },
                                          { 11, mergetest::MergeTestPdf::stream(white, "/Type /XObject /Subtype /Image /Width 100 /Height 100 /ColorSpace /DeviceRGB /BitsPerComponent 8") } }).write();
    QVERIFY(!PDFRedactionVerifier::verify(&source, getCenterArea(), hidden, PDFRedactionVerifier::Options(), nullptr, nullptr).isVerified());
}

void DocumentToolRedactTest::verifierFindsVectorUnderFill()
{
    // A black rectangle inside of the area under the black fill: the pixels without the fill show it
    // (it is black, not the paper) and so do the objects; the same rectangle outside of the area is fine
    const PDFDocument source = createPdf({ "0 0 0 rg 60 60 20 20 re f" }, "<< >>").read();
    const QByteArray fill = "q 0 0 0 rg 50 50 m 150 50 l 150 150 l 50 150 l h f Q";

    const QByteArray inside = createPdf({ "0 0 0 rg 60 60 20 20 re f", fill }, "<< >>").write();
    QVERIFY(!PDFRedactionVerifier::verify(&source, getCenterArea(), inside, getBlackFill(), nullptr, nullptr).isVerified());

    const QByteArray outside = createPdf({ "0 0 0 rg 10 10 20 20 re f", fill }, "<< >>").write();
    QVERIFY(PDFRedactionVerifier::verify(&source, getCenterArea(), outside, getBlackFill(), nullptr, nullptr).isVerified());

    // A rectangle across the boundary of the area, but not reaching its inside by more than a pixel
    const QByteArray boundary = createPdf({ "0 0 0 rg 10 60 40.2 20 re f", fill }, "<< >>").write();
    QVERIFY(PDFRedactionVerifier::verify(&source, getCenterArea(), boundary, getBlackFill(), nullptr, nullptr).isVerified());

    // No fill: the area must be paper
    PDFRedactionVerifier::Options noFill;
    const QByteArray noFillCopy = createPdf({ "1 0 0 rg 60 60 20 20 re f" }, "<< >>").write();
    QVERIFY(!PDFRedactionVerifier::verify(&source, getCenterArea(), noFillCopy, noFill, nullptr, nullptr).isVerified());
}

void DocumentToolRedactTest::verifierFindsFillOutsideArea()
{
    // R6-5 (the fill of the areas is the last content stream): the page without its fill is clean, but the fill (its
    // last content stream) is drawn elsewhere - the area shows the paper and the fill covers content,
    // which was not to be redacted (a fill mirrored by a matrix left by the content). The page with its
    // fill must show the fill color inside of the area; a fill of another color fails too.
    const PDFDocument source = createPdf({ "0 0 1 rg 10 10 20 20 re f" }, "<< >>").read();

    const QByteArray covering = createPdf({ "0 0 1 rg 10 10 20 20 re f", "q 0 0 0 rg 50 50 m 150 50 l 150 150 l 50 150 l h f Q" }, "<< >>").write();
    QVERIFY(PDFRedactionVerifier::verify(&source, getCenterArea(), covering, getBlackFill(), nullptr, nullptr).isVerified());

    const QByteArray elsewhere = createPdf({ "0 0 1 rg 10 10 20 20 re f", "q 0 0 0 rg 50 155 m 150 155 l 150 195 l 50 195 l h f Q" }, "<< >>").write();
    const PDFRedactionVerifier::Result result = PDFRedactionVerifier::verify(&source, getCenterArea(), elsewhere, getBlackFill(), nullptr, nullptr);
    QVERIFY(!result.isVerified());
    QCOMPARE(result.errors.size(), size_t(1));
    QCOMPARE(result.errors.front().pageIndex, PDFInteger(0));
    QVERIFY(result.errors.front().message.contains(QStringLiteral("page 1")));

    // The content leaves a flip of y in the graphics state: the fill of the area (50, 120) - (150, 150) is
    // drawn at (50, 50) - (150, 80) (ISO 32000-2, 7.8.2: the state goes on into the next stream)
    QPainterPath upperArea;
    upperArea.addRect(QRectF(50, 120, 100, 30));
    const std::vector<PDFRedactionVerifier::Area> upper = { { 0, upperArea } };
    const QByteArray flipped = createPdf({ "1 0 0 -1 0 200 cm", "q 0 0 0 rg 50 120 m 150 120 l 150 150 l 50 150 l h f Q" }, "<< >>").write();
    QVERIFY(!PDFRedactionVerifier::verify(&source, upper, flipped, getBlackFill(), nullptr, nullptr).isVerified());
    const QByteArray balanced = createPdf({ "q 1 0 0 -1 0 200 cm Q", "q 0 0 0 rg 50 120 m 150 120 l 150 150 l 50 150 l h f Q" }, "<< >>").write();
    QVERIFY(PDFRedactionVerifier::verify(&source, upper, balanced, getBlackFill(), nullptr, nullptr).isVerified());

    PDFRedactionVerifier::Options redFill;
    redFill.fillColor = QColor(Qt::red);
    QVERIFY(!PDFRedactionVerifier::verify(&source, getCenterArea(), covering, redFill, nullptr, nullptr).isVerified());
}

void DocumentToolRedactTest::verifierFindsMeshUnderFill()
{
    // R6-18: a shading (a mesh) under the fill is content, whatever its color
    const QByteArray shading = "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 200 0] /Extend [true true] "
                               "/Function << /FunctionType 2 /Domain [0 1] /C0 [0 0 0] /C1 [0 0 0] /N 1 >> >>";
    const PDFDocument source = createPdf({ "/Sh1 sh" }, "<< /Shading << /Sh1 10 0 R >> >>", { { 10, shading } }).read();
    const QByteArray fill = "q 0 0 0 rg 50 50 m 150 50 l 150 150 l 50 150 l h f Q";
    const QByteArray copy = createPdf({ "/Sh1 sh", fill }, "<< /Shading << /Sh1 10 0 R >> >>", { { 10, shading } }).write();
    const PDFRedactionVerifier::Result result = PDFRedactionVerifier::verify(&source, getCenterArea(), copy, getBlackFill(), nullptr, nullptr);
    QVERIFY(!result.isVerified());
}

void DocumentToolRedactTest::verifierFindsText()
{
    // 6.8 step 1: the text of the copy is curves; a text operator (also outside of the areas) fails
    std::map<int, QByteArray> objects = getType3Font();
    const PDFDocument source = createPdf({ "0 0 1 rg 0 0 10 10 re f" }, "<< >>").read();
    const QByteArray fill = "q 0 0 0 rg 50 50 m 150 50 l 150 150 l 50 150 l h f Q";
    const QByteArray copy = createPdf({ "BT /F1 10 Tf 10 10 Td (AB) Tj ET", fill }, "<< /Font << /F1 10 0 R >> >>", objects).write();
    const PDFRedactionVerifier::Result result = PDFRedactionVerifier::verify(&source, getCenterArea(), copy, getBlackFill(), nullptr, nullptr);
    QVERIFY(!result.isVerified());
}

void DocumentToolRedactTest::verifierFindsStructure_data()
{
    QTest::addColumn<QByteArray>("catalogEntries");
    QTest::addColumn<QByteArray>("pageEntries");
    QTest::addColumn<QByteArray>("trailerEntries");
    QTest::addColumn<bool>("hasUnusedObject");

    // 6.8 step 1, R6-14: only the pages, their resources, the outline and /Info of direct values
    QTest::newRow("clean") << QByteArray() << QByteArray() << QByteArray() << false;
    QTest::newRow("acroform") << QByteArray("/AcroForm << /Fields [] >>") << QByteArray() << QByteArray() << false;
    QTest::newRow("open-action") << QByteArray("/OpenAction << /S /JavaScript /JS (x) >>") << QByteArray() << QByteArray() << false;
    QTest::newRow("annotations") << QByteArray() << QByteArray("/Annots [] ") << QByteArray() << false;
    QTest::newRow("info-reference") << QByteArray() << QByteArray() << QByteArray("/Info << /Title 3 0 R >>") << false;
    QTest::newRow("info-custom") << QByteArray() << QByteArray() << QByteArray("/Info << /Secret (x) >>") << false;
    QTest::newRow("unused-object") << QByteArray() << QByteArray() << QByteArray() << true;
}

void DocumentToolRedactTest::verifierFindsStructure()
{
    QFETCH(QByteArray, catalogEntries);
    QFETCH(QByteArray, pageEntries);
    QFETCH(QByteArray, trailerEntries);
    QFETCH(bool, hasUnusedObject);

    const PDFDocument source = createPdf({ "0 0 1 rg 0 0 10 10 re f" }, "<< >>").read();
    const QByteArray fill = "q 0 0 0 rg 50 50 m 150 50 l 150 150 l 50 150 l h f Q";
    std::map<int, QByteArray> objects;
    if (hasUnusedObject)
    {
        objects[20] = "(left over)";
    }
    mergetest::MergeTestPdf pdf = createPdf({ "0 0 1 rg 0 0 10 10 re f", fill }, "<< >>", objects, pageEntries, catalogEntries);
    pdf.setTrailerEntries(trailerEntries);
    const bool isClean = catalogEntries.isEmpty() && pageEntries.isEmpty() && trailerEntries.isEmpty() && !hasUnusedObject;
    QCOMPARE(PDFRedactionVerifier::verify(&source, getCenterArea(), pdf.write(), getBlackFill(), nullptr, nullptr).isVerified(), isClean);
}

void DocumentToolRedactTest::verifierWordsInKeptTexts()
{
    // 6.8 step 3, owner answer Q6.10: the word "ABAB" (Type 3 glyphs) is under the area; the copy is
    // clean, but its kept title has the word - a warning when the title is kept, never the word itself
    std::map<int, QByteArray> objects = getType3Font();
    const PDFDocument source = createPdf({ "BT /F1 20 Tf 60 100 Td (ABAB) Tj ET" }, "<< /Font << /F1 10 0 R >> >>", objects).read();
    const QByteArray fill = "q 0 0 0 rg 50 50 m 150 50 l 150 150 l 50 150 l h f Q";
    mergetest::MergeTestPdf pdf = createPdf({ "", fill }, "<< >>");
    pdf.setTrailerEntries("/Info << /Title (Report abab of the year) /Author (Jane) >>");
    const QByteArray copy = pdf.write();

    PDFRedactionVerifier::Options options = getBlackFill();
    options.isTitleKept = true;
    const PDFRedactionVerifier::Result kept = PDFRedactionVerifier::verify(&source, getCenterArea(), copy, options, nullptr, nullptr);
    QVERIFY(kept.isVerified());
    QCOMPARE(kept.warnings.size(), size_t(1));
    QVERIFY(!kept.warnings.front().message.contains(QStringLiteral("ABAB"), Qt::CaseInsensitive));

    // The title, which was not kept by the options, is an error
    const PDFRedactionVerifier::Result notKept = PDFRedactionVerifier::verify(&source, getCenterArea(), copy, getBlackFill(), nullptr, nullptr);
    QVERIFY(!notKept.isVerified());

    // A word of the page outside of the areas is no finding
    std::vector<PDFRedactionVerifier::Area> otherArea;
    QPainterPath path;
    path.addRect(QRectF(0, 0, 30, 30));
    otherArea.push_back({ 0, path });
    const QByteArray otherFill = "q 0 0 0 rg 0 0 m 30 0 l 30 30 l 0 30 l h f Q";
    mergetest::MergeTestPdf otherPdf = createPdf({ "", otherFill }, "<< >>");
    otherPdf.setTrailerEntries("/Info << /Title (Report abab of the year) >>");
    const PDFRedactionVerifier::Result other = PDFRedactionVerifier::verify(&source, otherArea, otherPdf.write(), options, nullptr, nullptr);
    QVERIFY(other.isVerified());
    QVERIFY(other.warnings.empty());
}

void DocumentToolRedactTest::verifierSoftMaskWarning()
{
    // R6-16, owner answer Q6.19 a: a soft mask of an ExtGState (11.6.5.2), which the rebuilt page
    // cannot keep, is a warning with the page number - not a failure
    const QByteArray mask = mergetest::MergeTestPdf::stream("1 g 0 0 200 200 re f", "/Type /XObject /Subtype /Form /BBox [0 0 200 200] /Group << /S /Transparency /CS /DeviceGray >>");
    const PDFDocument source = createPdf({ "/GS1 gs 0 0 1 rg 0 0 200 200 re f" }, "<< /ExtGState << /GS1 << /SMask << /S /Luminosity /G 10 0 R >> >> >> >>", { { 10, mask } }).read();
    const QByteArray fill = "q 0 0 0 rg 50 50 m 150 50 l 150 150 l 50 150 l h f Q";
    const QByteArray copy = createPdf({ "0 0 1 rg 0 0 200 50 re f", fill }, "<< >>").write();
    const PDFRedactionVerifier::Result result = PDFRedactionVerifier::verify(&source, getCenterArea(), copy, getBlackFill(), nullptr, nullptr);
    QVERIFY(result.isVerified());
    QCOMPARE(result.warnings.size(), size_t(1));
    QCOMPARE(result.warnings.front().pageIndex, PDFInteger(0));
    QVERIFY(result.warnings.front().message.contains(QStringLiteral("1")));
}

void DocumentToolRedactTest::runMarksIsVerifiedCopy_data()
{
    QTest::addColumn<QByteArray>("content");
    QTest::addColumn<QByteArray>("resources");
    QTest::addColumn<bool>("noFill");

    // Every kind of content under a mark: the copy passes its own check (no false finding)
    const QByteArray image = "<< /XObject << /Im1 10 0 R >> >>";
    const QByteArray shading = "<< /Shading << /Sh1 11 0 R >> >>";
    QTest::newRow("vector") << QByteArray("0 0 1 rg 0 0 200 200 re f 1 0 0 RG 5 w 0 100 m 200 100 l S") << QByteArray("<< >>") << false;
    QTest::newRow("vector-no-fill") << QByteArray("0 0 1 rg 0 0 200 200 re f 1 0 0 RG 5 w 0 100 m 200 100 l S") << QByteArray("<< >>") << true;
    QTest::newRow("image") << QByteArray("q 200 0 0 200 0 0 cm /Im1 Do Q") << image << false;
    QTest::newRow("image-no-fill") << QByteArray("q 200 0 0 200 0 0 cm /Im1 Do Q") << image << true;
    QTest::newRow("shading") << QByteArray("/Sh1 sh") << shading << false;
    QTest::newRow("type3") << QByteArray("BT /F1 30 Tf 40 90 Td (ABAB) Tj ET") << QByteArray("<< /Font << /F1 12 0 R >> >>") << false;
}

void DocumentToolRedactTest::runMarksIsVerifiedCopy()
{
    QFETCH(QByteArray, content);
    QFETCH(QByteArray, resources);
    QFETCH(bool, noFill);

    std::map<int, QByteArray> objects;
    objects[10] = mergetest::MergeTestPdf::stream(createImageData(0, 0), "/Type /XObject /Subtype /Image /Width 100 /Height 100 /ColorSpace /DeviceRGB /BitsPerComponent 8");
    objects[11] = "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 200 0] /Extend [true true] /Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >> >>";
    const std::map<int, QByteArray> font = getType3Font();
    objects[12] = font.at(10);
    objects[13] = font.at(11);
    objects[14] = font.at(12);
    objects[15] = font.at(13);
    objects[12].replace("/A 11 0 R /B 12 0 R", "/A 13 0 R /B 14 0 R");
    objects[12].replace("/ToUnicode 13 0 R", "/ToUnicode 15 0 R");
    objects[20] = "<< /Type /Annot /Subtype /Redact /Rect [50 50 150 150] >>";
    const PDFDocument document = createPdf({ content }, resources, objects, "/Annots [20 0 R]").read();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("noFill"), noFill);
    const PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.size(), size_t(1));
    const PDFDocumentToolOutputDocument& output = result.documents.front();
    QCOMPARE(output.kind, PDFDocumentToolOutputDocument::Kind::New);
    QCOMPARE(output.suggestedFileName, QStringLiteral("report_REDACTED.pdf"));
    QVERIFY(!output.writtenData.isEmpty());

    // The written bytes are the copy: one page, no annotation, not encrypted
    const PDFDocument copy = mergetest::MergeTestPdf::readBytes(output.writtenData);
    QCOMPARE(copy.getCatalog()->getPageCount(), size_t(1));
    QVERIFY(copy.getCatalog()->getPage(0)->getAnnotations().empty());
    QVERIFY(!copy.getTrailerDictionary()->hasKey("Encrypt"));

    // MCP data (6.5): counts and the state of the check, never a text
    QCOMPARE(result.data.value(QStringLiteral("areaCount")).toInteger(), 1);
    QCOMPARE(result.data.value(QStringLiteral("pageCount")).toInteger(), 1);
    QCOMPARE(result.data.value(QStringLiteral("verified")).toBool(), true);

    // The facts of 437:13887
    QStringList factIds;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        factIds << fact.id;
    }
    QVERIFY(factIds.contains(QStringLiteral("removed")));
    QVERIFY(factIds.contains(QStringLiteral("fill")));
    QVERIFY(factIds.contains(QStringLiteral("kept")));

    // 437:13887 (review RS6FIGMA): the default Keep (title and metadata, no outline) says "Title and metadata"
    const auto kept = std::find_if(result.facts.cbegin(), result.facts.cend(), [](const PDFDocumentToolFact& fact) { return fact.id == QStringLiteral("kept"); });
    QVERIFY(kept != result.facts.cend());
    QCOMPARE(kept->value, QStringLiteral("Title and metadata"));
}

void DocumentToolRedactTest::runRestrictedDocumentNeedsOwner()
{
    // R6-9, B139: the copy is not encrypted - a document opened by its user password with restricted
    // permissions (Table 22) needs its owner password (the rule of Merge), in the core for every caller
    const PDFDocument plain = createPdf({ "0 0 1 rg 0 0 200 200 re f" }, "<< >>", { { 20, "<< /Type /Annot /Subtype /Redact /Rect [50 50 150 150] >>" } }, "/Annots [20 0 R]").read();
    const QByteArray encrypted = mergetest::MergeTestPdf::encrypt(plain, QString(), QStringLiteral("owner"), 0);
    bool isRead = false;
    const PDFDocument document = mergetest::MergeTestPdf::readBytes(encrypted, QString(), &isRead);
    QVERIFY(isRead);

    const PDFDocumentToolResult refused = run(document, QVariantMap());
    QCOMPARE(refused.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(refused.documents.empty());

    const PDFDocumentToolResult permitted = run(document, QVariantMap(), true);
    QVERIFY2(permitted.isSuccess(), qPrintable(permitted.message));
}

void DocumentToolRedactTest::runAreasOfMcp()
{
    // 6.5: the areas of MCP in the default user space (the origin at the lower left corner of the page)
    const PDFDocument document = createPdf({ "0 0 1 rg 0 0 200 200 re f" }, "<< >>").read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("areas"), QStringLiteral("[{\"page\": 1, \"rect\": [10, 20, 30, 40]}, {\"page\": 1, \"rect\": [100, 100, 50, 50]}]"));
    const PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("areaCount")).toInteger(), 2);

    // The fill of the copy is at the given rectangles
    const PDFDocument copy = mergetest::MergeTestPdf::readBytes(result.documents.front().writtenData);
    const PDFObject& contents = copy.getObject(copy.getCatalog()->getPage(0)->getContents());
    QVERIFY(contents.isArray());
    const QByteArray fill = copy.getDecodedStream(copy.getObject(contents.getArray()->getItem(contents.getArray()->getCount() - 1)).getStream());
    QVERIFY(fill.contains("10 20 m") || fill.contains("10 60 m") || fill.contains("40 20 m") || fill.contains("40 60 m"));

    // Invalid areas (untrusted input of MCP)
    for (const char* areas : { "not json", "[{\"page\": 0, \"rect\": [0, 0, 10, 10]}]", "[{\"page\": 2, \"rect\": [0, 0, 10, 10]}]", "[{\"page\": 1, \"rect\": [0, 0, -10, 10]}]",
                               "[{\"page\": 1, \"rect\": [0, 0, 10]}]", "[{\"page\": 1, \"rect\": [0, 0, 10, 10], \"text\": \"x\"}]", "{\"page\": 1}" })
    {
        QVariantMap invalid;
        invalid.insert(QStringLiteral("areas"), QString::fromLatin1(areas));
        QCOMPARE(run(document, invalid).status, PDFDocumentToolResultStatus::Failed);
    }
}

void DocumentToolRedactTest::runSearch()
{
    // The search (6.5, K6.21): "ABAB" on the first line and "BABA" on the second (Type 3 glyphs). The
    // text "ABAB BABA" continues on the next line: a match only with Across line breaks.
    std::map<int, QByteArray> objects = getType3Font();
    const PDFDocument document = createPdf({ "BT /F1 20 Tf 20 120 Td (ABAB) Tj 0 -24 Td (BABA) Tj ET" }, "<< /Font << /F1 10 0 R >> >>", objects).read();

    QVariantMap word;
    word.insert(QStringLiteral("useMarks"), false);
    word.insert(QStringLiteral("search"), QStringLiteral("abab"));
    const PDFDocumentToolResult found = run(document, word);
    QVERIFY2(found.isSuccess(), qPrintable(found.message));
    QCOMPARE(found.data.value(QStringLiteral("areaCount")).toInteger(), 1);

    QVariantMap twoLines;
    twoLines.insert(QStringLiteral("useMarks"), false);
    twoLines.insert(QStringLiteral("search"), QStringLiteral("ABAB BABA"));
    QCOMPARE(run(document, twoLines).status, PDFDocumentToolResultStatus::NothingToDo);

    twoLines.insert(QStringLiteral("searchAcrossLineBreaks"), true);
    const PDFDocumentToolResult across = run(document, twoLines);
    QVERIFY2(across.isSuccess(), qPrintable(across.message));
    QCOMPARE(across.data.value(QStringLiteral("areaCount")).toInteger(), 1);

    // An invalid regular expression is an error of the parameter
    QVariantMap invalid;
    invalid.insert(QStringLiteral("search"), QStringLiteral("(AB"));
    invalid.insert(QStringLiteral("searchRegex"), true);
    QCOMPARE(run(document, invalid).status, PDFDocumentToolResultStatus::Failed);

    // The search helper of the panel gives the same matches
    const std::shared_ptr<const PDFDocumentTextIndex> index = PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QCOMPARE(PDFDocumentToolRedact::findMatches(*index, QStringLiteral("ABAB BABA"), PDFDocumentTextSearchOptions(), false, nullptr).results.size(), size_t(0));
    const PDFDocumentTextSearchResult acrossResult = PDFDocumentToolRedact::findMatches(*index, QStringLiteral("ABAB BABA"), PDFDocumentTextSearchOptions(), true, nullptr);
    QCOMPARE(acrossResult.results.size(), size_t(1));
    QVERIFY(PDFDocumentToolRedact::getMatchArea(acrossResult.results.front()).boundingRect().contains(QRectF(20, 96, 80, 44)));
}

void DocumentToolRedactTest::runWholePages()
{
    // R6-30: Pages... - a whole page is a blank page of the same size with the fill
    mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("", 3);
    const PDFDocument document = pdf.read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("useMarks"), false);
    PDFDocumentToolPageSelection selection;
    selection.pages = QStringLiteral("2");
    parameters.insert(QStringLiteral("pages"), selection.toVariant());
    const PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("pageCount")).toInteger(), 1);

    const PDFDocument copy = mergetest::MergeTestPdf::readBytes(result.documents.front().writtenData);
    QCOMPARE(copy.getCatalog()->getPageCount(), size_t(3));
    const PDFObject& contents = copy.getObject(copy.getCatalog()->getPage(1)->getContents());
    QVERIFY(contents.isArray());
    QCOMPARE(copy.getDecodedStream(copy.getObject(contents.getArray()->getItem(0)).getStream()).trimmed(), QByteArray());
}

void DocumentToolRedactTest::runNothingMarked()
{
    // No mark, no area: nothing to redact - an error of the validation, no copy
    const PDFDocument document = createPdf({ "0 0 1 rg 0 0 200 200 re f" }, "<< >>").read();
    const PDFDocumentToolResult result = run(document, QVariantMap());
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.documents.empty());

    // The search, which finds nothing
    QVariantMap parameters;
    parameters.insert(QStringLiteral("search"), QStringLiteral("nothing"));
    QCOMPARE(run(document, parameters).status, PDFDocumentToolResultStatus::NothingToDo);
}

void DocumentToolRedactTest::runLayers()
{
    // R6-15: a layer OFF (by /D of /OCProperties, the state of an export) is not carried over - the fact
    // says so; the states of a tab (the GUI) decide instead
    const QByteArray catalogEntries = "/OCProperties << /OCGs [30 0 R] /D << /OFF [30 0 R] >> >>";
    std::map<int, QByteArray> objects = { { 30, "<< /Type /OCG /Name (Hidden) >>" }, { 20, "<< /Type /Annot /Subtype /Redact /Rect [0 0 10 10] >>" } };
    const PDFDocument document = createPdf({ "/OC /L1 BDC 1 0 0 rg 100 100 50 50 re f EMC" }, "<< /Properties << /L1 30 0 R >> >>", objects, "/Annots [20 0 R]", catalogEntries).read();

    auto hasLayersFact = [](const PDFDocumentToolResult& result)
    {
        return std::any_of(result.facts.cbegin(), result.facts.cend(), [](const PDFDocumentToolFact& fact) { return fact.id == QStringLiteral("layers"); });
    };

    const PDFDocumentToolResult exported = run(document, QVariantMap());
    QVERIFY2(exported.isSuccess(), qPrintable(exported.message));
    QVERIFY(hasLayersFact(exported));

    std::map<PDFObjectReference, OCState> states;
    states[PDFObjectReference(30, 0)] = OCState::ON;
    const PDFDocumentToolResult shown = run(document, QVariantMap(), false, states);
    QVERIFY2(shown.isSuccess(), qPrintable(shown.message));
    QVERIFY(!hasLayersFact(shown));
}

void DocumentToolRedactTest::runKeptTitleWarning()
{
    // Owner answer Q6.10: the kept title with a redacted word - the copy is made, the warning is a row
    // of the steps and of the data of MCP
    std::map<int, QByteArray> objects = getType3Font();
    objects[20] = "<< /Type /Annot /Subtype /Redact /Rect [50 50 150 150] >>";
    objects[21] = "<< /Title (ABAB report) >>";
    mergetest::MergeTestPdf pdf = createPdf({ "BT /F1 20 Tf 60 100 Td (ABAB) Tj ET" }, "<< /Font << /F1 10 0 R >> >>", objects, "/Annots [20 0 R]");
    pdf.setTrailerEntries("/Info 21 0 R");
    const PDFDocument document = pdf.read();

    const PDFDocumentToolResult result = run(document, QVariantMap());
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("warnings")).toArray().size(), 1);
    QVERIFY(std::any_of(result.steps.cbegin(), result.steps.cend(), [](const PDFDocumentToolStep& step) { return step.state == PDFDocumentToolStep::State::Warning; }));

    // Without the title kept: no warning
    QVariantMap parameters;
    parameters.insert(QStringLiteral("keepTitle"), false);
    const PDFDocumentToolResult withoutTitle = run(document, parameters);
    QVERIFY2(withoutTitle.isSuccess(), qPrintable(withoutTitle.message));
    QCOMPARE(withoutTitle.data.value(QStringLiteral("warnings")).toArray().size(), 0);
}

void DocumentToolRedactTest::givenParameters()
{
    // A value, which nothing uses, is an error of a caller, who gives only some values (MCP)
    const PDFDocumentToolRedact tool;
    QVariantMap searchOption;
    searchOption.insert(QStringLiteral("searchRegex"), true);
    QCOMPARE(tool.checkGivenParameters(searchOption).size(), size_t(1));

    QVariantMap fill;
    fill.insert(QStringLiteral("noFill"), true);
    fill.insert(QStringLiteral("fillColor"), QColor(Qt::red));
    QCOMPARE(tool.checkGivenParameters(fill).size(), size_t(1));

    QVariantMap valid;
    valid.insert(QStringLiteral("search"), QStringLiteral("x"));
    valid.insert(QStringLiteral("searchRegex"), true);
    QVERIFY(tool.checkGivenParameters(valid).empty());
}

void DocumentToolRedactTest::mappedParameters()
{
    // The areas and the whole pages follow their pages into another version of the document (the
    // pages 1 and 2 swapped); an area of a page, which is not there, is dropped
    mergetest::MergeTestPdf from = mergetest::MergeTestPdf::createPages("", 3);
    mergetest::MergeTestPdf to = mergetest::MergeTestPdf::createPages("", 3);
    to.set(2, "<< /Type /Pages /Kids [4 0 R 3 0 R] /Count 2 /MediaBox [0 0 600 800] /Resources << >> >>");
    const PDFDocument fromDocument = from.read();
    const PDFDocument toDocument = to.read();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("areas"), QStringLiteral("[{\"page\": 1, \"rect\": [1, 2, 3, 4]}, {\"page\": 3, \"rect\": [5, 6, 7, 8]}]"));
    const PDFDocumentToolRedact tool;
    const QVariantMap mapped = tool.mapParameters(parameters, &fromDocument, &toDocument);
    const QJsonArray areas = QJsonDocument::fromJson(mapped.value(QStringLiteral("areas")).toString().toUtf8()).array();
    QCOMPARE(areas.size(), 1);
    QCOMPARE(areas.at(0).toObject().value(QStringLiteral("page")).toInteger(), 2);
    QCOMPARE(areas.at(0).toObject().value(QStringLiteral("rect")).toArray(), QJsonArray({ 1, 2, 3, 4 }));
}

void DocumentToolRedactTest::searchAcrossLineBreaksRule()
{
    // K6.21 (the rule of the search panel, P5 open point 9): off - a match, whose characters are not all on
    // one line, is dropped; on - each run of white space of a plain term is any run of white space (the end
    // of a line is a space of the text flow), an expression of the user is searched as it is
    QCOMPARE(PDFDocumentToolRedact::createLineBreakPattern(QStringLiteral("a  b.c\t d")), QStringLiteral("a\\s+b\\.c\\s+d"));
    QCOMPARE(PDFDocumentToolRedact::createLineBreakPattern(QStringLiteral("word")), QStringLiteral("word"));

    const PDFDocument document = createPdf({ "BT /F1 20 Tf 20 120 Td (ABAB) Tj 0 -24 Td (BABA) Tj ET" }, "<< /Font << /F1 10 0 R >> >>", getType3Font()).read();
    const std::shared_ptr<const PDFDocumentTextIndex> index = PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);

    const PDFDocumentTextSearchOptions plain;
    const QString twoSpaces = QStringLiteral("ABAB  BABA");
    QCOMPARE(PDFDocumentToolRedact::findMatches(*index, twoSpaces, plain, false, nullptr).results.size(), size_t(0));
    const PDFDocumentTextSearchResult across = PDFDocumentToolRedact::findMatches(*index, twoSpaces, plain, true, nullptr);
    QCOMPARE(across.results.size(), size_t(1));
    QCOMPARE(across.results.front().pageIndex, PDFInteger(0));

    // A match on one line stays with the option off
    QCOMPARE(PDFDocumentToolRedact::findMatches(*index, QStringLiteral("BA"), plain, false, nullptr).results.size(), size_t(3));

    // An expression of the user: as it is, the option only decides whether a match may cross a line end
    PDFDocumentTextSearchOptions regex;
    regex.regularExpression = true;
    QCOMPARE(PDFDocumentToolRedact::findMatches(*index, QStringLiteral("ABAB\\sBABA"), regex, true, nullptr).results.size(), size_t(1));
    QCOMPARE(PDFDocumentToolRedact::findMatches(*index, QStringLiteral("ABAB\\sBABA"), regex, false, nullptr).results.size(), size_t(0));
    QCOMPARE(PDFDocumentToolRedact::findMatches(*index, QStringLiteral("ABAB\\s\\sBABA"), regex, true, nullptr).results.size(), size_t(0));

    // The whole words of a plain term across a line break
    PDFDocumentTextSearchOptions wholeWords;
    wholeWords.wholeWords = true;
    QCOMPARE(PDFDocumentToolRedact::findMatches(*index, QStringLiteral("ABAB BABA"), wholeWords, true, nullptr).results.size(), size_t(1));
    QCOMPARE(PDFDocumentToolRedact::findMatches(*index, QStringLiteral("BAB BAB"), wholeWords, true, nullptr).results.size(), size_t(0));
}

void DocumentToolRedactTest::explicitMaskBitsPerComponent()
{
    // ISO 32000-2, 8.9.6.3 and Table 87: an explicit /Mask is an image mask, whose /BitsPerComponent is
    // optional and 1. A mask without it (2 x 1: the first sample 0 = painted, the second 1 = masked
    // out) masks the image; the default 8 of a color image read 8 bits for 2 samples (an invalid mask)
    const QByteArray pixels("\xFF\x00\x00\x00\x00\xFF", 6);
    const std::map<int, QByteArray> objects = { { 10, mergetest::MergeTestPdf::stream(pixels, "/Type /XObject /Subtype /Image /Width 2 /Height 1 /ColorSpace /DeviceRGB /BitsPerComponent 8 /Mask 11 0 R") },
                                                { 11, mergetest::MergeTestPdf::stream(QByteArray(1, char(0x40)), "/Type /XObject /Subtype /Image /Width 2 /Height 1 /ImageMask true") } };
    const PDFDocument document = createPdf({ "q 200 0 0 200 0 0 cm /Im1 Do Q" }, "<< /XObject << /Im1 10 0 R >> >>", objects).read();

    const PDFObject& imageObject = document.getObjectByReference(PDFObjectReference(10, 0));
    QVERIFY(imageObject.isStream());
    PDFImage image;
    try
    {
        const PDFColorSpacePointer colorSpace = PDFAbstractColorSpace::createDeviceColorSpaceByName(nullptr, &document, nullptr, QByteArrayLiteral("DeviceRGB"));
        image = PDFImage::createImage(&document, imageObject.getStream(), colorSpace, false, RenderingIntent::Perceptual, nullptr);
    }
    catch (const PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }
    QCOMPARE(image.getSoftMaskData().getBitsPerComponent(), 1u);

    PDFCMSGeneric cms;
    const QImage converted = image.getImage(&cms, nullptr, nullptr);
    QCOMPARE(converted.size(), QSize(2, 1));
    QCOMPARE(qAlpha(converted.pixel(0, 0)), 255);
    QCOMPARE(qRed(converted.pixel(0, 0)), 255);
    QCOMPARE(qAlpha(converted.pixel(1, 0)), 0);
}

void DocumentToolRedactTest::type3GlyphBoxes()
{
    // B147 (ISO 32000-2, 9.6.4, 9.6.5, Tables 111 and 112): the box of a glyph of a Type 3 font in the text
    // layout is the box of its d1 operator, else /FontBBox of the font, else (a d0 glyph of a font, whose
    // /FontBBox is all zeros: no assumption) the estimate of its advance - the advance wide and 1.2 advance
    // high from 0.2 advance below the baseline. Font size 20, font matrix 0.001: a glyph unit is 0.02 point.
    const QByteArray widths = "/FirstChar 65 /LastChar 66 /Widths [1000 1000] /Resources << >> /ToUnicode 13 0 R";
    const QByteArray encoding = "/Encoding << /Type /Encoding /Differences [65 /A /B] >>";
    std::map<int, QByteArray> objects = getType3Font();
    objects[10] = "<< /Type /Font /Subtype /Type3 /FontBBox [0 -200 1000 800] /FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /A 11 0 R /B 12 0 R >> " + encoding + " " + widths + " >>";
    objects[11] = mergetest::MergeTestPdf::stream("1000 0 0 0 500 700 d1 0 0 500 700 re f");
    objects[12] = mergetest::MergeTestPdf::stream("1000 0 d0 0 0 1 rg 0 0 1000 800 re f");
    objects[20] = "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 0 0] /FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /A 12 0 R /B 12 0 R >> " + encoding + " " + widths + " >>";
    const PDFDocument document = createPdf({ "BT /F1 20 Tf 20 100 Td (AB) Tj ET BT /F2 20 Tf 20 150 Td (A) Tj ET" }, "<< /Font << /F1 10 0 R /F2 20 0 R >> >>", objects).read();

    const std::shared_ptr<const PDFDocumentTextIndex> index = PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);
    std::vector<std::pair<QChar, QRectF>> boxes;
    const PDFTextLayout layout = index->getTextLayout(0);
    for (const PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const PDFTextLine& line : block.getLines())
        {
            for (const TextCharacter& character : line.getCharacters())
            {
                boxes.emplace_back(character.character, character.boundingBox.boundingRect());
            }
        }
    }
    std::sort(boxes.begin(), boxes.end(), [](const auto& left, const auto& right) { return left.second.top() < right.second.top() || (left.second.top() == right.second.top() && left.second.left() < right.second.left()); });

    const auto isNear = [](const QRectF& box, const QRectF& expected)
    {
        return qAbs(box.left() - expected.left()) < 0.01 && qAbs(box.top() - expected.top()) < 0.01 && qAbs(box.width() - expected.width()) < 0.01 && qAbs(box.height() - expected.height()) < 0.01;
    };
    const auto toText = [](const QRectF& box) { return QStringLiteral("%1 %2 %3 %4").arg(box.left()).arg(box.top()).arg(box.width()).arg(box.height()); };
    QStringList found;
    for (const auto& [character, box] : boxes)
    {
        found << QString(character) + QLatin1Char(' ') + toText(box);
    }
    QVERIFY2(boxes.size() == 3, qPrintable(found.join(QStringLiteral("; "))));

    // A (d1 0 0 500 700) at x 20: 20 ... 30, 100 ... 114; B (d0, /FontBBox 0 -200 1000 800) at x 40: 40 ... 60, 96 ... 116
    // (the boxes are in the user space of the page, whose y goes up - the layout keeps its coordinates)
    QCOMPARE(boxes[0].first, QChar('B'));
    QVERIFY2(isNear(boxes[0].second, QRectF(40, 96, 20, 20)), qPrintable(toText(boxes[0].second)));
    QCOMPARE(boxes[1].first, QChar('A'));
    QVERIFY2(isNear(boxes[1].second, QRectF(20, 100, 10, 14)), qPrintable(toText(boxes[1].second)));

    // A of the second font (d0, /FontBBox all zeros) at y 150: the estimate 20 ... 40, 146 ... 170
    QCOMPARE(boxes[2].first, QChar('A'));
    QVERIFY2(isNear(boxes[2].second, QRectF(20, 146, 20, 24)), qPrintable(toText(boxes[2].second)));
}

void DocumentToolRedactTest::areasLimitFitsAreaCount()
{
    // ASan D1 of step 6: the areas of every page of a large document were refused by the length of the
    // text ("Areas: the text is longer than 65536 characters") before their count was checked. Plan 7.2
    // limits the areas by their count (10 000): the length of their JSON has to fit that count, and MCP
    // stays bounded (the schema gives the length, at most the longest structured String).
    const PDFDocumentToolRedact tool;
    const PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    const auto it = std::find_if(definitions.cbegin(), definitions.cend(), [](const PDFDocumentToolParameterDefinition& definition) { return definition.getName() == QLatin1String("areas"); });
    QVERIFY(it != definitions.cend());
    QVERIFY(it->isStructuredData());
    QCOMPARE(it->getMaximumLength(), PDFDocumentToolRedact::MAXIMUM_AREAS_LENGTH);
    QVERIFY(PDFDocumentToolRedact::MAXIMUM_AREAS_LENGTH <= PDFDocumentToolParameterDefinition::MAXIMUM_DATA_TEXT_LENGTH);
    const QJsonObject schema = PDFDocumentToolParameters::toJsonSchema(definitions);
    QCOMPARE(schema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("areas")).toObject().value(QStringLiteral("maxLength")).toInteger(),
             qint64(PDFDocumentToolRedact::MAXIMUM_AREAS_LENGTH));

    // The most areas the plan allows, each with numbers of full precision (17 digits and a sign), is
    // longer than any other text of a parameter, and passes as a value and for a document of one page
    const QString area = QStringLiteral("{\"page\": 1, \"rect\": [-12345.678901234567, -12345.678901234567, 12345.678901234567, 12345.678901234567]}");
    const auto createAreas = [&area](int count)
    {
        QStringList areas;
        for (int i = 0; i < count; ++i)
        {
            areas << area;
        }
        return QStringLiteral("[%1]").arg(areas.join(QStringLiteral(", ")));
    };
    const QString mostAreas = createAreas(PDFDocumentToolRedact::MAXIMUM_AREA_COUNT);
    QVERIFY(mostAreas.size() > PDFDocumentToolParameterDefinition::MAXIMUM_TEXT_LENGTH);

    const PDFDocument document = createPdf({ "0 0 1 rg 0 0 200 200 re f" }, "<< >>").read();
    const PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(document)), QByteArray(), QString(), false));
    QVariantMap normalized;
    QVERIFY(PDFDocumentToolParameters::normalize(definitions, QVariantMap({ { QStringLiteral("areas"), mostAreas } }), &normalized).empty());
    QVERIFY(tool.validate(context, normalized).empty());

    // One more area: the length still fits, the count is the error (with the limit)
    const QString tooMany = createAreas(PDFDocumentToolRedact::MAXIMUM_AREA_COUNT + 1);
    QVERIFY(tooMany.size() <= PDFDocumentToolRedact::MAXIMUM_AREAS_LENGTH);
    QVERIFY(PDFDocumentToolParameters::normalize(definitions, QVariantMap({ { QStringLiteral("areas"), tooMany } }), &normalized).empty());
    const std::vector<PDFDocumentToolParameterError> countErrors = tool.validate(context, normalized);
    QCOMPARE(countErrors.size(), size_t(1));
    QCOMPARE(countErrors.front().parameter, QStringLiteral("areas"));
    QVERIFY2(countErrors.front().message.contains(QLatin1String("10000")), qPrintable(countErrors.front().message));

    // Both edges of the length: MAXIMUM_AREAS_LENGTH characters pass, one more is refused (a JSON padded by white space)
    const QString longest = QStringLiteral("[%1]").arg(QString(PDFDocumentToolRedact::MAXIMUM_AREAS_LENGTH - 2 - area.size(), QLatin1Char(' ')) + area);
    QCOMPARE(longest.size(), qsizetype(PDFDocumentToolRedact::MAXIMUM_AREAS_LENGTH));
    QVERIFY(PDFDocumentToolParameters::normalize(definitions, QVariantMap({ { QStringLiteral("areas"), longest } }), &normalized).empty());
    QVERIFY(tool.validate(context, normalized).empty());
    const std::vector<PDFDocumentToolParameterError> lengthErrors = PDFDocumentToolParameters::normalize(definitions, QVariantMap({ { QStringLiteral("areas"), QString(longest).insert(1, QLatin1Char(' ')) } }), &normalized);
    QCOMPARE(lengthErrors.size(), size_t(1));
    QCOMPARE(lengthErrors.front().parameter, QStringLiteral("areas"));
}

/// The filled paths of a page in its default user space (ISO 32000-2, 8.3.2.3)
class RedactFilledPathCollector : public PDFPageContentProcessor
{
public:
    using PDFPageContentProcessor::PDFPageContentProcessor;

    std::vector<QPainterPath> filledPaths;

protected:
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override
    {
        Q_UNUSED(stroke);
        Q_UNUSED(text);
        if (fill)
        {
            QPainterPath pagePath = getCurrentWorldMatrix().map(path);
            pagePath.setFillRule(fillRule);
            filledPaths.push_back(std::move(pagePath));
        }
    }
};

void DocumentToolRedactTest::areasStayInPlaceOnAnyPageSize_data()
{
    QTest::addColumn<QSizeF>("pageSize");

    // QPdfWriter takes the height of its page in whole points, a size within 3 points of a standard
    // one as that size (B149): 210 x 295 is drawn as 210 x 297 (ASan D2: Transparency/a.pdf), a
    // height of 468.516541 as 469 (Transparency/FranckCondon.pdf); 200 x 200 is exact
    QTest::newRow("near-standard-size") << QSizeF(210.0, 295.0);
    QTest::newRow("fractional-height") << QSizeF(454.967285, 468.516541);
    QTest::newRow("whole-points") << QSizeF(200.0, 200.0);
}

void DocumentToolRedactTest::areasStayInPlaceOnAnyPageSize()
{
    // ASan D2 of step 6 (12 files of the corpus refused "Content under 1 areas ... could not be
    // removed"): the redacted content was drawn by a matrix without the correction of the height of the
    // page of QPdfWriter, so it moved against the areas and a strip of it stayed under them. The content
    // of the copy keeps its place exactly: no filled path reaches inside of the area (ISO 32000-2,
    // 12.5.6.23: the content within the area is removed) and the content just outside of it is kept.
    QFETCH(QSizeF, pageSize);

    const QByteArray width = QByteArray::number(pageSize.width(), 'f', 6);
    const QByteArray height = QByteArray::number(pageSize.height(), 'f', 6);
    mergetest::MergeTestPdf pdf = createPdf({ "0 0 0 rg 0 0 " + width + " " + height + " re f" }, "<< >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + width + " " + height + "] /Resources << >> /Contents [4 0 R] >>");
    const PDFDocument document = pdf.read();

    const QRectF area(pageSize.width() / 3.0, pageSize.height() / 3.0, pageSize.width() / 3.0, pageSize.height() / 3.0);
    QVariantMap parameters;
    parameters.insert(QStringLiteral("useMarks"), false);
    parameters.insert(QStringLiteral("noFill"), true);
    parameters.insert(QStringLiteral("areas"), QStringLiteral("[{\"page\": 1, \"rect\": [%1, %2, %3, %4]}]").arg(area.x(), 0, 'f', 6).arg(area.y(), 0, 'f', 6).arg(area.width(), 0, 'f', 6).arg(area.height(), 0, 'f', 6));
    const PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const PDFDocument copy = mergetest::MergeTestPdf::readBytes(result.documents.front().writtenData);
    const PDFPage* page = copy.getCatalog()->getPage(0);
    QVERIFY(page);
    QCOMPARE(page->getMediaBox(), QRectF(QPointF(0, 0), pageSize));

    PDFOptionalContentActivity activity(&copy, OCUsage::View, nullptr);
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&copy), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&copy), &activity));
    PDFCMSGeneric cms;
    RedactFilledPathCollector collector(page, &copy, &fontCache, &iccProfileCache, &cms, &activity, QTransform(), PDFMeshQualitySettings());
    QVERIFY(collector.processContents().isEmpty());
    QVERIFY(!collector.filledPaths.empty());
    const auto isFilled = [&collector](const QPointF& point)
    {
        return std::any_of(collector.filledPaths.cbegin(), collector.filledPaths.cend(), [&point](const QPainterPath& path) { return path.contains(point); });
    };

    // 0.1 pt inside and outside of each edge of the area, and its center
    constexpr PDFReal MARGIN = 0.1;
    const QPointF center = area.center();
    QVERIFY(!isFilled(center));
    QVERIFY(!isFilled(QPointF(area.left() + MARGIN, center.y())));
    QVERIFY(!isFilled(QPointF(area.right() - MARGIN, center.y())));
    QVERIFY(!isFilled(QPointF(center.x(), area.top() + MARGIN)));
    QVERIFY(!isFilled(QPointF(center.x(), area.bottom() - MARGIN)));
    QVERIFY(isFilled(QPointF(area.left() - MARGIN, center.y())));
    QVERIFY(isFilled(QPointF(area.right() + MARGIN, center.y())));
    QVERIFY(isFilled(QPointF(center.x(), area.top() - MARGIN)));
    QVERIFY(isFilled(QPointF(center.x(), area.bottom() + MARGIN)));
}

void DocumentToolRedactTest::verifierFindsInvisibleTextInForm()
{
    // The check of the hidden data of the copy leaves the invisible text out (its scan stops at a budget
    // of decoded content and then reports it as present - a copy of BIBLE21.pdf without any text was
    // refused); the check of the text operators of every content stream and form XObject finds it
    // without a limit: an invisible text (ISO 32000-2, 9.3.6: render mode 3) in a form XObject of a page
    std::map<int, QByteArray> objects = getType3Font();
    objects[20] = mergetest::MergeTestPdf::stream("BT 3 Tr /F1 10 Tf 10 10 Td (AB) Tj ET", "/Type /XObject /Subtype /Form /BBox [0 0 200 200] /Resources << /Font << /F1 10 0 R >> >>");
    const PDFDocument source = createPdf({ "0 0 1 rg 0 0 10 10 re f" }, "<< >>").read();
    const QByteArray fill = "q 0 0 0 rg 50 50 m 150 50 l 150 150 l 50 150 l h f Q";
    const QByteArray copy = createPdf({ "/Fm1 Do", fill }, "<< /XObject << /Fm1 20 0 R >> >>", objects).write();
    const PDFRedactionVerifier::Result result = PDFRedactionVerifier::verify(&source, getCenterArea(), copy, getBlackFill(), nullptr, nullptr);
    QVERIFY(!result.isVerified());
    QVERIFY(std::any_of(result.errors.cbegin(), result.errors.cend(), [](const PDFRedactionVerifier::Finding& finding) { return finding.message == QLatin1String("A form of the redacted copy has text."); }));
}

QTEST_GUILESS_MAIN(DocumentToolRedactTest)

#include "tst_documenttoolredacttest.moc"
