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

// PDFStructureTree (SECURITY_TODO.md item 66): the children of /K, which are integers, and the
// items of /ParentTree, which are not references.
//
// ISO 32000-2:
//  - 14.7.2, Table 354: the children of the structure tree root are structure elements;
//  - Table 355, /K: a child is a structure element, an integer marked-content identifier, a
//    marked-content reference or an object reference, alone or in an array in any combination;
//    /Pg is "required if K is an integer object or an array containing integer objects" - it is
//    the entry of the element itself, nothing says that it is inherited;
//  - 14.7.5.2, Table 357: an integer is the identifier of a sequence in the content stream of
//    the page of /Pg of the structure element;
//  - 14.7.5.4, Table 359: the value of the parent tree for a content stream is an array, "the
//    array element corresponding to each sequence shall be found by using the sequence's
//    marked-content identifier as a zero-based index into the array"; for an object
//    (/StructParent) it is a reference to the element;
//  - 7.9.7, Table 37: the number tree; 7.3.10: an indirect object as the value of an entry.
//
// The documents are written by hand (PDF text of numbered objects, mergetestpdf.h) and the
// expected values are derived from their text and from the standard, never taken from a run.

#include "contenttestdocuments.h"
#include "mergetestpdf.h"
#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdocumenttextflow.h"
#include "pdfexception.h"
#include "pdfobject.h"
#include "pdfstructuretree.h"

#include <QtTest>

#include <vector>

using namespace pdf;
using mergetest::MergeTestPdf;

/// Collects the marked-content identifiers in the order of the visits: what a user of the
/// visitor, which handles the marked-content references, gets
class StructureTreeTestIdentifierVisitor : public PDFStructureTreeAbstractVisitor
{
public:
    virtual void visitStructureMarkedContentReference(const PDFStructureMarkedContentReference* structureMarkedContentReference) override
    {
        identifiers.push_back(structureMarkedContentReference->getMarkedContentIdentifier());
    }

    std::vector<PDFInteger> identifiers;
};

/// Helpers of the test
class StructureTreeTestHelper
{
public:
    StructureTreeTestHelper() = delete;

