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

// Edit Content, stage E0: the layout of a shown text by ISO 32000-2, 9.4.4 with the widths of
// the font dictionary (package P06; GUI_overhaul/edit_tool/zadani_jadro.md 4.7, zadani_overeni.md
// chapter 8).
//
// The fonts are those of contenttestdocuments.h (their widths are written there: they are not
// the widths of the font programs, so a test sees, where an advance came from) and a few fonts
// written here. Every expected position is computed by hand in the comment of its test from
// the formulas of the standard:
//
//   9.4.2  Td: Tlm = [1 0 0 1 tx ty] x Tlm, Tm = Tlm;  T*: 0 -Tl TD;  Tm: Tm = Tlm = the operands
//   9.4.4  tx = ((w0 - Tj / 1000) * Tfs + Tc + Tw) * Th, ty = 0                 (horizontal)
//          tx = 0, ty = (w1 - Tj / 1000) * Tfs + Tc + Tw                        (vertical)
//          Tm = [1 0 0 1 tx ty] x Tm
//   9.3.3  Tw belongs to the single-byte character code 32 only
//
// A matrix is written as the six numbers [a b c d e f] of the standard.

#include "contenttestdocuments.h"
#include "pdfcontenttextlayout.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdffont.h"
#include "pdfparser.h"

#include <QtTest>
#include <QTransform>

#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

using namespace pdf;
using pdftest::ContentTestDocuments;

/// A document and a font created from one of its objects (the document must live as long as
/// the font is being created; it is kept for the whole test)
struct ContentTextLayoutFont
{
    PDFDocument document;
    PDFFontPointer font;
};

class ContentTextLayoutTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void initialValues();
    void textLineOperators();
    void lineMoveDiscardsAdvances();
    void simpleFontAdvances_data();
    void simpleFontAdvances();
    void wordSpacing_data();
    void wordSpacing();
    void simpleFontDictionaryWidths();
    void fontWithoutWidths();
    void type0Widths();
    void codesOfString();
    void type0IncompleteCode();
    void type3Widths();
    void type3FontMatrix();
    void verticalWritingMode();
    void adjustments();
    void quoteOperators();
    void textRiseAndMatrix();
    void unicodeTexts();
    void unicodeOfToUnicodeFirst();
    void unicodeOfCharacterCollection();
    void outlines();
    void type3Glyphs();
    void editableFonts();
    void completeness();
    void hostileNumbers();
    void hostileFonts();
    void longString();
    void manyThreads();

private:
    /// Tolerance of the compared numbers: the expected values are sums and products of a few
    /// decimal numbers
    static constexpr PDFReal EPSILON = 1e-9;

    static bool isNumber(PDFReal actual, PDFReal expected) { return std::abs(actual - expected) <= EPSILON; }
    static bool isMatrix(const QTransform& matrix, PDFReal a, PDFReal b, PDFReal c, PDFReal d, PDFReal e, PDFReal f);
    static QString matrixText(const QTransform& matrix);
    static QString numberText(PDFReal actual, PDFReal expected);

    /// A font of the document of ContentTestDocuments::createFontsDocument (/F1, /F2, /FC, /F0,
    /// /FM, /FN, /FV, /T3, /TN)
    PDFFontPointer getFont(const char* name) const;

    /// A font of a dictionary written as a text (no stream in it)
    static ContentTextLayoutFont createFont(const QByteArray& fontDictionary);

    /// A font, whose dictionary needs objects of a document (streams)
    static ContentTextLayoutFont createFont(const std::function<PDFObject(PDFDocumentBuilder*)>& createFontObject);

    /// One of the standard 14 fonts without /Widths
    static ContentTextLayoutFont createStandardFont();

    /// A Type 3 font with one glyph (the code 65, the glyph name /a, the width 500 of the glyph
    /// space) and /FontMatrix [0.002 0.001 0 0.002 5 0], without /ToUnicode
    /// \param hasWidths false: the font dictionary has no /Widths (a damaged font, the key is required)
    static ContentTextLayoutFont createSkewedType3Font(bool hasWidths);

    static PDFRealizedFontPointer createRealizedFont(const PDFFontPointer& font, PDFReal size);

    static PDFContentTextState createState(const PDFFontPointer& font, PDFReal fontSize);
    static PDFContentTextLayout::Code createCode(unsigned int code, int byteCount, CID cid);

    PDFDocument m_document;
};

#define COMPARE_NUMBER(actual, expected) \
    QVERIFY2(ContentTextLayoutTest::isNumber(actual, expected), qPrintable(ContentTextLayoutTest::numberText(actual, expected)))

#define COMPARE_MATRIX(actual, a, b, c, d, e, f) \
    QVERIFY2(ContentTextLayoutTest::isMatrix(actual, a, b, c, d, e, f), qPrintable(ContentTextLayoutTest::matrixText(actual)))

bool ContentTextLayoutTest::isMatrix(const QTransform& matrix, PDFReal a, PDFReal b, PDFReal c, PDFReal d, PDFReal e, PDFReal f)
{
    return isNumber(matrix.m11(), a) && isNumber(matrix.m12(), b) && isNumber(matrix.m21(), c) && isNumber(matrix.m22(), d) &&
           isNumber(matrix.dx(), e) && isNumber(matrix.dy(), f) && matrix.m13() == 0.0 && matrix.m23() == 0.0 && matrix.m33() == 1.0;
}

QString ContentTextLayoutTest::matrixText(const QTransform& matrix)
{
    return QStringLiteral("the matrix is [%1 %2 %3 %4 %5 %6]").arg(matrix.m11(), 0, 'g', 15).arg(matrix.m12(), 0, 'g', 15)
            .arg(matrix.m21(), 0, 'g', 15).arg(matrix.m22(), 0, 'g', 15).arg(matrix.dx(), 0, 'g', 15).arg(matrix.dy(), 0, 'g', 15);
}

QString ContentTextLayoutTest::numberText(PDFReal actual, PDFReal expected)
{
    return QStringLiteral("the number is %1, expected %2").arg(actual, 0, 'g', 15).arg(expected, 0, 'g', 15);
}

PDFFontPointer ContentTextLayoutTest::getFont(const char* name) const
{
    const PDFDictionary* resources = m_document.getDictionaryFromObject(m_document.getCatalog()->getPage(0)->getResources());
    const PDFDictionary* fonts = resources ? m_document.getDictionaryFromObject(resources->get("Font")) : nullptr;
    if (!fonts || !fonts->hasKey(name))
    {
        return PDFFontPointer();
    }

    return PDFFont::createFont(fonts->get(name), name, &m_document);
}

ContentTextLayoutFont ContentTextLayoutTest::createFont(const QByteArray& fontDictionary)
{
    PDFDocumentBuilder builder;
    builder.createDocument();

    ContentTextLayoutFont result;
    result.document = builder.build();

    PDFParser parser(fontDictionary, nullptr, PDFParser::None);
    result.font = PDFFont::createFont(parser.getObject(), "FX", &result.document);
    return result;
}

ContentTextLayoutFont ContentTextLayoutTest::createFont(const std::function<PDFObject(PDFDocumentBuilder*)>& createFontObject)
{
    PDFDocumentBuilder builder;
    builder.createDocument();
    const PDFObject fontObject = createFontObject(&builder);

    ContentTextLayoutFont result;
    result.document = builder.build();
    result.font = PDFFont::createFont(fontObject, "FX", &result.document);
    return result;
}

