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
#include <QThread>

#include <barrier>
#include <map>
#include <tuple>
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

QTEST_MAIN(FontEncodingTest)

#include "tst_fontencodingtest.moc"