    /// The catalog 1 with \p catalogEntries, the page tree 2 and the pages 3 and 4 (media box
    /// [0 0 200 200], no content)
    static MergeTestPdf createPages(const QByteArray& catalogEntries)
    {
        MergeTestPdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R " + catalogEntries + " >>");
        pdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 200 200] /Resources << >> >>");
        pdf.set(3, "<< /Type /Page /Parent 2 0 R >>");
        pdf.set(4, "<< /Type /Page /Parent 2 0 R >>");
        return pdf;
    }

    /// The pages of createPages
    static std::vector<PDFObjectReference> getPages()
    {
        return { PDFObjectReference(3, 0), PDFObjectReference(4, 0) };
    }

    static PDFStructureTree parse(const PDFDocument& document)
    {
        return PDFStructureTree::parse(&document.getStorage(), document.getCatalog()->getStructureTreeRoot());
    }

    /// "p1" for the first of \p pages, "p2" for the second, "-" for no page, "?" for another object
    static QString describePage(PDFObjectReference page, const std::vector<PDFObjectReference>& pages)
    {
        if (!page.isValid())
        {
            return QStringLiteral("-");
        }

        for (size_t i = 0; i < pages.size(); ++i)
        {
            if (pages[i] == page)
            {
                return QStringLiteral("p%1").arg(i + 1);
            }
        }

        return QStringLiteral("?");
    }

    /// The items below \p item in their order: an element is its type with its kids in brackets
    /// ("P[...]"), a marked content is "mcid:<identifier>@<page>" with "+stm" for a content stream
    /// of its own, an object reference is "objr"
    static QString describe(const PDFStructureItem* item, const std::vector<PDFObjectReference>& pages)
    {
        QStringList parts;
        for (size_t i = 0; i < item->getChildCount(); ++i)
        {
            const PDFStructureItem* child = item->getChild(i);
            if (const PDFStructureElement* element = child->asStructureElement())
            {
                const QString name = QString::fromLatin1(element->getTypeName());
                parts << (child->getChildCount() > 0 ? name + QLatin1Char('[') + describe(child, pages) + QLatin1Char(']') : name);
            }
            else if (const PDFStructureMarkedContentReference* markedContent = child->asStructureMarkedContentReference())
            {
                parts << QStringLiteral("mcid:%1@%2%3").arg(markedContent->getMarkedContentIdentifier())
                                                       .arg(describePage(markedContent->getPageReference(), pages),
                                                            markedContent->getContentStreamReference().isValid() ? QStringLiteral("+stm") : QString());
            }
            else if (child->asStructureObjectReference())
            {
                parts << QStringLiteral("objr");
            }
            else
            {
                parts << QStringLiteral("?");
            }
        }

        return parts.join(QLatin1Char(' '));
    }

    /// The first kid of \p item, which is a structure element (nullptr: none)
    static const PDFStructureElement* getFirstElement(const PDFStructureItem* item)
    {
        for (size_t i = 0; i < item->getChildCount(); ++i)
        {
            if (const PDFStructureElement* element = item->getChild(i)->asStructureElement())
            {
                return element;
            }
        }

        return nullptr;
    }

    /// The text flow of the document in the order of its structure
    static PDFDocumentTextFlow createStructureFlow(const PDFDocument& document, QStringList* errors)
    {
        PDFDocumentTextFlowFactory factory;
        PDFDocumentTextFlow flow = factory.create(&document, PDFDocumentTextFlowFactory::Algorithm::Structure);
        for (const PDFRenderError& error : factory.getErrors())
        {
            errors->append(error.message);
        }
        return flow;
    }

    /// The items of the flow with a text as "<kind>:<text>", the kind is "text" or "alt"
    /// (an alternative description) or "other"
    static QStringList describeFlow(const PDFDocumentTextFlow& flow)
    {
        QStringList result;
        for (const PDFDocumentTextFlow::Item& item : flow.getItems())
        {
            const QString text = item.text.simplified();
            if (text.isEmpty())
            {
                continue;
            }

            QString kind = QStringLiteral("other");
            if (item.flags.testFlag(PDFDocumentTextFlow::Text))
            {
                kind = QStringLiteral("text");
            }
            else if (item.flags.testFlag(PDFDocumentTextFlow::StructureAlternativeDescription))
            {
                kind = QStringLiteral("alt");
            }
            result << kind + QLatin1Char(':') + text;
        }
        return result;
    }
};

class StructureTreeTest : public QObject
{
    Q_OBJECT

private slots:
    void integerKids();
    void pageOfIntegerKid();
    void kidsOfOtherTypes();
    void kidsInIndirectArray();
    void kidsOfTaggedDocument();
    void parentTreeIndices();
    void parentTreeOfTaggedDocument();
    void parentTreeKeyListedTwice();
    void cycle();
    void sharedElement();
    void depthLimit();
    void noStructureTree();
    void textByStructureAfterNull();
    void textByStructureOfTaggedDocument();
};

