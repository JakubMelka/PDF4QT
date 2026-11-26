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

#include "pdffont.h"
#include "pdfnametounicode.h"
#include "pdfparser.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QRect>
#include <QThread>

#include <algorithm>
#include <barrier>
#include <map>
#include <tuple>
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttextflow.h"
#include "pdfeditorfallbackfont.h"
#include "contenttestdocuments.h"

class FontEncodingTest : public QObject
{
    Q_OBJECT

private slots:
    void test_cmap_encode_roundtrip();
    void test_type0_encode_invariant();
    void test_type0_cid_to_unicode();
    void test_type0_cid_encode_ambiguity();
    void test_type0_dictionary_advances_data();
    void test_type0_dictionary_advances();
    void test_type0_cjk_substitution();
    void test_type0_missing_substitute_glyph();
    void test_glyph_outline();
    void test_realized_font_concurrent_use();
    void test_font_cache_concurrent_updates();
    void test_type0_cjk_collection_face();
    void test_type0_cjk_collection_coverage_data();
    void test_type0_cjk_collection_coverage();
    void test_simple_font_encode();
    void test_standard_font_zapf_dingbats();
    void test_standard_font_symbolic_clone_data();
    void test_standard_font_symbolic_clone();
    void test_type3_font_encode();
    void test_fallback_font_generator();
    void test_nested_stream_extraction();
    void test_differences_name_character_data();
    void test_differences_name_character();
    void test_differences_font_not_embedded_data();
    void test_differences_font_not_embedded();
    void test_differences_truetype_embedded();
    void test_differences_type1_embedded();
    void test_differences_tounicode_character();
    void test_differences_type3();
    void test_glyph_name_text_data();
    void test_glyph_name_text();
    void test_glyph_name_text_zapf_dingbats_lookup_data();
    void test_glyph_name_text_zapf_dingbats_lookup();
    void test_type3_names_text_extraction_data();
    void test_type3_names_text_extraction();
    void test_glyph_name_text_hostile_data();
    void test_glyph_name_text_hostile();
    void test_differences_text_data();
    void test_differences_text();
    void test_differences_text_type1_embedded();
    void test_differences_text_truetype_embedded();
    void test_differences_text_tounicode();
    void test_differences_text_type3();
    void test_builtin_encoding_text();
    void test_differences_text_not_embedded_data();
    void test_differences_text_not_embedded();
    void test_differences_text_extraction_data();
    void test_differences_text_extraction();
    void test_type0_encoding_cmap_without_codes();
    void test_tounicode_cmap_of_texts_only();
    void test_cmap_without_codes_interprets_nothing();
    void test_simple_font_real_widths();
    void test_tounicode_lookup_many_entries();
    void test_tounicode_lookup_first_entry();

private:
    /// A font of a hand-written font dictionary with its document, and the text sequence of the shown codes
    struct TestFont
    {
        pdf::PDFDocument document;
        pdf::PDFFontPointer font;
        pdf::PDFRealizedFontPointer realizedFont;
        pdf::TextSequence sequence;
    };

    /// A stream of the document of a test font
    struct TestStream
    {
        QByteArray data;
        QByteArray dictionaryEntries;   ///< Entries of the stream dictionary besides /Length
    };

    /// Creates a font of a font dictionary, in which "@1", "@2", ... are the references to the
    /// streams, realizes it in the size of 20 and shows the codes. A failure leaves the font null.
    static void createTestFont(TestFont* testFont, QByteArray fontDictionary, const std::vector<TestStream>& streams, const QByteArray& shownCodes);

    /// The eexec and charstring encryption of the Type 1 font format (Adobe Type 1 Font Format, 7)
    static QByteArray encryptType1(const QByteArray& plain, quint16 key);

    /// A number of a Type 1 charstring (Adobe Type 1 Font Format, 6.2)
    static QByteArray encodeType1Number(int value);

    /// The encrypted charstring of a glyph of the advance width, which is a filled rectangle
    /// (an empty rectangle = no outline)
    static QByteArray createType1Glyph(int width, const QRect& rectangle);

    /// A Type 1 font program with the built-in encoding StandardEncoding and the glyphs .notdef,
    /// B, C, four.sups, uni20AC, one, one.oldstyle, f_i, uni00410042 and uni0041.smcp (rectangles
    /// of different sizes), as it is stored in /FontFile: the clear text and the binary
    /// encrypted part
    static QByteArray createType1FontProgram(qsizetype* clearTextLength);

    /// The same font program with another built-in encoding: \p encoding is the PostScript text,
    /// which follows "/Encoding " in the font dictionary of the program
    static QByteArray createType1FontProgram(qsizetype* clearTextLength, const QByteArray& encoding);

    /// The string of one code point (a surrogate pair outside of the basic multilingual plane)
    static QString codePointText(char32_t codePoint);

    /// The font dictionary text of the embedded Type 1 test font with /Differences from the code
    /// 65 on; "@1" is the reference to the font program
    static QByteArray type1TestFontDictionary(const QByteArray& differences, int codeCount);

    /// The stream of the font program for createTestFont
    static TestStream type1TestFontStream(const QByteArray& fontProgram, qsizetype clearTextLength);

    /// A font of a hand-written font dictionary, in which "@1" is the reference to a stream with
    /// \p streamData, in \p document; null and the message of the exception, when createFont refuses it
    static pdf::PDFFontPointer createFontWithStream(pdf::PDFDocument* document, QByteArray fontDictionary, const QByteArray& streamData, QString* errorMessage);

    /// The font dictionary of a Type 0 font of the collection Adobe-Identity, whose /Encoding is
    /// "@1" (createFontWithStream)
    static QByteArray type0FontDictionaryWithEncodingStream();
};

void FontEncodingTest::test_cmap_encode_roundtrip()
{
    // CMap with a non-zero based code range - regression test for the wrong
    // range check in PDFFontCMap::encode.
    QByteArray cmapData =
        "1 begincodespacerange\n"
        "<20> <7E>\n"
        "endcodespacerange\n"
        "1 begincidrange\n"
        "<20> <7E> 1\n"
        "endcidrange\n";

    pdf::PDFFontCMap cmap = pdf::PDFFontCMap::createFromData(cmapData);
    QVERIFY(cmap.isValid());

    for (pdf::CID cid = 1; cid <= 95; ++cid)
    {
        QByteArray encoded = cmap.encode(cid);
        QVERIFY2(!encoded.isEmpty(), qPrintable(QString("CID %1 not encoded").arg(cid)));

        std::vector<pdf::CID> cids = cmap.interpret(encoded);
        QCOMPARE(cids.size(), size_t(1));
        QCOMPARE(cids.front(), cid);
    }

    // CIDs outside of the range must not be encoded
    QVERIFY(cmap.encode(0).isEmpty());
    QVERIFY(cmap.encode(96).isEmpty());
}

void FontEncodingTest::test_type0_encode_invariant()
{
    // Non-identity encoding CMap: 2-byte codes 0x0020..0x007E map to CIDs 100..194
    QByteArray encodingCMapData =
        "1 begincodespacerange\n"
        "<0000> <FFFF>\n"
        "endcodespacerange\n"
        "1 begincidrange\n"
        "<0020> <007E> 100\n"
        "endcidrange\n";

    // ToUnicode CMap maps the character codes to unicode
    QByteArray toUnicodeCMapData =
        "1 begincodespacerange\n"
        "<0000> <FFFF>\n"
        "endcodespacerange\n"
        "1 beginbfrange\n"
        "<0020> <007E> <0020>\n"
        "endbfrange\n";

    pdf::PDFFontCMap cmap = pdf::PDFFontCMap::createFromData(encodingCMapData);
    pdf::PDFFontCMap toUnicode = pdf::PDFFontCMap::createFromData(toUnicodeCMapData);
    QVERIFY(cmap.isValid());
    QVERIFY(toUnicode.isValid());

    pdf::PDFType0Font font(pdf::CIDSystemInfo(), "F1", pdf::FontDescriptor(), cmap, toUnicode, pdf::PDFCIDtoGIDMapper(QByteArray()), 500.0, {});

    for (unsigned int code = 0x20; code <= 0x7E; ++code)
    {
        // Simulate the forward pass: decode the code and get the unicode character
        QByteArray codeBytes;
        codeBytes.append(static_cast<char>((code >> 8) & 0xFF));
        codeBytes.append(static_cast<char>(code & 0xFF));

        std::vector<pdf::PDFFontCMap::MappedCode> mappedCodes = cmap.interpretWithCode(codeBytes);
        QCOMPARE(mappedCodes.size(), size_t(1));
        QCOMPARE(mappedCodes.front().cid, pdf::CID(code - 0x20 + 100));

        QChar character = toUnicode.getToUnicode(mappedCodes.front().code, mappedCodes.front().byteCount);
        QVERIFY(!character.isNull());

        // The invariant: the character produced by decoding must encode back
        // to a byte sequence, which decodes to the same character.
        pdf::PDFEncodedText encodedText = font.encodeText(QString(character));
        QVERIFY2(encodedText.isValid, qPrintable(QString("Character U+%1 not encoded").arg(uint(character.unicode()), 4, 16, QChar('0'))));
        QCOMPARE(encodedText.encodedText, codeBytes);
    }

    // Characters not present in the font must not be encoded
    QVERIFY(!font.encodeText(QString(QChar(0x2026))).isValid);
    QVERIFY(font.encodeCharacter(0x2026).isEmpty());
}

void FontEncodingTest::test_type0_cid_to_unicode()
{
    // Identity-H encoding, no ToUnicode CMap - this is how non-embedded CID keyed
    // fonts of the predefined adobe collections are usually written (issue #393).
    // The CIDs must be translated to unicode using the predefined unicode CMap of
    // the character collection, otherwise neither the text can be extracted, nor
    // a glyph can be found in the substituted system font.
    QByteArray identityCMapData =
        "1 begincodespacerange\n"
        "<0000> <FFFF>\n"
        "endcodespacerange\n"
        "1 begincidrange\n"
        "<0000> <FFFF> 0\n"
        "endcidrange\n";

    pdf::PDFFontCMap cmap = pdf::PDFFontCMap::createFromData(identityCMapData);
    QVERIFY(cmap.isValid());

    auto createFont = [&cmap](const char* registry, const char* ordering)
    {
        pdf::CIDSystemInfo cidSystemInfo;
        cidSystemInfo.registry = registry;
        cidSystemInfo.ordering = ordering;
        cidSystemInfo.supplement = 2;

        return pdf::PDFType0Font(cidSystemInfo, "F1", pdf::FontDescriptor(), cmap, pdf::PDFFontCMap(),
                                 pdf::PDFCIDtoGIDMapper(QByteArray()), 1000.0, {});
    };

    // Adobe-Japan1: CIDs 1 - 94 are the proportional latin characters, CID 843 is
    // hiragana letter A. Values are taken from the UniJIS-UCS2-H CMap.
    pdf::PDFType0Font japaneseFont = createFont("Adobe", "Japan1");
    QCOMPARE(japaneseFont.getUnicodeFromCID(1), QChar(' '));
    QCOMPARE(japaneseFont.getUnicodeFromCID(34), QChar('A'));
    QCOMPARE(japaneseFont.getUnicodeFromCID(53), QChar('T'));
    QCOMPARE(japaneseFont.getUnicodeFromCID(66), QChar('a'));
    QCOMPARE(japaneseFont.getUnicodeFromCID(843), QChar(0x3042));
    // Vertical punctuation has separate CIDs, absent from UniJIS-UCS2-H.
    QCOMPARE(japaneseFont.getUnicodeFromCID(7887), QChar(0x3001));
    QCOMPARE(japaneseFont.getUnicodeFromCID(7888), QChar(0x3002));

    // CID 0 is the notdef glyph, it has no unicode value
    QVERIFY(japaneseFont.getUnicodeFromCID(0).isNull());

    // The latin part is the same in all of the adobe collections
    for (const char* ordering : { "GB1", "CNS1", "Korea1" })
    {
        pdf::PDFType0Font font = createFont("Adobe", ordering);
        QCOMPARE(font.getUnicodeFromCID(1), QChar(' '));
        QCOMPARE(font.getUnicodeFromCID(34), QChar('A'));
        QCOMPARE(font.getUnicodeFromCID(66), QChar('a'));
    }

    // Adobe-Identity has no character collection, so no mapping exists
    pdf::PDFType0Font identityFont = createFont("Adobe", "Identity");
    QVERIFY(identityFont.getUnicodeFromCID(1).isNull());
    QVERIFY(identityFont.getUnicodeFromCID(34).isNull());

    // Text encoding must use the same mapping as decoding - 'A' is CID 34,
    // which is the character code 0x0022 of the Identity-H encoding
    QCOMPARE(japaneseFont.encodeCharacter(U'A'), QByteArray("\x00\x22", 2));
    QCOMPARE(japaneseFont.encodeCharacter(0x3042), QByteArray("\x03\x4B", 2));
    QVERIFY(identityFont.encodeCharacter(U'A').isEmpty());
}

void FontEncodingTest::test_type0_cid_encode_ambiguity()
{
    // CID fallback must respect the decoder's first matching entry, including
    // a shorter code that consumes the prefix of a longer code.
    for (const QByteArray& data : {
        QByteArray("2 begincidrange\n<0021> <0022> 33\n<0022> <0022> 66\nendcidrange\n"),
        QByteArray("2 begincidchar\n<22> 34\n<2200> 66\nendcidchar\n") })
    {
        const auto cmap = pdf::PDFFontCMap::createFromData(data);
        pdf::PDFType0Font font(pdf::CIDSystemInfo{ "Adobe", "Japan1", 2 }, "F1", pdf::FontDescriptor(), cmap,
            pdf::PDFFontCMap(), pdf::PDFCIDtoGIDMapper(QByteArray()), 1000.0, {});
        const QByteArray encoded = font.encodeCharacter(U'A');
        QVERIFY(!encoded.isEmpty());
        const auto decoded = cmap.interpretWithCode(encoded);
        QCOMPARE(decoded.size(), size_t(1));
        QCOMPARE(font.getUnicodeFromCID(decoded.front().cid), QChar('A'));
        QVERIFY(font.encodeCharacter(U'a').isEmpty());
    }
}

void FontEncodingTest::test_type0_dictionary_advances_data()
{
    QTest::addColumn<QByteArray>("encoding");
    QTest::addColumn<QByteArray>("metrics");
    QTest::addColumn<double>("advance");
    QTest::newRow("horizontal-default") << QByteArray("Identity-H") << QByteArray() << 1000.0;
    QTest::newRow("horizontal-ignores-DW2") << QByteArray("Identity-H") << QByteArray("/DW2 [880 -700]") << 1000.0;
    QTest::newRow("horizontal-DW") << QByteArray("Identity-H") << QByteArray("/DW 600") << 600.0;
    QTest::newRow("horizontal-W-range") << QByteArray("Identity-H") << QByteArray("/W [34 35 450.5]") << 450.5;
    QTest::newRow("horizontal-W-array") << QByteArray("Identity-H") << QByteArray("/W [33 [200 450.5]]") << 450.5;
    QTest::newRow("vertical-default") << QByteArray("Identity-V") << QByteArray() << -1000.0;
    QTest::newRow("vertical-ignores-W") << QByteArray("Identity-V") << QByteArray("/DW 600 /W [34 34 400]") << -1000.0;
    QTest::newRow("vertical-DW2") << QByteArray("Identity-V") << QByteArray("/DW2 [880 -700]") << -700.0;
    QTest::newRow("vertical-W2-range") << QByteArray("Identity-V") << QByteArray("/W2 [34 35 -450.5 250 880]") << -450.5;
    QTest::newRow("vertical-W2-array") << QByteArray("Identity-V") << QByteArray("/W2 [33 [-200 250 880 -450.5 250 880]]") << -450.5;
}

void FontEncodingTest::test_type0_dictionary_advances()
{
    QFETCH(QByteArray, encoding);
    QFETCH(QByteArray, metrics);
    QFETCH(double, advance);

    const QByteArray data = "<< /Type /Font /Subtype /Type0 /BaseFont /Test /Encoding /" + encoding +
        " /DescendantFonts [<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Test "
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Japan1) /Supplement 2 >> " + metrics + " >>] >>";
    pdf::PDFParser parser(data, nullptr, pdf::PDFParser::None);
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFDocument document = builder.build();
    const auto font = pdf::PDFFont::createFont(parser.getObject(), "F1", &document);
    const auto* type0 = dynamic_cast<const pdf::PDFType0Font*>(font.get());
    QVERIFY(type0);
    QCOMPARE(type0->getGlyphAdvance(34), advance);
}

void FontEncodingTest::test_type0_cjk_substitution()
{
    // Gothic must not be matched to Franklin Gothic, and the descriptor's latin
    // family must not bypass the CJK fallback.
    QString cjkFamily;
    for (const QString& candidate : { QStringLiteral("MS PGothic"), QStringLiteral("Noto Sans CJK JP"), QStringLiteral("Hiragino Sans") })
    {
        if (QFontDatabase::families().contains(candidate))
        {
            cjkFamily = candidate;
            break;
        }
    }
    if (cjkFamily.isEmpty())
    {
        QSKIP("No supported Japanese test font installed.");
    }

    pdf::PDFRenderErrorReporterDummy reporter;
    const auto cmap = pdf::PDFFontCMap::createFromName("Identity-H");
    auto realize = [&](const QByteArray& name, const QByteArray& family)
    {
        pdf::FontDescriptor descriptor;
        descriptor.fontName = name;
        descriptor.fontFamily = family;
        pdf::PDFFontPointer font(new pdf::PDFType0Font(pdf::CIDSystemInfo{ "Adobe", "Japan1", 2 }, "F1", descriptor,
            cmap, pdf::PDFFontCMap(), pdf::PDFCIDtoGIDMapper(QByteArray()), 1000.0,
            std::unordered_map<pdf::CID, pdf::PDFReal>{ { 34, 450.5 } }));
        return pdf::PDFRealizedFont::createRealizedFont(font, 20.0, &reporter);
    };
    for (const QByteArray& name : { QByteArray("Gothic"), QByteArray("PDF4QTNonexistentCJKFont") })
    {
        const auto actual = realize(name, "Arial");
        QVERIFY(actual->getPostScriptName() != QStringLiteral("ArialMT"));
        pdf::TextSequence sequence;
        actual->fillTextSequence(QByteArray::fromHex("0022034b"), sequence, &reporter);
        QCOMPARE(sequence.items.size(), size_t(2));
        QCOMPARE(sequence.items[0].character, QChar('A'));
        QCOMPARE(sequence.items[0].advance, 9.01);
        QCOMPARE(sequence.items[1].character, QChar(0x3042));
        QCOMPARE(sequence.items[1].advance, 20.0);
        QVERIFY(sequence.items[1].glyph && !sequence.items[1].glyph->isEmpty());
    }
}

void FontEncodingTest::test_type0_cjk_collection_face()
{
    // Noto CJK shares several regional faces in one TTC. The Korean face is
    // index 1, so silently loading face 0 would return the Japanese font.
    const QString family = QStringLiteral("Noto Sans CJK KR");
    if (!QFontDatabase::families().contains(family))
    {
        QSKIP("Noto Sans CJK KR is not installed.");
    }

    pdf::FontDescriptor descriptor;
    descriptor.fontName = family.toLatin1();
    pdf::PDFFontPointer font(new pdf::PDFType0Font(pdf::CIDSystemInfo{ "Adobe", "Korea1", 2 }, "F1", descriptor,
        pdf::PDFFontCMap::createFromName("Identity-H"), pdf::PDFFontCMap(), pdf::PDFCIDtoGIDMapper(QByteArray()), 1000.0, {}));
    pdf::PDFRenderErrorReporterDummy reporter;
    const auto realized = pdf::PDFRealizedFont::createRealizedFont(font, 20.0, &reporter);
    QCOMPARE(realized->getPostScriptName(), QStringLiteral("NotoSansCJKkr-Regular"));
}

void FontEncodingTest::test_type0_cjk_collection_coverage_data()
{
    QTest::addColumn<QByteArray>("ordering");
    QTest::addColumn<char32_t>("codePoint");
    QTest::newRow("Japan1") << QByteArray("Japan1") << U'\u3042';
    QTest::newRow("GB1") << QByteArray("GB1") << U'\u4e2d';
    QTest::newRow("CNS1") << QByteArray("CNS1") << U'\u4e2d';
    QTest::newRow("Korea1") << QByteArray("Korea1") << U'\uac00';
}

