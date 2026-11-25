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
// Placeholder of the skeleton: the single function checks only the plain types of
// pdfcontenttextlayout.h and the state of a new layout, which have real code in the skeleton, and
// never calls a function of the package (they are stubs ending the process). Package P06 replaces
// it by its tests. The expected values are written by hand from the standard, never taken from a
// run.

#include "pdfcontenttextlayout.h"

#include <QtTest>
#include <QTransform>

using namespace pdf;

class ContentTextLayoutTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void skeletonPlainTypes();
};

void ContentTextLayoutTest::skeletonPlainTypes()
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

    // A code of a string before it is decoded
    PDFContentTextLayout::Code code;
    QVERIFY(code.bytes.isEmpty());
    QCOMPARE(code.code, 0u);
    QCOMPARE(code.cid, CID(0));

    // A new layout is at the beginning of a text object: the text matrix and the text line matrix
    // are the identity matrix (9.4.1), and nothing was shown without a width yet
    PDFContentTextLayout layout;
    QVERIFY(layout.getTextMatrix().isIdentity());
    QVERIFY(layout.getTextLineMatrix().isIdentity());
    QVERIFY(layout.isComplete());
}

QTEST_MAIN(ContentTextLayoutTest)

#include "tst_contenttextlayouttest.moc"