ContentTextLayoutFont ContentTextLayoutTest::createStandardFont()
{
    return createFont("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
}

ContentTextLayoutFont ContentTextLayoutTest::createSkewedType3Font(bool hasWidths)
{
    return createFont([hasWidths](PDFDocumentBuilder* builder)
    {
        PDFObject procedure = ContentTestDocuments::addStreamObject(builder, ContentTestDocuments::DictionaryEntries(), QByteArray("500 0 d0 0 0 100 100 re f"));
        PDFObject encoding = ContentTestDocuments::createDictionaryObject({ { "Type", PDFObject::createName("Encoding") },
                                                                            { "Differences", ContentTestDocuments::createArrayObject({ PDFObject::createInteger(65), PDFObject::createName("a") }) } });

        ContentTestDocuments::DictionaryEntries font = { { "Type", PDFObject::createName("Font") },
                                                         { "Subtype", PDFObject::createName("Type3") },
                                                         { "FontBBox", ContentTestDocuments::createIntegerArrayObject({ 0, 0, 1000, 1000 }) },
                                                         { "FontMatrix", ContentTestDocuments::createNumberArrayObject({ 0.002, 0.001, 0.0, 0.002, 5.0, 0.0 }) },
                                                         { "FirstChar", PDFObject::createInteger(65) },
                                                         { "LastChar", PDFObject::createInteger(65) },
                                                         { "Encoding", std::move(encoding) },
                                                         { "CharProcs", ContentTestDocuments::createDictionaryObject({ { "a", std::move(procedure) } }) } };
        if (hasWidths)
        {
            font.emplace_back("Widths", ContentTestDocuments::createIntegerArrayObject({ 500 }));
        }

        return ContentTestDocuments::createDictionaryObject(std::move(font));
    });
}

PDFRealizedFontPointer ContentTextLayoutTest::createRealizedFont(const PDFFontPointer& font, PDFReal size)
{
    PDFRenderErrorReporterDummy reporter;
    return PDFRealizedFont::createRealizedFont(font, size, &reporter);
}

PDFContentTextState ContentTextLayoutTest::createState(const PDFFontPointer& font, PDFReal fontSize)
{
    PDFContentTextState state;
    state.font = font;
    state.fontSize = fontSize;
    return state;
}

PDFContentTextLayout::Code ContentTextLayoutTest::createCode(unsigned int code, int byteCount, CID cid)
{
    PDFContentTextLayout::Code result;
    for (int i = byteCount - 1; i >= 0; --i)
    {
        result.bytes.append(static_cast<char>((code >> (8 * i)) & 0xFF));
    }
    result.code = code;
    result.cid = cid;
    return result;
}

void ContentTextLayoutTest::initTestCase()
{
    m_document = ContentTestDocuments::createFontsDocument(QByteArray("BT ET"));

    for (const char* name : { "F1", "F2", "FC", "F0", "FM", "FN", "FV", "T3", "TN" })
    {
        QVERIFY2(getFont(name), name);
    }
}

void ContentTextLayoutTest::initialValues()
{
    // The initial values of the text state (ISO 32000-2, 9.3.1, Table 102): Tc 0, Tw 0, Th 100
    // (percent), Tl 0, Tmode 0 (fill), Trise 0; Tf and Tfs have no initial value - no font, the
    // text cannot be laid out
    PDFContentTextState state;
    QVERIFY(state.font.isNull());
    QVERIFY(state.realizedFont.isNull());
    QCOMPARE(state.fontSize, 0.0);
    QCOMPARE(state.characterSpacing, 0.0);
    QCOMPARE(state.wordSpacing, 0.0);
    QCOMPARE(state.horizontalScaling, 100.0);
    QCOMPARE(state.leading, 0.0);
    QCOMPARE(state.rise, 0.0);
    QVERIFY(state.renderingMode == TextRenderingMode::Fill);
    QCOMPARE(int(state.renderingMode), 0);

    // A default item is a glyph without anything: no code, no Unicode value, no width of the font
    // dictionary (which is not the width 0), no outline, no move
    PDFContentTextItem item;
    QVERIFY(item.kind == PDFContentTextItem::Kind::Glyph);
    QVERIFY(item.codeBytes.isEmpty());
    QCOMPARE(item.code, 0u);
    QCOMPARE(item.cid, CID(0));
    QVERIFY(item.unicode.isEmpty());
    QVERIFY(!item.width.has_value());
    QVERIFY(!item.isWordSpace);
    QVERIFY(!item.hasOutline);
    QVERIFY(item.outline == nullptr);
    QCOMPARE(item.adjustment, 0.0);
    QVERIFY(item.textMatrix.isIdentity());
    QCOMPARE(item.advance, 0.0);

    // A new layout is at the beginning of a text object: the text matrix and the text line matrix
    // are the identity matrix (9.4.1), and nothing was shown without a width yet
    PDFContentTextLayout layout;
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(layout.getTextLineMatrix().isIdentity());
    QVERIFY(layout.isComplete());
}

void ContentTextLayoutTest::textLineOperators()
{
    PDFContentTextLayout layout;
    layout.beginText();

    // 10 20 Td on the identity: Tlm = [1 0 0 1 10 20], Tm = Tlm
    layout.moveByOffset(10.0, 20.0);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 1, 0, 0, 1, 10, 20);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 10, 20);

    // 2 0 0 3 5 7 Tm replaces both matrices (it is not concatenated with [1 0 0 1 10 20])
    layout.setMatrix(QTransform(2, 0, 0, 3, 5, 7));
    COMPARE_MATRIX(layout.getTextLineMatrix(), 2, 0, 0, 3, 5, 7);
    COMPARE_MATRIX(layout.getTextMatrix(), 2, 0, 0, 3, 5, 7);

    // 10 20 Td: Tlm = [1 0 0 1 10 20] x [2 0 0 3 5 7]; e = 10 * 2 + 5 = 25, f = 20 * 3 + 7 = 67
    // (the offset is in the text space: the product in the other order gives e = 15, f = 27)
    layout.moveByOffset(10.0, 20.0);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 2, 0, 0, 3, 25, 67);
    COMPARE_MATRIX(layout.getTextMatrix(), 2, 0, 0, 3, 25, 67);

    // T* with the leading 12 is 0 -12 TD (Table 106), which moves as 0 -12 Td: f = -12 * 3 + 67 = 31
    layout.moveByLeading(12.0);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 2, 0, 0, 3, 25, 31);
    COMPARE_MATRIX(layout.getTextMatrix(), 2, 0, 0, 3, 25, 31);

    // A text turned by 90 degrees, 0 1 -1 0 100 50 Tm, then 10 0 Td: e = 10 * 0 + 0 * (-1) + 100 = 100,
    // f = 10 * 1 + 0 * 0 + 50 = 60 - the text moves up on the page
    layout.setMatrix(QTransform(0, 1, -1, 0, 100, 50));
    layout.moveByOffset(10.0, 0.0);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 0, 1, -1, 0, 100, 60);
    COMPARE_MATRIX(layout.getTextMatrix(), 0, 1, -1, 0, 100, 60);

    // 5 -14 TD is -(-14) TL and 5 -14 Td (9.4.2, Table 106): the caller keeps the leading 14;
    // then T* moves by it once more. On [1 0 0 1 100 90]: [1 0 0 1 105 76], then [1 0 0 1 105 62]
    layout.setMatrix(QTransform(1, 0, 0, 1, 100, 90));
    layout.moveByOffset(5.0, -14.0);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 1, 0, 0, 1, 105, 76);
    layout.moveByLeading(14.0);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 1, 0, 0, 1, 105, 62);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 105, 62);

    // BT: both matrices are the identity again (9.4.1)
    layout.beginText();
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(layout.getTextLineMatrix().isIdentity());
}

void ContentTextLayoutTest::lineMoveDiscardsAdvances()
{
    // /F1 10 Tf 100 100 Td (A) Tj: A has the width 700, tx = 0.7 * 10 = 7, so Tm = [1 0 0 1 107 100],
    // while the text line matrix stays at the start of the line [1 0 0 1 100 100]
    PDFContentTextLayout layout;
    layout.beginText();
    const PDFContentTextState state = createState(getFont("F1"), 10.0);

    layout.moveByOffset(100.0, 100.0);
    const std::vector<PDFContentTextItem> items = layout.showString("A", state);
    QCOMPARE(items.size(), size_t(1));
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 107, 100);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 1, 0, 0, 1, 100, 100);

    // 0 -12 Td starts from the text line matrix, not from the text matrix: [1 0 0 1 100 88]
    layout.moveByOffset(0.0, -12.0);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 1, 0, 0, 1, 100, 88);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 100, 88);
}

void ContentTextLayoutTest::simpleFontAdvances_data()
{
    QTest::addColumn<PDFReal>("fontSize");
    QTest::addColumn<PDFReal>("characterSpacing");
    QTest::addColumn<PDFReal>("wordSpacing");
    QTest::addColumn<PDFReal>("horizontalScaling");
    QTest::addColumn<QList<PDFReal>>("advances");

    // The string (A B) in /F1: the codes 65 (the width 700), 32 (300) and 66 (650);
    // tx = (w0 * Tfs + Tc + Tw) * Th, Tw for the code 32 only

    // 0.7 * 10 = 7; 0.3 * 10 = 3; 0.65 * 10 = 6.5
    QTest::newRow("widths") << 10.0 << 0.0 << 0.0 << 100.0 << QList<PDFReal>{ 7.0, 3.0, 6.5 };

    // Tc 1 for every code: 7 + 1 = 8; 3 + 1 = 4; 6.5 + 1 = 7.5
    QTest::newRow("Tc") << 10.0 << 1.0 << 0.0 << 100.0 << QList<PDFReal>{ 8.0, 4.0, 7.5 };

    // Tw 2 for the code 32 only: 7; 3 + 2 = 5; 6.5
    QTest::newRow("Tw") << 10.0 << 0.0 << 2.0 << 100.0 << QList<PDFReal>{ 7.0, 5.0, 6.5 };

    // Tz 50: 7 * 0.5 = 3.5; 3 * 0.5 = 1.5; 6.5 * 0.5 = 3.25
    QTest::newRow("Tz") << 10.0 << 0.0 << 0.0 << 50.0 << QList<PDFReal>{ 3.5, 1.5, 3.25 };

    // Tc 1, Tw 2, Tz 50 (the spacings are scaled too): (7 + 1) * 0.5 = 4; (3 + 1 + 2) * 0.5 = 3;
    // (6.5 + 1) * 0.5 = 3.75
    QTest::newRow("Tc Tw Tz") << 10.0 << 1.0 << 2.0 << 50.0 << QList<PDFReal>{ 4.0, 3.0, 3.75 };

    // The font size -10 (9.3.1 allows it), Tc 1, Tw 2: the widths turn, the spacings do not -
    // -7 + 1 = -6; -3 + 1 + 2 = 0; -6.5 + 1 = -5.5
    QTest::newRow("negative font size") << -10.0 << 1.0 << 2.0 << 100.0 << QList<PDFReal>{ -6.0, 0.0, -5.5 };

    // The font size 0.5 and Tc -0.1: 0.35 - 0.1 = 0.25; 0.15 - 0.1 = 0.05; 0.325 - 0.1 = 0.225
    QTest::newRow("negative Tc") << 0.5 << -0.1 << 0.0 << 100.0 << QList<PDFReal>{ 0.25, 0.05, 0.225 };
}

void ContentTextLayoutTest::simpleFontAdvances()
{
    QFETCH(PDFReal, fontSize);
    QFETCH(PDFReal, characterSpacing);
    QFETCH(PDFReal, wordSpacing);
    QFETCH(PDFReal, horizontalScaling);
    QFETCH(QList<PDFReal>, advances);

    PDFContentTextState state = createState(getFont("F1"), fontSize);
    state.characterSpacing = characterSpacing;
    state.wordSpacing = wordSpacing;
    state.horizontalScaling = horizontalScaling;

    PDFContentTextLayout layout;
    layout.beginText();
    const std::vector<PDFContentTextItem> items = layout.showString("A B", state);
    QCOMPARE(items.size(), size_t(3));

    // The item i is at the sum of the advances before it; the text matrix ends at the sum of all
    PDFReal position = 0.0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const PDFContentTextItem& item = items[i];
        QVERIFY(item.kind == PDFContentTextItem::Kind::Glyph);
        COMPARE_MATRIX(item.textMatrix, 1, 0, 0, 1, position, 0);
        COMPARE_NUMBER(item.advance, advances[qsizetype(i)]);
        position += advances[qsizetype(i)];
    }
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, position, 0);
    QVERIFY(layout.getTextLineMatrix().isIdentity());
    QVERIFY(layout.isComplete());

    // The codes, the widths of /Widths in the text space units and the texts
    QCOMPARE(items[0].codeBytes, QByteArray("A"));
    QCOMPARE(items[0].code, 65u);
    QCOMPARE(items[0].cid, CID(65));
    QCOMPARE(items[0].unicode, QStringLiteral("A"));
    QVERIFY(items[0].width.has_value());
    COMPARE_NUMBER(*items[0].width, 0.7);
    QVERIFY(!items[0].isWordSpace);

    QCOMPARE(items[1].codeBytes, QByteArray(" "));
    QCOMPARE(items[1].code, 32u);
    QCOMPARE(items[1].unicode, QStringLiteral(" "));
    QVERIFY(items[1].width.has_value());
    COMPARE_NUMBER(*items[1].width, 0.3);
    QVERIFY(items[1].isWordSpace);

    QCOMPARE(items[2].code, 66u);
    QCOMPARE(items[2].unicode, QStringLiteral("B"));
    QVERIFY(items[2].width.has_value());
    COMPARE_NUMBER(*items[2].width, 0.65);
    QVERIFY(!items[2].isWordSpace);
}