void StructureTreeTest::integerKids()
{
    using Helper = StructureTreeTestHelper;

    // Table 355, /K: an integer alone (P), integers in an array mixed with a marked-content
    // reference, a structure element and an object reference (H1), an integer, which is an
    // indirect object (16 0 R, listed by two elements and twice by one of them: it is a value,
    // not a node of the tree). The page of an integer is the page of /Pg of its element
    // (14.7.5.2): the page 3 for P and L, the page 4 for H1; the marked-content reference of H1
    // has its own /Pg 3 0 R (Table 357).
    MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R /MarkInfo << /Marked true >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K 11 0 R >>");
    pdf.set(11, "<< /Type /StructElem /S /Document /P 10 0 R /K [12 0 R 13 0 R 14 0 R] >>");
    pdf.set(12, "<< /Type /StructElem /S /P /P 11 0 R /Pg 3 0 R /K 5 >>");
    pdf.set(13, "<< /Type /StructElem /S /H1 /P 11 0 R /Pg 4 0 R /K [7 << /Type /MCR /Pg 3 0 R /MCID 2 >> 15 0 R << /Type /OBJR /Obj 20 0 R >> 9 16 0 R] >>");
    pdf.set(14, "<< /Type /StructElem /S /L /P 11 0 R /Pg 3 0 R /K [16 0 R 16 0 R] >>");
    pdf.set(15, "<< /Type /StructElem /S /Span /P 13 0 R >>");
    pdf.set(16, "4");
    pdf.set(20, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] >>");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = Helper::parse(document);
    QVERIFY(tree.isValid());
    QCOMPARE(Helper::describe(&tree, Helper::getPages()),
             QStringLiteral("Document[P[mcid:5@p1] H1[mcid:7@p2 mcid:2@p1 Span objr mcid:9@p2 mcid:4@p2] L[mcid:4@p1 mcid:4@p1]]"));

    // The item of the integer 5 of P: a marked content of the page of the element, in the content
    // stream of the page (no /Stm, no /StmOwn), not an object of its own, a leaf below its element
    const PDFStructureItem* documentElement = tree.getChild(0);
    const PDFStructureItem* paragraph = documentElement->getChild(0);
    QCOMPARE(paragraph->getChildCount(), size_t(1));
    const PDFStructureMarkedContentReference* markedContent = paragraph->getChild(0)->asStructureMarkedContentReference();
    QVERIFY(markedContent);
    QCOMPARE(markedContent->getMarkedContentIdentifier(), PDFInteger(5));
    QCOMPARE(markedContent->getPageReference(), PDFObjectReference(3, 0));
    QVERIFY(!markedContent->getContentStreamReference().isValid());
    QVERIFY(!markedContent->getContentStreamOwnerReference().isValid());
    QVERIFY(!markedContent->getSelfReference().isValid());
    QCOMPARE(markedContent->getChildCount(), size_t(0));
    QCOMPARE(markedContent->getParent(), paragraph);
    QCOMPARE(markedContent->getTree(), &tree);
    QVERIFY(!markedContent->asStructureElement());

    // The object reference of H1 keeps its place between the integers
    const PDFStructureObjectReference* objectReference = documentElement->getChild(1)->getChild(3)->asStructureObjectReference();
    QVERIFY(objectReference);
    QCOMPARE(objectReference->getObjectReference(), PDFObjectReference(20, 0));

    // A visitor, which handles the marked-content references, gets the integers in the order of /K
    StructureTreeTestIdentifierVisitor visitor;
    tree.accept(&visitor);
    const std::vector<PDFInteger> expectedIdentifiers = { 5, 7, 2, 9, 4, 4, 4 };
    QVERIFY(visitor.identifiers == expectedIdentifiers);
}

void StructureTreeTest::pageOfIntegerKid()
{
    using Helper = StructureTreeTestHelper;

    // /Pg of Table 355 is the entry of the element, which lists the integer ("required if K is
    // an integer object or an array containing integer objects"); the standard does not make it
    // inherited. The integers of the second P (/Pg 4 0 R) and of Sect (/Pg 3 0 R) have the pages
    // of their own elements; the first P has no /Pg, so its integer 1 has no page - not the page
    // of Sect above it. Its marked-content reference without /Pg has no page either (Table 357:
    // the page of /Pg of the reference or of the element, none of them has it), the one with
    // /Pg 4 0 R has the page 4.
    MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R /MarkInfo << /Marked true >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K 11 0 R >>");
    pdf.set(11, "<< /Type /StructElem /S /Sect /P 10 0 R /Pg 3 0 R /K [12 0 R 13 0 R 0] >>");
    pdf.set(12, "<< /Type /StructElem /S /P /P 11 0 R /K [1 << /Type /MCR /MCID 2 >> << /Type /MCR /Pg 4 0 R /MCID 3 >>] >>");
    pdf.set(13, "<< /Type /StructElem /S /P /P 11 0 R /Pg 4 0 R /K 1 >>");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = Helper::parse(document);
    QCOMPARE(Helper::describe(&tree, Helper::getPages()),
             QStringLiteral("Sect[P[mcid:1@- mcid:2@- mcid:3@p2] P[mcid:1@p2] mcid:0@p1]"));
}

void StructureTreeTest::kidsOfOtherTypes()
{
    using Helper = StructureTreeTestHelper;

    // Table 355 lists four kinds of the kids. A name, a string, a real number, null, a boolean,
    // an array inside of the array and an indirect string (17 0 R) are none of them: they are
    // not kids. The integers 1 and 8 around them are.
    MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R /MarkInfo << /Marked true >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K [11 0 R] >>");
    pdf.set(11, "<< /Type /StructElem /S /P /P 10 0 R /Pg 3 0 R /K [1 /Name (string) 2.5 null true [3] 17 0 R 8] >>");
    pdf.set(17, "(indirect string)");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = Helper::parse(document);
    QCOMPARE(Helper::describe(&tree, Helper::getPages()), QStringLiteral("P[mcid:1@p1 mcid:8@p1]"));
}