void FontEncodingTest::test_type0_cjk_collection_coverage()
{
    QFETCH(QByteArray, ordering);
    QFETCH(char32_t, codePoint);

    // The substituted font is found either in the platform fallback list, or by the
    // last resort script matching (fontconfig language on unix, character set of the
    // installed fonts on windows). Whichever font is found, it must really cover the
    // script - a latin font matched by name similarity would render nothing.
    pdf::FontDescriptor descriptor;
    descriptor.fontName = "PDF4QTNonexistentCJKFont";

    pdf::PDFFontPointer font(new pdf::PDFType0Font(pdf::CIDSystemInfo{ "Adobe", ordering, 2 }, "F1", descriptor,
        pdf::PDFFontCMap::createFromName("Identity-H"), pdf::PDFFontCMap(), pdf::PDFCIDtoGIDMapper(QByteArray()), 1000.0, {}));

    const QByteArray encoded = font->encodeCharacter(codePoint);
    QVERIFY2(!encoded.isEmpty(), "Character is not in the character collection");

    pdf::PDFRenderErrorReporterDummy reporter;
    pdf::PDFRealizedFontPointer realized;
    try
    {
        realized = pdf::PDFRealizedFont::createRealizedFont(font, 20.0, &reporter);
    }
    catch (const pdf::PDFException&)
    {
        QSKIP("No font covering the script of this character collection is installed.");
    }

    pdf::TextSequence sequence;
    realized->fillTextSequence(encoded, sequence, &reporter);
    QCOMPARE(sequence.items.size(), size_t(1));
    QCOMPARE(sequence.items.front().character, QChar(char16_t(codePoint)));
    QVERIFY2(sequence.items.front().glyph && !sequence.items.front().glyph->isEmpty(),
             qPrintable(QStringLiteral("Substituted font %1 has no glyph for the script").arg(realized->getPostScriptName())));
}

void FontEncodingTest::test_type0_missing_substitute_glyph()
{
    QString family;
    for (const QString& candidate : { QStringLiteral("Arial"), QStringLiteral("Liberation Sans"), QStringLiteral("DejaVu Sans") })
    {
        if (QFontDatabase::families().contains(candidate))
        {
            family = candidate;
            break;
        }
    }
    if (family.isEmpty())
    {
        QSKIP("No supported Latin test font installed.");
    }

    pdf::FontDescriptor descriptor;
    descriptor.fontName = family.toLatin1();
    const auto cmap = pdf::PDFFontCMap::createFromName("Identity-H");
    const auto toUnicode = pdf::PDFFontCMap::createFromData("1 beginbfchar\n<0022> <FFFF>\nendbfchar\n");
    pdf::PDFFontPointer font(new pdf::PDFType0Font(pdf::CIDSystemInfo{ "Adobe", "Japan1", 2 }, "F1", descriptor,
        cmap, toUnicode, pdf::PDFCIDtoGIDMapper(QByteArray()), 500.0, std::unordered_map<pdf::CID, pdf::PDFReal>()));
    pdf::PDFRenderErrorReporterDummy reporter;
    const auto realized = pdf::PDFRealizedFont::createRealizedFont(font, 20.0, &reporter);
    pdf::TextSequence sequence;
    // Neither a missing Unicode glyph nor an unmapped CID may become an unrelated
    // glyph index in the substitute (or an out-of-range FreeType glyph load).
    realized->fillTextSequence(QByteArray::fromHex("0022ffff"), sequence, &reporter);
    QCOMPARE(sequence.items.size(), size_t(2));
    for (const auto& item : sequence.items)
    {
        QVERIFY(!item.glyph);
        QCOMPARE(item.advance, -500.0);
    }
}

void FontEncodingTest::test_glyph_outline()
{
    // Outline of a glyph is shared by the threads, so it holds a copy of the elements
    // of a painter path. Path created from the outline must be the same, as the original
    // path transformed by the matrix, and it must be a new path for each call.
    auto getElements = [](const QPainterPath& path)
    {
        std::vector<std::tuple<int, qreal, qreal>> elements;
        for (int i = 0; i < path.elementCount(); ++i)
        {
            const QPainterPath::Element element = path.elementAt(i);
            elements.emplace_back(int(element.type), element.x, element.y);
        }
        return elements;
    };

    // Two contours with lines and curves, as they are created from the outline of a font
    QPainterPath path;
    path.moveTo(1.0, 2.0);
    path.lineTo(11.0, 2.5);
    path.quadTo(14.0, 9.0, 8.0, 12.0);
    path.cubicTo(6.0, 15.0, 2.0, 13.0, 1.5, 7.0);
    path.lineTo(1.0, 2.0);
    path.moveTo(4.0, 5.0);
    path.lineTo(7.0, 5.0);
    path.cubicTo(8.0, 6.0, 8.0, 8.0, 5.0, 9.0);
    path.closeSubpath();
    QVERIFY(path.elementCount() > 12);

    const pdf::PDFGlyphOutline outline(path);
    QVERIFY(!outline.isEmpty());
    QCOMPARE(outline.getBoundingRect(), path.boundingRect());

    // The curve bulges out of the polygon of the end points, and the control points
    // lie outside of the curve - the rectangle is the exact one
    QVERIFY(outline.getBoundingRect().right() > 11.0);
    QVERIFY(outline.getBoundingRect() != path.controlPointRect());

    QTransform rotation;
    rotation.translate(120.0, -35.0);
    rotation.rotate(33.0);
    rotation.scale(0.7, 1.9);

    const std::vector<QTransform> matrices = { QTransform(),
                                               QTransform::fromTranslate(250.5, 700.25),
                                               QTransform::fromScale(0.012, -0.012),
                                               QTransform(1.0, 0.0, 0.3, 1.0, 5.0, 6.0),
                                               rotation,
                                               QTransform(1.0, 0.0, 0.001, 0.0, 1.0, 0.002, 10.0, 20.0, 1.0) };

    for (const QTransform& matrix : matrices)
    {
        const QPainterPath expected = matrix.map(path);
        const QPainterPath created = outline.createPath(matrix);
        QVERIFY(getElements(created) == getElements(expected));
        QCOMPARE(created.fillRule(), expected.fillRule());
        QCOMPARE(created.boundingRect(), expected.boundingRect());
        QCOMPARE(created.controlPointRect(), expected.controlPointRect());
    }
    QVERIFY(!matrices.back().isAffine());

    // Created path is independent of the outline, and of the other created paths
    QPainterPath first = outline.createPath(QTransform());
    const QPainterPath second = outline.createPath(QTransform());
    first.translate(100.0, 100.0);
    first.lineTo(0.0, 0.0);
    QVERIFY(getElements(second) == getElements(path));
    QVERIFY(getElements(outline.createPath(QTransform())) == getElements(path));
    QCOMPARE(outline.getBoundingRect(), path.boundingRect());

    // Fill rule is a part of the outline
    QPainterPath windingPath = path;
    windingPath.setFillRule(Qt::WindingFill);
    QCOMPARE(pdf::PDFGlyphOutline(windingPath).createPath(rotation).fillRule(), Qt::WindingFill);

    // Each outline created from a path has its own identifier, also if the path
    // is the same. A copy of an outline is the same outline.
    const pdf::PDFGlyphOutline otherOutline(path);
    const pdf::PDFGlyphOutline outlineCopy = outline;
    QVERIFY(outline.getId() != 0);
    QVERIFY(otherOutline.getId() != 0);
    QVERIFY(otherOutline.getId() != outline.getId());
    QCOMPARE(outlineCopy.getId(), outline.getId());
    QCOMPARE(pdf::PDFGlyphOutline().getId(), quint64(0));

    // Outline of a glyph, which has nothing to paint (a space)
    const pdf::PDFGlyphOutline emptyOutline{ QPainterPath() };
    QVERIFY(emptyOutline.isEmpty());
    QVERIFY(emptyOutline.createPath(rotation).isEmpty());
    QCOMPARE(emptyOutline.createPath(rotation).elementCount(), 0);
    QCOMPARE(emptyOutline.getBoundingRect(), QRectF());
    QVERIFY(pdf::PDFGlyphOutline().isEmpty());

    QPainterPath singlePointPath;
    singlePointPath.moveTo(3.0, 4.0);
    QVERIFY(pdf::PDFGlyphOutline(singlePointPath).isEmpty());
}

void FontEncodingTest::test_realized_font_concurrent_use()
{
    // Realized font is shared by the threads compiling the pages. FreeType face
    // may be used by one thread at a time, so the character lookups are read from
    // the face when the font is realized, and the glyph loading is serialized.
    // Loaded glyphs are found without a lock, and each glyph exists only once,
    // so all threads must get the same glyph for the same character.
    pdf::FontDescriptor descriptor;
    descriptor.fontName = "PDF4QTNonexistentCJKFont";
    pdf::PDFFontPointer font(new pdf::PDFType0Font(pdf::CIDSystemInfo{ "Adobe", "Japan1", 2 }, "F1", descriptor,
        pdf::PDFFontCMap::createFromName("Identity-H"), pdf::PDFFontCMap(), pdf::PDFCIDtoGIDMapper(QByteArray()), 1000.0, {}));

    QByteArray encoded;
    for (char32_t codePoint = U'ぁ'; codePoint <= U'ゖ'; ++codePoint)
    {
        encoded += font->encodeCharacter(codePoint);
    }
    for (char32_t codePoint = U'一'; codePoint <= U'俿'; ++codePoint)
    {
        encoded += font->encodeCharacter(codePoint);
    }
    QVERIFY(!encoded.isEmpty());

    pdf::PDFRenderErrorReporterDummy reporter;
    pdf::PDFRealizedFontPointer referenceFont;
    pdf::PDFRealizedFontPointer sharedFont;
    try
    {
        referenceFont = pdf::PDFRealizedFont::createRealizedFont(font, 20.0, &reporter);
        sharedFont = pdf::PDFRealizedFont::createRealizedFont(font, 20.0, &reporter);
    }
    catch (const pdf::PDFException&)
    {
        QSKIP("No font covering the Japanese character collection is installed.");
    }

    struct Item
    {
        QChar character;
        pdf::PDFReal advance = 0.0;
        int elementCount = -1;
        QRectF bounds;
        QRectF controlBounds;
        std::vector<std::tuple<int, qreal, qreal>> outline;

        bool operator==(const Item&) const = default;
    };

    auto collect = [](const pdf::TextSequence& sequence)
    {
        std::vector<Item> items;
        for (const pdf::TextSequenceItem& item : sequence.items)
        {
            Item result;
            result.character = item.character;
            result.advance = item.advance;
            if (item.glyph)
            {
                // Each thread has its own path created from the outline shared by the threads
                const QPainterPath path = item.glyph->createPath(QTransform());
                result.elementCount = path.elementCount();
                result.bounds = item.glyph->getBoundingRect();
                result.controlBounds = path.controlPointRect();
                for (int index = 0; index < result.elementCount; ++index)
                {
                    const QPainterPath::Element element = path.elementAt(index);
                    result.outline.emplace_back(int(element.type), element.x, element.y);
                }
            }
            items.push_back(std::move(result));
        }
        return items;
    };

    pdf::TextSequence referenceSequence;
    referenceFont->fillTextSequence(encoded, referenceSequence, &reporter);
    const std::vector<Item> reference = collect(referenceSequence);
    QVERIFY(std::any_of(reference.cbegin(), reference.cend(), [](const Item& item) { return item.elementCount > 0; }));
    const size_t referenceCharacterInfos = referenceFont->getCharacterInfos().size();

    auto collectGlyphs = [](const pdf::TextSequence& sequence)
    {
        std::vector<const pdf::PDFGlyphOutline*> glyphs;
        for (const pdf::TextSequenceItem& item : sequence.items)
        {
            glyphs.push_back(item.glyph);
        }
        return glyphs;
    };

    // Number of different glyphs of the text (glyphs are published in blocks of 256 glyphs)
    std::vector<const pdf::PDFGlyphOutline*> referenceGlyphs = collectGlyphs(referenceSequence);
    std::sort(referenceGlyphs.begin(), referenceGlyphs.end());
    referenceGlyphs.erase(std::unique(referenceGlyphs.begin(), referenceGlyphs.end()), referenceGlyphs.end());
    qInfo().noquote() << QString("Different glyphs: %1").arg(referenceGlyphs.size());

    constexpr int threadCount = 8;
    std::barrier start(threadCount);
    std::vector<std::vector<Item>> results(threadCount);
    std::vector<std::vector<const pdf::PDFGlyphOutline*>> glyphs(threadCount);
    std::vector<std::vector<const pdf::PDFGlyphOutline*>> repeatedGlyphs(threadCount);
    std::vector<size_t> characterInfos(threadCount, 0);
    std::vector<std::unique_ptr<QThread>> threads;
    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(QThread::create([&, i]()
        {
            pdf::PDFRenderErrorReporterDummy threadReporter;
            start.arrive_and_wait();
            if (i % 4 == 3)
            {
                // Direct access to the face, concurrently with the glyph loading
                characterInfos[i] = sharedFont->getCharacterInfos().size();
            }

            pdf::TextSequence sequence;
            sharedFont->fillTextSequence(encoded, sequence, &threadReporter);
            results[i] = collect(sequence);
            glyphs[i] = collectGlyphs(sequence);

            // All glyphs are loaded now
            pdf::TextSequence repeatedSequence;
            sharedFont->fillTextSequence(encoded, repeatedSequence, &threadReporter);
            repeatedGlyphs[i] = collectGlyphs(repeatedSequence);
        }));
    }
    for (const auto& thread : threads)
    {
        thread->start();
    }
    for (const auto& thread : threads)
    {
        QVERIFY(thread->wait(60000));
    }

    for (int i = 0; i < threadCount; ++i)
    {
        QVERIFY(results[i] == reference);
        QVERIFY(glyphs[i] == glyphs[0]);
        QVERIFY(repeatedGlyphs[i] == glyphs[0]);
        if (i % 4 == 3)
        {
            QCOMPARE(characterInfos[i], referenceCharacterInfos);
        }
    }
}

void FontEncodingTest::test_font_cache_concurrent_updates()
{
    pdf::PDFDictionaryBuilder fontDictionary;
    fontDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Font"));
    fontDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Type1"));
    fontDictionary.addEntry(pdf::PDFInplaceOrMemoryString("BaseFont"), pdf::PDFObject::createName("Helvetica"));
    const pdf::PDFObject directFont = pdf::PDFObject::createDictionary(std::move(fontDictionary));

    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFObject indirectFont = pdf::PDFObject::createReference(builder.addObject(directFont));
    pdf::PDFDocument firstDocument = builder.build();
    pdf::PDFDocument secondDocument = firstDocument;
    pdf::PDFFontCache cache(4, 4);
    cache.setDocument(pdf::PDFModifiedDocument(&firstDocument, nullptr));

    constexpr int threadCount = 6;
    std::barrier start(threadCount);
    std::vector<QString> errors(threadCount);
    std::vector<std::unique_ptr<QThread>> threads;
    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back(QThread::create([&, i]()
        {
            start.arrive_and_wait();
            try
            {
                pdf::PDFRenderErrorReporterDummy reporter;
                for (int iteration = 0; iteration < 100; ++iteration)
                {
                    if (i == 0)
                    {
                        cache.setCacheLimits(size_t(iteration % 5), size_t(iteration % 3));
                        cache.shrink();
                    }
                    else if (i == 1)
                    {
                        cache.setDocument(pdf::PDFModifiedDocument(
                            iteration % 2 ? &firstDocument : &secondDocument, nullptr));
                    }
                    else
                    {
                        const auto font = cache.getFont(i % 2 ? directFont : indirectFont, "F1");
                        const auto realized = cache.getRealizedFont(font, 20.0, &reporter);
                        pdf::TextSequence sequence;
                        realized->fillTextSequence("A", sequence, &reporter);
                        // The local shared pointer must retain the glyph after a cache eviction.
                        QThread::yieldCurrentThread();
                        if (sequence.items.size() != 1 || !sequence.items.front().glyph ||
                            sequence.items.front().glyph->getBoundingRect().isEmpty())
                        {
                            errors[i] = QStringLiteral("Invalid glyph after a cache update");
                            break;
                        }
                    }
                }
            }
            catch (const pdf::PDFException& exception)
            {
                errors[i] = exception.getMessage();
            }
        }));
    }
    for (const auto& thread : threads)
        thread->start();
    for (const auto& thread : threads)
        QVERIFY(thread->wait(60000));
    for (const QString& error : errors)
        QVERIFY2(error.isEmpty(), qPrintable(error));
}

void FontEncodingTest::test_simple_font_encode()
{
    pdf::encoding::EncodingTable encoding = {};
    pdf::encoding::EncodingTable toUnicode = {};
    pdf::GlyphIndices glyphIndices = {};
    pdf::GlyphNames glyphNames = {};

    // Character code 65: encoding table says 'A', but ToUnicode table
    // says U+00C1 - the forward pass prefers the ToUnicode table.
    encoding[65] = QChar('A');
    toUnicode[65] = QChar(0x00C1);

    // Character code 66: matching tables, but zero glyph index - the forward
    // pass falls back to the unicode charmap during rendering, so the reverse
    // pass must accept it too.
    encoding[66] = QChar('B');
    toUnicode[66] = QChar('B');

    pdf::PDFTrueTypeFont font(pdf::CIDSystemInfo(), "F1", pdf::FontDescriptor(), "Font", "Font", 0, 255,
                              std::vector<pdf::PDFReal>(256, 500.0), pdf::PDFEncoding::Encoding::Custom,
                              encoding, toUnicode, true, pdf::StandardFontType::Invalid, glyphIndices, glyphNames);

    // getUnicode(65) is U+00C1, so U+00C1 must encode to code 65
    QCOMPARE(font.encodeCharacter(0x00C1), QByteArray(1, char(65)));

    // 'A' is masked by the ToUnicode table, it cannot be produced by decoding
    QVERIFY(font.encodeCharacter(U'A').isEmpty());

    // 'B' has zero glyph index, but decoding still produces it
    QCOMPARE(font.encodeCharacter(U'B'), QByteArray(1, char(66)));

    pdf::PDFEncodedText encodedText = font.encodeText(QString(QChar(0x00C1)) + QChar('B'));
    QVERIFY(encodedText.isValid);
    QCOMPARE(encodedText.encodedText, QByteArray("\x41\x42"));
}

void FontEncodingTest::test_standard_font_zapf_dingbats()
{
    // Font ZapfDingbats is one of the standard fonts, so it is usually not embedded, and
    // the marks of the check boxes in the forms are its characters. The substituted system
    // font must have the glyphs of the dingbats - the font Symbol has none of them.
    bool hasDingbatsFont = false;
    for (const QString& candidate : { QStringLiteral("ZapfDingbats"), QStringLiteral("Zapf Dingbats"), QStringLiteral("D050000L"),
                                      QStringLiteral("Dingbats"), QStringLiteral("Segoe UI Symbol"), QStringLiteral("DejaVu Sans") })
    {
        hasDingbatsFont = hasDingbatsFont || QFontDatabase::families().contains(candidate);
    }

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFDocument document = builder.build();

    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << pdf::WrapName("Font");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << pdf::WrapName("Type1");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("BaseFont");
    factory << pdf::WrapName("ZapfDingbats");
    factory.endDictionaryItem();
    factory.endDictionary();

    const pdf::PDFFontPointer font = pdf::PDFFont::createFont(factory.takeObject(), "ZaDb", &document);
    pdf::PDFRenderErrorReporterDummy reporter;
    pdf::PDFRealizedFontPointer realized;
    try
    {
        realized = pdf::PDFRealizedFont::createRealizedFont(font, 20.0, &reporter);
    }
    catch (const pdf::PDFException& exception)
    {
        QVERIFY2(!hasDingbatsFont, qPrintable(exception.getMessage()));
        QSKIP(qPrintable(QStringLiteral("Font ZapfDingbats cannot be substituted: %1").arg(exception.getMessage())));
    }

    // The font database of Qt need not list the fonts (offscreen), so the substituted font is checked, too
    const QStringList dingbatsFonts = { QStringLiteral("ZapfDingbats"), QStringLiteral("ZapfDingbatsITC"), QStringLiteral("D050000L"),
                                        QStringLiteral("Dingbats"), QStringLiteral("SegoeUISymbol"), QStringLiteral("DejaVuSans") };
    if (!hasDingbatsFont && !dingbatsFonts.contains(realized->getPostScriptName()))
    {
        QSKIP(qPrintable(QStringLiteral("No font with the dingbats is installed, font ZapfDingbats is substituted by %1.").arg(realized->getPostScriptName())));
    }

    // Check mark, cross, circle, square, diamond and star - the marks of the check boxes and radio buttons
    pdf::TextSequence sequence;
    realized->fillTextSequence("48lnuH", sequence, &reporter);
    QCOMPARE(sequence.items.size(), size_t(6));
    QCOMPARE(sequence.items[0].character, QChar(0x2714));
    QCOMPARE(sequence.items[1].character, QChar(0x2718));
    for (const pdf::TextSequenceItem& item : sequence.items)
    {
        QVERIFY2(item.glyph && !item.glyph->isEmpty(),
                 qPrintable(QStringLiteral("Substituted font %1 has no glyph of the character U+%2").arg(realized->getPostScriptName()).arg(int(item.character.unicode()), 4, 16, QChar('0'))));
    }

    // Ornaments a89 ... a96, a205, a206 of the codes 0x80 ... 0x8D (ISO 32000-2, Annex D.6)
    pdf::TextSequence ornaments;
    realized->fillTextSequence("\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8A\x8B\x8C\x8D", ornaments, &reporter);
    QCOMPARE(ornaments.items.size(), size_t(14));
    for (size_t i = 0; i < ornaments.items.size(); ++i)
    {
        const pdf::TextSequenceItem& item = ornaments.items[i];
        QCOMPARE(item.character, QChar(char16_t(0x2768 + i)));
        QVERIFY2(item.glyph && !item.glyph->isEmpty(),
                 qPrintable(QStringLiteral("Substituted font %1 has no glyph of the character U+%2").arg(realized->getPostScriptName()).arg(int(item.character.unicode()), 4, 16, QChar('0'))));
    }
}