void ContentTextLayoutTest::wordSpacing_data()
{
    QTest::addColumn<QByteArray>("fontName");
    QTest::addColumn<QByteArray>("string");
    QTest::addColumn<PDFReal>("advance");
    QTest::addColumn<bool>("isWordSpace");
    QTest::addColumn<QString>("unicode");

    // One code at the font size 10 with Tw 5 (9.3.3: "Word spacing shall be applied to every
    // occurrence of the single-byte character code 32 in a string when using a simple font
    // (including Type 3) or a composite font that defines code 32 as a single-byte code. It shall
    // not apply to occurrences of the byte value 32 in multiple-byte codes.")

    // /F1: the code 32 is the space, the width 300: 0.3 * 10 + 5 = 8
    QTest::newRow("simple font, code 32") << QByteArray("F1") << QByteArray(" ") << 8.0 << true << QStringLiteral(" ");

    // /F1: the code 68 shows the glyph /space (U+0020), the width 550: 0.55 * 10 = 5.5, no Tw
    QTest::newRow("simple font, space under the code 68") << QByteArray("F1") << QByteArray("D") << 5.5 << false << QStringLiteral(" ");

    // /F2: the code 32 shows the glyph /A, the width 300: 0.3 * 10 + 5 = 8 - Tw though it is no space
    QTest::newRow("simple font, code 32 is the letter A") << QByteArray("F2") << QByteArray(" ") << 8.0 << true << QStringLiteral("A");

    // /F2: the code 65 shows the glyph /space, the width 700: 0.7 * 10 = 7
    QTest::newRow("simple font, space under the code 65") << QByteArray("F2") << QByteArray("A") << 7.0 << false << QStringLiteral(" ");

    // /F0: the code <0020> of two bytes is U+0020, the CID 32 has the width 250: 0.25 * 10 = 2.5
    QTest::newRow("Type 0, code 0x0020 of two bytes") << QByteArray("F0") << QByteArray("\x00\x20", 2) << 2.5 << false << QStringLiteral(" ");

    // /FM: the code <20> has one byte, the CID 32 has the width 250: 0.25 * 10 + 5 = 7.5
    QTest::newRow("Type 0, single-byte code 32") << QByteArray("FM") << QByteArray("\x20") << 7.5 << true << QStringLiteral(" ");

    // /FM: the code <8020> of two bytes is U+0020, the CID 32800 has /DW 900: 0.9 * 10 = 9
    QTest::newRow("Type 0, byte 32 in a code of two bytes") << QByteArray("FM") << QByteArray("\x80\x20", 2) << 9.0 << false << QStringLiteral(" ");

    // /FN: the single-byte code <20> is the CID 0 with /DW 900 and without a text: 0.9 * 10 + 5 = 14
    QTest::newRow("Type 0, single-byte code 32 without a glyph") << QByteArray("FN") << QByteArray("\x20") << 14.0 << true << QString();

    // /T3: the code 32 has the width 50 of the glyph space, 50 * 0.01 = 0.5: 0.5 * 10 + 5 = 10
    QTest::newRow("Type 3, code 32") << QByteArray("T3") << QByteArray(" ") << 10.0 << true << QStringLiteral(" ");

    // /T3: the code 65 has the width 60: 0.6 * 10 = 6
    QTest::newRow("Type 3, code 65") << QByteArray("T3") << QByteArray("A") << 6.0 << false << QStringLiteral("A");
}

void ContentTextLayoutTest::wordSpacing()
{
    QFETCH(QByteArray, fontName);
    QFETCH(QByteArray, string);
    QFETCH(PDFReal, advance);
    QFETCH(bool, isWordSpace);
    QFETCH(QString, unicode);

    PDFContentTextState state = createState(getFont(fontName.constData()), 10.0);
    state.wordSpacing = 5.0;

    PDFContentTextLayout layout;
    layout.beginText();
    const std::vector<PDFContentTextItem> items = layout.showString(string, state);
    QCOMPARE(items.size(), size_t(1));
    QCOMPARE(items[0].codeBytes, string);
    QCOMPARE(items[0].isWordSpace, isWordSpace);
    QCOMPARE(items[0].unicode, unicode);
    COMPARE_NUMBER(items[0].advance, advance);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, advance, 0);
    QVERIFY(layout.isComplete());
}

void ContentTextLayoutTest::simpleFontDictionaryWidths()
{
    // /F1: /FirstChar 32, /LastChar 68, /Widths by ContentTestDocuments::simpleFontWidth,
    // /MissingWidth 400 (9.6.2.1, Table 109; 9.8.1, Table 120: the width of the codes, whose
    // widths are not in /Widths)
    const PDFFontPointer font = getFont("F1");
    const auto width = [&font](unsigned int code) { return PDFContentTextLayout::getDictionaryWidth(font.data(), createCode(code, 1, code)); };

    QVERIFY(width(32).has_value());
    COMPARE_NUMBER(*width(32), 0.3);
    COMPARE_NUMBER(*width(33), 0.5);
    COMPARE_NUMBER(*width(65), 0.7);
    COMPARE_NUMBER(*width(68), 0.55);

    // Outside of /FirstChar ... /LastChar: /MissingWidth
    QVERIFY(width(31).has_value());
    COMPARE_NUMBER(*width(31), 0.4);
    QVERIFY(width(69).has_value());
    COMPARE_NUMBER(*width(69), 0.4);
    QVERIFY(width(255).has_value());
    COMPARE_NUMBER(*width(255), 0.4);

    // <C8> (A) at the size 10: the code 200 moves by 0.4 * 10 = 4, so A is at 4 and the text ends
    // at 4 + 7 = 11; every code had a width
    PDFContentTextLayout layout;
    layout.beginText();
    std::vector<PDFContentTextItem> items = layout.showString(QByteArray("\xC8" "A"), createState(font, 10.0));
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items[0].code, 200u);
    COMPARE_NUMBER(items[0].advance, 4.0);
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 4, 0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 11, 0);
    QVERIFY(layout.isComplete());

    // /FC has /FirstChar 1: the codes 1 ... 31 have the width 450 in /Widths, the code 0 is
    // before /FirstChar
    const PDFFontPointer controlFont = getFont("FC");
    const std::optional<PDFReal> controlWidth = PDFContentTextLayout::getDictionaryWidth(controlFont.data(), createCode(1, 1, 1));
    QVERIFY(controlWidth.has_value());
    COMPARE_NUMBER(*controlWidth, 0.45);
    const std::optional<PDFReal> zeroWidth = PDFContentTextLayout::getDictionaryWidth(controlFont.data(), createCode(0, 1, 0));
    QVERIFY(zeroWidth.has_value());
    COMPARE_NUMBER(*zeroWidth, 0.4);

    // A font with /Widths and without /MissingWidth: /FirstChar 65, /LastChar 70, /Widths
    // [610 620] (shorter than the range). The codes 65 and 66 have their widths; the code 67
    // (inside of the range, no item) and the code 71 (outside) have the default /MissingWidth 0
    // (Table 120: "Default value: 0") - a width 0, not "no width"
    const ContentTextLayoutFont shortFont = createFont("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding "
                                                       "/FirstChar 65 /LastChar 70 /Widths [610 620] "
                                                       "/FontDescriptor << /Type /FontDescriptor /FontName /Helvetica /Flags 32 /FontBBox [-166 -225 1000 931] "
                                                       "/ItalicAngle 0 /Ascent 718 /Descent -207 /CapHeight 718 /StemV 88 >> >>");
    QVERIFY(shortFont.font);
    const auto shortWidth = [&shortFont](unsigned int code) { return PDFContentTextLayout::getDictionaryWidth(shortFont.font.data(), createCode(code, 1, code)); };
    QVERIFY(shortWidth(65).has_value());
    COMPARE_NUMBER(*shortWidth(65), 0.61);
    COMPARE_NUMBER(*shortWidth(66), 0.62);
    QVERIFY(shortWidth(67).has_value());
    COMPARE_NUMBER(*shortWidth(67), 0.0);
    QVERIFY(shortWidth(71).has_value());
    COMPARE_NUMBER(*shortWidth(71), 0.0);

    // (ACB) at the size 10: A at 0, C at 6.1, B at 6.1 + 0 = 6.1, the end at 6.1 + 6.2 = 12.3
    layout.beginText();
    items = layout.showString("ACB", createState(shortFont.font, 10.0));
    QCOMPARE(items.size(), size_t(3));
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 6.1, 0);
    COMPARE_NUMBER(items[1].advance, 0.0);
    COMPARE_MATRIX(items[2].textMatrix, 1, 0, 0, 1, 6.1, 0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 12.3, 0);
    QVERIFY(layout.isComplete());
    QVERIFY(PDFContentTextLayout::isEditable(shortFont.font.data()));
}

void ContentTextLayoutTest::fontWithoutWidths()
{
    // One of the standard 14 fonts without /Widths (9.6.2.2): the dictionary gives no width, the
    // reader knows the widths by itself. The model has the glyphs with their codes and texts,
    // each one moves by its spacing only: (AB) with Tc 1.5 - A at 0, B at 1.5, the end at 3.
    const ContentTextLayoutFont standardFont = createStandardFont();
    QVERIFY(standardFont.font);
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(standardFont.font.data(), createCode(65, 1, 65)).has_value());
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(standardFont.font.data(), createCode(32, 1, 32)).has_value());
    QVERIFY(!PDFContentTextLayout::isEditable(standardFont.font.data()));

    PDFContentTextState state = createState(standardFont.font, 10.0);
    state.characterSpacing = 1.5;

    PDFContentTextLayout layout;
    layout.beginText();
    QVERIFY(layout.isComplete());
    const std::vector<PDFContentTextItem> items = layout.showString("AB", state);
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items[0].code, 65u);
    QCOMPARE(items[0].unicode, QStringLiteral("A"));
    QVERIFY(!items[0].width.has_value());
    COMPARE_NUMBER(items[0].advance, 1.5);
    QCOMPARE(items[1].code, 66u);
    QCOMPARE(items[1].unicode, QStringLiteral("B"));
    QVERIFY(!items[1].width.has_value());
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 1.5, 0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 3, 0);
    QVERIFY(!layout.isComplete());

    // /MissingWidth completes /Widths, it does not replace it: a font without /Widths has no
    // width in its dictionary, also when its descriptor has /MissingWidth (the widths of such
    // a font are those of the reader - the layout does not claim them)
    const ContentTextLayoutFont missingWidthFont = createFont("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding "
                                                              "/FontDescriptor << /Type /FontDescriptor /FontName /Helvetica /Flags 32 /FontBBox [-166 -225 1000 931] "
                                                              "/ItalicAngle 0 /Ascent 718 /Descent -207 /CapHeight 718 /StemV 88 /MissingWidth 600 >> >>");
    QVERIFY(missingWidthFont.font);
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(missingWidthFont.font.data(), createCode(65, 1, 65)).has_value());
    QVERIFY(!PDFContentTextLayout::isEditable(missingWidthFont.font.data()));
}