void StructureTreeTest::kidsInIndirectArray()
{
    using Helper = StructureTreeTestHelper;

    // 7.3.10: any object may be an indirect object, referred to "as the value of a dictionary
    // entry" - also the array of /K. The root has /K 18 0 R = [11 0 R], the paragraph has
    // /K 19 0 R = [6 12 0 R 7]: the same kids as if the arrays were written in the dictionaries.
    MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R /MarkInfo << /Marked true >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K 18 0 R >>");
    pdf.set(11, "<< /Type /StructElem /S /P /P 10 0 R /Pg 3 0 R /K 19 0 R >>");
    pdf.set(12, "<< /Type /StructElem /S /Span /P 11 0 R /Pg 3 0 R /K 2 >>");
    pdf.set(18, "[11 0 R]");
    pdf.set(19, "[6 12 0 R 7]");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = Helper::parse(document);
    QVERIFY(tree.isValid());
    QCOMPARE(Helper::describe(&tree, Helper::getPages()), QStringLiteral("P[mcid:6@p1 Span[mcid:2@p1] mcid:7@p1]"));
    QCOMPARE(tree.getChild(0)->getSelfReference(), PDFObjectReference(11, 0));
}

void StructureTreeTest::kidsOfTaggedDocument()
{
    using Helper = StructureTreeTestHelper;

    // The tagged document of contenttestdocuments.h: P /K 0, Span /K << /Type /MCR /MCID 1 >>,
    // Figure /K [3], Link /K << /Type /OBJR >>, all with /Pg of the only page
    const PDFDocument document = pdftest::ContentTestDocuments::createTaggedDocument();
    const PDFObjectReference page = document.getCatalog()->getPage(0)->getPageReference();

    const PDFStructureTree tree = Helper::parse(document);
    QCOMPARE(Helper::describe(&tree, { page }), QStringLiteral("Document[P[mcid:0@p1] Span[mcid:1@p1] Figure[mcid:3@p1] Link[objr]]"));
}