void FontEncodingTest::test_standard_font_symbolic_clone_data()
{
    QTest::addColumn<QByteArray>("baseFont");
    QTest::addColumn<QString>("postScriptName");
    QTest::addColumn<int>("encoding");
    QTest::addColumn<int>("minimalCharacterCount");
    QTest::addColumn<QByteArrayList>("differences");
    QTest::addColumn<QByteArray>("codesOfDifferences");
    QTest::addColumn<QString>("charactersOfDifferences");

    // Check mark and cross
    QTest::newRow("ZapfDingbats") << QByteArray("ZapfDingbats") << QStringLiteral("D050000L") << int(pdf::PDFEncoding::Encoding::ZapfDingbats)
                                  << 201 << QByteArrayList{ "a20", "a24" } << QByteArray("48") << (QString(QChar(0x2714)) + QChar(0x2718));

    // Names of the font Symbol, which are not the first names of the Adobe Glyph List (forall, approximatelyequal)
    QTest::newRow("Symbol") << QByteArray("Symbol") << QStringLiteral("StandardSymbolsPS") << int(pdf::PDFEncoding::Encoding::Symbol)
                            << 187 << QByteArrayList{ "universal", "congruent" } << QByteArray("\x22\x40") << (QString(QChar(0x2200)) + QChar(0x2245));
}

void FontEncodingTest::test_standard_font_symbolic_clone()
{
    // D050000L and StandardSymbolsPS of URW (package fonts-urw-base35 on Linux, also in
    // Ghostscript) are clones of ZapfDingbats and Symbol with the glyph names of the original
    // fonts (a1 ... a191, alpha, universal, ...). Their unicode charmaps map the character codes
    // (U+0021 ... U+00FE), not the unicode characters of the glyphs, so their glyphs must be
    // found by the glyph names of the encoding. The font database of Qt need not list the font
    // (offscreen), so the substituted font itself decides, whether the test runs.
    QFETCH(QByteArray, baseFont);
    QFETCH(QString, postScriptName);
    QFETCH(int, encoding);
    QFETCH(int, minimalCharacterCount);
    QFETCH(QByteArrayList, differences);
    QFETCH(QByteArray, codesOfDifferences);
    QFETCH(QString, charactersOfDifferences);

    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 100, 100));
    const pdf::PDFDocument document = builder.build();
    pdf::PDFRenderErrorReporterDummy reporter;

    auto createRealizedFont = [&document, &reporter, &baseFont, &differences](bool withDifferences)
    {
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("Font");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << pdf::WrapName("Type1");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("BaseFont");
        factory << pdf::WrapName(baseFont);
        factory.endDictionaryItem();
        if (withDifferences)
        {
            // Codes 'A', 'B', ... changed by their glyph names
            factory.beginDictionaryItem("Encoding");
            factory.beginDictionary();
            factory.beginDictionaryItem("Differences");
            factory.beginArray();
            factory << pdf::PDFInteger('A');
            for (const QByteArray& glyphName : differences)
            {
                factory << pdf::WrapName(glyphName);
            }
            factory.endArray();
            factory.endDictionaryItem();
            factory.endDictionary();
            factory.endDictionaryItem();
        }
        factory.endDictionary();

        const pdf::PDFFontPointer font = pdf::PDFFont::createFont(factory.takeObject(), "F1", &document);
        return pdf::PDFRealizedFont::createRealizedFont(font, 20.0, &reporter);
    };

    pdf::PDFRealizedFontPointer realized;
    try
    {
        realized = createRealizedFont(false);
    }
    catch (const pdf::PDFException& exception)
    {
        QSKIP(qPrintable(QStringLiteral("Font %1 cannot be substituted: %2").arg(QString::fromLatin1(baseFont), exception.getMessage())));
    }
    if (realized->getPostScriptName() != postScriptName)
    {
        QSKIP(qPrintable(QStringLiteral("Font %1 is not installed, font %2 is substituted by %3.").arg(postScriptName, QString::fromLatin1(baseFont), realized->getPostScriptName())));
    }

    // Every character of the encoding has its own glyph in the clone
    const pdf::encoding::EncodingTable* encodingTable = pdf::PDFEncoding::getTableForEncoding(static_cast<pdf::PDFEncoding::Encoding>(encoding));
    QVERIFY(encodingTable);
    std::map<quint64, int> glyphs;
    int characterCount = 0;
    for (int code = 0x21; code < 256; ++code)
    {
        const QChar character = (*encodingTable)[code];
        if (character.isNull() || character == QChar(QChar::SpecialCharacter::ReplacementCharacter))
        {
            continue;
        }

        ++characterCount;
        pdf::TextSequence sequence;
        realized->fillTextSequence(QByteArray(1, char(code)), sequence, &reporter);
        const QString description = QStringLiteral("Code 0x%1 (U+%2)").arg(code, 2, 16, QChar('0')).arg(int(character.unicode()), 4, 16, QChar('0'));
        QVERIFY2(sequence.items.size() == 1, qPrintable(description));
        QVERIFY2(sequence.items.front().character == character, qPrintable(description));
        QVERIFY2(sequence.items.front().glyph && !sequence.items.front().glyph->isEmpty(), qPrintable(description));

        const auto [it, inserted] = glyphs.emplace(sequence.items.front().glyph->getId(), code);
        QVERIFY2(inserted, qPrintable(QStringLiteral("%1 has the glyph of the code 0x%2").arg(description).arg(it->second, 2, 16, QChar('0'))));
    }
    QVERIFY2(characterCount >= minimalCharacterCount, qPrintable(QString::number(characterCount)));

    // Glyph names of the Differences are found in the clone, too
    const pdf::PDFRealizedFontPointer realizedWithDifferences = createRealizedFont(true);
    pdf::TextSequence expected;
    pdf::TextSequence sequence;
    QByteArray codes;
    for (qsizetype i = 0; i < differences.size(); ++i)
    {
        codes.append(char('A' + i));
    }
    realized->fillTextSequence(codesOfDifferences, expected, &reporter);
    realizedWithDifferences->fillTextSequence(codes, sequence, &reporter);
    QCOMPARE(sequence.items.size(), size_t(differences.size()));
    QCOMPARE(expected.items.size(), size_t(differences.size()));
    for (size_t i = 0; i < sequence.items.size(); ++i)
    {
        QCOMPARE(sequence.items[i].character, charactersOfDifferences.at(qsizetype(i)));
        QCOMPARE(expected.items[i].character, charactersOfDifferences.at(qsizetype(i)));
        QVERIFY(sequence.items[i].glyph && !sequence.items[i].glyph->isEmpty());
        QCOMPARE(sequence.items[i].glyph->getBoundingRect(), expected.items[i].glyph->getBoundingRect());
    }
    QVERIFY(expected.items[0].glyph->getBoundingRect() != expected.items[1].glyph->getBoundingRect());
}

void FontEncodingTest::test_type3_font_encode()
{
    QByteArray toUnicodeCMapData =
        "1 begincodespacerange\n"
        "<00> <FF>\n"
        "endcodespacerange\n"
        "2 beginbfchar\n"
        "<21> <20AC>\n"
        "<22> <0158>\n"
        "endbfchar\n";

    pdf::PDFFontCMap toUnicode = pdf::PDFFontCMap::createFromData(toUnicodeCMapData);
    QVERIFY(toUnicode.isValid());

    std::map<int, QByteArray> contentStreams;
    contentStreams[0x21] = "500 0 d0";
    contentStreams[0x22] = "500 0 d0";

    pdf::PDFType3Font font(pdf::FontDescriptor(), "F1", 0x21, 0x22, QTransform(0.001, 0, 0, 0.001, 0, 0),
                           std::move(contentStreams), { 500.0, 500.0 }, pdf::PDFObject(), std::move(toUnicode));

    QCOMPARE(font.encodeCharacter(0x20AC), QByteArray(1, char(0x21)));
    QCOMPARE(font.encodeCharacter(0x0158), QByteArray(1, char(0x22)));
    QVERIFY(font.encodeCharacter(U'A').isEmpty());

    pdf::PDFEncodedText encodedText = font.encodeText(QString(QChar(0x20AC)) + QChar(0x0158));
    QVERIFY(encodedText.isValid);
    QCOMPARE(encodedText.encodedText, QByteArray("\x21\x22"));
}

void FontEncodingTest::test_fallback_font_generator()
{
    pdf::PDFEditorFallbackFontManager manager;
    pdf::PDFDictionaryBuilder fontDictionary;

    QStringList errors;
    auto errorCallback = [&errors](const QString& error) { errors << error; };

    const std::u32string codePoints = U"€Ř"; // Euro sign, R with caron
    std::vector<pdf::PDFEditorFallbackFontManager::Run> runs = manager.encode(codePoints, nullptr, fontDictionary, errorCallback);

    QCOMPARE(runs.size(), size_t(1));
    QCOMPARE(runs.front().fontResourceKey, QByteArray("PDF4QT_Fb1"));
    QCOMPARE(runs.front().encodedBytes.size(), qsizetype(2));

    // The generated font dictionary must be accepted by the font parser
    QVERIFY(fontDictionary.hasKey("PDF4QT_Fb1"));
    pdf::PDFObject fontObject = fontDictionary.get("PDF4QT_Fb1");
    QVERIFY(fontObject.isDictionary());

    pdf::PDFDocumentBuilder documentBuilder;
    documentBuilder.createDocument();
    pdf::PDFDocument document = documentBuilder.build();

    pdf::PDFFontPointer font;
    try
    {
        font = pdf::PDFFont::createFont(fontObject, "PDF4QT_Fb1", &document);
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    QVERIFY(font);
    QCOMPARE(font->getFontType(), pdf::FontType::Type3);

    // Decoding the produced bytes must give back the original characters
    const pdf::PDFFontCMap* parsedToUnicode = font->getToUnicode();
    QVERIFY(parsedToUnicode && parsedToUnicode->isValid());

    const QByteArray& encodedBytes = runs.front().encodedBytes;
    QCOMPARE(parsedToUnicode->getToUnicode(static_cast<unsigned char>(encodedBytes[0])), QChar(0x20AC));
    QCOMPARE(parsedToUnicode->getToUnicode(static_cast<unsigned char>(encodedBytes[1])), QChar(0x0158));

    // Re-encoding through the parsed Type 3 font must reproduce the bytes
    pdf::PDFEncodedText reencoded = font->encodeText(QString(QChar(0x20AC)) + QChar(0x0158));
    QVERIFY(reencoded.isValid);
    QCOMPARE(reencoded.encodedText, encodedBytes);

    // Encoding the same characters again must reuse the assigned codes
    std::vector<pdf::PDFEditorFallbackFontManager::Run> runsAgain = manager.encode(codePoints, nullptr, fontDictionary, errorCallback);
    QCOMPARE(runsAgain.size(), size_t(1));
    QCOMPARE(runsAgain.front().encodedBytes, encodedBytes);
}

void FontEncodingTest::test_nested_stream_extraction()
{
    pdf::PDFEditorFallbackFontManager manager;
    pdf::PDFDictionaryBuilder fontDictionary;

    const std::u32string codePoints = U"€";
    std::vector<pdf::PDFEditorFallbackFontManager::Run> runs = manager.encode(codePoints, nullptr, fontDictionary, nullptr);
    QVERIFY(!runs.empty());

    pdf::PDFDocumentBuilder documentBuilder;
    documentBuilder.createDocument();

    // The generated Type 3 font contains nested streams (CharProcs, ToUnicode),
    // which must be extracted into standalone objects before serialization.
    documentBuilder.replaceObjectsByReferences(fontDictionary);

    for (size_t i = 0; i < fontDictionary.getCount(); ++i)
    {
        QVERIFY(fontDictionary.getValue(i).isReference());
    }

    pdf::PDFDocument document = documentBuilder.build();

    std::function<void(const pdf::PDFObject&, bool)> verifyNoNestedStreams = [&](const pdf::PDFObject& object, bool isTopLevel)
    {
        QVERIFY(!object.isStream() || isTopLevel);

        const pdf::PDFDictionary* dictionary = nullptr;
        if (object.isDictionary())
        {
            dictionary = object.getDictionary();
        }
        else if (object.isStream())
        {
            dictionary = object.getStream()->getDictionary();
        }

        if (dictionary)
        {
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                verifyNoNestedStreams(dictionary->getValue(i), false);
            }
        }

        if (object.isArray())
        {
            const pdf::PDFArray* array = object.getArray();
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                verifyNoNestedStreams(array->getItem(i), false);
            }
        }
    };

    const auto& objects = document.getStorage().getObjects();
    for (const auto& entry : objects)
    {
        verifyNoNestedStreams(entry.object, true);
    }
}

// ----------------------------------------------------------------------------
// A glyph name of /Differences (ISO 32000-2, 9.6.5.1, Table 112) replaces the base encoding for
// its code always, also a name, which is in no glyph list:
//  - Type 1 (9.6.5.2): the glyph of that name in the font program; a name, which does not exist
//    in the program, is the glyph .notdef;
//  - TrueType (9.6.5.4): the name is mapped through the Adobe Glyph List and the "cmap", a name,
//    which cannot be mapped so, is looked up in the "post" table;
//  - Type 3 (9.6.4, 9.6.5.3): the name is a key of /CharProcs, no glyph is painted without it;
//  - the character of the code (9.10.2) is the one of /ToUnicode, else of the name in the glyph
//    list; nothing says, what a code of another name is.
// ----------------------------------------------------------------------------

void FontEncodingTest::createTestFont(TestFont* testFont, QByteArray fontDictionary, const std::vector<TestStream>& streams, const QByteArray& shownCodes)
{
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();

    for (size_t i = 0; i < streams.size(); ++i)
    {
        const QByteArray dictionaryText = "<< /Length " + QByteArray::number(streams[i].data.size()) + " " + streams[i].dictionaryEntries + " >>";
        pdf::PDFParser dictionaryParser(dictionaryText, nullptr, pdf::PDFParser::None);
        const pdf::PDFObject dictionary = dictionaryParser.getObject();
        QVERIFY(dictionary.isDictionary());

        QByteArray data = streams[i].data;
        const pdf::PDFObjectReference reference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(pdf::PDFDictionaryBuilder(*dictionary.getDictionary()), std::move(data))));
        fontDictionary.replace("@" + QByteArray::number(qulonglong(i + 1)), QByteArray::number(reference.objectNumber) + " " + QByteArray::number(reference.generation) + " R");
    }
    testFont->document = builder.build();

    try
    {
        pdf::PDFParser parser(fontDictionary, nullptr, pdf::PDFParser::None);
        pdf::PDFRenderErrorReporterDummy reporter;
        testFont->font = pdf::PDFFont::createFont(parser.getObject(), "F1", &testFont->document);
        testFont->realizedFont = pdf::PDFRealizedFont::createRealizedFont(testFont->font, 20.0, &reporter);
        testFont->realizedFont->fillTextSequence(shownCodes, testFont->sequence, &reporter);
    }
    catch (const pdf::PDFException& exception)
    {
        testFont->realizedFont.reset();
        QFAIL(qPrintable(exception.getMessage()));
    }
}

QByteArray FontEncodingTest::encryptType1(const QByteArray& plain, quint16 key)
{
    QByteArray cipher;
    cipher.reserve(plain.size());

    quint16 r = key;
    for (const char character : plain)
    {
        const quint8 encrypted = quint8(quint8(character) ^ quint8(r >> 8));
        r = quint16((encrypted + r) * 52845u + 22719u);
        cipher.append(char(encrypted));
    }

    return cipher;
}

QByteArray FontEncodingTest::encodeType1Number(int value)
{
    QByteArray result;
    if (value >= -107 && value <= 107)
    {
        result.append(char(value + 139));
    }
    else if (value >= 108 && value <= 1131)
    {
        const int rest = value - 108;
        result.append(char(247 + (rest >> 8)));
        result.append(char(rest & 0xFF));
    }
    else
    {
        Q_ASSERT(value >= -1131 && value <= -108);
        const int rest = -value - 108;
        result.append(char(251 + (rest >> 8)));
        result.append(char(rest & 0xFF));
    }
    return result;
}

QByteArray FontEncodingTest::createType1Glyph(int width, const QRect& rectangle)
{
    // The operators of Adobe Type 1 Font Format, 6.4
    constexpr char HSBW = 13;
    constexpr char RMOVETO = 21;
    constexpr char HLINETO = 6;
    constexpr char VLINETO = 7;
    constexpr char CLOSEPATH = 9;
    constexpr char ENDCHAR = 14;

    // Four leading bytes of the encryption (lenIV)
    QByteArray plain(4, '\0');
    plain.append(encodeType1Number(0));
    plain.append(encodeType1Number(width));
    plain.append(HSBW);

    if (!rectangle.isEmpty())
    {
        plain.append(encodeType1Number(rectangle.x()));
        plain.append(encodeType1Number(rectangle.y()));
        plain.append(RMOVETO);
        plain.append(encodeType1Number(rectangle.width()));
        plain.append(HLINETO);
        plain.append(encodeType1Number(rectangle.height()));
        plain.append(VLINETO);
        plain.append(encodeType1Number(-rectangle.width()));
        plain.append(HLINETO);
        plain.append(CLOSEPATH);
    }
    plain.append(ENDCHAR);

    return encryptType1(plain, 4330);
}

QByteArray FontEncodingTest::createType1FontProgram(qsizetype* clearTextLength)
{
    return createType1FontProgram(clearTextLength, "StandardEncoding def\n");
}

QByteArray FontEncodingTest::createType1FontProgram(qsizetype* clearTextLength, const QByteArray& encoding)
{
    const QByteArray clearText =
        "%!PS-AdobeFont-1.0: PDF4QTTestFont 001.000\n"
        "11 dict begin\n"
        "/FontInfo 3 dict dup begin\n"
        "/version (001.000) readonly def\n"
        "/FullName (PDF4QTTestFont) readonly def\n"
        "/FamilyName (PDF4QTTestFont) readonly def\n"
        "end readonly def\n"
        "/FontName /PDF4QTTestFont def\n"
        "/PaintType 0 def\n"
        "/FontType 1 def\n"
        "/FontMatrix [0.001 0 0 0.001 0 0] readonly def\n"
        "/Encoding " + encoding +
        "/FontBBox {0 0 1000 1000} readonly def\n"
        "currentdict end\n"
        "currentfile eexec\n";

    // The glyphs: rectangles of the left bottom corner (x, y) and of a size, each of another shape
    const std::vector<std::pair<QByteArray, QByteArray>> glyphs =
    {
        { ".notdef", createType1Glyph(500, QRect()) },
        { "B", createType1Glyph(500, QRect(100, 0, 300, 700)) },
        { "C", createType1Glyph(600, QRect(100, 0, 400, 300)) },
        { "four.sups", createType1Glyph(400, QRect(100, 400, 200, 300)) },
        { "uni20AC", createType1Glyph(700, QRect(100, 0, 500, 500)) },
        { "one", createType1Glyph(500, QRect(100, 0, 150, 650)) },
        { "one.oldstyle", createType1Glyph(500, QRect(100, 0, 250, 450)) },
        { "f_i", createType1Glyph(500, QRect(100, 0, 350, 750)) },
        { "uni00410042", createType1Glyph(500, QRect(100, 0, 450, 350)) },
        { "uni0041.smcp", createType1Glyph(500, QRect(100, 0, 550, 250)) }
    };

    // Four leading bytes of the encryption, then the private dictionary with the charstrings
    QByteArray privateText(4, '\0');
    privateText.append("dup /Private 8 dict dup begin\n"
                       "/RD {string currentfile exch readstring pop} executeonly def\n"
                       "/ND {noaccess def} executeonly def\n"
                       "/NP {noaccess put} executeonly def\n"
                       "/BlueValues [] def\n"
                       "/MinFeature {16 16} def\n"
                       "/password 5839 def\n"
                       "2 index /CharStrings ");
    privateText.append(QByteArray::number(qulonglong(glyphs.size())));
    privateText.append(" dict dup begin\n");
    for (const auto& [name, charstring] : glyphs)
    {
        privateText.append("/");
        privateText.append(name);
        privateText.append(" ");
        privateText.append(QByteArray::number(charstring.size()));
        privateText.append(" RD ");
        privateText.append(charstring);
        privateText.append(" ND\n");
    }
    privateText.append("end\n"
                       "end\n"
                       "readonly put\n"
                       "noaccess put\n"
                       "dup /FontName get exch definefont pop\n"
                       "mark currentfile closefile\n");

    *clearTextLength = clearText.size();
    return clearText + encryptType1(privateText, 55665);
}

