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

#include <QtTest/QtTest>

#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfobject.h"
#include "pdfpage.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdffont.h"
#include "pdfoptionalcontent.h"
#include "pdfrenderer.h"
#include "pdftextlayout.h"
#include "pdftextlayoutgenerator.h"

class ActualTextTest : public QObject
{
    Q_OBJECT

private slots:
    void test_actualTextRepairsGlyphText();
    void test_actualTextSameLengthFastPath();
    void test_markedContentWithoutActualText();
    void test_noMarkedContent();
    void test_shrinkingPath();
    void test_nestedSpans();
    void test_namedProperties();
    void test_rotatedText();
    void test_emptyActualText();
    void test_unbalancedEMC();
    void test_geometryPreserved();
};

namespace
{

/// Builds a one-page document whose page content stream is \p content,
/// with a standard Helvetica font registered as /F1 in page resources.
/// Optional \p propertiesDict provides a /Properties resource dictionary
/// for named property lookups.
pdf::PDFDocument buildDocument(QByteArray content,
                               pdf::PDFDictionary* propertiesDict = nullptr)
{
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference pageRef = builder.appendPage(QRectF(0, 0, 200, 200));

    pdf::PDFDictionary fontDict;
    pdf::PDFDictionary f1;
    f1.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Font"));
    f1.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Type1"));
    f1.addEntry(pdf::PDFInplaceOrMemoryString("BaseFont"), pdf::PDFObject::createName("Helvetica"));
    fontDict.addEntry(pdf::PDFInplaceOrMemoryString("F1"),
                      pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(f1))));

    pdf::PDFDictionary resources;
    resources.addEntry(pdf::PDFInplaceOrMemoryString("Font"),
                       pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(fontDict))));

    if (propertiesDict)
    {
        resources.addEntry(pdf::PDFInplaceOrMemoryString("Properties"),
                           pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(*propertiesDict))));
    }

    pdf::PDFDictionary contentDict;
    contentDict.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH),
                         pdf::PDFObject::createInteger(content.size()));
    pdf::PDFStream contentStream(std::move(contentDict), std::move(content));
    pdf::PDFObjectReference contentRef = builder.addObject(
        pdf::PDFObject::createStream(std::make_shared<pdf::PDFStream>(contentStream)));

    pdf::PDFDictionary pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"),
                        pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(resources))));
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentRef));

    builder.mergeTo(pageRef, pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(pageUpdate))));

    return builder.build();
}

/// Runs the text layout generator on the first page and concatenates the
/// extracted characters in visual (block/line) order.
QString extractLayoutText(pdf::PDFDocument& document)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(0);

    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(32, 32);
    pdf::PDFOptionalContentActivity activity(&document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(&document, &activity));

    pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::None, page, &document, &fontCache, &cms, &activity,
                                          QTransform(), pdf::PDFMeshQualitySettings());
    generator.processContents();

    pdf::PDFTextLayout layout = generator.createTextLayout();

    QString text;
    for (const pdf::PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const pdf::PDFTextLine& line : block.getLines())
        {
            for (const pdf::TextCharacter& character : line.getCharacters())
            {
                text += character.character;
            }
        }
    }
    return text;
}

/// Returns the characters from the layout in visual order, for geometry checks.
QVector<pdf::TextCharacter> extractLayoutCharacters(pdf::PDFDocument& document)
{
    const pdf::PDFPage* page = document.getCatalog()->getPage(0);

    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(32, 32);
    pdf::PDFOptionalContentActivity activity(&document, pdf::OCUsage::View, nullptr);
    fontCache.setDocument(pdf::PDFModifiedDocument(&document, &activity));

    pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::None, page, &document, &fontCache, &cms, &activity,
                                          QTransform(), pdf::PDFMeshQualitySettings());
    generator.processContents();

    pdf::PDFTextLayout layout = generator.createTextLayout();

    QVector<pdf::TextCharacter> characters;
    for (const pdf::PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const pdf::PDFTextLine& line : block.getLines())
        {
            for (const pdf::TextCharacter& character : line.getCharacters())
            {
                characters.append(character);
            }
        }
    }
    return characters;
}

}   // namespace

void ActualTextTest::test_actualTextRepairsGlyphText()
{
    // The content stream shows two glyphs ("xy") but the marked-content span
    // declares the authoritative text "abc" as BOM'd UTF-16BE. The layout must
    // replace the glyph range with the /ActualText payload (different length:
    // exercises the character-list rebuild path in PDFTextLayout::replaceCharacters).
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "/Span << /ActualText <FEFF006100620063> >> BDC\n"
        "(xy) Tj\n"
        "EMC\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    QCOMPARE(extractLayoutText(document), QStringLiteral("abc"));
}

void ActualTextTest::test_actualTextSameLengthFastPath()
{
    // Same-length replacement (3 glyphs, 3 characters): the in-place fast path
    // in PDFTextLayout::replaceCharacters must keep geometry untouched.
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "/Span << /ActualText <FEFF006100620063> >> BDC\n"
        "(abc) Tj\n"
        "EMC\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    QCOMPARE(extractLayoutText(document), QStringLiteral("abc"));
}