void StructureTreeTest::parentTreeIndices()
{
    using Helper = StructureTreeTestHelper;

    // 14.7.5.4: the array of a content stream is indexed by the marked-content identifier. The
    // parent tree has two leaves:
    //   key 0: [null 12 13]                a null at the start
    //   key 1: [12 5 << /S /P >> 13 null]  an integer and a dictionary, where a reference is expected, a null at the end
    //   key 2: 14 0 R                      the element of an object (/StructParent of the annotation 20)
    //   key 3: 33 0 R = [13 null 12]       the array is an indirect object, a null in the middle
    //   key 4: null, key 5: [], key 7: (text)   no element at all; the key 6 is not in the tree
    MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R /MarkInfo << /Marked true >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K [12 0 R 13 0 R 14 0 R] /ParentTree 30 0 R /ParentTreeNextKey 8 >>");
    pdf.set(12, "<< /Type /StructElem /S /P /P 10 0 R /Pg 3 0 R /K [1 0] >>");
    pdf.set(13, "<< /Type /StructElem /S /P /P 10 0 R /Pg 3 0 R /K [2 3] >>");
    pdf.set(14, "<< /Type /StructElem /S /Link /P 10 0 R /Pg 3 0 R /K << /Type /OBJR /Obj 20 0 R >> >>");
    pdf.set(20, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /StructParent 2 >>");
    pdf.set(30, "<< /Kids [31 0 R 32 0 R] >>");
    pdf.set(31, "<< /Limits [0 1] /Nums [0 [null 12 0 R 13 0 R] 1 [12 0 R 5 << /S /P >> 13 0 R null]] >>");
    pdf.set(32, "<< /Limits [2 7] /Nums [2 14 0 R 3 33 0 R 4 null 5 [] 7 (text)] >>");
    pdf.set(33, "[13 0 R null 12 0 R]");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = Helper::parse(document);
    const PDFObjectReference none;
    const PDFObjectReference first(12, 0);
    const PDFObjectReference second(13, 0);
    const PDFObjectReference link(14, 0);

    // Key 0: the identifier 0 has no element, 1 and 2 have theirs, 3 is behind the array
    QCOMPARE(tree.getParent(0, 0), none);
    QCOMPARE(tree.getParent(0, 1), first);
    QCOMPARE(tree.getParent(0, 2), second);
    QCOMPARE(tree.getParent(0, 3), none);
    QCOMPARE(tree.getParent(0, -1), none);

    // Key 1: the items, which are not references, are no elements and keep their indices
    QCOMPARE(tree.getParent(1, 0), first);
    QCOMPARE(tree.getParent(1, 1), none);
    QCOMPARE(tree.getParent(1, 2), none);
    QCOMPARE(tree.getParent(1, 3), second);
    QCOMPARE(tree.getParent(1, 4), none);
    QCOMPARE(tree.getParent(1, 5), none);

    // Key 2: the element of the object
    QCOMPARE(tree.getParent(2, 0), link);
    QCOMPARE(tree.getParent(2, 1), none);

    // Key 3: the indirect array
    QCOMPARE(tree.getParent(3, 0), second);
    QCOMPARE(tree.getParent(3, 1), none);
    QCOMPARE(tree.getParent(3, 2), first);

    // Keys without an element
    QCOMPARE(tree.getParent(4, 0), none);
    QCOMPARE(tree.getParent(5, 0), none);
    QCOMPARE(tree.getParent(6, 0), none);
    QCOMPARE(tree.getParent(7, 0), none);

    // The whole array of a key: the index of an item is the identifier
    const std::vector<PDFObjectReference> expectedFirstArray = { none, first, second };
    QVERIFY(tree.getParents(0) == expectedFirstArray);
    const std::vector<PDFObjectReference> expectedSecondArray = { first, none, none, second, none };
    QVERIFY(tree.getParents(1) == expectedSecondArray);
    const std::vector<PDFObjectReference> expectedObject = { link };
    QVERIFY(tree.getParents(2) == expectedObject);
    QVERIFY(tree.getParents(4).empty());
    QVERIFY(tree.getParents(6).empty());

    // The entries of all keys in the order of the keys, the items without an element included:
    // 3 of the key 0, 5 of the key 1, 1 of the key 2, 3 of the key 3
    QCOMPARE(tree.getParentTreeEntry(0).id, PDFInteger(0));
    QCOMPARE(tree.getParentTreeEntry(0).reference, none);
    QCOMPARE(tree.getParentTreeEntry(1).reference, first);
    QCOMPARE(tree.getParentTreeEntry(3).id, PDFInteger(1));
    QCOMPARE(tree.getParentTreeEntry(3).reference, first);
    QCOMPARE(tree.getParentTreeEntry(8).id, PDFInteger(2));
    QCOMPARE(tree.getParentTreeEntry(8).reference, link);
    QCOMPARE(tree.getParentTreeEntry(11).id, PDFInteger(3));
    QCOMPARE(tree.getParentTreeEntry(11).reference, first);
    QCOMPARE(tree.getParentTreeEntry(12).reference, none);
    QCOMPARE(tree.getParentTreeEntry(-1).reference, none);
}

void StructureTreeTest::parentTreeOfTaggedDocument()
{
    using Helper = StructureTreeTestHelper;

    // The tagged document of contenttestdocuments.h: /Nums [0 [P Span null Figure] 1 Link], the
    // page has /StructParents 0 and the identifiers 0, 1 and 3, the annotation /StructParent 1
    const PDFDocument document = pdftest::ContentTestDocuments::createTaggedDocument();
    const PDFStructureTree tree = Helper::parse(document);
    QCOMPARE(tree.getChildCount(), size_t(1));
    const PDFStructureItem* documentElement = tree.getChild(0);
    QCOMPARE(documentElement->getChildCount(), size_t(4));

    const PDFObjectReference paragraph = documentElement->getChild(0)->getSelfReference();
    const PDFObjectReference span = documentElement->getChild(1)->getSelfReference();
    const PDFObjectReference figure = documentElement->getChild(2)->getSelfReference();
    const PDFObjectReference link = documentElement->getChild(3)->getSelfReference();
    QVERIFY(paragraph.isValid() && span.isValid() && figure.isValid() && link.isValid());
    QCOMPARE(documentElement->getChild(2)->asStructureElement()->getTypeName(), QByteArray("Figure"));

    const PDFObjectReference none;
    QCOMPARE(tree.getParent(0, 0), paragraph);
    QCOMPARE(tree.getParent(0, 1), span);
    QCOMPARE(tree.getParent(0, 2), none);
    QCOMPARE(tree.getParent(0, 3), figure);
    QCOMPARE(tree.getParent(0, 4), none);
    QCOMPARE(tree.getParent(1, 0), link);
    QCOMPARE(tree.getParent(1, 1), none);
    QCOMPARE(tree.getParent(2, 0), none);

    const std::vector<PDFObjectReference> expectedPageArray = { paragraph, span, none, figure };
    QVERIFY(tree.getParents(0) == expectedPageArray);
}

void StructureTreeTest::parentTreeKeyListedTwice()
{
    using Helper = StructureTreeTestHelper;

    // A number tree has each key once (7.9.7). A key listed twice is one entry for a reader,
    // which looks the key up: here the first one in the order of the tree. The two arrays are
    // never one array - the identifier 1 is not an index of [12 0 R], so it has no element
    // (it must not get the element 13 of the second array).
    MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R /MarkInfo << /Marked true >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K [12 0 R 13 0 R 14 0 R] /ParentTree << /Nums [0 [12 0 R] 0 [13 0 R 14 0 R]] >> >>");
    pdf.set(12, "<< /Type /StructElem /S /P /P 10 0 R /Pg 3 0 R /K 0 >>");
    pdf.set(13, "<< /Type /StructElem /S /P /P 10 0 R /Pg 3 0 R /K 1 >>");
    pdf.set(14, "<< /Type /StructElem /S /P /P 10 0 R /Pg 3 0 R /K 2 >>");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = Helper::parse(document);
    QCOMPARE(tree.getParent(0, 0), PDFObjectReference(12, 0));
    QCOMPARE(tree.getParent(0, 1), PDFObjectReference());
    QCOMPARE(tree.getParents(0).size(), size_t(1));
}

void StructureTreeTest::cycle()
{
    using Helper = StructureTreeTestHelper;

    // Sect lists P and P lists Sect back (an element, which is its own descendant); Div lists
    // itself. An element is read once: the kids, which lead back, are no kids, the integers
    // around them stay (0 of Sect; 1 and 2 of P; 3 of Div).
    MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R /MarkInfo << /Marked true >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K [11 0 R 13 0 R] >>");
    pdf.set(11, "<< /Type /StructElem /S /Sect /P 10 0 R /Pg 3 0 R /K [0 12 0 R] >>");
    pdf.set(12, "<< /Type /StructElem /S /P /P 11 0 R /Pg 3 0 R /K [1 11 0 R 2] >>");
    pdf.set(13, "<< /Type /StructElem /S /Div /P 10 0 R /Pg 3 0 R /K [13 0 R 3] >>");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = Helper::parse(document);
    QCOMPARE(Helper::describe(&tree, Helper::getPages()), QStringLiteral("Sect[mcid:0@p1 P[mcid:1@p1 mcid:2@p1]] Div[mcid:3@p1]"));
}

void StructureTreeTest::sharedElement()
{
    using Helper = StructureTreeTestHelper;

    // Figure is listed by both sections; its /P names the first one (Table 355: an element has
    // one parent). It is read once, below the first section, with its integer; the second section
    // has only its own integer 3 - the figure is not there a second time.
    MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R /MarkInfo << /Marked true >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K [14 0 R 15 0 R] >>");
    pdf.set(14, "<< /Type /StructElem /S /Sect /P 10 0 R /K [16 0 R] >>");
    pdf.set(15, "<< /Type /StructElem /S /Sect /P 10 0 R /Pg 3 0 R /K [16 0 R 3] >>");
    pdf.set(16, "<< /Type /StructElem /S /Figure /P 14 0 R /Pg 4 0 R /Alt (Shared) /K 4 >>");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = Helper::parse(document);
    QCOMPARE(Helper::describe(&tree, Helper::getPages()), QStringLiteral("Sect[Figure[mcid:4@p2]] Sect[mcid:3@p1]"));
    QCOMPARE(tree.getChild(0)->getChild(0)->getSelfReference(), PDFObjectReference(16, 0));
    QCOMPARE(tree.getChild(0)->getChild(0)->getParent(), tree.getChild(0));
}

void StructureTreeTest::depthLimit()
{
    using Helper = StructureTreeTestHelper;

    // A chain of 300 elements Div, each one lists the integer 0 and the next element; the root
    // lists a paragraph behind the chain. The items are read to the depth
    // PDFStructureTree::MAXIMUM_DEPTH (the kids of the root have the depth 1): the chain ends
    // there, the deepest element read has no kids (they would be deeper), and the paragraph
    // behind the chain is read - the limit does not fail the tree.
    constexpr int chainLength = 300;
    constexpr int firstObject = 100;
    QVERIFY(chainLength > PDFStructureTree::MAXIMUM_DEPTH + 1);

    MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R /MarkInfo << /Marked true >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K [" + QByteArray::number(firstObject) + " 0 R 500 0 R] >>");
    for (int i = 0; i < chainLength; ++i)
    {
        const int number = firstObject + i;
        const QByteArray parent = QByteArray::number(i == 0 ? 10 : number - 1) + " 0 R";
        const QByteArray kids = i + 1 < chainLength ? "[0 " + QByteArray::number(number + 1) + " 0 R]" : QByteArray("0");
        pdf.set(number, "<< /Type /StructElem /S /Div /P " + parent + " /Pg 3 0 R /K " + kids + " >>");
    }
    pdf.set(500, "<< /Type /StructElem /S /P /P 10 0 R /Pg 3 0 R /K 5 >>");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = Helper::parse(document);
    QCOMPARE(tree.getChildCount(), size_t(2));

    int depth = 0;
    const PDFStructureItem* item = &tree;
    const PDFStructureItem* parent = nullptr;
    while (const PDFStructureElement* element = Helper::getFirstElement(item))
    {
        parent = item;
        item = element;
        ++depth;
    }
    QCOMPARE(depth, PDFStructureTree::MAXIMUM_DEPTH);
    QCOMPARE(item->getSelfReference(), PDFObjectReference(firstObject + PDFStructureTree::MAXIMUM_DEPTH - 1, 0));
    QCOMPARE(item->getChildCount(), size_t(0));

    // The element above it has both of its kids: the integer and the element
    QVERIFY(parent);
    QCOMPARE(parent->getChildCount(), size_t(2));
    QVERIFY(parent->getChild(0)->asStructureMarkedContentReference());

    // The paragraph behind the chain
    const PDFStructureElement* paragraph = tree.getChild(1)->asStructureElement();
    QVERIFY(paragraph);
    QCOMPARE(paragraph->getSelfReference(), PDFObjectReference(500, 0));
    QCOMPARE(Helper::describe(paragraph, Helper::getPages()), QStringLiteral("mcid:5@p1"));
}

void StructureTreeTest::noStructureTree()
{
    using Helper = StructureTreeTestHelper;
    const PDFObjectReference none;

    // An empty document (no catalog, no pages)
    {
        PDFDocument document;
        const PDFStructureTree tree = Helper::parse(document);
        QVERIFY(!tree.isValid());
        QCOMPARE(tree.getChildCount(), size_t(0));
        QCOMPARE(tree.getParent(0, 0), none);
        QVERIFY(tree.getParents(0).empty());
        QCOMPARE(tree.getParentTreeEntry(0).reference, none);
    }

    // No /StructTreeRoot
    {
        const PDFDocument document = Helper::createPages(QByteArray()).read();
        QCOMPARE(document.getCatalog()->getPageCount(), size_t(2));
        const PDFStructureTree tree = Helper::parse(document);
        QVERIFY(!tree.isValid());
        QCOMPARE(tree.getParent(0, 0), none);
    }

    // /StructTreeRoot is not a dictionary
    {
        const PDFDocument document = Helper::createPages("/StructTreeRoot 7").read();
        const PDFStructureTree tree = Helper::parse(document);
        QVERIFY(!tree.isValid());
        QCOMPARE(tree.getParent(0, 0), none);
    }

    // 14.7.2, Table 354: the children of the root are structure elements. Integers below the
    // root are no marked content (the root has no page): the tree has no kids, it is not valid.
    // Its parent tree is read anyway.
    {
        MergeTestPdf pdf = Helper::createPages("/StructTreeRoot 10 0 R");
        pdf.set(10, "<< /Type /StructTreeRoot /K [0 1] /ParentTree << /Nums [0 [null 3 0 R]] >> >>");
        const PDFDocument document = pdf.read();
        const PDFStructureTree tree = Helper::parse(document);
        QVERIFY(!tree.isValid());
        QCOMPARE(tree.getChildCount(), size_t(0));
        QCOMPARE(tree.getParent(0, 0), none);
        QCOMPARE(tree.getParent(0, 1), PDFObjectReference(3, 0));
    }
}

void StructureTreeTest::textByStructureAfterNull()
{
    using Helper = StructureTreeTestHelper;

    // The page shows three texts in the marked contents 1 (Alpha), 2 (Beta) and 4 (Gamma); the
    // identifiers 0 and 3 are not used, the array of the page in the parent tree has null for
    // them: [null 12 13 null 14], so Alpha belongs to the element 12, Beta to 13 and Gamma to
    // 14 (14.7.5.4). The Document lists the elements as [14 13 12], the order of the structure
    // is the reverse of the order of the content: Gamma, Beta, Alpha.
    MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 10 0 R /MarkInfo << /Marked true >> >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /Resources << /Font << /F1 6 0 R >> >> /Contents 5 0 R /StructParents 0 >>");
    pdf.set(5, MergeTestPdf::stream("/P << /MCID 1 >> BDC\n"
                                    "BT /F1 12 Tf 20 150 Td (Alpha) Tj ET\n"
                                    "EMC\n"
                                    "/P << /MCID 2 >> BDC\n"
                                    "BT /F1 12 Tf 20 130 Td (Beta) Tj ET\n"
                                    "EMC\n"
                                    "/H1 << /MCID 4 >> BDC\n"
                                    "BT /F1 12 Tf 20 110 Td (Gamma) Tj ET\n"
                                    "EMC\n"));
    pdf.set(6, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    pdf.set(10, "<< /Type /StructTreeRoot /K 11 0 R /ParentTree << /Nums [0 [null 12 0 R 13 0 R null 14 0 R]] >> /ParentTreeNextKey 1 >>");
    pdf.set(11, "<< /Type /StructElem /S /Document /P 10 0 R /K [14 0 R 13 0 R 12 0 R] >>");
    pdf.set(12, "<< /Type /StructElem /S /P /P 11 0 R /Pg 3 0 R /K 1 >>");
    pdf.set(13, "<< /Type /StructElem /S /P /P 11 0 R /Pg 3 0 R /K 2 >>");
    pdf.set(14, "<< /Type /StructElem /S /H1 /P 11 0 R /Pg 3 0 R /K 4 >>");
    const PDFDocument document = pdf.read();

    QStringList errors;
    const PDFDocumentTextFlow flow = Helper::createStructureFlow(document, &errors);
    QCOMPARE(Helper::describeFlow(flow), QStringList({ QStringLiteral("text:Gamma"), QStringLiteral("text:Beta"), QStringLiteral("text:Alpha") }));
    QCOMPARE(errors, QStringList());
}

void StructureTreeTest::textByStructureOfTaggedDocument()
{
    using Helper = StructureTreeTestHelper;

    // The tagged document of contenttestdocuments.h: the content shows (First) in the marked
    // content 0 of P (/K 0), (Second) in the marked content 1 of Span and a square in the marked
    // content 3 of Figure (/Alt (Square)); the Document lists P, Span, Figure, Link. The text in
    // the order of the structure: First at its element P, Second, then the description of the
    // figure. Every identifier of the page has its element (the identifier 3 is behind the null
    // of the array of the page), so nothing is reported.
    const PDFDocument document = pdftest::ContentTestDocuments::createTaggedDocument();

    QStringList errors;
    const PDFDocumentTextFlow flow = Helper::createStructureFlow(document, &errors);
    QCOMPARE(Helper::describeFlow(flow), QStringList({ QStringLiteral("text:First"), QStringLiteral("text:Second"), QStringLiteral("alt:Square") }));
    QCOMPARE(errors, QStringList());
}

QTEST_MAIN(StructureTreeTest)

#include "tst_structuretreetest.moc"