void FontEncodingTest::test_differences_name_character_data()
{
    // The character of the lookup of the whole name (getUnicodeUsingResolvedName) and the
    // character of the code in a font, which is not embedded. They differ, where the lookup
    // gives nothing and the text of the name (PDFNameToUnicode::getUnicodeTextForName) is one
    // character: such a font takes it as the key of the glyph (owner 2026-11-26).
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<QChar>("character");
    QTest::addColumn<QChar>("fontCharacter");

    // Names of the Adobe Glyph List and of the font ZapfDingbats (ISO 32000-2, 9.10.2, D.6)
    QTest::newRow("A") << QByteArray("A") << QChar(u'A') << QChar(u'A');
    QTest::newRow("Euro") << QByteArray("Euro") << QChar(0x20AC) << QChar(0x20AC);
    QTest::newRow("foursuperior") << QByteArray("foursuperior") << QChar(0x2074) << QChar(0x2074);
    QTest::newRow("a20") << QByteArray("a20") << QChar(0x2714) << QChar(0x2714);
    QTest::newRow("u") << QByteArray("u") << QChar(u'u') << QChar(u'u');
    QTest::newRow("uacute") << QByteArray("uacute") << QChar(0x00FA) << QChar(0x00FA);

    // The names of a code point of the glyph naming convention of the glyph list: "uni" with
    // four and "u" with four to six upper case hexadecimal digits
    QTest::newRow("uni20AC") << QByteArray("uni20AC") << QChar(0x20AC) << QChar(0x20AC);
    QTest::newRow("u20AC") << QByteArray("u20AC") << QChar(0x20AC) << QChar(0x20AC);
    QTest::newRow("u0041") << QByteArray("u0041") << QChar(u'A') << QChar(u'A');
    QTest::newRow("u020AC") << QByteArray("u020AC") << QChar(0x20AC) << QChar(0x20AC);
    QTest::newRow("u0020AC") << QByteArray("u0020AC") << QChar(0x20AC) << QChar(0x20AC);
    QTest::newRow("uFFFD") << QByteArray("uFFFD") << QChar(0xFFFD) << QChar(0xFFFD);

    // A code point outside of the basic multilingual plane has no character of one code unit,
    // a surrogate is no code point of a name
    QTest::newRow("u1F600") << QByteArray("u1F600") << QChar() << QChar();
    QTest::newRow("u01F600") << QByteArray("u01F600") << QChar() << QChar();
    QTest::newRow("uD83D") << QByteArray("uD83D") << QChar() << QChar();

    // No name of a code point
    QTest::newRow("u20A") << QByteArray("u20A") << QChar() << QChar();
    QTest::newRow("u00020AC") << QByteArray("u00020AC") << QChar() << QChar();
    QTest::newRow("u20AG") << QByteArray("u20AG") << QChar() << QChar();

    // Lower case digits: no name of a code point for the lookup; the text of the name accepts
    // them (a deliberate deviation from the Adobe Glyph List Specification), so the font,
    // which is not embedded, has the character
    QTest::newRow("u20ac") << QByteArray("u20ac") << QChar() << QChar(0x20AC);

    // Names of no glyph list: four.sups has the text 4 (the suffix is dropped), the font, which
    // is not embedded, takes it as its character
    QTest::newRow("four.sups") << QByteArray("four.sups") << QChar() << QChar(u'4');
    QTest::newRow("nonexistent.name") << QByteArray("nonexistent.name") << QChar() << QChar();
    QTest::newRow(".notdef") << QByteArray(".notdef") << QChar() << QChar();
    QTest::newRow("empty") << QByteArray() << QChar() << QChar();
}

void FontEncodingTest::test_differences_name_character()
{
    QFETCH(QByteArray, name);
    QFETCH(QChar, character);
    QFETCH(QChar, fontCharacter);

    // The character of a font created from a dictionary: a font, which is not embedded, with the
    // name at the code 65 (the base encoding has A there)
    TestFont testFont;
    createTestFont(&testFont, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /FirstChar 65 /LastChar 65 /Widths [600] "
                              "/Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [65 /" + (name.isEmpty() ? QByteArray("A") : name) + "] >> >>",
                   { }, "A");
    QVERIFY(testFont.realizedFont);

    const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(testFont.font.get());
    QVERIFY(simpleFont);
    if (!name.isEmpty())
    {
        QCOMPARE(simpleFont->getUnicode(65), fontCharacter);
        QCOMPARE(testFont.sequence.items.size(), size_t(1));

        // A code without a character has no glyph of a font, which is not embedded; an item with
        // a glyph has the character (the substitute need not have a glyph of every character)
        if (fontCharacter.isNull())
        {
            QVERIFY(!testFont.sequence.items[0].glyph);
            QVERIFY(testFont.sequence.items[0].character.isNull());
        }
        else if (testFont.sequence.items[0].glyph)
        {
            QCOMPARE(testFont.sequence.items[0].character, fontCharacter);
        }
    }

    QCOMPARE(pdf::PDFNameToUnicode::getUnicodeUsingResolvedName(name), character);
}

void FontEncodingTest::test_differences_font_not_embedded_data()
{
    QTest::addColumn<QByteArray>("fontEntries");

    // Standard fonts: their substitute is a font of the resources of the library
    QTest::newRow("Type 1") << QByteArray("/Subtype /Type1 /BaseFont /Helvetica");
    QTest::newRow("TrueType") << QByteArray("/Subtype /TrueType /BaseFont /ArialMT");
}

void FontEncodingTest::test_differences_font_not_embedded()
{
    QFETCH(QByteArray, fontEntries);

    // The codes A ... F by /Differences: a name of the glyph list, two names of no glyph list, two
    // names of a code point and .notdef. The base encoding has the letters A ... F there.
    TestFont testFont;
    createTestFont(&testFont, "<< /Type /Font " + fontEntries + " /FirstChar 65 /LastChar 70 /Widths [600 610 620 630 640 650] "
                              "/Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding "
                              "/Differences [65 /B /four.sups /nonexistent.name /uni20AC /u20AC /.notdef] >> >>",
                   { }, "ABCDEF");
    QVERIFY(testFont.realizedFont);

    // The glyphs of the letters A, B, of the Euro sign (the code 128 of WinAnsiEncoding, D.2)
    // and of the digit 4
    TestFont reference;
    createTestFont(&reference, "<< /Type /Font " + fontEntries + " /FirstChar 65 /LastChar 128 /Encoding /WinAnsiEncoding >>", { }, "AB\x80" "4");
    QVERIFY(reference.realizedFont);
    QCOMPARE(reference.sequence.items.size(), size_t(4));
    for (const pdf::TextSequenceItem& item : reference.sequence.items)
    {
        QVERIFY(item.glyph && !item.glyph->isEmpty());
    }
    const QRectF glyphA = reference.sequence.items[0].glyph->getBoundingRect();
    const QRectF glyphB = reference.sequence.items[1].glyph->getBoundingRect();
    const QRectF glyphEuro = reference.sequence.items[2].glyph->getBoundingRect();
    const QRectF glyphFour = reference.sequence.items[3].glyph->getBoundingRect();
    QVERIFY(glyphA != glyphB);
    QVERIFY(glyphB != glyphEuro);
    QVERIFY(glyphFour != glyphB);
    QVERIFY(glyphFour != glyphEuro);
    QCOMPARE(reference.sequence.items[2].character, QChar(0x20AC));
    QCOMPARE(reference.sequence.items[3].character, QChar(u'4'));

    const std::vector<pdf::TextSequenceItem>& items = testFont.sequence.items;
    QCOMPARE(items.size(), size_t(6));
    for (size_t i = 0; i < items.size(); ++i)
    {
        QCOMPARE(items[i].code, static_cast<unsigned int>(65 + i));
        QCOMPARE(items[i].codeLength, 1u);
    }

    // A name of the glyph list: its character, the glyph of the character in the substitute
    QCOMPARE(items[0].character, QChar(u'B'));
    QVERIFY(items[0].glyph);
    QCOMPARE(items[0].glyph->getBoundingRect(), glyphB);

    // four.sups: the font is not embedded, its substitute is asked by characters and has no
    // glyph of that name for the code. The text of the name is 4 (the suffix is dropped), one
    // character: it is the character of the code and the key of its glyph - the digit 4 of the
    // substitute, not the letter B of the base encoding (owner 2026-11-26; an embedded font
    // stays strict - test_differences_truetype_embedded, test_differences_type1_embedded)
    QCOMPARE(items[1].character, QChar(u'4'));
    QCOMPARE(items[1].textCharacter, QChar(u'4'));
    QVERIFY(items[1].glyph);
    QCOMPARE(items[1].glyph->getBoundingRect(), glyphFour);

    // A name of no glyph list without a text and .notdef: the font has no such glyph (the glyph
    // .notdef, 9.6.5.2), so nothing of the base encoding is painted and the code has no
    // character (9.10.2); the advance is the width of the dictionary (620 and 650 thousandths
    // of the size 20)
    const std::vector<std::pair<size_t, double>> codesWithoutGlyph = { { 2, 12.4 }, { 5, 13.0 } };
    for (const auto& [index, advance] : codesWithoutGlyph)
    {
        QVERIFY2(!items[index].glyph, qPrintable(QString::number(index)));
        QVERIFY2(items[index].character.isNull(), qPrintable(QString("%1: U+%2").arg(index).arg(uint(items[index].character.unicode()), 4, 16, QChar('0'))));
        QCOMPARE(items[index].advance, advance);
    }

    // Names of a code point: the character and its glyph
    for (const size_t index : { size_t(3), size_t(4) })
    {
        QCOMPARE(items[index].character, QChar(0x20AC));
        QVERIFY(items[index].glyph);
        QCOMPARE(items[index].glyph->getBoundingRect(), glyphEuro);
    }
}

void FontEncodingTest::test_differences_truetype_embedded()
{
    // The embedded font program is LiberationSans-Regular of the resources of the library. It
    // has a (3, 1) "cmap" and a "post" table of the format 2 with the glyph names; its glyph
    // four.sups is the glyph of U+2074 in the "cmap", the name is in no glyph list.
    QFile file(QStringLiteral(":/fonts/liberation-fonts-ttf/LiberationSans-Regular.ttf"));
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray fontProgram = file.readAll();
    QVERIFY(!fontProgram.isEmpty());

    // The codes A ... G of a nonsymbolic font (9.6.5.4): a name of the glyph list, a name of the
    // "post" table only, a name of nothing, two names of a code point, .notdef and B
    TestFont testFont;
    createTestFont(&testFont, "<< /Type /Font /Subtype /TrueType /BaseFont /LiberationSans /FirstChar 65 /LastChar 71 /Widths [600 610 620 630 640 650 660] "
                              "/FontDescriptor << /Type /FontDescriptor /FontName /LiberationSans /Flags 32 /FontBBox [-544 -303 1302 980] "
                              "/ItalicAngle 0 /Ascent 905 /Descent -212 /CapHeight 729 /StemV 80 /FontFile2 @1 >> "
                              "/Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding "
                              "/Differences [65 /foursuperior /four.sups /nonexistent.name /uni20AC /u20AC /.notdef /B] >> >>",
                   { TestStream{ fontProgram, "/Length1 " + QByteArray::number(fontProgram.size()) } }, "ABCDEFG");
    QVERIFY(testFont.realizedFont);

    const std::vector<pdf::TextSequenceItem>& items = testFont.sequence.items;
    QCOMPARE(items.size(), size_t(7));
    for (size_t i = 0; i < items.size(); ++i)
    {
        QCOMPARE(items[i].code, static_cast<unsigned int>(65 + i));
        QCOMPARE(items[i].codeLength, 1u);
    }

    // foursuperior: the glyph list gives U+2074, the "cmap" its glyph
    QCOMPARE(items[0].character, QChar(0x2074));
    QVERIFY(items[0].glyph && !items[0].glyph->isEmpty());

    // B: the letter, another glyph
    QCOMPARE(items[6].character, QChar(u'B'));
    QVERIFY(items[6].glyph && !items[6].glyph->isEmpty());
    QVERIFY(items[6].glyph->getBoundingRect() != items[0].glyph->getBoundingRect());

    // four.sups: no glyph list has the name, the "post" table gives the glyph - the same one as
    // of foursuperior, not the letter B of the base encoding. The name gives no character.
    QVERIFY(items[1].glyph);
    QCOMPARE(items[1].glyph->getBoundingRect(), items[0].glyph->getBoundingRect());
    QCOMPARE(items[1].glyph, items[0].glyph);
    QVERIFY2(items[1].character.isNull(), qPrintable(QString::number(items[1].character.unicode(), 16)));

    // A name of nothing and .notdef: no glyph, no character, the width of the dictionary
    // (620 and 650 thousandths of the size 20)
    QVERIFY(!items[2].glyph);
    QVERIFY2(items[2].character.isNull(), qPrintable(QString::number(items[2].character.unicode(), 16)));
    QCOMPARE(items[2].advance, 12.4);
    QVERIFY(!items[5].glyph);
    QVERIFY2(items[5].character.isNull(), qPrintable(QString::number(items[5].character.unicode(), 16)));
    QCOMPARE(items[5].advance, 13.0);

    // Names of a code point: the character and its glyph of the "cmap"
    QCOMPARE(items[3].character, QChar(0x20AC));
    QVERIFY(items[3].glyph && !items[3].glyph->isEmpty());
    QVERIFY(items[3].glyph->getBoundingRect() != items[0].glyph->getBoundingRect());
    QVERIFY(items[3].glyph->getBoundingRect() != items[6].glyph->getBoundingRect());
    QCOMPARE(items[4].character, QChar(0x20AC));
    QCOMPARE(items[4].glyph, items[3].glyph);
}

void FontEncodingTest::test_differences_type1_embedded()
{
    // A hand-written Type 1 font program: the glyphs .notdef, B, C, four.sups and uni20AC are
    // rectangles of different shapes, the built-in encoding is StandardEncoding
    qsizetype clearTextLength = 0;
    const QByteArray fontProgram = createType1FontProgram(&clearTextLength);

    // The codes A ... F: /Differences names B at the code 65, leaves the code 66 to the built-in
    // encoding (B) and names a glyph of nothing at 67 (the built-in encoding has C there), a
    // glyph of no glyph list at 68, a glyph named by a code point at 69 and .notdef at 70
    TestFont testFont;
    createTestFont(&testFont, "<< /Type /Font /Subtype /Type1 /BaseFont /PDF4QTTestFont /FirstChar 65 /LastChar 70 /Widths [500 510 520 530 540 550] "
                              "/FontDescriptor << /Type /FontDescriptor /FontName /PDF4QTTestFont /Flags 32 /FontBBox [0 0 1000 1000] "
                              "/ItalicAngle 0 /Ascent 700 /Descent 0 /CapHeight 700 /StemV 80 /FontFile @1 >> "
                              "/Encoding << /Type /Encoding /Differences [65 /B 67 /nonexistent.name /four.sups /uni20AC /.notdef] >> >>",
                   { TestStream{ fontProgram, "/Length1 " + QByteArray::number(clearTextLength) + " /Length2 " + QByteArray::number(fontProgram.size() - clearTextLength) + " /Length3 0" } },
                   "ABCDEF");
    QVERIFY(testFont.realizedFont);

    const std::vector<pdf::TextSequenceItem>& items = testFont.sequence.items;
    QCOMPARE(items.size(), size_t(6));
    for (size_t i = 0; i < items.size(); ++i)
    {
        QCOMPARE(items[i].code, static_cast<unsigned int>(65 + i));
        QCOMPARE(items[i].codeLength, 1u);
    }

    // The glyph of an item is the rectangle of the size in the units of the glyph space; the glyph
    // B (300 x 700) of the first item is the measure
    QVERIFY(items[0].glyph && !items[0].glyph->isEmpty());
    const QRectF glyphB = items[0].glyph->getBoundingRect();
    QVERIFY(glyphB.width() > 0.0 && glyphB.height() > 0.0);
    QVERIFY(qAbs(glyphB.width() / glyphB.height() - 300.0 / 700.0) < 0.02);
    auto isRectangle = [&glyphB](const pdf::TextSequenceItem& item, double width, double height)
    {
        if (!item.glyph || item.glyph->isEmpty())
        {
            return false;
        }

        const QRectF rectangle = item.glyph->getBoundingRect();
        return qAbs(rectangle.width() / glyphB.width() - width / 300.0) < 0.03 && qAbs(rectangle.height() / glyphB.height() - height / 700.0) < 0.03;
    };

    // B by /Differences and B of the built-in encoding
    QCOMPARE(items[0].character, QChar(u'B'));
    QCOMPARE(items[1].character, QChar(u'B'));
    QVERIFY(isRectangle(items[1], 300.0, 700.0));

    // A name, which does not exist in the font program: the glyph .notdef (9.6.5.2) - not the
    // glyph C of the built-in encoding, nothing is painted; no character; the width of the
    // dictionary (520 thousandths of the size 20)
    QVERIFY2(!items[2].glyph, isRectangle(items[2], 400.0, 300.0) ? "the glyph C of the built-in encoding" : "another glyph");
    QVERIFY2(items[2].character.isNull(), qPrintable(QString::number(items[2].character.unicode(), 16)));
    QCOMPARE(items[2].advance, 10.4);

    // A name of no glyph list, which exists in the font program: its glyph (200 x 300), no character
    QVERIFY(isRectangle(items[3], 200.0, 300.0));
    QVERIFY2(items[3].character.isNull(), qPrintable(QString::number(items[3].character.unicode(), 16)));

    // A glyph named by a code point (500 x 500): the glyph and the character
    QVERIFY(isRectangle(items[4], 500.0, 500.0));
    QCOMPARE(items[4].character, QChar(0x20AC));

    // .notdef: nothing is painted, no character, the width of the dictionary (550 thousandths)
    QVERIFY(!items[5].glyph || items[5].glyph->isEmpty());
    QVERIFY2(items[5].character.isNull(), qPrintable(QString::number(items[5].character.unicode(), 16)));
    if (!items[5].glyph)
    {
        QCOMPARE(items[5].advance, 11.0);
    }
}

void FontEncodingTest::test_differences_tounicode_character()
{
    // The character of /ToUnicode is the first method (9.10.2): a code of a name of no glyph
    // list has it, and a name of the glyph list does not win over it
    TestFont testFont;
    createTestFont(&testFont, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /FirstChar 65 /LastChar 67 /Widths [600 610 620] "
                              "/Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [65 /nonexistent.name /B /four.sups] >> "
                              "/ToUnicode @1 >>",
                   { TestStream{ "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n2 beginbfchar\n<41> <0058>\n<42> <0059>\nendbfchar\n", QByteArray() } },
                   "ABC");
    QVERIFY(testFont.realizedFont);

    const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(testFont.font.get());
    QVERIFY(simpleFont);
    QCOMPARE(simpleFont->getUnicode(65), QChar(u'X'));
    QCOMPARE(simpleFont->getUnicode(66), QChar(u'Y'));

    // No /ToUnicode of the code and a name of no glyph list: the lookup of the whole name gives
    // no character; the font is not embedded, so the character of the code is the text of the
    // name, the digit 4 (owner 2026-11-26)
    QVERIFY(pdf::PDFNameToUnicode::getUnicodeUsingResolvedName("four.sups").isNull());
    QCOMPARE(simpleFont->getUnicode(67), QChar(u'4'));
}

