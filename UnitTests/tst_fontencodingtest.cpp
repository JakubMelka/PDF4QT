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
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfeditorfallbackfont.h"

class FontEncodingTest : public QObject
{
    Q_OBJECT

private slots:
    void test_cmap_encode_roundtrip();
    void test_type0_encode_invariant();
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
    pdf::PDFDictionary fontDictionary;

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
    pdf::PDFDictionary fontDictionary;

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
