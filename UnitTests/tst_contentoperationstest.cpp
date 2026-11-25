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

// Edit Content, stage E0: the operations of a content, its resources, the writer of a content
// stream and the ExtGState dictionaries of the editor (package P01; GUI_overhaul/edit_tool/
// zadani_jadro.md 4.2, 4.4, zadani_overeni.md chapter 8).
//
// Placeholder of the skeleton: the single function checks only the plain types of
// pdfcontentoperation.h, which have real code in the skeleton, and never calls a function of the
// package (they are stubs ending the process). Package P01 replaces it by its tests. The expected
// values are written by hand from the plan and from ISO 32000-2, never taken from a run.

#include "pdfcontentoperation.h"

#include <QtTest>

#include <vector>

using namespace pdf;

class ContentOperationsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void skeletonPlainTypes();
};

void ContentOperationsTest::skeletonPlainTypes()
{
    // A position is a stream of the content (an index from 0) and an offset in its decoded data;
    // the default one names no stream
    QVERIFY(!PDFContentStreamPosition().isValid());
    QVERIFY((PDFContentStreamPosition{ 0, 0 }).isValid());
    QVERIFY((PDFContentStreamPosition{ 3, 120 }).isValid());
    QVERIFY(!(PDFContentStreamPosition{ -1, 0 }).isValid());
    QVERIFY(!(PDFContentStreamPosition{ 0, -1 }).isValid());

    // The streams of /Contents follow each other (ISO 32000-2, 7.8.2): a position of a later stream
    // is behind every position of an earlier one, inside of a stream the offset decides
    QVERIFY((PDFContentStreamPosition{ 0, 500 }) < (PDFContentStreamPosition{ 1, 0 }));
    QVERIFY((PDFContentStreamPosition{ 1, 0 }) < (PDFContentStreamPosition{ 1, 1 }));
    QVERIFY((PDFContentStreamPosition{ 1, 1 }) > (PDFContentStreamPosition{ 0, 500 }));
    QVERIFY((PDFContentStreamPosition{ 2, 7 }) == (PDFContentStreamPosition{ 2, 7 }));
    QVERIFY((PDFContentStreamPosition{ 2, 7 }) != (PDFContentStreamPosition{ 2, 8 }));
    QVERIFY(!((PDFContentStreamPosition{ 2, 7 }) < (PDFContentStreamPosition{ 2, 7 })));
    QVERIFY((PDFContentStreamPosition{ 2, 7 }) <= (PDFContentStreamPosition{ 2, 7 }));

    // A range: from the end of the previous operation to the end of the operator. The operands
    // may begin in one stream and the operator may be in the next one (zadani_jadro.md 4.2), so a
    // range over the boundary of two streams is valid; an empty range too; a reversed one is not
    QVERIFY(!PDFContentByteRange().isValid());
    QVERIFY((PDFContentByteRange{ { 0, 3 }, { 0, 9 } }).isValid());
    QVERIFY((PDFContentByteRange{ { 0, 90 }, { 1, 2 } }).isValid());
    QVERIFY((PDFContentByteRange{ { 0, 4 }, { 0, 4 } }).isValid());
    QVERIFY(!(PDFContentByteRange{ { 1, 2 }, { 0, 90 } }).isValid());
    QVERIFY(!(PDFContentByteRange{ { 0, 9 }, { 0, 3 } }).isValid());
    QVERIFY(!(PDFContentByteRange{ { -1, 0 }, { 0, 3 } }).isValid());
    QVERIFY(!(PDFContentByteRange{ { 0, 3 }, { 0, -1 } }).isValid());
    QVERIFY((PDFContentByteRange{ { 0, 3 }, { 0, 9 } }) == (PDFContentByteRange{ { 0, 3 }, { 0, 9 } }));
    QVERIFY(!((PDFContentByteRange{ { 0, 3 }, { 0, 9 } }) == (PDFContentByteRange{ { 0, 3 }, { 1, 9 } })));

    // A default operation is nothing of a document and nothing of the editor: an unknown operator
    // without bytes, operands and flags - not modified (only Generated and Suppressed are)
    PDFContentOperation operation;
    QVERIFY(operation.getOperator() == PDFContentOperator::Invalid);
    QVERIFY(operation.getRawBytes().isEmpty());
    QVERIFY(!operation.getRange().isValid());
    QVERIFY(operation.getOperands().empty());
    QVERIFY(operation.getResourceNames().empty());
    QVERIFY(operation.getFlags() == PDFContentOperation::Flags(PDFContentOperation::None));
    QVERIFY(!operation.isModified());

    // The categories of the named resources of a content: the subdictionaries of a resource
    // dictionary (ISO 32000-2, 7.8.3, Table 34) without /ProcSet, which is an array of names and
    // names no resource of an operator - seven of them, in the order of zadani_jadro.md 4.2
    const std::vector<PDFContentResourceCategory> categories = { PDFContentResourceCategory::Font, PDFContentResourceCategory::XObject,
                                                                 PDFContentResourceCategory::ExtGState, PDFContentResourceCategory::ColorSpace,
                                                                 PDFContentResourceCategory::Pattern, PDFContentResourceCategory::Shading,
                                                                 PDFContentResourceCategory::Properties };
    QCOMPARE(categories.size(), size_t(7));
    for (size_t i = 0; i < categories.size(); ++i)
    {
        QCOMPARE(int(categories[i]), int(i));
    }

    // A resource name belongs to its category: the same name of two categories are two
    // resources; the names are ordered by the category first, by the bytes of the name then
    const PDFContentResourceName font{ PDFContentResourceCategory::Font, QByteArrayLiteral("R1") };
    const PDFContentResourceName xobject{ PDFContentResourceCategory::XObject, QByteArrayLiteral("R1") };
    QVERIFY(PDFContentResourceName().category == PDFContentResourceCategory::Font);
    QVERIFY(PDFContentResourceName().name.isEmpty());
    QVERIFY(font != xobject);
    QVERIFY(font == (PDFContentResourceName{ PDFContentResourceCategory::Font, QByteArrayLiteral("R1") }));
    QVERIFY((PDFContentResourceName{ PDFContentResourceCategory::Font, QByteArrayLiteral("Z") }) < (PDFContentResourceName{ PDFContentResourceCategory::XObject, QByteArrayLiteral("A") }));
    QVERIFY((PDFContentResourceName{ PDFContentResourceCategory::Font, QByteArrayLiteral("F1") }) < (PDFContentResourceName{ PDFContentResourceCategory::Font, QByteArrayLiteral("F2") }));
    QVERIFY((PDFContentResourceName{ PDFContentResourceCategory::Properties, QByteArrayLiteral("MC1") }) > (PDFContentResourceName{ PDFContentResourceCategory::Shading, QByteArrayLiteral("Sh9") }));
}

QTEST_MAIN(ContentOperationsTest)

#include "tst_contentoperationstest.moc"