void FontEncodingTest::test_differences_type3()
{
    // The names of a Type 3 font are keys of /CharProcs (9.6.4, 9.6.5.3): any name is a glyph,
    // a name, which is not a key, paints nothing. The font has no base encoding.
    TestFont testFont;
    createTestFont(&testFont, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] "
                              "/CharProcs << /A @1 /my.glyph @2 >> "
                              "/Encoding << /Type /Encoding /Differences [65 /A /my.glyph /nonexistent.name] >> "
                              "/FirstChar 65 /LastChar 67 /Widths [500 600 700] /Resources << >> /ToUnicode @3 >>",
                   { TestStream{ "500 0 0 0 100 100 d1 0 0 100 100 re f", QByteArray() },
                     TestStream{ "600 0 0 0 200 200 d1 0 0 200 200 re f", QByteArray() },
                     TestStream{ "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n1 beginbfchar\n<41> <0058>\nendbfchar\n", QByteArray() } },
                   "ABC");
    QVERIFY(testFont.realizedFont);
    QCOMPARE(testFont.font->getFontType(), pdf::FontType::Type3);

    const std::vector<pdf::TextSequenceItem>& items = testFont.sequence.items;
    QCOMPARE(items.size(), size_t(3));
    for (size_t i = 0; i < items.size(); ++i)
    {
        QCOMPARE(items[i].code, static_cast<unsigned int>(65 + i));
        QCOMPARE(items[i].codeLength, 1u);
    }

    // A name of the glyph list: its glyph description; the character of /ToUnicode (9.10.2)
    QVERIFY(items[0].isContentStream());
    QCOMPARE(*items[0].characterContentStream, QByteArray("500 0 0 0 100 100 d1 0 0 100 100 re f"));
    QCOMPARE(items[0].character, QChar(u'X'));
    QCOMPARE(items[0].advance, 500.0);

    // A name of no glyph list: its glyph description, no character
    QVERIFY(items[1].isContentStream());
    QCOMPARE(*items[1].characterContentStream, QByteArray("600 0 0 0 200 200 d1 0 0 200 200 re f"));
    QVERIFY(items[1].character.isNull());
    QCOMPARE(items[1].advance, 600.0);

    // A name, which is not a key of /CharProcs: no glyph is painted, the code is advanced by
    // its width of the dictionary
    QVERIFY(!items[2].isContentStream());
    QVERIFY(!items[2].glyph);
    QVERIFY(items[2].character.isNull());
    QCOMPARE(items[2].advance, 700.0);
}

// ----------------------------------------------------------------------------
// The text of a glyph name (ISO 32000-2, 9.10.2) by the Adobe Glyph List Specification
// (https://github.com/adobe-type-tools/agl-specification), section 2 "The mapping":
//  1. everything from the first period of the name on is dropped;
//  2. the rest is split to components at the underscores;
//  3. each component is mapped and the strings are joined: a name of the ZapfDingbats list, if
//     the font is ZapfDingbats; else a name of the glyph list; else "uni" and groups of four
//     upper case hexadecimal digits, each of 0000 - D7FF or E000 - FFFF; else "u" and four to
//     six upper case hexadecimal digits of 0000 - D7FF or E000 - 10FFFF; else nothing.
// The result is a string of UTF-16 code units. The text and the glyph of a code are two
// things: the glyph of one.oldstyle is the glyph of that name, never the glyph of the
// character 1, which is its text. The text layout has one character per glyph, a text of more
// code units is given by PDFFont::getEncodingText only.
// ----------------------------------------------------------------------------

QString FontEncodingTest::codePointText(char32_t codePoint)
{
    return QString::fromUcs4(&codePoint, 1);
}

QByteArray FontEncodingTest::type1TestFontDictionary(const QByteArray& differences, int codeCount)
{
    QByteArray widths;
    for (int i = 0; i < codeCount; ++i)
    {
        widths += "500 ";
    }

    QByteArray dictionary = "<< /Type /Font /Subtype /Type1 /BaseFont /PDF4QTTestFont /FirstChar 65 /LastChar " + QByteArray::number(64 + codeCount) + " /Widths [" + widths + "] "
                            "/FontDescriptor << /Type /FontDescriptor /FontName /PDF4QTTestFont /Flags 32 /FontBBox [0 0 1000 1000] "
                            "/ItalicAngle 0 /Ascent 700 /Descent 0 /CapHeight 700 /StemV 80 /FontFile @1 >> ";
    if (!differences.isEmpty())
    {
        dictionary += "/Encoding << /Type /Encoding /Differences [65 " + differences + "] >> ";
    }
    dictionary += ">>";
    return dictionary;
}

FontEncodingTest::TestStream FontEncodingTest::type1TestFontStream(const QByteArray& fontProgram, qsizetype clearTextLength)
{
    return TestStream{ fontProgram, "/Length1 " + QByteArray::number(clearTextLength) + " /Length2 " + QByteArray::number(fontProgram.size() - clearTextLength) + " /Length3 0" };
}

void FontEncodingTest::test_glyph_name_text_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<bool>("isZapfDingbats");
    QTest::addColumn<QString>("text");

    const QString none;

    // U+1040C is D801 DC0C in UTF-16 (the specification says so); U+1F600 - 10000 = F600:
    // D800 + (F600 >> 10) = D83D, DC00 + (F600 & 3FF) = DE00; U+10FFFF is DBFF DFFF, U+10000
    // is D800 DC00
    const QString u1040C = QString(QChar(0xD801)) + QChar(0xDC0C);
    const QString u1F600 = QString(QChar(0xD83D)) + QChar(0xDE00);
    const QString u10FFFF = QString(QChar(0xDBFF)) + QChar(0xDFFF);
    const QString u10000 = QString(QChar(0xD800)) + QChar(0xDC00);

    // The examples of the specification (section 3)
    QTest::newRow("example Lcommaaccent") << QByteArray("Lcommaaccent") << false << QString(QChar(0x013B));
    QTest::newRow("example uni20AC0308") << QByteArray("uni20AC0308") << false << (QString(QChar(0x20AC)) + QChar(0x0308));
    QTest::newRow("example u1040C") << QByteArray("u1040C") << false << u1040C;
    QTest::newRow("example uniD801DC0C") << QByteArray("uniD801DC0C") << false << none;

    // The specification maps uni20ac to an empty string (lower case digits). The library accepts
    // lower case digits: a deliberate deviation (NOTES.txt 9.10.2, owner 2026-11-26)
    QTest::newRow("example uni20ac") << QByteArray("uni20ac") << false << QString(QChar(0x20AC));
    QTest::newRow("example three components") << QByteArray("Lcommaaccent_uni20AC0308_u1040C.alternate") << false << (QString(QChar(0x013B)) + QChar(0x20AC) + QChar(0x0308) + u1040C);
    QTest::newRow("example uni013B") << QByteArray("uni013B") << false << QString(QChar(0x013B));
    QTest::newRow("example u013B") << QByteArray("u013B") << false << QString(QChar(0x013B));
    QTest::newRow("example foo") << QByteArray("foo") << false << none;
    QTest::newRow("example .notdef") << QByteArray(".notdef") << false << none;

    // Step 1: a suffix is dropped, from the first period on
    QTest::newRow("suffix one.oldstyle") << QByteArray("one.oldstyle") << false << QStringLiteral("1");
    QTest::newRow("suffix a.sc") << QByteArray("a.sc") << false << QStringLiteral("a");
    QTest::newRow("suffix f.alt") << QByteArray("f.alt") << false << QStringLiteral("f");
    QTest::newRow("suffix uni0041.smcp") << QByteArray("uni0041.smcp") << false << QStringLiteral("A");
    QTest::newRow("suffix u0041.smcp") << QByteArray("u0041.smcp") << false << QStringLiteral("A");
    QTest::newRow("suffix two periods") << QByteArray("A.B.one") << false << QStringLiteral("A");
    QTest::newRow("suffix empty") << QByteArray("one.") << false << QStringLiteral("1");
    QTest::newRow("suffix with an underscore") << QByteArray("A_B.alt_one") << false << QStringLiteral("AB");
    QTest::newRow("suffix only") << QByteArray(".alt") << false << none;
    QTest::newRow("period only") << QByteArray(".") << false << none;

    // Step 2: components. The glyph list of the library has the whole names f_i, f_f, f_f_i and
    // f_f_l (ligatures): a component has no underscore, so they are never looked up as a whole.
    QTest::newRow("components f_i") << QByteArray("f_i") << false << QStringLiteral("fi");
    QTest::newRow("components f_f_l") << QByteArray("f_f_l") << false << QStringLiteral("ffl");
    QTest::newRow("components f_f_i") << QByteArray("f_f_i") << false << QStringLiteral("ffi");
    QTest::newRow("components T_h") << QByteArray("T_h") << false << QStringLiteral("Th");
    QTest::newRow("components of code points") << QByteArray("uni0041_u0042") << false << QStringLiteral("AB");
    QTest::newRow("components, unknown among known") << QByteArray("f_foo_i") << false << QStringLiteral("fi");
    QTest::newRow("components, unknown first") << QByteArray("foo_A") << false << QStringLiteral("A");
    QTest::newRow("components, wrong code point among known") << QByteArray("A_uniD800_B") << false << QStringLiteral("AB");
    QTest::newRow("components, empty in the middle") << QByteArray("A__B") << false << QStringLiteral("AB");
    QTest::newRow("components, empty first") << QByteArray("_A") << false << QStringLiteral("A");
    QTest::newRow("components, empty last") << QByteArray("A_") << false << QStringLiteral("A");
    QTest::newRow("underscores only") << QByteArray("___") << false << none;
    QTest::newRow("underscores and a period") << QByteArray("_._") << false << none;
    QTest::newRow("empty name") << QByteArray() << false << none;

    // "uni": groups of four upper case hexadecimal digits
    QTest::newRow("uni one group") << QByteArray("uni0041") << false << QStringLiteral("A");
    QTest::newRow("uni two groups") << QByteArray("uni00410042") << false << QStringLiteral("AB");
    QTest::newRow("uni three groups") << QByteArray("uni004100420043") << false << QStringLiteral("ABC");
    QTest::newRow("uniD7FF") << QByteArray("uniD7FF") << false << QString(QChar(0xD7FF));
    QTest::newRow("uniE000") << QByteArray("uniE000") << false << QString(QChar(0xE000));
    QTest::newRow("uniFFFF") << QByteArray("uniFFFF") << false << QString(QChar(0xFFFF));
    QTest::newRow("uni0000") << QByteArray("uni0000") << false << QString(QChar(0x0000));
    QTest::newRow("uni surrogate D800") << QByteArray("uniD800") << false << none;
    QTest::newRow("uni surrogate DFFF") << QByteArray("uniDFFF") << false << none;
    QTest::newRow("uni surrogate after a group") << QByteArray("uni0041D800") << false << none;
    QTest::newRow("uni surrogate before a group") << QByteArray("uniDC000041") << false << none;
    QTest::newRow("uni no digit") << QByteArray("uni") << false << none;
    QTest::newRow("uni three digits") << QByteArray("uni004") << false << none;
    QTest::newRow("uni five digits") << QByteArray("uni00410") << false << none;
    QTest::newRow("uni six digits") << QByteArray("uni004100") << false << none;
    QTest::newRow("uni seven digits") << QByteArray("uni0041004") << false << none;
    QTest::newRow("uni lower case digits") << QByteArray("uni00e9") << false << QString(QChar(0x00E9));
    QTest::newRow("uni lower case digit in the second group") << QByteArray("uni0041004a") << false << QStringLiteral("AJ");
    QTest::newRow("uni lower case surrogate") << QByteArray("unid800") << false << none;
    QTest::newRow("uni lower case digits, three") << QByteArray("uni0e9") << false << none;
    QTest::newRow("uni wrong digit") << QByteArray("uni00G1") << false << none;
    QTest::newRow("Uni upper case prefix") << QByteArray("Uni0041") << false << none;

    // "u": four to six upper case hexadecimal digits
    QTest::newRow("u four digits") << QByteArray("u0041") << false << QStringLiteral("A");
    QTest::newRow("u five digits") << QByteArray("u1F600") << false << u1F600;
    QTest::newRow("u six digits") << QByteArray("u01F600") << false << u1F600;
    QTest::newRow("u six digits of a character of one code unit") << QByteArray("u000041") << false << QStringLiteral("A");
    QTest::newRow("u0000") << QByteArray("u0000") << false << QString(QChar(0x0000));
    QTest::newRow("uD7FF") << QByteArray("uD7FF") << false << QString(QChar(0xD7FF));
    QTest::newRow("uE000") << QByteArray("uE000") << false << QString(QChar(0xE000));
    QTest::newRow("uFFFF") << QByteArray("uFFFF") << false << QString(QChar(0xFFFF));
    QTest::newRow("u10000") << QByteArray("u10000") << false << u10000;
    QTest::newRow("u10FFFF") << QByteArray("u10FFFF") << false << u10FFFF;
    QTest::newRow("u surrogate D800") << QByteArray("uD800") << false << none;
    QTest::newRow("u surrogate DFFF") << QByteArray("u00DFFF") << false << none;
    QTest::newRow("u above 10FFFF") << QByteArray("u110000") << false << none;
    QTest::newRow("u FFFFFF") << QByteArray("uFFFFFF") << false << none;
    QTest::newRow("u three digits") << QByteArray("u041") << false << none;
    QTest::newRow("u seven digits") << QByteArray("u0000041") << false << none;
    QTest::newRow("u eight digits") << QByteArray("u00410042") << false << none;
    QTest::newRow("u lower case digits") << QByteArray("u00e9") << false << QString(QChar(0x00E9));
    QTest::newRow("u lower case digits, five") << QByteArray("u1f600") << false << u1F600;
    QTest::newRow("u lower case surrogate") << QByteArray("ud83d") << false << none;
    QTest::newRow("u lower case, above 10FFFF") << QByteArray("u11ffff") << false << none;
    QTest::newRow("u wrong digit") << QByteArray("u00G1") << false << none;
    QTest::newRow("u alone is the letter of the glyph list") << QByteArray("u") << false << QStringLiteral("u");

    // The ZapfDingbats list (a20 is U+2714 and a1 is U+2701 in the list of ISO 32000-2, D.6) is
    // the first one for the font ZapfDingbats; the font has the names of the glyph list too.
    // The specification gives a name of that list nothing in another font. The library asks the
    // list there too, after the glyph list and the forms of a code point: a deliberate deviation
    // (NOTES.txt 9.10.2, owner 2026-11-26). No name is in both lists, so the two orders give
    // the same texts.
    QTest::newRow("ZapfDingbats a20") << QByteArray("a20") << true << QString(QChar(0x2714));
    QTest::newRow("another font a20") << QByteArray("a20") << false << QString(QChar(0x2714));
    QTest::newRow("ZapfDingbats components") << QByteArray("a1_a20.alt") << true << (QString(QChar(0x2701)) + QChar(0x2714));
    QTest::newRow("another font components") << QByteArray("a1_a20.alt") << false << (QString(QChar(0x2701)) + QChar(0x2714));
    QTest::newRow("ZapfDingbats, a name of the glyph list") << QByteArray("A") << true << QStringLiteral("A");
    QTest::newRow("ZapfDingbats, both lists") << QByteArray("a20_A_uni0042") << true << (QString(QChar(0x2714)) + QStringLiteral("AB"));
    QTest::newRow("another font, both lists") << QByteArray("a20_A_uni0042") << false << (QString(QChar(0x2714)) + QStringLiteral("AB"));
    QTest::newRow("another font, unknown and ZapfDingbats") << QByteArray("foo_a20") << false << QString(QChar(0x2714));

    // Any bytes: a name is data of a document
    QTest::newRow("bytes above 127") << QByteArray("\xFF\xFE\x80") << false << none;
    QTest::newRow("zero byte in the name") << QByteArray("A\0B", 3) << false << none;
    QTest::newRow("zero byte in a component") << QByteArray("A_\0_B", 5) << false << QStringLiteral("AB");
    QTest::newRow("space in the name") << QByteArray("A B") << false << none;

    // A name without a terminating zero byte (a part of a longer buffer): nothing behind the
    // name is read - "uni00410" has five digits, though "042" follows in the buffer
    QTest::newRow("part of a buffer, uni") << QByteArray::fromRawData("uni00410042", 8) << false << none;
    QTest::newRow("part of a buffer, u") << QByteArray::fromRawData("u004142", 5) << false << QStringLiteral("A");
    QTest::newRow("part of a buffer, component") << QByteArray::fromRawData("f_i", 1) << false << QStringLiteral("f");
    QTest::newRow("part of a buffer, suffix") << QByteArray::fromRawData("one.oldstyle", 3) << false << QStringLiteral("1");
}

void FontEncodingTest::test_glyph_name_text()
{
    QFETCH(QByteArray, name);
    QFETCH(bool, isZapfDingbats);
    QFETCH(QString, text);

    QCOMPARE(pdf::PDFNameToUnicode::getUnicodeTextForName(name, isZapfDingbats), text);
}

void FontEncodingTest::test_glyph_name_text_zapf_dingbats_lookup_data()
{
    using Lookup = pdf::PDFNameToUnicode::ZapfDingbatsLookup;

    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<int>("lookup");
    QTest::addColumn<QString>("text");

    const QString checkMark = QString(QChar(0x2714));

    // a20 (U+2714), a65 and a66 are names of the ZapfDingbats list only (ISO 32000-2, D.6). The
    // font ZapfDingbats asks the list first, another simple font last (a deviation from the
    // specification, NOTES.txt 9.10.2), a Type 3 font never: a65, a66 are the names pdfTeX and
    // dvips give to the glyphs of the codes 65, 66 of a bitmap font - no dingbats.
    QTest::newRow("a20, first") << QByteArray("a20") << int(Lookup::First) << checkMark;
    QTest::newRow("a20, last") << QByteArray("a20") << int(Lookup::Last) << checkMark;
    QTest::newRow("a20, never") << QByteArray("a20") << int(Lookup::Never) << QString();
    QTest::newRow("a65, never") << QByteArray("a65") << int(Lookup::Never) << QString();
    QTest::newRow("a66.alt, never") << QByteArray("a66.alt") << int(Lookup::Never) << QString();
    QTest::newRow("components, never") << QByteArray("a20_A_a65") << int(Lookup::Never) << QStringLiteral("A");
    QTest::newRow("components, last") << QByteArray("a20_A") << int(Lookup::Last) << (checkMark + QStringLiteral("A"));

    // The glyph list and the forms of a code point do not depend on it (lower case digits too)
    QTest::newRow("A, never") << QByteArray("A") << int(Lookup::Never) << QStringLiteral("A");
    QTest::newRow("one.oldstyle, never") << QByteArray("one.oldstyle") << int(Lookup::Never) << QStringLiteral("1");
    QTest::newRow("f_i, never") << QByteArray("f_i") << int(Lookup::Never) << QStringLiteral("fi");
    QTest::newRow("uni20AC, never") << QByteArray("uni20AC") << int(Lookup::Never) << QString(QChar(0x20AC));
    QTest::newRow("uni20ac, never") << QByteArray("uni20ac") << int(Lookup::Never) << QString(QChar(0x20AC));
    QTest::newRow("u0041, never") << QByteArray("u0041") << int(Lookup::Never) << QStringLiteral("A");
}

void FontEncodingTest::test_glyph_name_text_zapf_dingbats_lookup()
{
    QFETCH(QByteArray, name);
    QFETCH(int, lookup);
    QFETCH(QString, text);

    const pdf::PDFNameToUnicode::ZapfDingbatsLookup zapfDingbatsLookup = static_cast<pdf::PDFNameToUnicode::ZapfDingbatsLookup>(lookup);
    QCOMPARE(pdf::PDFNameToUnicode::getUnicodeTextForName(name, zapfDingbatsLookup), text);

    // The overload of a simple font: the font ZapfDingbats is "first", another font "last"
    if (zapfDingbatsLookup != pdf::PDFNameToUnicode::ZapfDingbatsLookup::Never)
    {
        QCOMPARE(pdf::PDFNameToUnicode::getUnicodeTextForName(name, zapfDingbatsLookup == pdf::PDFNameToUnicode::ZapfDingbatsLookup::First), text);
    }
}

void FontEncodingTest::test_type3_names_text_extraction_data()
{
    QTest::addColumn<QByteArray>("differences");
    QTest::addColumn<QString>("expectedText");

    // A bitmap font of pdfTeX / dvips: the glyphs of the codes 65 and 66 are named a65 and a66,
    // no /ToUnicode. The names are in no glyph list but in the ZapfDingbats one (U+2746, U+2747),
    // which a Type 3 font never asks: the page has no text, as before the texts of the names.
    QTest::newRow("pdfTeX names") << QByteArray("/a65 /a66") << QString();

    // A name of the glyph list gives its text in a Type 3 font
    QTest::newRow("a name of the glyph list") << QByteArray("/A /a66") << QStringLiteral("A");
    QTest::newRow("names of the glyph list") << QByteArray("/A /B") << QStringLiteral("AB");
}