void ContentTextLayoutTest::type0Widths()
{
    // /F0: /Identity-H (codes of two bytes, the code is the CID), /DW 900,
    // /W [ 32 [250] 65 [700 650] 100 102 480 ] (9.7.4.3)
    const PDFFontPointer font = getFont("F0");
    const auto width = [&font](CID cid) { return PDFContentTextLayout::getDictionaryWidth(font.data(), createCode(cid, 2, cid)); };

    QVERIFY(width(32).has_value());
    COMPARE_NUMBER(*width(32), 0.25);
    COMPARE_NUMBER(*width(65), 0.7);
    COMPARE_NUMBER(*width(66), 0.65);
    COMPARE_NUMBER(*width(100), 0.48);
    COMPARE_NUMBER(*width(102), 0.48);
    QVERIFY(width(103).has_value());
    COMPARE_NUMBER(*width(103), 0.9);
    COMPARE_NUMBER(*width(0), 0.9);

    // <0041 0042 0064 0065 0003> at the size 10: the widths 700, 650, 480, 480 and /DW 900 -
    // the items at 0, 7, 13.5, 18.3 and 23.1, the end at 32.1
    PDFContentTextLayout layout;
    layout.beginText();
    std::vector<PDFContentTextItem> items = layout.showString(QByteArray("\x00\x41\x00\x42\x00\x64\x00\x65\x00\x03", 10), createState(font, 10.0));
    QCOMPARE(items.size(), size_t(5));

    const PDFReal positions[] = { 0.0, 7.0, 13.5, 18.3, 23.1 };
    const PDFReal widths[] = { 0.7, 0.65, 0.48, 0.48, 0.9 };
    const unsigned int codes[] = { 0x41, 0x42, 0x64, 0x65, 0x03 };
    for (size_t i = 0; i < items.size(); ++i)
    {
        COMPARE_MATRIX(items[i].textMatrix, 1, 0, 0, 1, positions[i], 0);
        QVERIFY(items[i].width.has_value());
        COMPARE_NUMBER(*items[i].width, widths[i]);
        COMPARE_NUMBER(items[i].advance, widths[i] * 10.0);
        QCOMPARE(items[i].code, codes[i]);
        QCOMPARE(items[i].cid, CID(codes[i]));
        QCOMPARE(items[i].codeBytes.size(), qsizetype(2));
        QVERIFY(!items[i].isWordSpace);
    }
    QCOMPARE(items[2].codeBytes, QByteArray("\x00\x64", 2));
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 32.1, 0);
    QVERIFY(layout.isComplete());

    // /ToUnicode: <0041> is A, <0042> is B, <0064> is the two code units "ff"; <0065> and <0003>
    // are not mapped and the collection Adobe-Identity has no Unicode CMap - no text
    QCOMPARE(items[0].unicode, QStringLiteral("A"));
    QCOMPARE(items[1].unicode, QStringLiteral("B"));
    QCOMPARE(items[2].unicode, QStringLiteral("ff"));
    QCOMPARE(items[3].unicode, QString());
    QCOMPARE(items[4].unicode, QString());

    // The character spacing belongs to a code, not to a byte: <0041 0042> with Tc 1 and Tz 50 -
    // (7 + 1) * 0.5 = 4, (6.5 + 1) * 0.5 = 3.75, the end at 7.75
    PDFContentTextState state = createState(font, 10.0);
    state.characterSpacing = 1.0;
    state.horizontalScaling = 50.0;
    layout.beginText();
    items = layout.showString(QByteArray("\x00\x41\x00\x42", 4), state);
    QCOMPARE(items.size(), size_t(2));
    COMPARE_NUMBER(items[0].advance, 4.0);
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 4, 0);
    COMPARE_NUMBER(items[1].advance, 3.75);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 7.75, 0);
}

void ContentTextLayoutTest::codesOfString()
{
    // A simple font: one byte is one code (9.4.3), every value of a byte
    std::vector<PDFContentTextLayout::Code> codes = PDFContentTextLayout::decodeString(getFont("F1").data(), QByteArray("\x00" "A" "\xFF", 3));
    QCOMPARE(codes.size(), size_t(3));
    QCOMPARE(codes[0].bytes, QByteArray("\x00", 1));
    QCOMPARE(codes[0].code, 0u);
    QCOMPARE(codes[0].cid, CID(0));
    QCOMPARE(codes[1].bytes, QByteArray("A"));
    QCOMPARE(codes[1].code, 65u);
    QCOMPARE(codes[1].cid, CID(65));
    QCOMPARE(codes[2].bytes, QByteArray("\xFF"));
    QCOMPARE(codes[2].code, 255u);
    QCOMPARE(codes[2].cid, CID(255));

    // A Type 3 font: one byte is one code
    codes = PDFContentTextLayout::decodeString(getFont("T3").data(), QByteArray("AB"));
    QCOMPARE(codes.size(), size_t(2));
    QCOMPARE(codes[1].bytes, QByteArray("B"));
    QCOMPARE(codes[1].code, 66u);

    // /FM: the codes <00> ... <7F> have one byte, <8000> ... <FFFF> two bytes, the CID is the
    // value of the code (9.7.6.2). <20 41 8020> is the three codes 0x20, 0x41 and 0x8020.
    codes = PDFContentTextLayout::decodeString(getFont("FM").data(), QByteArray("\x20\x41\x80\x20", 4));
    QCOMPARE(codes.size(), size_t(3));
    QCOMPARE(codes[0].bytes, QByteArray("\x20"));
    QCOMPARE(codes[0].code, 0x20u);
    QCOMPARE(codes[0].cid, CID(32));
    QCOMPARE(codes[1].bytes, QByteArray("\x41"));
    QCOMPARE(codes[1].code, 0x41u);
    QCOMPARE(codes[1].cid, CID(65));
    QCOMPARE(codes[2].bytes, QByteArray("\x80\x20", 2));
    QCOMPARE(codes[2].code, 0x8020u);
    QCOMPARE(codes[2].cid, CID(32800));

    // The same string shown at the size 10: the widths 250, 700 and /DW 900 - the items at 0, 2.5
    // and 9.5, the end at 18.5
    PDFContentTextLayout layout;
    layout.beginText();
    const std::vector<PDFContentTextItem> items = layout.showString(QByteArray("\x20\x41\x80\x20", 4), createState(getFont("FM"), 10.0));
    QCOMPARE(items.size(), size_t(3));
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 2.5, 0);
    COMPARE_MATRIX(items[2].textMatrix, 1, 0, 0, 1, 9.5, 0);
    QCOMPARE(items[2].codeBytes, QByteArray("\x80\x20", 2));
    QCOMPARE(items[2].cid, CID(32800));
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 18.5, 0);

    // /FN: the code is not the CID - <20> is the CID 0, <41> is the CID 65. The width belongs
    // to the CID: /DW 900 for the CID 0 and 700 for the CID 65.
    codes = PDFContentTextLayout::decodeString(getFont("FN").data(), QByteArray("\x20\x41", 2));
    QCOMPARE(codes.size(), size_t(2));
    QCOMPARE(codes[0].code, 0x20u);
    QCOMPARE(codes[0].cid, CID(0));
    QCOMPARE(codes[1].code, 0x41u);
    QCOMPARE(codes[1].cid, CID(65));
    const std::optional<PDFReal> notdefWidth = PDFContentTextLayout::getDictionaryWidth(getFont("FN").data(), codes[0]);
    QVERIFY(notdefWidth.has_value());
    COMPARE_NUMBER(*notdefWidth, 0.9);
    const std::optional<PDFReal> letterWidth = PDFContentTextLayout::getDictionaryWidth(getFont("FN").data(), codes[1]);
    QVERIFY(letterWidth.has_value());
    COMPARE_NUMBER(*letterWidth, 0.7);

    // /FN: the code <30> is in the codespace range <00> ... <7F>, but the CMap has no CID for
    // it: it is the CID 0 (9.7.6.3), with the width /DW 900 - still a whole code of the string
    codes = PDFContentTextLayout::decodeString(getFont("FN").data(), QByteArray("\x30"));
    QCOMPARE(codes.size(), size_t(1));
    QCOMPARE(codes[0].bytes, QByteArray("\x30"));
    QCOMPARE(codes[0].code, 0x30u);
    QCOMPARE(codes[0].cid, CID(0));

    // An empty string has no code
    QVERIFY(PDFContentTextLayout::decodeString(getFont("F1").data(), QByteArray()).empty());
    QVERIFY(PDFContentTextLayout::decodeString(getFont("F0").data(), QByteArray()).empty());
}

void ContentTextLayoutTest::type0IncompleteCode()
{
    // /F0 has codes of two bytes: <0041 00> is the code 0x0041 and a byte, which is no code. The
    // glyph A is laid out (the end at 7); the items do not say the whole string, so the text
    // is not complete.
    PDFContentTextLayout layout;
    layout.beginText();
    std::vector<PDFContentTextItem> items = layout.showString(QByteArray("\x00\x41\x00", 3), createState(getFont("F0"), 10.0));
    QCOMPARE(items.size(), size_t(1));
    QCOMPARE(items[0].codeBytes, QByteArray("\x00\x41", 2));
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 7, 0);
    QVERIFY(!layout.isComplete());

    // /FM: <80> is the first byte of a code of two bytes - no code at all
    layout.beginText();
    QVERIFY(layout.isComplete());
    items = layout.showString(QByteArray("\x80"), createState(getFont("FM"), 10.0));
    QVERIFY(items.empty());
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(!layout.isComplete());

    // A whole string keeps the text complete
    layout.beginText();
    items = layout.showString(QByteArray("\x00\x41", 2), createState(getFont("F0"), 10.0));
    QCOMPARE(items.size(), size_t(1));
    QVERIFY(layout.isComplete());
}