void ActualTextTest::test_markedContentWithoutActualText()
{
    // Marked content without an /ActualText property must be a no-op: the
    // span is pushed and popped, but no replacement happens.
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "/Span << /SomeProperty 42 >> BDC\n"
        "(xy) Tj\n"
        "EMC\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    QCOMPARE(extractLayoutText(document), QStringLiteral("xy"));
}

void ActualTextTest::test_noMarkedContent()
{
    // Baseline: no marked content at all, extraction must be unchanged.
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "(xy) Tj\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    QCOMPARE(extractLayoutText(document), QStringLiteral("xy"));
}

void ActualTextTest::test_shrinkingPath()
{
    // Shrinking path: 5 glyphs "abcde" with /ActualText "ab" (decomposed-yeh case).
    // The replacement is shorter than the glyph range.
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "/Span << /ActualText <FEFF00610062> >> BDC\n"
        "(abcde) Tj\n"
        "EMC\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    QCOMPARE(extractLayoutText(document), QStringLiteral("ab"));
}

void ActualTextTest::test_nestedSpans()
{
    // Two nested BDC/EMC spans, both with /ActualText. The inner span replaces
    // its glyphs first, then the outer span replaces its range (which now
    // includes the inner replacement).
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "/Span << /ActualText <FEFF004100420043> >> BDC\n"
        "(xy) Tj\n"
        "/Span << /ActualText <FEFF00580059> >> BDC\n"
        "(z) Tj\n"
        "EMC\n"
        "EMC\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    // Inner span: "z" -> "XY", outer span: "xy" (now "XY" after inner) -> "ABC"
    QCOMPARE(extractLayoutText(document), QStringLiteral("ABC"));
}

void ActualTextTest::test_namedProperties()
{
    // Named /Properties: /Span /P1 BDC where P1 is resolved through /Properties resource dict.
    pdf::PDFDictionary propertiesDict;
    pdf::PDFDictionary p1Dict;
    p1Dict.addEntry(pdf::PDFInplaceOrMemoryString("ActualText"),
                    pdf::PDFObject::createString(QByteArray::fromHex("FEFF006100620063")));
    propertiesDict.addEntry(pdf::PDFInplaceOrMemoryString("P1"),
                            pdf::PDFObject::createDictionary(std::make_shared<pdf::PDFDictionary>(std::move(p1Dict))));

    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "/Span /P1 BDC\n"
        "(xy) Tj\n"
        "EMC\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content, &propertiesDict);

    QCOMPARE(extractLayoutText(document), QStringLiteral("abc"));
}

void ActualTextTest::test_rotatedText()
{
    // Rotated text: 180° rotation matrix. Verify geometry is preserved.
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "1 0 0 1 0 0 cm\n"  // identity first
        "-1 0 0 -1 0 0 cm\n" // then 180° rotation
        "/Span << /ActualText <FEFF00610062> >> BDC\n"
        "(xy) Tj\n"
        "EMC\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    QCOMPARE(extractLayoutText(document), QStringLiteral("ab"));

    // Verify geometry: characters should have positions
    QVector<pdf::TextCharacter> characters = extractLayoutCharacters(document);
    QCOMPARE(characters.size(), 2);
    // With 180° rotation, x positions should be decreasing (right to left)
    QVERIFY(characters[0].position.x() > characters[1].position.x());
}

void ActualTextTest::test_emptyActualText()
{
    // Empty /ActualText: /ActualText <FEFF> BDC — verify the glyph range is removed from extraction.
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "(before) Tj\n"
        "/Span << /ActualText <FEFF> >> BDC\n"
        "(xy) Tj\n"
        "EMC\n"
        "(after) Tj\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    // "before" + "" (removed) + "after" = "beforeafter"
    QCOMPARE(extractLayoutText(document), QStringLiteral("beforeafter"));
}

void ActualTextTest::test_unbalancedEMC()
{
    // Unbalanced EMC: extra EMC without matching BDC — verify no crash.
    // The generator's m_actualTextSpans stack is empty, so the extra EMC is a no-op.
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "(xy) Tj\n"
        "EMC\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    // Should not crash; extraction should be unchanged
    QCOMPARE(extractLayoutText(document), QStringLiteral("xy"));
}

void ActualTextTest::test_geometryPreserved()
{
    // Geometry assertions: verify that a ligature expansion places characters
    // at increasing x positions.
    const QByteArray content =
        "BT\n"
        "/F1 12 Tf\n"
        "50 50 Td\n"
        "/Span << /ActualText <FEFF0061006200630064> >> BDC\n"
        "(x) Tj\n"  // single glyph -> 4 characters
        "EMC\n"
        "ET\n";

    pdf::PDFDocument document = buildDocument(content);

    QCOMPARE(extractLayoutText(document), QStringLiteral("abcd"));

    // Verify geometry: characters should have increasing x positions
    QVector<pdf::TextCharacter> characters = extractLayoutCharacters(document);
    QCOMPARE(characters.size(), 4);

    for (int i = 1; i < characters.size(); ++i)
    {
        QVERIFY2(characters[i].position.x() > characters[i-1].position.x(),
                 qPrintable(QString("Character %1 x=%2 should be > character %3 x=%4")
                            .arg(i).arg(characters[i].position.x())
                            .arg(i-1).arg(characters[i-1].position.x())));
    }
}

QTEST_MAIN(ActualTextTest)

#include "tst_actualtexttest.moc"