void FontEncodingTest::test_type3_names_text_extraction()
{
    QFETCH(QByteArray, differences);
    QFETCH(QString, expectedText);

    using D = pdftest::ContentTestDocuments;

    // A document of one page, which shows the codes 65 and 66 of a Type 3 font with a glyph
    // procedure of each name of /Differences [65 ...]
    auto createResources = [&differences](pdf::PDFDocumentBuilder* builder)
    {
        QByteArray charProcs;
        for (const QByteArray& glyphName : differences.split(' '))
        {
            const pdf::PDFObject procedure = D::addStreamObject(builder, D::DictionaryEntries(), QByteArray("500 0 0 0 400 700 d1 0 0 400 700 re f"));
            charProcs += glyphName + " " + QByteArray::number(procedure.getReference().objectNumber) + " " + QByteArray::number(procedure.getReference().generation) + " R ";
        }

        const QByteArray fontDictionary = "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] "
                                          "/CharProcs << " + charProcs + ">> /Encoding << /Type /Encoding /Differences [65 " + differences + "] >> "
                                          "/FirstChar 65 /LastChar 66 /Widths [500 500] /Resources << >> >>";
        pdf::PDFParser parser(fontDictionary, nullptr, pdf::PDFParser::None);
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), D::createDictionaryObject({ { "F1", D::addObject(builder, parser.getObject()) } }));
        return resources;
    };

    const pdf::PDFDocument document = D::createDocument(QByteArray("BT /F1 20 Tf 20 100 Td (AB) Tj ET"), createResources);

    for (const pdf::PDFDocumentTextFlowFactory::Algorithm algorithm : { pdf::PDFDocumentTextFlowFactory::Algorithm::Layout, pdf::PDFDocumentTextFlowFactory::Algorithm::Content })
    {
        pdf::PDFDocumentTextFlowFactory factory;
        factory.setStructureExport(true);
        const pdf::PDFDocumentTextFlow flow = factory.create(&document, algorithm);

        QString text;
        for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
        {
            if (item.isText())
            {
                for (const QChar character : item.text)
                {
                    if (!character.isSpace())
                    {
                        text += character;
                    }
                }
            }
        }
        QCOMPARE(text, expectedText);
    }
}

void FontEncodingTest::test_glyph_name_text_hostile_data()
{
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<QString>("text");

    // A million components or groups: a pass over the name for each of them would be 1e12 steps
    constexpr qsizetype COUNT = 1000000;

    // Components, each a letter: as many letters
    QTest::newRow("many components") << QByteArray("A_").repeated(COUNT) << QString(COUNT, QChar(u'A'));

    // Groups of "uni": as many letters; one digit more, or a surrogate, and it is no such name
    QTest::newRow("many groups") << (QByteArray("uni") + QByteArray("0042").repeated(COUNT)) << QString(COUNT, QChar(u'B'));
    QTest::newRow("many groups and a digit") << (QByteArray("uni") + QByteArray("0042").repeated(COUNT) + "0") << QString();
    QTest::newRow("many groups, the last one a surrogate") << (QByteArray("uni") + QByteArray("0042").repeated(COUNT) + "D800") << QString();

    // Only separators, only periods, one long unknown component, a long suffix
    QTest::newRow("underscores only") << QByteArray(COUNT, '_') << QString();
    QTest::newRow("periods only") << QByteArray(COUNT, '.') << QString();
    QTest::newRow("one long component") << QByteArray(COUNT, 'x') << QString();
    QTest::newRow("many digits of u") << (QByteArray("u") + QByteArray(COUNT, '0')) << QString();
    QTest::newRow("a long suffix") << (QByteArray("one.") + QByteArray("_A").repeated(COUNT)) << QStringLiteral("1");
    QTest::newRow("long components around a letter") << (QByteArray(COUNT, 'x') + "_A_" + QByteArray(COUNT, 'y')) << QStringLiteral("A");
}

void FontEncodingTest::test_glyph_name_text_hostile()
{
    QFETCH(QByteArray, name);
    QFETCH(QString, text);

    // The time is linear to the length of the name: a name of a few megabytes is a matter of
    // milliseconds (a pass over the name per component would not end within the limit)
    QElapsedTimer timer;
    timer.start();
    const QString result = pdf::PDFNameToUnicode::getUnicodeTextForName(name, false);
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(result.size(), text.size());
    QVERIFY(result == text);

    const qint64 limit = qint64(10000) * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QVERIFY2(elapsed < limit, qPrintable(QStringLiteral("%1 ms").arg(elapsed)));
}

void FontEncodingTest::test_differences_text_data()
{
    QTest::addColumn<QByteArray>("baseFont");
    QTest::addColumn<QByteArray>("name");
    QTest::addColumn<QChar>("key");
    QTest::addColumn<QChar>("textCharacter");
    QTest::addColumn<QString>("text");

    const QByteArray helvetica = "Helvetica";
    const QChar none;

    // A name of the glyph list: one character for the glyph and for the text
    QTest::newRow("A") << helvetica << QByteArray("A") << QChar(u'A') << QChar(u'A') << QStringLiteral("A");
    QTest::newRow("uni20AC") << helvetica << QByteArray("uni20AC") << QChar(0x20AC) << QChar(0x20AC) << QString(QChar(0x20AC));

    // A name with a suffix: the text of the name without the suffix. The lookup of the whole
    // name gives no key of a glyph; the font of this test is not embedded (its substitute is
    // asked by characters), so the one character of the text is the key of the glyph too
    // (owner 2026-11-26). An embedded font has no key for such a name - the glyph is the one of
    // the whole name (test_differences_text_type1_embedded, test_differences_text_truetype_embedded).
    QTest::newRow("one.oldstyle") << helvetica << QByteArray("one.oldstyle") << QChar(u'1') << QChar(u'1') << QStringLiteral("1");
    QTest::newRow("a.sc") << helvetica << QByteArray("a.sc") << QChar(u'a') << QChar(u'a') << QStringLiteral("a");
    QTest::newRow("f.alt") << helvetica << QByteArray("f.alt") << QChar(u'f') << QChar(u'f') << QStringLiteral("f");
    QTest::newRow("uni0041.smcp") << helvetica << QByteArray("uni0041.smcp") << QChar(u'A') << QChar(u'A') << QStringLiteral("A");
    QTest::newRow("u0041.smcp") << helvetica << QByteArray("u0041.smcp") << QChar(u'A') << QChar(u'A') << QStringLiteral("A");
    QTest::newRow("nonexistent.name") << helvetica << QByteArray("nonexistent.name") << none << none << QString();
    QTest::newRow(".notdef") << helvetica << QByteArray(".notdef") << none << none << QString();

    // A name of one component among empty ones is a text of one code unit
    QTest::newRow("_A") << helvetica << QByteArray("_A") << QChar(u'A') << QChar(u'A') << QStringLiteral("A");

    // A font, which is not one of the standard 14 fonts and is not embedded, by the same rule
    QTest::newRow("one.oldstyle, another font") << QByteArray("PDF4QTNoSuchFont") << QByteArray("one.oldstyle") << QChar(u'1') << QChar(u'1') << QStringLiteral("1");
    QTest::newRow("T_h, another font") << QByteArray("PDF4QTNoSuchFont") << QByteArray("T_h") << none << none << QStringLiteral("Th");

    // Names of more code units: the whole text by getEncodingText only; the text layout has
    // one character per glyph - none, or the ligature, which the glyph list of the library
    // has for the whole name (f_i, f_f, f_f_i, f_f_l), as before
    QTest::newRow("T_h") << helvetica << QByteArray("T_h") << none << none << QStringLiteral("Th");
    QTest::newRow("uni00410042") << helvetica << QByteArray("uni00410042") << none << none << QStringLiteral("AB");
    QTest::newRow("u1F600") << helvetica << QByteArray("u1F600") << none << none << (QString(QChar(0xD83D)) + QChar(0xDE00));
    QTest::newRow("f_i") << helvetica << QByteArray("f_i") << QChar(0xFB01) << QChar(0xFB01) << QStringLiteral("fi");
    QTest::newRow("f_f") << helvetica << QByteArray("f_f") << QChar(0xFB00) << QChar(0xFB00) << QStringLiteral("ff");
    QTest::newRow("f_f_i") << helvetica << QByteArray("f_f_i") << QChar(0xFB03) << QChar(0xFB03) << QStringLiteral("ffi");
    QTest::newRow("f_f_l") << helvetica << QByteArray("f_f_l") << QChar(0xFB04) << QChar(0xFB04) << QStringLiteral("ffl");
    QTest::newRow("f_f_l.alt") << helvetica << QByteArray("f_f_l.alt") << none << none << QStringLiteral("ffl");

    // The specification gives no text to these names (lower case digits, a name of the
    // ZapfDingbats list in a font, which is not ZapfDingbats). The library keeps their texts: two
    // deliberate deviations (NOTES.txt 9.10.2, owner 2026-11-26)
    QTest::newRow("uni20ac") << helvetica << QByteArray("uni20ac") << QChar(0x20AC) << QChar(0x20AC) << QString(QChar(0x20AC));
    QTest::newRow("a20, Helvetica") << helvetica << QByteArray("a20") << QChar(0x2714) << QChar(0x2714) << QString(QChar(0x2714));
    QTest::newRow("a20, ZapfDingbats") << QByteArray("ZapfDingbats") << QByteArray("a20") << QChar(0x2714) << QChar(0x2714) << QString(QChar(0x2714));
    QTest::newRow("a20, subset of ZapfDingbats") << QByteArray("ABCDEF+ZapfDingbats") << QByteArray("a20") << QChar(0x2714) << QChar(0x2714) << QString(QChar(0x2714));

    // A surrogate value is no code point of a name: the lookup of the whole name gives the
    // surrogate as before (the key), the text is none
    QTest::newRow("uniD800") << helvetica << QByteArray("uniD800") << QChar(0xD800) << none << QString();

    // U+0000 is "no character" through the library
    QTest::newRow("u0000") << helvetica << QByteArray("u0000") << none << none << QString();
    QTest::newRow("uni00000041") << helvetica << QByteArray("uni00000041") << QChar(u'A') << QChar(u'A') << QStringLiteral("A");

    // The longest text of a code (PDFFont::MAXIMUM_ENCODING_TEXT_LENGTH is 256 code units): a
    // name of a longer text gives no text
    QTest::newRow("256 code units") << helvetica << (QByteArray("uni") + QByteArray("0041").repeated(256)) << none << none << QString(256, QChar(u'A'));
    QTest::newRow("257 code units") << helvetica << (QByteArray("uni") + QByteArray("0041").repeated(257)) << none << none << QString();
    QTest::newRow("129 surrogate pairs") << helvetica << (QByteArray("u1F600_").repeated(129)) << none << none << QString();
}