void ContentTextLayoutTest::type3Widths()
{
    // /T3: /FontMatrix [0.01 0 0 0.01 0 0], /Widths in the glyph space - 32 -> 50, 65 -> 60,
    // 66 -> 80, 67 -> 70, the other codes of 32 ... 67 -> 0 (9.6.4, Table 110). w0 = width * 0.01.
    const PDFFontPointer font = getFont("T3");
    const auto width = [&font](unsigned int code) { return PDFContentTextLayout::getDictionaryWidth(font.data(), createCode(code, 1, code)); };

    QVERIFY(width(65).has_value());
    COMPARE_NUMBER(*width(65), 0.6);
    COMPARE_NUMBER(*width(66), 0.8);
    COMPARE_NUMBER(*width(67), 0.7);
    COMPARE_NUMBER(*width(32), 0.5);

    // The code 33 is in /Widths with the width 0: a width
    QVERIFY(width(33).has_value());
    COMPARE_NUMBER(*width(33), 0.0);

    // The codes 31 and 97 are outside of /FirstChar ... /LastChar. Table 110: "For character
    // codes outside the range FirstChar to LastChar, the width shall be 0" - the width 0, not
    // "no width"
    QVERIFY(width(31).has_value());
    COMPARE_NUMBER(*width(31), 0.0);
    QVERIFY(width(97).has_value());
    COMPARE_NUMBER(*width(97), 0.0);

    // (AB C) at the size 10 with Tc 1 and Tz 200 - the character spacing applies to a Type 3 font
    // as to any font: A (6 + 1) * 2 = 14, B (8 + 1) * 2 = 18, the code 32 (5 + 1) * 2 = 12,
    // C (7 + 1) * 2 = 16; the items at 0, 14, 32 and 44, the end at 60
    PDFContentTextState state = createState(font, 10.0);
    state.characterSpacing = 1.0;
    state.horizontalScaling = 200.0;

    PDFContentTextLayout layout;
    layout.beginText();
    std::vector<PDFContentTextItem> items = layout.showString("AB C", state);
    QCOMPARE(items.size(), size_t(4));
    COMPARE_MATRIX(items[0].textMatrix, 1, 0, 0, 1, 0, 0);
    COMPARE_NUMBER(items[0].advance, 14.0);
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 14, 0);
    COMPARE_NUMBER(items[1].advance, 18.0);
    COMPARE_MATRIX(items[2].textMatrix, 1, 0, 0, 1, 32, 0);
    COMPARE_NUMBER(items[2].advance, 12.0);
    COMPARE_MATRIX(items[3].textMatrix, 1, 0, 0, 1, 44, 0);
    COMPARE_NUMBER(items[3].advance, 16.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 60, 0);
    QVERIFY(layout.isComplete());

    // The code 33 (the width 0) moves by its spacing: 1 * 2 = 2, and the text stays complete
    layout.beginText();
    items = layout.showString("!", state);
    QCOMPARE(items.size(), size_t(1));
    QVERIFY(items[0].width.has_value());
    COMPARE_NUMBER(items[0].advance, 2.0);
    QVERIFY(layout.isComplete());

    // The code 97 (outside of /Widths, the width 0) moves by its spacing too; the text is complete
    layout.beginText();
    items = layout.showString("a", state);
    QCOMPARE(items.size(), size_t(1));
    QVERIFY(items[0].width.has_value());
    COMPARE_NUMBER(*items[0].width, 0.0);
    COMPARE_NUMBER(items[0].advance, 2.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 2, 0);
    QVERIFY(layout.isComplete());

    // The font size -10 with Tc 1: A moves by 0.6 * (-10) + 1 = -5
    PDFContentTextState negativeState = createState(font, -10.0);
    negativeState.characterSpacing = 1.0;
    layout.beginText();
    items = layout.showString("A", negativeState);
    QCOMPARE(items.size(), size_t(1));
    COMPARE_NUMBER(items[0].advance, -5.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, -5, 0);
}

void ContentTextLayoutTest::type3FontMatrix()
{
    // /FontMatrix [0.002 0.001 0 0.002 5 0], the width 500 of the glyph space. 9.6.4, Table 110:
    // "If FontMatrix specifies a rotation, only the horizontal component of the transformed
    // width shall be used. That is, the resulting displacement shall always be horizontal in text
    // space". The vector (500, 0) transformed by the matrix is (500 * 0.002, 500 * 0.001) = (1, 0.5):
    // w0 = 1; the vertical component 0.5 is not used, and the translation 5 of the matrix does not
    // belong to a vector.
    const ContentTextLayoutFont fixture = createSkewedType3Font(true);
    QVERIFY(fixture.font);
    QCOMPARE(fixture.font->getFontType(), FontType::Type3);
    QVERIFY(PDFContentTextLayout::isEditable(fixture.font.data()));

    const std::optional<PDFReal> width = PDFContentTextLayout::getDictionaryWidth(fixture.font.data(), createCode(65, 1, 65));
    QVERIFY(width.has_value());
    COMPARE_NUMBER(*width, 1.0);

    // (A) at the size 10: tx = 1 * 10 = 10, ty = 0
    PDFContentTextLayout layout;
    layout.beginText();
    const std::vector<PDFContentTextItem> items = layout.showString("A", createState(fixture.font, 10.0));
    QCOMPARE(items.size(), size_t(1));
    COMPARE_NUMBER(items[0].advance, 10.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 10, 0);
    QVERIFY(layout.isComplete());

    // The font has no /ToUnicode: the text of the code 65 is the one of its glyph name /a (9.10.2)
    QCOMPARE(items[0].unicode, QStringLiteral("a"));
}

void ContentTextLayoutTest::verticalWritingMode()
{
    // /FV: /Identity-V, /DW2 [880 -900], /W2 [65 [-700 500 880]] (9.7.4.3): the vertical
    // displacement w1 of the CID 65 is -0.7, of every other CID -0.9. 9.4.4: tx = 0,
    // ty = w1 * Tfs + Tc + Tw.
    const PDFFontPointer font = getFont("FV");

    // The glyphs of such a text are not edited, and its dictionary width w0 is not readable
    QVERIFY(!PDFContentTextLayout::isEditable(font.data()));
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(font.data(), createCode(0x41, 2, 0x41)).has_value());
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(font.data(), createCode(0x42, 2, 0x42)).has_value());

    // <0041 0042> at the size 10: A at (0, 0) moves by -0.7 * 10 = -7, B at (0, -7) moves by
    // -0.9 * 10 = -9; the end at (0, -16). Nothing moves along x.
    PDFContentTextLayout layout;
    layout.beginText();
    std::vector<PDFContentTextItem> items = layout.showString(QByteArray("\x00\x41\x00\x42", 4), createState(font, 10.0));
    QCOMPARE(items.size(), size_t(2));
    COMPARE_MATRIX(items[0].textMatrix, 1, 0, 0, 1, 0, 0);
    COMPARE_NUMBER(items[0].advance, 0.0);
    QVERIFY(!items[0].width.has_value());
    QCOMPARE(items[0].unicode, QStringLiteral("A"));
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 0, -7);
    COMPARE_NUMBER(items[1].advance, 0.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 0, -16);
    QVERIFY(!layout.isComplete());

    // Tc 1 is added, the horizontal scaling 50 does not apply to ty: A moves by -7 + 1 = -6,
    // B by -9 + 1 = -8; then the number 100 of TJ moves by -100 / 1000 * 10 = -1 along y
    // (9.4.3, Table 107: the amount is subtracted from the vertical coordinate). The end at
    // (0, -6 - 8 - 1) = (0, -15).
    PDFContentTextState state = createState(font, 10.0);
    state.characterSpacing = 1.0;
    state.horizontalScaling = 50.0;
    layout.beginText();
    items = layout.showString(QByteArray("\x00\x41\x00\x42", 4), state);
    QCOMPARE(items.size(), size_t(2));
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 0, -6);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 0, -14);

    const PDFContentTextItem spacer = layout.applyAdjustment(100.0, state);
    QVERIFY(spacer.kind == PDFContentTextItem::Kind::Spacer);
    COMPARE_MATRIX(spacer.textMatrix, 1, 0, 0, 1, 0, -14);
    COMPARE_NUMBER(spacer.advance, 0.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 0, -15);

    // Under 2 0 0 2 10 20 Tm the move -7 of the text space is -14 of the space of the text
    // object: [2 0 0 2 10 6]
    layout.beginText();
    layout.setMatrix(QTransform(2, 0, 0, 2, 10, 20));
    items = layout.showString(QByteArray("\x00\x41", 2), createState(font, 10.0));
    QCOMPARE(items.size(), size_t(1));
    COMPARE_MATRIX(layout.getTextMatrix(), 2, 0, 0, 2, 10, 6);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 2, 0, 0, 2, 10, 20);

    // The same descendant font under /Identity-H (/F0) is horizontal and uses /W, not /W2:
    // A moves by 0.7 * 10 = 7 along x
    layout.beginText();
    items = layout.showString(QByteArray("\x00\x41", 2), createState(getFont("F0"), 10.0));
    QCOMPARE(items.size(), size_t(1));
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 7, 0);
}

void ContentTextLayoutTest::adjustments()
{
    // /F1 10 Tf [(A) -200 (B) 300 (A)] TJ (9.4.3, Table 107; 9.4.4: tx = -Tj / 1000 * Tfs * Th):
    //   A at 0, tx = 7                          -> 7
    //   -200: tx = 200 / 1000 * 10 = 2          -> 9
    //   B at 9, tx = 6.5                        -> 15.5
    //   300: tx = -300 / 1000 * 10 = -3         -> 12.5
    //   A at 12.5, tx = 7                       -> 19.5
    PDFContentTextState state = createState(getFont("F1"), 10.0);
    PDFContentTextLayout layout;
    layout.beginText();

    std::vector<PDFContentTextItem> items = layout.showString("A", state);
    QCOMPARE(items.size(), size_t(1));
    COMPARE_MATRIX(items[0].textMatrix, 1, 0, 0, 1, 0, 0);

    PDFContentTextItem spacer = layout.applyAdjustment(-200.0, state);
    QVERIFY(spacer.kind == PDFContentTextItem::Kind::Spacer);
    COMPARE_NUMBER(spacer.adjustment, -200.0);
    COMPARE_MATRIX(spacer.textMatrix, 1, 0, 0, 1, 7, 0);
    COMPARE_NUMBER(spacer.advance, 2.0);
    QVERIFY(spacer.codeBytes.isEmpty());
    QVERIFY(spacer.unicode.isEmpty());
    QVERIFY(!spacer.width.has_value());
    QVERIFY(!spacer.isWordSpace);
    QVERIFY(!spacer.hasOutline);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 9, 0);

    items = layout.showString("B", state);
    QCOMPARE(items.size(), size_t(1));
    COMPARE_MATRIX(items[0].textMatrix, 1, 0, 0, 1, 9, 0);

    spacer = layout.applyAdjustment(300.0, state);
    COMPARE_NUMBER(spacer.adjustment, 300.0);
    COMPARE_MATRIX(spacer.textMatrix, 1, 0, 0, 1, 15.5, 0);
    COMPARE_NUMBER(spacer.advance, -3.0);

    items = layout.showString("A", state);
    QCOMPARE(items.size(), size_t(1));
    COMPARE_MATRIX(items[0].textMatrix, 1, 0, 0, 1, 12.5, 0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 19.5, 0);
    QVERIFY(layout.getTextLineMatrix().isIdentity());

    // A spacer keeps the text complete: it needs no width
    QVERIFY(layout.isComplete());

    // Tc 4, Tw 3 and Tz 50: the number is scaled by Th, and it gets no spacing -
    // A: (7 + 4) * 0.5 = 5.5; -200: 2 * 0.5 = 1 -> 6.5
    state.characterSpacing = 4.0;
    state.wordSpacing = 3.0;
    state.horizontalScaling = 50.0;
    layout.beginText();
    layout.showString("A", state);
    spacer = layout.applyAdjustment(-200.0, state);
    COMPARE_MATRIX(spacer.textMatrix, 1, 0, 0, 1, 5.5, 0);
    COMPARE_NUMBER(spacer.advance, 1.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 6.5, 0);

    // The font size -10: -200 moves by 200 / 1000 * (-10) = -2
    layout.beginText();
    spacer = layout.applyAdjustment(-200.0, createState(getFont("F1"), -10.0));
    COMPARE_NUMBER(spacer.advance, -2.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, -2, 0);

    // A number needs no font: with the font size 10 of a state without a font -100 moves by 1
    layout.beginText();
    spacer = layout.applyAdjustment(-100.0, createState(PDFFontPointer(), 10.0));
    COMPARE_NUMBER(spacer.advance, 1.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 1, 0);
    QVERIFY(layout.isComplete());
}

