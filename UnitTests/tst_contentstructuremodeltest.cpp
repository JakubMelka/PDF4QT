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

// Edit Content, stage E0: the structure tree as the editor reads it, the consistency of the
// structure with the edits of the content and the writers of the number tree and of the
// structure (package P04; GUI_overhaul/edit_tool/zadani_jadro.md 4.12, zadani_overeni.md 6.1, 6.2,
// chapter 8).
//
// Placeholder of the skeleton: the single function checks only the plain types and the limits of
// pdfcontentstructure.h and pdfnumbertreewriter.h and never calls a function of the package (they
// are stubs ending the process). Package P04 replaces it by its tests. The expected values are
// written by hand from the plan, never taken from a run.

#include "pdfcontentstructure.h"
#include "pdfnumbertreewriter.h"
#include "pdfstructuretreewriter.h"

#include <QtTest>

using namespace pdf;

class ContentStructureModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void skeletonPlainTypes();
};

void ContentStructureModelTest::skeletonPlainTypes()
{
    // A default child of an element names nothing: no element, no MCID, no object
    PDFContentStructureChild child;
    QVERIFY(child.kind == PDFContentStructureChild::Kind::Element);
    QCOMPARE(child.element, PDFContentStructureElementId(-1));
    QCOMPARE(child.mcid, PDFInteger(-1));
    QVERIFY(!child.page.isValid());
    QVERIFY(!child.stream.isValid());
    QVERIFY(!child.streamOwner.isValid());
    QVERIFY(!child.object.isValid());

    // A default element is no element of a model (the root has the identifier 0)
    PDFContentStructureElement element;
    QCOMPARE(element.id, PDFContentStructureElementId(-1));
    QCOMPARE(element.parent, PDFContentStructureElementId(-1));
    QVERIFY(!element.reference.isValid());
    QVERIFY(!element.title.has_value());
    QVERIFY(!element.alternateText.has_value());
    QVERIFY(!element.actualText.has_value());
    QVERIFY(element.children.empty());
    QVERIFY(!element.isModified);

    // A default change of the structure changes nothing and names nothing
    PDFContentStructureChange change;
    QVERIFY(change.action == PDFContentStructureChange::Action::None);
    QCOMPARE(change.structuralParentKey, PDFInteger(-1));
    QCOMPARE(change.mcid, PDFInteger(-1));
    QCOMPARE(change.newMcid, PDFInteger(-1));
    QCOMPARE(change.element, PDFContentStructureElementId(-1));
    QVERIFY(change.text.isEmpty());

    // An item of the parent tree, which the model does not own, is written back unchanged: it
    // is the default kind, so nothing unknown is ever taken for an array of elements
    PDFContentParentTreeItem item;
    QVERIFY(item.kind == PDFContentParentTreeItem::Kind::Foreign);
    QVERIFY(item.elements.empty());
    QVERIFY(item.raw.isNull());
    QVERIFY(!item.isModified);

    // The result of writing an unmodified model: nothing written, no error
    PDFStructureTreeWriteResult result;
    QVERIFY(!result.isWritten);
    QVERIFY(result.errors.empty());

    // The limits: /ParentTree is read to the depth 32 (zadani_overeni.md 6.1), an MCID is a
    // value of 0 ... 2^31 - 1 (6.2), the leaves of a written number tree have 64 keys
    // (zadani_jadro.md 4.12)
    QCOMPARE(PDFContentStructureModel::MAXIMUM_NUMBER_TREE_DEPTH, 32);
    QCOMPARE(PDFContentStructureModel::MAXIMUM_MARKED_CONTENT_ID, PDFInteger(2147483647));
    QCOMPARE(PDFNumberTreeWriter::KEYS_PER_LEAF, size_t(64));
}

QTEST_MAIN(ContentStructureModelTest)

#include "tst_contentstructuremodeltest.moc"