void FontEncodingTest::test_differences_text()
{
    QFETCH(QByteArray, baseFont);
    QFETCH(QByteArray, name);
    QFETCH(QChar, key);
    QFETCH(QChar, textCharacter);
    QFETCH(QString, text);

    QCOMPARE(pdf::PDFFont::MAXIMUM_ENCODING_TEXT_LENGTH, qsizetype(256));

    // A font, which is not embedded, of a hand-written dictionary: the name at the code 65,
    // the code 66 is left to the base encoding
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFDocument document = builder.build();

    const QByteArray fontDictionary = "<< /Type /Font /Subtype /Type1 /BaseFont /" + baseFont + " /FirstChar 65 /LastChar 66 /Widths [600 600] "
                                      "/Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [65 /" + name + "] >> >>";
    pdf::PDFFontPointer font;
    try
    {
        pdf::PDFParser parser(fontDictionary, nullptr, pdf::PDFParser::None);
        font = pdf::PDFFont::createFont(parser.getObject(), "F1", &document);
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(font.get());
    QVERIFY(simpleFont);

    // The key of the glyph is the character of the encoding table, as before
    QCOMPARE(simpleFont->getUnicode(65), key);
    QCOMPARE((*simpleFont->getEncoding())[65], key);
    QCOMPARE((*simpleFont->getGlyphNames())[65], name);

    // The text of the code
    QCOMPARE(simpleFont->getTextUnicode(65), textCharacter);
    QCOMPARE(font->getEncodingText(65, 1), text);
    QCOMPARE(font->getEncodingText(65, 0), text);

    // The code without a name of /Differences: B of the base encoding for the glyph and for the text
    QCOMPARE(simpleFont->getUnicode(66), QChar(u'B'));
    QCOMPARE(simpleFont->getTextUnicode(66), QChar(u'B'));
    QCOMPARE(font->getEncodingText(66, 1), QStringLiteral("B"));

    // A code of a simple font is one byte (9.4.3); no /ToUnicode is in the font
    QVERIFY(font->getEncodingText(65, 2).isEmpty());
    QVERIFY(font->getEncodingText(65 + 256, 1).isEmpty());
    QVERIFY(font->getToUnicodeText(65, 1).isEmpty());
    QVERIFY(simpleFont->getTextUnicode(65 + 256).isNull());
}

void FontEncodingTest::test_differences_text_type1_embedded()
{
    // The hand-written Type 1 font program: its glyphs are rectangles of different shapes -
    // B 300 x 700, one 150 x 650, one.oldstyle 250 x 450, f_i 350 x 750, uni00410042 450 x 350,
    // uni0041.smcp 550 x 250
    qsizetype clearTextLength = 0;
    const QByteArray fontProgram = createType1FontProgram(&clearTextLength);

    // The codes A ... G by /Differences, no /ToUnicode
    TestFont testFont;
    createTestFont(&testFont, type1TestFontDictionary("/B /one /one.oldstyle /f_i /uni00410042 /uni0041.smcp /four.sups", 7),
                   { type1TestFontStream(fontProgram, clearTextLength) }, "ABCDEFG");
    QVERIFY(testFont.realizedFont);

    const std::vector<pdf::TextSequenceItem>& items = testFont.sequence.items;
    QCOMPARE(items.size(), size_t(7));

    // The glyph B (300 x 700) of the first item is the measure of the rectangles
    QVERIFY(items[0].glyph && !items[0].glyph->isEmpty());
    const QRectF glyphB = items[0].glyph->getBoundingRect();
    QVERIFY(glyphB.width() > 0.0 && glyphB.height() > 0.0);
    auto isRectangle = [&glyphB](const pdf::TextSequenceItem& item, double width, double height)
    {
        if (!item.glyph || item.glyph->isEmpty())
        {
            return false;
        }

        const QRectF rectangle = item.glyph->getBoundingRect();
        return qAbs(rectangle.width() / glyphB.width() - width / 300.0) < 0.03 && qAbs(rectangle.height() / glyphB.height() - height / 700.0) < 0.03;
    };

    // one: a name of the glyph list - its glyph, the character 1 as the key and as the text
    QVERIFY(isRectangle(items[1], 150.0, 650.0));
    QCOMPARE(items[1].character, QChar(u'1'));
    QCOMPARE(items[1].textCharacter, QChar(u'1'));

    // one.oldstyle: the glyph of that name (9.6.5.2), not the glyph of one; the text is 1 and the
    // key of the glyph is not: the item has no character for the real text drawing, which
    // would paint the glyph of 1
    QVERIFY2(isRectangle(items[2], 250.0, 450.0), isRectangle(items[2], 150.0, 650.0) ? "the glyph of one" : "another glyph");
    QVERIFY2(items[2].character.isNull(), qPrintable(QString::number(items[2].character.unicode(), 16)));
    QCOMPARE(items[2].textCharacter, QChar(u'1'));

    // f_i: its glyph; the text is "fi", the text layout gets the ligature of the glyph list of
    // the library (one character per glyph)
    QVERIFY(isRectangle(items[3], 350.0, 750.0));
    QCOMPARE(items[3].character, QChar(0xFB01));
    QCOMPARE(items[3].textCharacter, QChar(0xFB01));
    QCOMPARE(testFont.font->getEncodingText(68, 1), QStringLiteral("fi"));

    // uni00410042: its glyph; the text "AB" has no single character
    QVERIFY(isRectangle(items[4], 450.0, 350.0));
    QVERIFY(items[4].character.isNull());
    QVERIFY(items[4].textCharacter.isNull());
    QCOMPARE(testFont.font->getEncodingText(69, 1), QStringLiteral("AB"));

    // uni0041.smcp: its glyph, the text A
    QVERIFY(isRectangle(items[5], 550.0, 250.0));
    QVERIFY(items[5].character.isNull());
    QCOMPARE(items[5].textCharacter, QChar(u'A'));

    // four.sups: its glyph (200 x 300), the text 4
    QVERIFY(isRectangle(items[6], 200.0, 300.0));
    QVERIFY(items[6].character.isNull());
    QCOMPARE(items[6].textCharacter, QChar(u'4'));

    // The table and the names, by which the glyphs are selected, are those of the dictionary
    const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(testFont.font.get());
    QVERIFY(simpleFont);
    QVERIFY(simpleFont->getUnicode(67).isNull());
    QVERIFY((*simpleFont->getEncoding())[67].isNull());
    QCOMPARE((*simpleFont->getGlyphNames())[67], QByteArray("one.oldstyle"));
    QCOMPARE(simpleFont->getTextUnicode(67), QChar(u'1'));

    // The glyphs of the codes 66 and 67 are two glyphs of the font program
    QVERIFY((*simpleFont->getGlyphIndices())[66] != 0);
    QVERIFY((*simpleFont->getGlyphIndices())[67] != 0);
    QVERIFY((*simpleFont->getGlyphIndices())[66] != (*simpleFont->getGlyphIndices())[67]);

    // The character map of the font (Document Report) shows the texts: 1 for the glyph one of
    // the built-in encoding (the code 49), of the code 66 and for the glyph one.oldstyle
    // of the code 67, each with its glyph
    const pdf::CharacterInfos characterInfos = testFont.realizedFont->getCharacterInfos();
    std::vector<pdf::GID> glyphsOfOne;
    for (const pdf::CharacterInfo& info : characterInfos)
    {
        if (info.character == QChar(u'1'))
        {
            glyphsOfOne.push_back(info.gid);
        }
    }
    std::sort(glyphsOfOne.begin(), glyphsOfOne.end());
    std::vector<pdf::GID> expectedGlyphsOfOne = { (*simpleFont->getGlyphIndices())[49], (*simpleFont->getGlyphIndices())[66], (*simpleFont->getGlyphIndices())[67] };
    std::sort(expectedGlyphsOfOne.begin(), expectedGlyphsOfOne.end());
    QCOMPARE((*simpleFont->getGlyphIndices())[49], (*simpleFont->getGlyphIndices())[66]);
    QVERIFY(glyphsOfOne == expectedGlyphsOfOne);
}

void FontEncodingTest::test_differences_text_truetype_embedded()
{
    // LiberationSans-Regular of the resources of the library: a (3, 1) "cmap", a "post" table
    // with the glyph names; it has the glyph one, no glyph one.oldstyle
    QFile file(QStringLiteral(":/fonts/liberation-fonts-ttf/LiberationSans-Regular.ttf"));
    QVERIFY(file.open(QFile::ReadOnly));
    const QByteArray fontProgram = file.readAll();
    QVERIFY(!fontProgram.isEmpty());

    // The codes A ... D of a nonsymbolic font (9.6.5.4): one, one.oldstyle, four.sups (a name of
    // the "post" table of the font) and uni0041.smcp
    TestFont testFont;
    createTestFont(&testFont, "<< /Type /Font /Subtype /TrueType /BaseFont /LiberationSans /FirstChar 65 /LastChar 68 /Widths [600 610 620 630] "
                              "/FontDescriptor << /Type /FontDescriptor /FontName /LiberationSans /Flags 32 /FontBBox [-544 -303 1302 980] "
                              "/ItalicAngle 0 /Ascent 905 /Descent -212 /CapHeight 729 /StemV 80 /FontFile2 @1 >> "
                              "/Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding "
                              "/Differences [65 /one /one.oldstyle /four.sups /uni0041.smcp] >> >>",
                   { TestStream{ fontProgram, "/Length1 " + QByteArray::number(fontProgram.size()) } }, "ABCD");
    QVERIFY(testFont.realizedFont);

    const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(testFont.font.get());
    QVERIFY(simpleFont);

    const std::vector<pdf::TextSequenceItem>& items = testFont.sequence.items;
    QCOMPARE(items.size(), size_t(4));

    // one: the glyph list gives 1, the "cmap" its glyph
    QVERIFY(items[0].glyph && !items[0].glyph->isEmpty());
    QCOMPARE(items[0].character, QChar(u'1'));
    QCOMPARE(items[0].textCharacter, QChar(u'1'));

    // one.oldstyle: the text is 1, but the name is not mapped to the "cmap" through its text -
    // the font has no glyph of that name, so the code has no glyph (never the glyph of one);
    // the width of the dictionary (610 thousandths of the size 20)
    QVERIFY(simpleFont->getUnicode(66).isNull());
    QCOMPARE(simpleFont->getTextUnicode(66), QChar(u'1'));
    QCOMPARE((*simpleFont->getGlyphIndices())[66], pdf::GID(0));
    QVERIFY2(!items[1].glyph, items[1].glyph == items[0].glyph ? "the glyph of one" : "another glyph");
    QVERIFY(items[1].character.isNull());
    QCOMPARE(items[1].advance, 12.2);

    // four.sups: the glyph of the "post" table - not the glyph of the character 4, which is
    // its text; no key of a glyph
    TestFont reference;
    createTestFont(&reference, "<< /Type /Font /Subtype /TrueType /BaseFont /LiberationSans /FirstChar 52 /LastChar 52 /Widths [600] "
                               "/FontDescriptor << /Type /FontDescriptor /FontName /LiberationSans /Flags 32 /FontBBox [-544 -303 1302 980] "
                               "/ItalicAngle 0 /Ascent 905 /Descent -212 /CapHeight 729 /StemV 80 /FontFile2 @1 >> /Encoding /WinAnsiEncoding >>",
                   { TestStream{ fontProgram, "/Length1 " + QByteArray::number(fontProgram.size()) } }, "4");
    QVERIFY(reference.realizedFont);
    QCOMPARE(reference.sequence.items.size(), size_t(1));
    QVERIFY(reference.sequence.items[0].glyph && !reference.sequence.items[0].glyph->isEmpty());
    QCOMPARE(reference.sequence.items[0].character, QChar(u'4'));

    QVERIFY(items[2].glyph && !items[2].glyph->isEmpty());
    QVERIFY(items[2].glyph->getBoundingRect() != reference.sequence.items[0].glyph->getBoundingRect());
    QVERIFY(items[2].character.isNull());
    QCOMPARE(items[2].textCharacter, QChar(u'4'));

    // uni0041.smcp: no glyph of that name, the text A is no key of a glyph
    QVERIFY(simpleFont->getUnicode(68).isNull());
    QCOMPARE(simpleFont->getTextUnicode(68), QChar(u'A'));
    QVERIFY(!items[3].glyph);
    QCOMPARE(items[3].advance, 12.6);
}

void FontEncodingTest::test_differences_text_tounicode()
{
    // /ToUnicode is the first method (9.10.2) and the priority stays as it was: a character of
    // /ToUnicode wins over the name; a text of /ToUnicode of more code units gives no character
    // (getToUnicodeText gives it) and the character of the code is the one of the encoding
    TestFont testFont;
    createTestFont(&testFont, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /FirstChar 65 /LastChar 67 /Widths [600 610 620] "
                              "/Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [65 /one.oldstyle /one.oldstyle /one.oldstyle] >> "
                              "/ToUnicode @1 >>",
                   { TestStream{ "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n2 beginbfchar\n<41> <0058>\n<43> <006600660069>\nendbfchar\n", QByteArray() } },
                   "ABC");
    QVERIFY(testFont.realizedFont);

    const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(testFont.font.get());
    QVERIFY(simpleFont);

    // The code 65: X of /ToUnicode; the text of the encoding alone is 1
    QCOMPARE(simpleFont->getUnicode(65), QChar(u'X'));
    QCOMPARE(simpleFont->getTextUnicode(65), QChar(u'X'));
    QCOMPARE(testFont.font->getToUnicodeText(65, 1), QStringLiteral("X"));
    QCOMPARE(testFont.font->getEncodingText(65, 1), QStringLiteral("1"));

    // The code 66: no /ToUnicode, the text of the name (the font is not embedded: it is the key
    // of the glyph too)
    QCOMPARE(simpleFont->getUnicode(66), QChar(u'1'));
    QCOMPARE(simpleFont->getTextUnicode(66), QChar(u'1'));
    QVERIFY(testFont.font->getToUnicodeText(66, 1).isEmpty());

    // The code 67: "ffi" of /ToUnicode has no single character, the character of the code is
    // the one of the encoding
    QCOMPARE(simpleFont->getUnicode(67), QChar(u'1'));
    QCOMPARE(simpleFont->getTextUnicode(67), QChar(u'1'));
    QCOMPARE(testFont.font->getToUnicodeText(67, 1), QStringLiteral("ffi"));
    QCOMPARE(testFont.font->getEncodingText(67, 1), QStringLiteral("1"));
}

void FontEncodingTest::test_differences_text_type3()
{
    // The names of a Type 3 font give its texts as the names of /Differences of a simple font;
    // /ToUnicode (the code 65 only) is first. The font is not ZapfDingbats.
    const QByteArray procedure = "500 0 0 0 100 100 d1 0 0 100 100 re f";
    TestFont testFont;
    createTestFont(&testFont, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] "
                              "/CharProcs << /A @1 /one.oldstyle @1 /f_i @1 /uni00410042 @1 /my.glyph @1 /a20 @1 /B @1 >> "
                              "/Encoding << /Type /Encoding /Differences [65 /A /one.oldstyle /f_i /uni00410042 /my.glyph /a20 /B /one] >> "
                              "/FirstChar 65 /LastChar 72 /Widths [500 500 500 500 500 500 500 500] /Resources << >> /ToUnicode @2 >>",
                   { TestStream{ procedure, QByteArray() },
                     TestStream{ "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n1 beginbfchar\n<41> <0058>\nendbfchar\n", QByteArray() } },
                   "ABCDEFGHI");
    QVERIFY(testFont.realizedFont);

    const pdf::PDFType3Font* type3Font = dynamic_cast<const pdf::PDFType3Font*>(testFont.font.get());
    QVERIFY(type3Font);

    const std::vector<pdf::TextSequenceItem>& items = testFont.sequence.items;
    QCOMPARE(items.size(), size_t(8));
    for (size_t i = 0; i < 7; ++i)
    {
        QVERIFY(items[i].isContentStream());
        QCOMPARE(items[i].code, static_cast<unsigned int>(65 + i));
    }

    // A: X of /ToUnicode; the text of the name alone is A
    QCOMPARE(items[0].character, QChar(u'X'));
    QCOMPARE(items[0].textCharacter, QChar(u'X'));
    QCOMPARE(type3Font->getEncodingText(65, 1), QStringLiteral("A"));

    // one.oldstyle: the text 1; getUnicode and the character of the item stay those of
    // /ToUnicode (encodeCharacter turns that one back to the code)
    QVERIFY(type3Font->getUnicode(66).isNull());
    QVERIFY(items[1].character.isNull());
    QCOMPARE(type3Font->getTextUnicode(66), QChar(u'1'));
    QCOMPARE(items[1].textCharacter, QChar(u'1'));
    QCOMPARE(type3Font->getEncodingText(66, 1), QStringLiteral("1"));
    QCOMPARE(type3Font->getEncodingText(66, 0), QStringLiteral("1"));
    QVERIFY(type3Font->getEncodingText(66, 2).isEmpty());
    QVERIFY(type3Font->encodeCharacter(U'1').isEmpty());

    // f_i: the text "fi", the ligature for the text layout
    QVERIFY(items[2].character.isNull());
    QCOMPARE(items[2].textCharacter, QChar(0xFB01));
    QCOMPARE(type3Font->getEncodingText(67, 1), QStringLiteral("fi"));

    // uni00410042: the text "AB", no single character
    QVERIFY(items[3].textCharacter.isNull());
    QCOMPARE(type3Font->getEncodingText(68, 1), QStringLiteral("AB"));

    // my.glyph: "my" is in no glyph list. a20 is a name of the ZapfDingbats list only: that list
    // is never asked for a Type 3 font (pdfTeX and dvips name the glyphs of their bitmap fonts
    // a<code>; a simple font asks it - test_differences_text, row "a20, Helvetica")
    QVERIFY(items[4].textCharacter.isNull());
    QVERIFY(type3Font->getEncodingText(69, 1).isEmpty());
    QVERIFY(items[5].character.isNull());
    QVERIFY2(items[5].textCharacter.isNull(), qPrintable(QString::number(items[5].textCharacter.unicode(), 16)));
    QVERIFY(type3Font->getEncodingText(70, 1).isEmpty());

    // B
    QVERIFY(items[6].character.isNull());
    QCOMPARE(items[6].textCharacter, QChar(u'B'));

    // The character map of the font (Document Report) shows the texts; the identifier of a glyph
    // of a Type 3 font is its code: X of /ToUnicode for 65, 1 for 66, the ligature for 67, none for 68
    std::map<pdf::GID, QChar> charactersOfGlyphs;
    for (const pdf::CharacterInfo& info : testFont.realizedFont->getCharacterInfos())
    {
        charactersOfGlyphs[info.gid] = info.character;
    }
    QCOMPARE(charactersOfGlyphs.size(), size_t(7));
    QCOMPARE(charactersOfGlyphs[65], QChar(u'X'));
    QCOMPARE(charactersOfGlyphs[66], QChar(u'1'));
    QCOMPARE(charactersOfGlyphs[67], QChar(0xFB01));
    QVERIFY(charactersOfGlyphs[68].isNull());

    // The last name of a code decides (Table 112): the code 65 is named T_h and then A, the code
    // 66 f_i and then one.oldstyle - nothing of the first names is left
    TestFont renamedFont;
    createTestFont(&renamedFont, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] "
                                 "/CharProcs << /A @1 /one.oldstyle @1 /T_h @1 /f_i @1 >> "
                                 "/Encoding << /Type /Encoding /Differences [65 /T_h /f_i 65 /A /one.oldstyle] >> "
                                 "/FirstChar 65 /LastChar 66 /Widths [500 500] /Resources << >> >>",
                   { TestStream{ procedure, QByteArray() } }, "AB");
    QVERIFY(renamedFont.realizedFont);
    QCOMPARE(renamedFont.sequence.items.size(), size_t(2));
    QCOMPARE(renamedFont.sequence.items[0].textCharacter, QChar(u'A'));
    QCOMPARE(renamedFont.font->getEncodingText(65, 1), QStringLiteral("A"));
    QCOMPARE(renamedFont.sequence.items[1].textCharacter, QChar(u'1'));
    QCOMPARE(renamedFont.font->getEncodingText(66, 1), QStringLiteral("1"));

    // one is not a key of /CharProcs: no glyph is painted, the item has no character; the
    // text of the code is known all the same. The code 73 has no name.
    QVERIFY(!items[7].isContentStream());
    QVERIFY(items[7].textCharacter.isNull());
    QCOMPARE(type3Font->getEncodingText(72, 1), QStringLiteral("1"));
    QVERIFY(type3Font->getEncodingText(73, 1).isEmpty());
    QVERIFY(type3Font->getTextUnicode(73).isNull());
    QVERIFY(type3Font->getTextUnicode(-1).isNull());
    QVERIFY(type3Font->getTextUnicode(256).isNull());
    QVERIFY(type3Font->getEncodingText(256 + 66, 1).isEmpty());
}

void FontEncodingTest::test_builtin_encoding_text()
{
    // The Type 1 font program with its own built-in encoding: one.oldstyle at the code 65, B at 66,
    // f_i at 67, uni0041.smcp at 68. The font dictionary has no /Encoding, so the built-in
    // encoding of the program is used (9.6.5.1).
    qsizetype clearTextLength = 0;
    const QByteArray fontProgram = createType1FontProgram(&clearTextLength, "256 array\n"
                                                                            "0 1 255 {1 index exch /.notdef put} for\n"
                                                                            "dup 65 /one.oldstyle put\n"
                                                                            "dup 66 /B put\n"
                                                                            "dup 67 /f_i put\n"
                                                                            "dup 68 /uni0041.smcp put\n"
                                                                            "readonly def\n");

    TestFont testFont;
    createTestFont(&testFont, type1TestFontDictionary(QByteArray(), 4), { type1TestFontStream(fontProgram, clearTextLength) }, "ABCD");
    QVERIFY(testFont.realizedFont);

    const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(testFont.font.get());
    QVERIFY(simpleFont);

    const std::vector<pdf::TextSequenceItem>& items = testFont.sequence.items;
    QCOMPARE(items.size(), size_t(4));

    // B (300 x 700) is the measure of the rectangles
    QVERIFY(items[1].glyph && !items[1].glyph->isEmpty());
    QCOMPARE(items[1].character, QChar(u'B'));
    QCOMPARE(items[1].textCharacter, QChar(u'B'));
    const QRectF glyphB = items[1].glyph->getBoundingRect();
    auto isRectangle = [&glyphB](const pdf::TextSequenceItem& item, double width, double height)
    {
        if (!item.glyph || item.glyph->isEmpty())
        {
            return false;
        }

        const QRectF rectangle = item.glyph->getBoundingRect();
        return qAbs(rectangle.width() / glyphB.width() - width / 300.0) < 0.03 && qAbs(rectangle.height() / glyphB.height() - height / 700.0) < 0.03;
    };

    // one.oldstyle (250 x 450) of the built-in encoding: the text is 1. The encoding table keeps
    // the character it had (A of the standard encoding for the code 65) - it is not touched.
    QVERIFY(isRectangle(items[0], 250.0, 450.0));
    QCOMPARE(items[0].character, QChar(u'A'));
    QCOMPARE(items[0].textCharacter, QChar(u'1'));
    QCOMPARE(simpleFont->getEncodingText(65, 1), QStringLiteral("1"));

    // f_i (350 x 750): the glyph list of the library has the whole name, the character stays;
    // the text of more code units is given by getEncodingText
    QVERIFY(isRectangle(items[2], 350.0, 750.0));
    QCOMPARE(items[2].character, QChar(0xFB01));
    QCOMPARE(items[2].textCharacter, QChar(0xFB01));
    QCOMPARE(simpleFont->getEncodingText(67, 1), QStringLiteral("fi"));

    // uni0041.smcp (550 x 250): the text is A
    QVERIFY(isRectangle(items[3], 550.0, 250.0));
    QCOMPARE(items[3].character, QChar(u'D'));
    QCOMPARE(items[3].textCharacter, QChar(u'A'));
    QCOMPARE(simpleFont->getEncodingText(68, 1), QStringLiteral("A"));
}

void FontEncodingTest::test_differences_text_not_embedded_data()
{
    QTest::addColumn<QByteArray>("fontEntries");
    QTest::addColumn<bool>("isStandardFont");

    // Standard fonts: their substitute is a font of the resources of the library. Verdana is
    // not one of them: its substitute is a font of the system (the test of the glyphs and
    // of the extracted text is skipped, when the system gives none).
    QTest::newRow("Type 1, Helvetica") << QByteArray("/Subtype /Type1 /BaseFont /Helvetica") << true;
    QTest::newRow("TrueType, ArialMT") << QByteArray("/Subtype /TrueType /BaseFont /ArialMT") << true;
    QTest::newRow("Type 1, Verdana") << QByteArray("/Subtype /Type1 /BaseFont /Verdana /FontDescriptor << /Type /FontDescriptor /FontName /Verdana /Flags 32 "
                                                   "/FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 700 /Descent 0 /CapHeight 700 /StemV 80 >>") << false;
    QTest::newRow("TrueType, Verdana") << QByteArray("/Subtype /TrueType /BaseFont /Verdana /FontDescriptor << /Type /FontDescriptor /FontName /Verdana /Flags 32 "
                                                     "/FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 700 /Descent 0 /CapHeight 700 /StemV 80 >>") << false;
}

void FontEncodingTest::test_differences_text_not_embedded()
{
    QFETCH(QByteArray, fontEntries);
    QFETCH(bool, isStandardFont);

    using D = pdftest::ContentTestDocuments;

    // A font without an embedded font program: its glyphs come from a substitute, which is asked
    // by characters. A name of /Differences, which the lookup of the whole name does not know and
    // whose text is one character, takes that character as the key of its glyph (owner
    // 2026-11-26): one.oldstyle paints the glyph of 1 and its text is 1. The codes A ... F:
    // one, one.oldstyle, T_h (a text of two characters: no key), A, uni0041.smcp and a name of
    // nothing.
    const QByteArray fontDictionary = "<< /Type /Font " + fontEntries + " /FirstChar 65 /LastChar 70 /Widths [600 610 620 630 640 650] "
                                      "/Encoding << /Type /Encoding /BaseEncoding /WinAnsiEncoding "
                                      "/Differences [65 /one /one.oldstyle /T_h /A /uni0041.smcp /nonexistent.name] >> >>";

    // The font itself (it does not depend on the substitute)
    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFDocument fontDocument = builder.build();

    pdf::PDFFontPointer font;
    try
    {
        pdf::PDFParser parser(fontDictionary, nullptr, pdf::PDFParser::None);
        font = pdf::PDFFont::createFont(parser.getObject(), "F1", &fontDocument);
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(font.get());
    QVERIFY(simpleFont);
    QVERIFY(!simpleFont->getFontDescriptor()->isEmbedded());

    QCOMPARE(simpleFont->getUnicode(65), QChar(u'1'));
    QCOMPARE(simpleFont->getUnicode(66), QChar(u'1'));
    QCOMPARE((*simpleFont->getEncoding())[66], QChar(u'1'));
    QCOMPARE((*simpleFont->getGlyphNames())[66], QByteArray("one.oldstyle"));
    QCOMPARE(simpleFont->getTextUnicode(66), QChar(u'1'));
    QVERIFY(simpleFont->getUnicode(67).isNull());
    QVERIFY(simpleFont->getTextUnicode(67).isNull());
    QCOMPARE(simpleFont->getEncodingText(67, 1), QStringLiteral("Th"));
    QCOMPARE(simpleFont->getUnicode(68), QChar(u'A'));
    QCOMPARE(simpleFont->getUnicode(69), QChar(u'A'));
    QCOMPARE(simpleFont->getTextUnicode(69), QChar(u'A'));
    QVERIFY(simpleFont->getUnicode(70).isNull());

    // The glyphs of the substitute
    pdf::PDFRealizedFontPointer realizedFont;
    pdf::TextSequence sequence;
    try
    {
        pdf::PDFRenderErrorReporterDummy reporter;
        realizedFont = pdf::PDFRealizedFont::createRealizedFont(font, 20.0, &reporter);
        realizedFont->fillTextSequence("ABCDEF", sequence, &reporter);
    }
    catch (const pdf::PDFException& exception)
    {
        if (isStandardFont)
        {
            QFAIL(qPrintable(exception.getMessage()));
        }
        QSKIP(qPrintable(QStringLiteral("No substitute of the font: %1").arg(exception.getMessage())));
    }

    const std::vector<pdf::TextSequenceItem>& items = sequence.items;
    QCOMPARE(items.size(), size_t(6));

    // one and one.oldstyle: the same glyph of the substitute, the character 1
    QVERIFY(items[0].glyph && !items[0].glyph->isEmpty());
    QCOMPARE(items[0].character, QChar(u'1'));
    QCOMPARE(items[1].glyph, items[0].glyph);
    QCOMPARE(items[1].character, QChar(u'1'));
    QCOMPARE(items[1].textCharacter, QChar(u'1'));

    // T_h: no key, no glyph; the width of the dictionary (620 thousandths of the size 20)
    QVERIFY(!items[2].glyph);
    QCOMPARE(items[2].advance, 12.4);

    // A and uni0041.smcp: the same glyph, another one than of the digit
    QVERIFY(items[3].glyph && !items[3].glyph->isEmpty());
    QVERIFY(items[3].glyph != items[0].glyph);
    QCOMPARE(items[4].glyph, items[3].glyph);
    QCOMPARE(items[4].character, QChar(u'A'));
    QCOMPARE(items[4].textCharacter, QChar(u'A'));

    // A name of nothing: no glyph, the width of the dictionary (650 thousandths)
    QVERIFY(!items[5].glyph);
    QCOMPARE(items[5].advance, 13.0);

    // The text of a page, which shows the six codes
    auto createResources = [&fontDictionary](pdf::PDFDocumentBuilder* documentBuilder)
    {
        pdf::PDFParser parser(fontDictionary, nullptr, pdf::PDFParser::None);
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), D::createDictionaryObject({ { "F1", D::addObject(documentBuilder, parser.getObject()) } }));
        return resources;
    };
    const pdf::PDFDocument document = D::createDocument(QByteArray("BT /F1 20 Tf 20 100 Td (ABCDEF) Tj ET"), createResources);

    pdf::PDFDocumentTextFlowFactory factory;
    const pdf::PDFDocumentTextFlow flow = factory.create(&document, pdf::PDFDocumentTextFlowFactory::Algorithm::Layout);

    QString text;
    for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        if (item.isText())
        {
            for (const QChar character : item.text)
            {
                if (!character.isSpace())
                {
                    text += character;
                }
            }
        }
    }
    QCOMPARE(text, QStringLiteral("11AA"));
}