void ContentTextLayoutTest::quoteOperators()
{
    // /F1 10 Tf 12 TL 100 100 Td, then (A) ' - 9.4.3, Table 107: "Move to the next line and show
    // a text string", the same as T* (A) Tj. T*: Tlm = [1 0 0 1 100 88]; A at (100, 88), tx = 7.
    PDFContentTextState state = createState(getFont("F1"), 10.0);
    state.leading = 12.0;

    PDFContentTextLayout layout;
    layout.beginText();
    layout.moveByOffset(100.0, 100.0);

    layout.moveByLeading(state.leading);
    std::vector<PDFContentTextItem> items = layout.showString("A", state);
    QCOMPARE(items.size(), size_t(1));
    COMPARE_MATRIX(items[0].textMatrix, 1, 0, 0, 1, 100, 88);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 107, 88);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 1, 0, 0, 1, 100, 88);

    // 2 1 (A B) " - the same as 2 Tw 1 Tc (A B) ': the next line is [1 0 0 1 100 76];
    // A at 100: 7 + 1 = 8; the code 32 at 108: 3 + 1 + 2 = 6; B at 114: 6.5 + 1 = 7.5; the end at 121.5
    state.wordSpacing = 2.0;
    state.characterSpacing = 1.0;
    layout.moveByLeading(state.leading);
    items = layout.showString("A B", state);
    QCOMPARE(items.size(), size_t(3));
    COMPARE_MATRIX(items[0].textMatrix, 1, 0, 0, 1, 100, 76);
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 108, 76);
    COMPARE_MATRIX(items[2].textMatrix, 1, 0, 0, 1, 114, 76);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 121.5, 76);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 1, 0, 0, 1, 100, 76);
}

void ContentTextLayoutTest::textRiseAndMatrix()
{
    // /F1 10 Tf 5 Ts 2 0 0 2 10 20 Tm (AB) Tj. The text rise is a part of the text rendering
    // matrix [Tfs * Th 0 0 Tfs 0 Trise] x Tm x CTM of one glyph (9.4.4), it never enters the text
    // matrix: A has Tm = [2 0 0 2 10 20]; tx = 7 of the text space is 14 of the space of the
    // text object, so B has [2 0 0 2 24 20] and the text ends at [2 0 0 2 37 20] (6.5 * 2 = 13).
    PDFContentTextState state = createState(getFont("F1"), 10.0);
    state.rise = 5.0;

    PDFContentTextLayout layout;
    layout.beginText();
    layout.setMatrix(QTransform(2, 0, 0, 2, 10, 20));
    std::vector<PDFContentTextItem> items = layout.showString("AB", state);
    QCOMPARE(items.size(), size_t(2));
    COMPARE_MATRIX(items[0].textMatrix, 2, 0, 0, 2, 10, 20);
    COMPARE_MATRIX(items[1].textMatrix, 2, 0, 0, 2, 24, 20);
    COMPARE_MATRIX(layout.getTextMatrix(), 2, 0, 0, 2, 37, 20);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 2, 0, 0, 2, 10, 20);

    // The advance of an item is tx in the text space, not in the space of the text object
    COMPARE_NUMBER(items[0].advance, 7.0);
    COMPARE_NUMBER(items[1].advance, 6.5);

    // 0 1 -1 0 100 50 Tm (the text turned by 90 degrees): tx = 7 moves the text up -
    // e = 7 * 0 + 100 = 100, f = 7 * 1 + 50 = 57
    layout.beginText();
    layout.setMatrix(QTransform(0, 1, -1, 0, 100, 50));
    items = layout.showString("A", state);
    QCOMPARE(items.size(), size_t(1));
    COMPARE_MATRIX(items[0].textMatrix, 0, 1, -1, 0, 100, 50);
    COMPARE_MATRIX(layout.getTextMatrix(), 0, 1, -1, 0, 100, 57);
}

void ContentTextLayoutTest::unicodeTexts()
{
    // /F1: /WinAnsiEncoding with /Differences [67 /Euro 68 /space], no /ToUnicode (9.10.2: the
    // glyph name of the encoding gives the text)
    const PDFFontPointer simpleFont = getFont("F1");
    const auto simpleText = [&simpleFont](unsigned int code) { return PDFContentTextLayout::getUnicode(simpleFont.data(), createCode(code, 1, code)); };
    QCOMPARE(simpleText(65), QStringLiteral("A"));
    QCOMPARE(simpleText(67), QString(QChar(0x20AC)));
    QCOMPARE(simpleText(68), QStringLiteral(" "));

    // The code 200 of WinAnsiEncoding is Egrave, U+00C8 (Annex D.2)
    QCOMPARE(simpleText(200), QString(QChar(0x00C8)));

    // The code 1 has no character in WinAnsiEncoding (Annex D.2): no Unicode value - not the
    // replacement character of the tables of the library
    QCOMPARE(simpleText(1), QString());

    // /T3: /ToUnicode maps <20>, <41> and <42>. The code 66 is B by /ToUnicode, though its glyph
    // name /bar would be U+007C: /ToUnicode goes first. The code 67 is not mapped and its glyph
    // name /nothing is no name of a character: no Unicode value.
    const PDFFontPointer type3Font = getFont("T3");
    const auto type3Text = [&type3Font](unsigned int code) { return PDFContentTextLayout::getUnicode(type3Font.data(), createCode(code, 1, code)); };
    QCOMPARE(type3Text(32), QStringLiteral(" "));
    QCOMPARE(type3Text(65), QStringLiteral("A"));
    QCOMPARE(type3Text(66), QStringLiteral("B"));
    QCOMPARE(type3Text(67), QString());

    // /F0: a code is matched with its length - <0041> is A, the single byte <41> is no code of
    // the /ToUnicode map
    const PDFFontPointer type0Font = getFont("F0");
    QCOMPARE(PDFContentTextLayout::getUnicode(type0Font.data(), createCode(0x41, 2, 0x41)), QStringLiteral("A"));
    QCOMPARE(PDFContentTextLayout::getUnicode(type0Font.data(), createCode(0x41, 1, 0x41)), QString());
    QCOMPARE(PDFContentTextLayout::getUnicode(type0Font.data(), createCode(0x64, 2, 0x64)), QStringLiteral("ff"));
    QCOMPARE(PDFContentTextLayout::getUnicode(type0Font.data(), createCode(0x65, 2, 0x65)), QString());

    // No font, no text
    QCOMPARE(PDFContentTextLayout::getUnicode(nullptr, createCode(65, 1, 65)), QString());
}

void ContentTextLayoutTest::unicodeOfToUnicodeFirst()
{
    // A simple font with /WinAnsiEncoding and /ToUnicode: <41> -> U+0058 (X), <42> -> U+FFFD,
    // <43> -> U+1F600 as the surrogate pair <D83DDE00>; <44> is not mapped
    const ContentTextLayoutFont fixture = createFont([](PDFDocumentBuilder* builder)
    {
        PDFObject toUnicode = ContentTestDocuments::addStreamObject(builder, ContentTestDocuments::DictionaryEntries(),
                                                                    QByteArray("/CIDInit /ProcSet findresource begin\n"
                                                                               "12 dict begin\n"
                                                                               "begincmap\n"
                                                                               "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                                                                               "/CMapName /Adobe-Identity-UCS def\n"
                                                                               "/CMapType 2 def\n"
                                                                               "1 begincodespacerange\n"
                                                                               "<00> <FF>\n"
                                                                               "endcodespacerange\n"
                                                                               "3 beginbfchar\n"
                                                                               "<41> <0058>\n"
                                                                               "<42> <FFFD>\n"
                                                                               "<43> <D83DDE00>\n"
                                                                               "endbfchar\n"
                                                                               "endcmap\n"
                                                                               "CMapName currentdict /CMap defineresource pop\n"
                                                                               "end\n"
                                                                               "end\n"));

        return ContentTestDocuments::createDictionaryObject({ { "Type", PDFObject::createName("Font") },
                                                              { "Subtype", PDFObject::createName("Type1") },
                                                              { "BaseFont", PDFObject::createName("Helvetica") },
                                                              { "Encoding", PDFObject::createName("WinAnsiEncoding") },
                                                              { "ToUnicode", std::move(toUnicode) } });
    });
    QVERIFY(fixture.font);
    const auto text = [&fixture](unsigned int code) { return PDFContentTextLayout::getUnicode(fixture.font.data(), createCode(code, 1, code)); };

    // 9.10.2: /ToUnicode is used first - the code 0x41 is X, not the A of the encoding
    QCOMPARE(text(0x41), QStringLiteral("X"));

    // What /ToUnicode says is the text of the document, also the replacement character
    QCOMPARE(text(0x42), QString(QChar(0xFFFD)));

    // A character outside of the basic multilingual plane is two code units
    QCOMPARE(text(0x43), QString(QChar(0xD83D)) + QChar(0xDE00));

    // A code, which /ToUnicode does not map, has the character of the encoding
    QCOMPARE(text(0x44), QStringLiteral("D"));
}

