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
#include "pdfparser.h"
#include <QFontDatabase>
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfeditorfallbackfont.h"

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
    void test_type0_cjk_collection_face();
    void test_type0_cjk_collection_coverage_data();
    void test_type0_cjk_collection_coverage();
    void test_simple_font_encode();
    void test_type3_font_encode();
    void test_fallback_font_generator();
    void test_nested_stream_extraction();
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
                              std::vector<pdf::PDFInteger>(256, 500), pdf::PDFEncoding::Encoding::Custom,
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

QTEST_MAIN(FontEncodingTest)

#include "tst_fontencodingtest.moc"