void FontEncodingTest::test_differences_text_extraction_data()
{
    QTest::addColumn<bool>("isType3");
    QTest::addColumn<int>("algorithm");

    QTest::newRow("Type 1, layout") << false << int(pdf::PDFDocumentTextFlowFactory::Algorithm::Layout);
    QTest::newRow("Type 1, content") << false << int(pdf::PDFDocumentTextFlowFactory::Algorithm::Content);
    QTest::newRow("Type 3, layout") << true << int(pdf::PDFDocumentTextFlowFactory::Algorithm::Layout);
    QTest::newRow("Type 3, content") << true << int(pdf::PDFDocumentTextFlowFactory::Algorithm::Content);
}

void FontEncodingTest::test_differences_text_extraction()
{
    QFETCH(bool, isType3);
    QFETCH(int, algorithm);

    using D = pdftest::ContentTestDocuments;

    // A document of one page, which shows the codes A ... F of one font without /ToUnicode:
    // /Differences [65 /one.oldstyle /f_i /uni00410042 /B /uni0041.smcp /nonexistent.name]. The
    // font is the embedded Type 1 test font (it has a glyph of each name but the last one), or a
    // Type 3 font with a glyph procedure of each name.
    const QByteArray differences = "/one.oldstyle /f_i /uni00410042 /B /uni0041.smcp /nonexistent.name";

    qsizetype clearTextLength = 0;
    const QByteArray fontProgram = createType1FontProgram(&clearTextLength);

    auto createResources = [&](pdf::PDFDocumentBuilder* builder)
    {
        QByteArray fontDictionary;
        if (isType3)
        {
            QByteArray charProcs;
            for (const char* glyphName : { "one.oldstyle", "f_i", "uni00410042", "B", "uni0041.smcp", "nonexistent.name" })
            {
                const pdf::PDFObject procedure = D::addStreamObject(builder, D::DictionaryEntries(), QByteArray("500 0 0 0 400 700 d1 0 0 400 700 re f"));
                charProcs += "/" + QByteArray(glyphName) + " " + QByteArray::number(procedure.getReference().objectNumber) + " " + QByteArray::number(procedure.getReference().generation) + " R ";
            }

            fontDictionary = "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] "
                             "/CharProcs << " + charProcs + ">> /Encoding << /Type /Encoding /Differences [65 " + differences + "] >> "
                             "/FirstChar 65 /LastChar 70 /Widths [500 500 500 500 500 500] /Resources << >> >>";
        }
        else
        {
            const pdf::PDFObject fontFile = D::addStreamObject(builder, { { "Length1", pdf::PDFObject::createInteger(clearTextLength) },
                                                                          { "Length2", pdf::PDFObject::createInteger(fontProgram.size() - clearTextLength) },
                                                                          { "Length3", pdf::PDFObject::createInteger(0) } }, fontProgram);
            fontDictionary = type1TestFontDictionary(differences, 6);
            fontDictionary.replace("@1", QByteArray::number(fontFile.getReference().objectNumber) + " " + QByteArray::number(fontFile.getReference().generation) + " R");
        }

        pdf::PDFParser parser(fontDictionary, nullptr, pdf::PDFParser::None);
        pdf::PDFDictionaryBuilder resources;
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"), D::createDictionaryObject({ { "F1", D::addObject(builder, parser.getObject()) } }));
        return resources;
    };

    const pdf::PDFDocument document = D::createDocument(QByteArray("BT /F1 20 Tf 20 100 Td (ABCDEF) Tj ET"), createResources);

    // The text of the page by the text flow (Extract Text, the search and the selection read
    // the same characters); the algorithm Content gives the text of an untagged page for an export
    pdf::PDFDocumentTextFlowFactory factory;
    factory.setStructureExport(true);
    const pdf::PDFDocumentTextFlow flow = factory.create(&document, static_cast<pdf::PDFDocumentTextFlowFactory::Algorithm>(algorithm));

    QString text;
    for (const pdf::PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        if (item.isText())
        {
            for (const QChar character : item.text)
            {
                if (!character.isSpace())
                {
                    text += character;
                }
            }
        }
    }

    // one.oldstyle is 1; f_i is the ligature (its text "fi" has two code units and the text
    // layout one character per glyph - the character of the glyph list of the library, as
    // before); uni00410042 ("AB") has no single character; B; uni0041.smcp is A; the last name
    // is nothing
    QCOMPARE(text, QStringLiteral("1") + QChar(0xFB01) + QStringLiteral("BA"));
}

pdf::PDFFontPointer FontEncodingTest::createFontWithStream(pdf::PDFDocument* document, QByteArray fontDictionary, const QByteArray& streamData, QString* errorMessage)
{
    using D = pdftest::ContentTestDocuments;

    pdf::PDFDocumentBuilder builder;
    builder.createDocument();
    const pdf::PDFObject stream = D::addStreamObject(&builder, D::DictionaryEntries(), streamData);
    fontDictionary.replace("@1", QByteArray::number(stream.getReference().objectNumber) + " " + QByteArray::number(stream.getReference().generation) + " R");
    *document = builder.build();

    try
    {
        pdf::PDFParser parser(fontDictionary, nullptr, pdf::PDFParser::None);
        return pdf::PDFFont::createFont(parser.getObject(), "F1", document);
    }
    catch (const pdf::PDFException& exception)
    {
        *errorMessage = exception.getMessage();
    }

    return pdf::PDFFontPointer();
}

QByteArray FontEncodingTest::type0FontDictionaryWithEncodingStream()
{
    return "<< /Type /Font /Subtype /Type0 /BaseFont /Test /Encoding @1 "
           "/DescendantFonts [<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Test "
           "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> >>] >>";
}

void FontEncodingTest::test_type0_encoding_cmap_without_codes()
{
    // A5L1-1. The /Encoding CMap of a Type 0 font maps the character codes of the shown strings
    // to CIDs (ISO 32000-2, 9.7.5; 9.7.6.2: the CMap divides a string into codes). A bfrange,
    // whose destination string has more than one code unit, maps no code to a CID - it gives
    // only the texts, which a /ToUnicode CMap uses (9.10.3). A CMap of only such ranges decodes
    // no code: the font is refused when it is created, with the error of a CMap without any
    // mapping, and nothing divides by its longest code length, which is 0.
    const QByteArray noMappingCMap = "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n";
    const QByteArray textsOnlyCMap = "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
                                     "2 beginbfrange\n<00> <7F> <00410042>\n<80> <FF> <D835DC00>\nendbfrange\n";
    const QByteArray codesCMap = "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
                                 "1 begincidrange\n<00> <FF> 0\nendcidrange\n";

    // The same font with a CMap of codes is created and decodes the codes A and B (a check,
    // that nothing else of the font dictionary is refused)
    {
        pdf::PDFDocument document;
        QString errorMessage;
        const pdf::PDFFontPointer font = createFontWithStream(&document, type0FontDictionaryWithEncodingStream(), codesCMap, &errorMessage);
        QVERIFY2(font, qPrintable(errorMessage));
        const std::vector<pdf::PDFFontCMap::MappedCode> codes = font->getCMap()->interpretWithCode(QByteArray("AB"));
        QCOMPARE(codes.size(), size_t(2));
        QCOMPARE(codes[0].cid, pdf::CID(0x41));
        QCOMPARE(codes[1].cid, pdf::CID(0x42));
    }

    // A CMap without any mapping: the error the texts-only CMap is compared with
    QString noMappingError;
    {
        pdf::PDFDocument document;
        const pdf::PDFFontPointer font = createFontWithStream(&document, type0FontDictionaryWithEncodingStream(), noMappingCMap, &noMappingError);
        QVERIFY(!font);
        QCOMPARE(noMappingError, QStringLiteral("Invalid CMAP in CID-keyed font."));
    }

    pdf::PDFDocument document;
    QString textsOnlyError;
    const pdf::PDFFontPointer font = createFontWithStream(&document, type0FontDictionaryWithEncodingStream(), textsOnlyCMap, &textsOnlyError);
    if (font)
    {
        // A font, which should have been refused: a shown string goes the way of every renderer
        // (PDFRealizedFontImpl::fillTextSequence of a Type 0 font decodes it so)
        const std::vector<pdf::PDFFontCMap::MappedCode> codes = font->getCMap()->interpretWithCode(QByteArray("AB"));
        QVERIFY2(false, qPrintable(QStringLiteral("The font is created, its CMap decodes %1 codes").arg(codes.size())));
    }
    QCOMPARE(textsOnlyError, noMappingError);
}

void FontEncodingTest::test_tounicode_cmap_of_texts_only()
{
    // A /ToUnicode CMap, whose only mappings are destinations of more code units (ISO 32000-2,
    // 9.10.3: a bfrange with one destination string maps the codes of the range to strings,
    // whose last byte grows with the code), is a mapping: its texts are the texts of the codes
    const QByteArray textsOnlyCMap = "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
                                     "1 beginbfrange\n<41> <42> <00660069>\nendbfrange\n";

    // A simple font: <41> is "fi", <42> is "fj", another code has no text
    {
        pdf::PDFDocument document;
        QString errorMessage;
        const pdf::PDFFontPointer font = createFontWithStream(&document, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /FirstChar 65 /LastChar 66 /Widths [500 500] /ToUnicode @1 >>", textsOnlyCMap, &errorMessage);
        QVERIFY2(font, qPrintable(errorMessage));
        QCOMPARE(font->getToUnicodeText(0x41, 1), QStringLiteral("fi"));
        QCOMPARE(font->getToUnicodeText(0x42, 1), QStringLiteral("fj"));
        QVERIFY(font->getToUnicodeText(0x43, 1).isEmpty());
    }

    // A Type 0 font (Identity-H, codes of two bytes): the same texts for <0041> and <0042>
    {
        const QByteArray type0TextsOnlyCMap = "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n"
                                              "1 beginbfrange\n<0041> <0042> <00660069>\nendbfrange\n";
        pdf::PDFDocument document;
        QString errorMessage;
        const pdf::PDFFontPointer font = createFontWithStream(&document, "<< /Type /Font /Subtype /Type0 /BaseFont /Test /Encoding /Identity-H "
                                                                         "/DescendantFonts [<< /Type /Font /Subtype /CIDFontType2 /BaseFont /Test "
                                                                         "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> >>] /ToUnicode @1 >>",
                                                              type0TextsOnlyCMap, &errorMessage);
        QVERIFY2(font, qPrintable(errorMessage));
        QCOMPARE(font->getToUnicodeText(0x41, 2), QStringLiteral("fi"));
        QCOMPARE(font->getToUnicodeText(0x42, 2), QStringLiteral("fj"));
        QVERIFY(font->getToUnicodeText(0x41, 1).isEmpty());
    }
}

void FontEncodingTest::test_cmap_without_codes_interprets_nothing()
{
    // A CMap without a code (a default one, or one of only texts of more code units) has the
    // longest code length 0 and decodes no code of any string
    const pdf::PDFFontCMap emptyCMap;
    QCOMPARE(emptyCMap.getMaxKeyLength(), 0u);
    QVERIFY(emptyCMap.interpretWithCode(QByteArray("AB")).empty());
    QVERIFY(emptyCMap.interpretWithCode(QByteArray()).empty());
    QVERIFY(emptyCMap.interpret(QByteArray("AB")).empty());

    const pdf::PDFFontCMap textsOnlyCMap = pdf::PDFFontCMap::createFromData("1 beginbfrange\n<00> <FF> <00410042>\nendbfrange\n");
    QCOMPARE(textsOnlyCMap.getMaxKeyLength(), 0u);
    QCOMPARE(textsOnlyCMap.getToUnicodeText(0x00, 1), QStringLiteral("AB"));
    QVERIFY(textsOnlyCMap.interpretWithCode(QByteArray("AB")).empty());
    QVERIFY(textsOnlyCMap.interpret(QByteArray("\x00\x01", 2)).empty());
}

void FontEncodingTest::test_simple_font_real_widths()
{
    // A2L2-3. ISO 32000-2, 9.6.2.1, Table 109: /Widths of a simple font is an array of numbers -
    // a real number is a width as well as an integer. The codes 65, 66 and 67 have the widths
    // 500, 600 and 722.5; 68 is past /LastChar.
    {
        pdf::PDFDocument document;
        QString errorMessage;
        const pdf::PDFFontPointer font = createFontWithStream(&document, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /FirstChar 65 /LastChar 67 /Widths [500 600.0 722.5] /ToUnicode @1 >>",
                                                              QByteArray("1 beginbfchar\n<41> <0041>\nendbfchar\n"), &errorMessage);
        const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(font.get());
        QVERIFY2(simpleFont, qPrintable(errorMessage));

        QVERIFY(simpleFont->hasGlyphAdvance(65));
        QVERIFY(simpleFont->hasGlyphAdvance(66));
        QVERIFY(simpleFont->hasGlyphAdvance(67));
        QVERIFY(!simpleFont->hasGlyphAdvance(68));
        QCOMPARE(double(simpleFont->getGlyphAdvance(65)), 500.0);
        QCOMPARE(double(simpleFont->getGlyphAdvance(66)), 600.0);
        QCOMPARE(double(simpleFont->getGlyphAdvance(67)), 722.5);
        QCOMPARE(double(simpleFont->getGlyphAdvance(68)), 0.0);
    }

    // A name among the numbers: the value is no array of numbers, the font has no width of the
    // dictionary for any code (as before: /MissingWidth applies, the advance is 0)
    {
        pdf::PDFDocument document;
        QString errorMessage;
        const pdf::PDFFontPointer font = createFontWithStream(&document, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /FirstChar 65 /LastChar 67 /Widths [500 /Wide 722.5] /ToUnicode @1 >>",
                                                              QByteArray("1 beginbfchar\n<41> <0041>\nendbfchar\n"), &errorMessage);
        const pdf::PDFSimpleFont* simpleFont = dynamic_cast<const pdf::PDFSimpleFont*>(font.get());
        QVERIFY2(simpleFont, qPrintable(errorMessage));

        for (size_t code = 65; code <= 67; ++code)
        {
            QVERIFY(!simpleFont->hasGlyphAdvance(code));
            QCOMPARE(double(simpleFont->getGlyphAdvance(code)), 0.0);
        }
    }
}

void FontEncodingTest::test_tounicode_lookup_many_entries()
{
    // A5L1-4. A /ToUnicode CMap of 50 000 two-byte codes, each mapped to one code unit: the code
    // c is U+1000 + (c xor 1), so no two neighbors can be merged into a range; and 50 000 texts
    // of two code units of the three-byte codes 0x010000 + c: "A" and U+1000 + (c xor 1) (each
    // also an entry of the map of the character 0, as every bfchar of a destination, which is
    // not one code unit). Every code is asked by its byte count and by any byte count (0, as a
    // simple and a Type 3 font ask): the time of the whole is linear in the number of the codes
    // and logarithmic in the size of the map.
    constexpr int COUNT = 50000;
    QByteArray data = "2 begincodespacerange\n<0000> <FFFF>\n<000000> <FFFFFF>\nendcodespacerange\n";
    data += QByteArray::number(2 * COUNT) + " beginbfchar\n";
    for (int c = 0; c < COUNT; ++c)
    {
        const QByteArray unit = QByteArray::number(0x1000 + (c ^ 1), 16).rightJustified(4, '0');
        data += "<" + QByteArray::number(c, 16).rightJustified(4, '0') + "> <" + unit + ">\n";
        data += "<" + QByteArray::number(0x10000 + c, 16).rightJustified(6, '0') + "> <0041" + unit + ">\n";
    }
    data += "endbfchar\n";

    const pdf::PDFFontCMap cmap = pdf::PDFFontCMap::createFromData(data);

    QElapsedTimer timer;
    timer.start();

    int failures = 0;
    QString firstFailure;
    for (int c = 0; c < COUNT; ++c)
    {
        const QChar character(char16_t(0x1000 + (c ^ 1)));
        const QString text = QStringLiteral("A") + character;
        const unsigned int code = unsigned(c);
        const unsigned int textCode = unsigned(0x10000 + c);

        const bool isRight = cmap.getToUnicodeText(code, 2) == QString(character) &&
                             cmap.getToUnicodeText(code, 0) == QString(character) &&
                             cmap.getToUnicode(code, 2) == character &&
                             cmap.getToUnicode(code) == character &&
                             cmap.getToUnicodeText(textCode, 3) == text &&
                             cmap.getToUnicodeText(textCode, 0) == text &&
                             cmap.getToUnicodeText(code, 3).isEmpty() &&
                             cmap.getToUnicodeText(textCode, 2).isEmpty();
        if (!isRight)
        {
            if (failures == 0)
            {
                firstFailure = QStringLiteral("code %1").arg(c);
            }
            ++failures;
        }
    }

    const qint64 elapsed = timer.elapsed();
    QVERIFY2(failures == 0, qPrintable(QStringLiteral("%1 codes are wrong, the first one: %2").arg(failures).arg(firstFailure)));

    const qint64 limit = qint64(1000) * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    QVERIFY2(elapsed < limit, qPrintable(QStringLiteral("%1 ms").arg(elapsed)));
}

void FontEncodingTest::test_tounicode_lookup_first_entry()
{
    // Two mappings of one code: the one found is the first one in the order of the map - the
    // entries by their byte count, then by their first code; the texts by their byte count, then
    // by their code, two texts of one code in the order of the CMap. Any byte count (0) finds
    // the first one of all byte counts.
    //
    //   1-byte entries: [10 .. 30] -> U+0100.., [20 .. 7E] -> U+0020.., <41> -> X, <42> -> 0
    //                   (the destination of more code units), the same <42> once more
    //   2-byte entries: <0005> -> Z, <0042> -> 0
    //   texts:          <42> "fi" and "fl" (in this order), <0042> "ff"
    const QByteArray data = "2 begincodespacerange\n<00> <FF>\n<0000> <FFFF>\nendcodespacerange\n"
                            "2 beginbfrange\n<10> <30> <0100>\n<20> <7E> <0020>\nendbfrange\n"
                            "5 beginbfchar\n<0042> <00660066>\n<41> <0058>\n<42> <00660069>\n<0005> <005A>\n<42> <0066006C>\nendbfchar\n";

    const pdf::PDFFontCMap parsedCMap = pdf::PDFFontCMap::createFromData(data);

    // The same answers after a serialization (the font cache of the predefined CMaps keeps them so)
    const pdf::PDFFontCMap deserializedCMap = pdf::PDFFontCMap::deserialize(parsedCMap.serialize());

    for (const pdf::PDFFontCMap* cmap : { &parsedCMap, &deserializedCMap })
    {
        // 0x25 is in both ranges: the range of the first code 10 is first
        QCOMPARE(cmap->getToUnicode(0x25, 1), QChar(0x0115));
        QCOMPARE(cmap->getToUnicode(0x25, 0), QChar(0x0115));
        QCOMPARE(cmap->getToUnicodeText(0x25, 0), QString(QChar(0x0115)));

        // 0x35 is in the second range only
        QCOMPARE(cmap->getToUnicode(0x35, 1), QChar(0x0035));

        // 0x41 is in the second range and in its own entry: the range is first (first code 20)
        QCOMPARE(cmap->getToUnicode(0x41, 1), QChar('A'));
        QCOMPARE(cmap->getToUnicodeText(0x41, 0), QStringLiteral("A"));

        // 0x05: no one-byte entry, the two-byte one for any byte count, nothing for one byte
        QCOMPARE(cmap->getToUnicode(0x05, 0), QChar('Z'));
        QCOMPARE(cmap->getToUnicode(0x05, 2), QChar('Z'));
        QVERIFY(cmap->getToUnicode(0x05, 1).isNull());

        // Codes of no entry
        QVERIFY(cmap->getToUnicode(0x0F, 0).isNull());
        QVERIFY(cmap->getToUnicode(0x7F, 1).isNull());
        QVERIFY(cmap->getToUnicodeText(0x7F, 0).isEmpty());

        // The texts of 0x42: the first one of the CMap of one byte for one byte and for any byte
        // count, the one of two bytes for two bytes
        QCOMPARE(cmap->getToUnicodeText(0x42, 1), QStringLiteral("fi"));
        QCOMPARE(cmap->getToUnicodeText(0x42, 0), QStringLiteral("fi"));
        QCOMPARE(cmap->getToUnicodeText(0x42, 2), QStringLiteral("ff"));

        // The character of 0x42 is the one of the second range, which is before the entry <42>
        QCOMPARE(cmap->getToUnicode(0x42, 1), QChar('B'));
    }
}

QTEST_MAIN(FontEncodingTest)

#include "tst_fontencodingtest.moc"