void ContentTextLayoutTest::unicodeOfCharacterCollection()
{
    // A Type 0 font of the character collection Adobe-Japan1 without /ToUnicode: 9.10.2 maps its
    // CID by the Unicode CMap of the collection (Adobe-Japan1-UCS2). The CIDs 1 ... 94 of
    // Adobe-Japan1 are the proportional ASCII characters from the space (the CID 34 is A), the
    // CID 843 is HIRAGANA LETTER A (U+3042); the CID 0 is .notdef. /Identity-H: the code of two
    // bytes is the CID.
    const ContentTextLayoutFont fixture = createFont("<< /Type /Font /Subtype /Type0 /BaseFont /Ryumin-Light /Encoding /Identity-H "
                                                     "/DescendantFonts [ << /Type /Font /Subtype /CIDFontType0 /BaseFont /Ryumin-Light "
                                                     "/CIDSystemInfo << /Registry (Adobe) /Ordering (Japan1) /Supplement 2 >> >> ] >>");
    QVERIFY(fixture.font);
    QCOMPARE(fixture.font->getFontType(), FontType::Type0);

    QCOMPARE(PDFContentTextLayout::getUnicode(fixture.font.data(), createCode(34, 2, 34)), QStringLiteral("A"));
    QCOMPARE(PDFContentTextLayout::getUnicode(fixture.font.data(), createCode(843, 2, 843)), QString(QChar(0x3042)));
    QCOMPARE(PDFContentTextLayout::getUnicode(fixture.font.data(), createCode(0, 2, 0)), QString());

    // The descendant font has no /DW: the default width is 1000 (9.7.4.1, Table 115), w0 = 1.
    // <0022 034B> at the size 10: A at 0, the hiragana letter at 10, the end at 20.
    PDFContentTextLayout layout;
    layout.beginText();
    const std::vector<PDFContentTextItem> items = layout.showString(QByteArray("\x00\x22\x03\x4B", 4), createState(fixture.font, 10.0));
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items[0].unicode, QStringLiteral("A"));
    QCOMPARE(items[0].cid, CID(34));
    QVERIFY(items[0].width.has_value());
    COMPARE_NUMBER(*items[0].width, 1.0);
    QCOMPARE(items[1].unicode, QString(QChar(0x3042)));
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 10, 0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 20, 0);
    QVERIFY(layout.isComplete());
}

void ContentTextLayoutTest::outlines()
{
    // /FC (Helvetica, not embedded: the outlines come from a font of the system) at the size 10,
    // the string <01> (A) ( ):
    //   the code 1 has the width 450 in /Widths and no glyph (WinAnsiEncoding has no character
    //   for it) - it is in the model, without an outline, and moves by 0.45 * 10 = 4.5;
    //   A has an outline; it moves by the width 700 of the dictionary (7), whatever the advance
    //   of the font program is (Helvetica and its substitutes have 667 for A);
    //   the code 32 has a glyph without marks: an outline, which is empty.
    const PDFFontPointer font = getFont("FC");
    PDFContentTextState state = createState(font, 10.0);
    state.realizedFont = createRealizedFont(font, 10.0);
    QVERIFY(state.realizedFont);

    PDFContentTextLayout layout;
    layout.beginText();
    std::vector<PDFContentTextItem> items = layout.showString(QByteArray("\x01" "A "), state);
    QCOMPARE(items.size(), size_t(3));

    QCOMPARE(items[0].code, 1u);
    QVERIFY(!items[0].hasOutline);
    QVERIFY(items[0].outline == nullptr);
    QVERIFY(items[0].width.has_value());
    COMPARE_NUMBER(*items[0].width, 0.45);
    COMPARE_NUMBER(items[0].advance, 4.5);

    QCOMPARE(items[1].code, 65u);
    QVERIFY(items[1].hasOutline);
    QVERIFY(items[1].outline != nullptr);
    QVERIFY(!items[1].outline->isEmpty());
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 4.5, 0);
    COMPARE_NUMBER(items[1].advance, 7.0);

    QCOMPARE(items[2].code, 32u);
    QVERIFY(items[2].hasOutline);
    QVERIFY(items[2].outline != nullptr);
    QVERIFY(items[2].outline->isEmpty());
    COMPARE_MATRIX(items[2].textMatrix, 1, 0, 0, 1, 11.5, 0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 14.5, 0);
    QVERIFY(layout.isComplete());

    // The outline of A is the one of the realized font: the same glyph shown again has the
    // same outline (the font owns it), another glyph has another one
    const std::vector<PDFContentTextItem> again = layout.showString("AB", state);
    QCOMPARE(again.size(), size_t(2));
    QVERIFY(again[0].outline == items[1].outline);
    QVERIFY(again[1].outline != nullptr);
    QVERIFY(again[1].outline != items[1].outline);

    // Without a realized font the layout is the same and no item has an outline
    state.realizedFont.reset();
    layout.beginText();
    items = layout.showString(QByteArray("\x01" "A "), state);
    QCOMPARE(items.size(), size_t(3));
    for (const PDFContentTextItem& item : items)
    {
        QVERIFY(!item.hasOutline);
        QVERIFY(item.outline == nullptr);
    }
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 4.5, 0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 14.5, 0);
    QVERIFY(layout.isComplete());
}

void ContentTextLayoutTest::type3Glyphs()
{
    // /T3 at the size 10, the string (aAC):
    //   the code 97 is outside of /Widths (the width 0, Table 110) and has no glyph procedure -
    //   it is in the model, no glyph;
    //   the code 65 (/square) has a glyph procedure: the font has the glyph, a Type 3 glyph has
    //   no outline;
    //   the code 67 (/nothing) has the width 70 and no glyph procedure - no glyph, tx = 7.
    const PDFFontPointer font = getFont("T3");
    PDFContentTextState state = createState(font, 10.0);
    state.realizedFont = createRealizedFont(font, 10.0);
    QVERIFY(state.realizedFont);

    PDFContentTextLayout layout;
    layout.beginText();
    const std::vector<PDFContentTextItem> items = layout.showString("aAC", state);
    QCOMPARE(items.size(), size_t(3));

    QCOMPARE(items[0].code, 97u);
    QVERIFY(!items[0].hasOutline);
    QVERIFY(items[0].outline == nullptr);
    QVERIFY(items[0].width.has_value());
    COMPARE_NUMBER(*items[0].width, 0.0);
    COMPARE_NUMBER(items[0].advance, 0.0);

    QCOMPARE(items[1].code, 65u);
    QVERIFY(items[1].hasOutline);
    QVERIFY(items[1].outline == nullptr);
    COMPARE_MATRIX(items[1].textMatrix, 1, 0, 0, 1, 0, 0);
    COMPARE_NUMBER(items[1].advance, 6.0);

    QCOMPARE(items[2].code, 67u);
    QVERIFY(!items[2].hasOutline);
    QVERIFY(items[2].outline == nullptr);
    QVERIFY(items[2].width.has_value());
    COMPARE_MATRIX(items[2].textMatrix, 1, 0, 0, 1, 6, 0);
    COMPARE_NUMBER(items[2].advance, 7.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 13, 0);

    // /TN: the code 32 has its width 50 and no glyph procedure
    const PDFFontPointer noSpaceFont = getFont("TN");
    PDFContentTextState noSpaceState = createState(noSpaceFont, 10.0);
    noSpaceState.realizedFont = createRealizedFont(noSpaceFont, 10.0);
    layout.beginText();
    const std::vector<PDFContentTextItem> noSpaceItems = layout.showString(" B", noSpaceState);
    QCOMPARE(noSpaceItems.size(), size_t(2));
    QVERIFY(!noSpaceItems[0].hasOutline);
    QVERIFY(noSpaceItems[0].isWordSpace);
    COMPARE_NUMBER(noSpaceItems[0].advance, 5.0);
    QVERIFY(noSpaceItems[1].hasOutline);
    COMPARE_MATRIX(noSpaceItems[1].textMatrix, 1, 0, 0, 1, 5, 0);
    QVERIFY(layout.isComplete());
}

void ContentTextLayoutTest::editableFonts()
{
    // The glyphs of a text can be edited in a font of the horizontal writing mode with widths in
    // its dictionary
    for (const char* name : { "F1", "F2", "FC", "F0", "FM", "FN", "T3", "TN" })
    {
        QVERIFY2(PDFContentTextLayout::isEditable(getFont(name).data()), name);
    }

    // The vertical writing mode
    QVERIFY(!PDFContentTextLayout::isEditable(getFont("FV").data()));

    // No widths in the dictionary (the standard 14 fonts get them in stage E2)
    const ContentTextLayoutFont standardFont = createStandardFont();
    QVERIFY(!PDFContentTextLayout::isEditable(standardFont.font.data()));

    // No font
    QVERIFY(!PDFContentTextLayout::isEditable(nullptr));
}

void ContentTextLayoutTest::completeness()
{
    const PDFContentTextState widthState = createState(getFont("F1"), 10.0);
    const ContentTextLayoutFont standardFont = createStandardFont();
    const PDFContentTextState noWidthState = createState(standardFont.font, 10.0);

    // A text of glyphs with widths is complete
    PDFContentTextLayout layout;
    layout.beginText();
    layout.showString("AB", widthState);
    layout.applyAdjustment(-50.0, widthState);
    layout.showString("A", widthState);
    QVERIFY(layout.isComplete());

    // One glyph without a width: the whole text object is not complete, also after glyphs with
    // widths and after a move to another line
    layout.showString("A", noWidthState);
    QVERIFY(!layout.isComplete());
    layout.showString("A", widthState);
    QVERIFY(!layout.isComplete());
    layout.moveByOffset(0.0, -12.0);
    layout.setMatrix(QTransform());
    QVERIFY(!layout.isComplete());

    // The next text object starts complete
    layout.beginText();
    QVERIFY(layout.isComplete());

    // An empty string shows nothing and changes nothing, in any font and without one
    layout.showString(QByteArray(), noWidthState);
    layout.showString(QByteArray(), createState(PDFFontPointer(), 10.0));
    QVERIFY(layout.isComplete());
    QVERIFY(layout.getTextMatrix().isIdentity());

    // A string without a font (Tf has no initial value, 9.3.1): nothing can be shown, nothing
    // moves, and the text is not complete
    const std::vector<PDFContentTextItem> items = layout.showString("AB", createState(PDFFontPointer(), 10.0));
    QVERIFY(items.empty());
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(!layout.isComplete());
}

void ContentTextLayoutTest::hostileNumbers()
{
    const PDFFontPointer font = getFont("F1");

    // The font size 1e308 with Tc 1e308 and Tz 200: (0.7e308 + 1e308) * 2 is over the range of
    // a number. Such a displacement is none: the item is there, the matrices stay matrices of
    // finite numbers, and the text is not complete.
    PDFContentTextState state = createState(font, 1e308);
    state.characterSpacing = 1e308;
    state.horizontalScaling = 200.0;

    PDFContentTextLayout layout;
    layout.beginText();
    layout.moveByOffset(3.0, 4.0);
    std::vector<PDFContentTextItem> items = layout.showString("A", state);
    QCOMPARE(items.size(), size_t(1));
    QCOMPARE(items[0].advance, 0.0);
    COMPARE_MATRIX(items[0].textMatrix, 1, 0, 0, 1, 3, 4);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 3, 4);
    QVERIFY(!layout.isComplete());

    // A character spacing, which is not a number
    state = createState(font, 10.0);
    state.characterSpacing = std::numeric_limits<PDFReal>::quiet_NaN();
    layout.beginText();
    items = layout.showString("AB", state);
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items[0].advance, 0.0);
    QCOMPARE(items[1].advance, 0.0);
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(!layout.isComplete());

    // A number of TJ: -1e308 / 1000 * 1e10 is over the range
    layout.beginText();
    const PDFContentTextItem spacer = layout.applyAdjustment(1e308, createState(font, 1e10));
    QVERIFY(spacer.kind == PDFContentTextItem::Kind::Spacer);
    QCOMPARE(spacer.advance, 0.0);
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(!layout.isComplete());

    // A finite advance, which takes the text matrix over the range: tx = 7 under 1e308 0 0 1 0 0 Tm
    // would be e = 7e308. It is not made.
    layout.beginText();
    layout.setMatrix(QTransform(1e308, 0, 0, 1, 0, 0));
    QVERIFY(layout.isComplete());
    items = layout.showString("A", createState(font, 10.0));
    QCOMPARE(items.size(), size_t(1));
    QCOMPARE(items[0].advance, 0.0);
    COMPARE_MATRIX(layout.getTextMatrix(), 1e308, 0, 0, 1, 0, 0);
    QVERIFY(!layout.isComplete());

    // 1e300 0 Td under 1e10 0 0 1e10 5 6 Tm would be e = 1e310: the move is not made, both
    // matrices stay, and the text is not complete
    layout.beginText();
    layout.setMatrix(QTransform(1e10, 0, 0, 1e10, 5, 6));
    QVERIFY(layout.isComplete());
    layout.moveByOffset(1e300, 0.0);
    COMPARE_MATRIX(layout.getTextLineMatrix(), 1e10, 0, 0, 1e10, 5, 6);
    COMPARE_MATRIX(layout.getTextMatrix(), 1e10, 0, 0, 1e10, 5, 6);
    QVERIFY(!layout.isComplete());

    // Operands of Tm, which are not numbers, set nothing: the matrices of 3 4 Td stay
    layout.beginText();
    layout.moveByOffset(3.0, 4.0);
    layout.setMatrix(QTransform(std::numeric_limits<PDFReal>::quiet_NaN(), 0, 0, 1, 0, 0));
    COMPARE_MATRIX(layout.getTextLineMatrix(), 1, 0, 0, 1, 3, 4);
    COMPARE_MATRIX(layout.getTextMatrix(), 1, 0, 0, 1, 3, 4);
    QVERIFY(!layout.isComplete());
    layout.beginText();
    layout.setMatrix(QTransform(1, 0, 0, 1, std::numeric_limits<PDFReal>::infinity(), 0));
    QVERIFY(layout.getTextLineMatrix().isIdentity());
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(!layout.isComplete());

    // The same in the vertical writing mode: -0.7 * 1e308 - 1.7e308 is over the range
    PDFContentTextState verticalState = createState(getFont("FV"), 1e308);
    verticalState.characterSpacing = -1.7e308;
    layout.beginText();
    items = layout.showString(QByteArray("\x00\x41", 2), verticalState);
    QCOMPARE(items.size(), size_t(1));
    QVERIFY(layout.getTextMatrix().isIdentity());
    layout.applyAdjustment(1e308, createState(getFont("FV"), 1e10));
    QVERIFY(layout.getTextMatrix().isIdentity());
}

void ContentTextLayoutTest::hostileFonts()
{
    // No font at all
    QVERIFY(PDFContentTextLayout::decodeString(nullptr, "AB").empty());
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(nullptr, createCode(65, 1, 65)).has_value());

    // A Type 0 font, whose CMap has no code (PDFFont::createFont refuses such a font, another
    // creator need not): nothing is decoded, nothing is shown, nothing is edited
    const PDFFontPointer emptyFont(new PDFType0Font(CIDSystemInfo(), "F1", FontDescriptor(), PDFFontCMap(), PDFFontCMap(),
                                                    PDFCIDtoGIDMapper(QByteArray()), 1000.0, { }));
    QVERIFY(PDFContentTextLayout::decodeString(emptyFont.data(), QByteArray("\x00\x41", 2)).empty());
    QVERIFY(!PDFContentTextLayout::isEditable(emptyFont.data()));
    QCOMPARE(PDFContentTextLayout::getUnicode(emptyFont.data(), createCode(0x41, 2, 0x41)), QString());

    PDFContentTextLayout layout;
    layout.beginText();
    const std::vector<PDFContentTextItem> items = layout.showString(QByteArray("\x00\x41", 2), createState(emptyFont, 10.0));
    QVERIFY(items.empty());
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(!layout.isComplete());

    // /FirstChar after /LastChar: /Widths belongs to no code - a font without widths
    const ContentTextLayoutFont swappedFont = createFont("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding "
                                                         "/FirstChar 70 /LastChar 60 /Widths [500 600] >>");
    QVERIFY(swappedFont.font);
    for (unsigned int code : { 0u, 60u, 65u, 70u, 71u, 255u })
    {
        QVERIFY(!PDFContentTextLayout::getDictionaryWidth(swappedFont.font.data(), createCode(code, 1, code)).has_value());
    }
    QVERIFY(!PDFContentTextLayout::isEditable(swappedFont.font.data()));

    // A negative /FirstChar: no code can be counted from it - a font without widths
    const ContentTextLayoutFont negativeFont = createFont("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding "
                                                          "/FirstChar -3 /LastChar 2 /Widths [100 200 300 400 500 600] >>");
    QVERIFY(negativeFont.font);
    for (unsigned int code : { 0u, 1u, 2u, 3u, 255u })
    {
        QVERIFY(!PDFContentTextLayout::getDictionaryWidth(negativeFont.font.data(), createCode(code, 1, code)).has_value());
    }
    QVERIFY(!PDFContentTextLayout::isEditable(negativeFont.font.data()));

    // A Type 3 font without /Widths (the key is required, Table 110): its dictionary gives no
    // width, not the width 0 of a code outside of /Widths - the text is not complete and its
    // glyphs are not edited
    const ContentTextLayoutFont noWidthsType3Font = createSkewedType3Font(false);
    QVERIFY(noWidthsType3Font.font);
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(noWidthsType3Font.font.data(), createCode(65, 1, 65)).has_value());
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(noWidthsType3Font.font.data(), createCode(66, 1, 66)).has_value());
    QVERIFY(!PDFContentTextLayout::isEditable(noWidthsType3Font.font.data()));
    layout.beginText();
    QCOMPARE(layout.showString("A", createState(noWidthsType3Font.font, 10.0)).size(), size_t(1));
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(!layout.isComplete());

    // A code of more bytes or over 255 is no code of a simple or of a Type 3 font
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(getFont("F1").data(), createCode(0x141, 2, 0x141)).has_value());
    QVERIFY(!PDFContentTextLayout::getDictionaryWidth(getFont("T3").data(), createCode(0x141, 2, 0x141)).has_value());
    QCOMPARE(PDFContentTextLayout::getUnicode(getFont("F1").data(), createCode(0x141, 2, 0x141)), QString());
}

void ContentTextLayoutTest::longString()
{
    // 20 000 times the code 65 at the size 10: the item i is at 7 * i, the end at 140 000 (the
    // sums of 7 are exact numbers)
    const int count = 20000;
    PDFContentTextLayout layout;
    layout.beginText();
    const std::vector<PDFContentTextItem> items = layout.showString(QByteArray(count, 'A'), createState(getFont("F1"), 10.0));
    QCOMPARE(items.size(), size_t(count));
    QCOMPARE(items[1].textMatrix.dx(), 7.0);
    QCOMPARE(items[size_t(count) - 1].textMatrix.dx(), 7.0 * (count - 1));
    QCOMPARE(layout.getTextMatrix().dx(), 7.0 * count);
    QCOMPARE(layout.getTextMatrix().dy(), 0.0);
    QVERIFY(layout.isComplete());
}

void ContentTextLayoutTest::manyThreads()
{
    // The static functions and the realized font are used by more threads at once (the tree
    // builder of each page has its own layout, the fonts are shared): every thread gets the
    // positions of (A B) in /F1 at the size 10 - 0, 7 and 10, the end at 16.5 - and the outline
    // of A of the shared realized font
    const PDFFontPointer font = getFont("F1");
    const PDFFontPointer type0Font = getFont("F0");
    PDFContentTextState state = createState(font, 10.0);
    state.realizedFont = createRealizedFont(font, 10.0);
    QVERIFY(state.realizedFont);
    const PDFContentTextState type0State = createState(type0Font, 10.0);

    PDFContentTextLayout referenceLayout;
    referenceLayout.beginText();
    const std::vector<PDFContentTextItem> referenceItems = referenceLayout.showString("A B", state);
    QCOMPARE(referenceItems.size(), size_t(3));
    const PDFGlyphOutline* outline = referenceItems[0].outline;
    QVERIFY(outline != nullptr);

    static constexpr int threadCount = 8;
    static constexpr int iterationCount = 200;
    std::atomic<int> failures = 0;

    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&state, &type0State, &failures, outline]()
        {
            for (int iteration = 0; iteration < iterationCount; ++iteration)
            {
                PDFContentTextLayout layout;
                layout.beginText();
                const std::vector<PDFContentTextItem> items = layout.showString("A B", state);
                const bool isSimpleRight = items.size() == 3 &&
                                           isMatrix(items[0].textMatrix, 1, 0, 0, 1, 0, 0) &&
                                           isMatrix(items[1].textMatrix, 1, 0, 0, 1, 7, 0) &&
                                           isMatrix(items[2].textMatrix, 1, 0, 0, 1, 10, 0) &&
                                           isMatrix(layout.getTextMatrix(), 1, 0, 0, 1, 16.5, 0) &&
                                           items[0].outline == outline && items[0].unicode == QStringLiteral("A") &&
                                           layout.isComplete();

                // <0041 0064> in /F0: A at 0, the code 0x64 ("ff", the width 480) at 7, the end at 11.8
                layout.beginText();
                const std::vector<PDFContentTextItem> type0Items = layout.showString(QByteArray("\x00\x41\x00\x64", 4), type0State);
                const bool isType0Right = type0Items.size() == 2 &&
                                          isMatrix(type0Items[1].textMatrix, 1, 0, 0, 1, 7, 0) &&
                                          isMatrix(layout.getTextMatrix(), 1, 0, 0, 1, 11.8, 0) &&
                                          type0Items[1].unicode == QStringLiteral("ff");

                if (!isSimpleRight || !isType0Right)
                {
                    ++failures;
                }
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    QCOMPARE(failures.load(), 0);
}

QTEST_MAIN(ContentTextLayoutTest)

#include "tst_contenttextlayouttest.moc"
