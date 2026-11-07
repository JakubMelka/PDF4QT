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

// The walks over the object graph of a document (PDFObjectWalk, review RCORE 2026-11-06):
// the helper and every walk of the tools of phase 2 and of the core they call, each with a
// cycle and with a node shared by more paths. The expected result is the one of the reachable
// graph (each node once, ISO 32000-2: a tree node has one parent - name and number trees
// 7.9.6 / 7.9.7, the structure tree 14.7.2, the fields 12.7.4) and the time is bounded.

#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfdocumenthiddendatascan.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentsanitizer.h"
#include "pdfdocumenttextflow.h"
#include "pdfobjectutils.h"
#include "pdfstructuretree.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

#include <map>

using namespace pdf;

/// PDF text of numbered objects with a classic cross-reference table
class WalkTestPdf
{
public:
    void set(int number, const QByteArray& text) { m_objects[number] = text; }

    void setStream(int number, const QByteArray& entries, const QByteArray& content)
    {
        set(number, "<< " + entries + " /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream");
    }

    /// Objects first .. first + levels - 1, each lists the next one twice in \p key; the last
    /// one is \p leaf. Every path of the 2^levels paths leads to the same leaf.
    void setSharedChain(int first, int levels, const QByteArray& key, const QByteArray& entries, const QByteArray& leaf)
    {
        for (int i = 0; i < levels; ++i)
        {
            const QByteArray next = QByteArray::number(first + i + 1) + " 0 R";
            set(first + i, "<< " + entries + " /" + key + " [" + next + " " + next + "] >>");
        }
        set(first + levels, leaf);
    }

    QByteArray write() const
    {
        QByteArray data = "%PDF-1.7\n";
        const int size = m_objects.rbegin()->first + 1;
        std::vector<qint64> offsets(size_t(size), -1);
        for (const auto& [number, text] : m_objects)
        {
            offsets[size_t(number)] = data.size();
            data += QByteArray::number(number) + " 0 obj\n" + text + "\nendobj\n";
        }
        const qint64 xref = data.size();
        data += "xref\n0 " + QByteArray::number(size) + "\n0000000000 65535 f \n";
        for (int i = 1; i < size; ++i)
        {
            data += offsets[size_t(i)] < 0 ? QByteArray("0000000000 65535 f \n") : QByteArray::number(offsets[size_t(i)]).rightJustified(10, '0') + " 00000 n \n";
        }
        data += "trailer\n<< /Size " + QByteArray::number(size) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        return data;
    }

    PDFDocument read() const
    {
        PDFDocumentReader reader(nullptr, nullptr, false, false);
        PDFDocument document = reader.readFromBuffer(write());
        if (reader.getReadingResult() != PDFDocumentReader::Result::OK)
        {
            qWarning() << "The test document cannot be read:" << reader.getErrorMessage();
        }
        return document;
    }

    /// The catalog 1 with \p catalogEntries and \p pageCount pages 3, 4, ... below the page tree 2
    static WalkTestPdf createPages(int pageCount, const QByteArray& catalogEntries = QByteArray())
    {
        WalkTestPdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R " + catalogEntries + " >>");
        QByteArray kids;
        for (int page = 0; page < pageCount; ++page)
        {
            kids += QByteArray::number(page + 3) + " 0 R ";
            pdf.set(page + 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << >> >>");
        }
        pdf.set(2, "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pageCount) + " >>");
        return pdf;
    }

private:
    std::map<int, QByteArray> m_objects;
};

/// Helpers of the test
class WalkTestHelper
{
public:
    WalkTestHelper() = delete;

    /// Limit of a walk, which must not repeat the shared nodes (a repeated walk takes far longer)
    static qint64 getTimeLimit()
    {
        return 5000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR"));
    }

    static QString getKeys(const std::vector<std::pair<PDFInteger, PDFObject>>& entries)
    {
        QStringList keys;
        for (const auto& entry : entries)
        {
            keys << QString::number(entry.first);
        }
        return keys.join(QLatin1Char(' '));
    }

    /// The items of the structure tree as "Sect[Figure] Sect" (the types of the elements)
    static QString getTree(const PDFStructureItem* item)
    {
        QStringList parts;
        for (size_t i = 0; i < item->getChildCount(); ++i)
        {
            const PDFStructureItem* child = item->getChild(i);
            const PDFStructureElement* element = child->asStructureElement();
            const QString name = element ? QString::fromLatin1(element->getTypeName()) : QStringLiteral("?");
            parts << (child->getChildCount() > 0 ? name + QLatin1Char('[') + getTree(child) + QLatin1Char(']') : name);
        }
        return parts.join(QLatin1Char(' '));
    }

    /// The items below \p item, whose parent is not the item above them or whose tree
    /// is not \p tree
    static qint64 countWrongLinks(const PDFStructureItem* item, const PDFStructureTree* tree)
    {
        qint64 count = 0;
        for (size_t i = 0; i < item->getChildCount(); ++i)
        {
            const PDFStructureItem* child = item->getChild(i);
            count += (child->getParent() != item || child->getTree() != tree) ? 1 : 0;
            count += countWrongLinks(child, tree);
        }
        return count;
    }

    static qint64 countItems(const PDFStructureItem* item)
    {
        qint64 count = 0;
        for (size_t i = 0; i < item->getChildCount(); ++i)
        {
            count += 1 + countItems(item->getChild(i));
        }
        return count;
    }

    /// The decoded streams of the storage, which contain \p marker
    static int countStreamsWith(const PDFObjectStorage& storage, const QByteArray& marker)
    {
        int count = 0;
        for (const PDFObjectStorage::Entry& entry : storage.getObjects())
        {
            if (entry.object.isStream() && storage.getDecodedStream(entry.object.getStream()).contains(marker))
            {
                ++count;
            }
        }
        return count;
    }
};

class ObjectWalkTest : public QObject
{
    Q_OBJECT

private slots:
    void walkEntersReferenceOnce();
    void walkLimits();
    void numberTree();
    void pageLabelsTree();
    void nameTree();
    void structureTree();
    void structureTreeSharedChain();
    void structureParentTree();
    void structureTreeMoved();
    void extractTextSharedElement();
    void removePagesTrees();
    void sanitizerFormsAndFields();
    void sanitizerDeepForms();
    void hiddenDataScan();
};

void ObjectWalkTest::walkEntersReferenceOnce()
{
    // A reference is entered once in the whole walk, whatever depth reaches it again
    PDFObjectWalk walk(4, 100);
    const PDFObjectReference reference(5, 0);
    QVERIFY(walk.enter(PDFObject::createReference(reference), 1));
    QVERIFY(!walk.enter(PDFObject::createReference(reference), 1));
    QVERIFY(!walk.enterReference(reference, 0));
    QVERIFY(walk.isVisited(reference));
    QVERIFY(!walk.isVisited(PDFObjectReference(6, 0)));

    // A direct object has no reference: each time its container is read
    QVERIFY(walk.enter(PDFObject::createInteger(1), 2));
    QVERIFY(walk.enter(PDFObject::createInteger(1), 2));

    // A refused visit is no limit of the walk; three nodes were entered, one of them an object
    QVERIFY(!walk.isLimitReached());
    QCOMPARE(walk.getNodeCount(), qint64(3));
    QCOMPARE(walk.getVisitedReferences(), std::set<PDFObjectReference>({ reference }));
}

void ObjectWalkTest::walkLimits()
{
    // A node deeper than the limit is not read, the walk says so; the object is not marked, a
    // shorter path reads it
    PDFObjectWalk deep(2);
    const PDFObjectReference reference(7, 0);
    QVERIFY(!deep.enterReference(reference, 3));
    QVERIFY(deep.isLimitReached());
    QVERIFY(!deep.isVisited(reference));
    QVERIFY(deep.enterReference(reference, 2));
    QCOMPARE(deep.getMaximumDepth(), 2);

    // The budget counts the direct nodes too
    PDFObjectWalk budget(10, 2);
    QVERIFY(budget.enter(PDFObject::createInteger(1), 0));
    QVERIFY(budget.enter(PDFObject::createInteger(2), 0));
    QVERIFY(!budget.isLimitReached());
    QVERIFY(!budget.enter(PDFObject::createInteger(3), 0));
    QVERIFY(!budget.enterReference(reference, 0));
    QVERIFY(budget.isLimitReached());
    QCOMPARE(budget.getNodeCount(), qint64(2));
}

void ObjectWalkTest::numberTree()
{
    // PDFObjectUtils::readNumberTree (Remove Pages reads the page labels by it). The trees are
    // not in the catalog: the reader of the document does not read them.
    WalkTestPdf pdf = WalkTestPdf::createPages(1);

    // A node, which is its own kid, and a kid, which leads back to the root: each node once
    pdf.set(20, "<< /Kids [20 0 R 21 0 R] >>");
    pdf.set(21, "<< /Nums [1 (a) 2 (b)] /Kids [20 0 R 21 0 R] >>");

    // 20 levels, each lists the next one twice: 2^20 paths to one leaf with one entry
    pdf.setSharedChain(30, 20, "Kids", QByteArray(), "<< /Nums [7 (x)] >>");
    const PDFDocument document = pdf.read();

    QElapsedTimer timer;
    timer.start();
    QCOMPARE(WalkTestHelper::getKeys(PDFObjectUtils::readNumberTree(&document.getStorage(), PDFObject::createReference(PDFObjectReference(20, 0)))), QStringLiteral("1 2"));
    QCOMPARE(WalkTestHelper::getKeys(PDFObjectUtils::readNumberTree(&document.getStorage(), PDFObject::createReference(PDFObjectReference(30, 0)))), QStringLiteral("7"));
    QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());

    // The depth limit of the caller holds: the leaf of the chain lies at the depth 20
    QCOMPARE(WalkTestHelper::getKeys(PDFObjectUtils::readNumberTree(&document.getStorage(), PDFObject::createReference(PDFObjectReference(30, 0)), 19)), QString());
}

void ObjectWalkTest::pageLabelsTree()
{
    // PDFNumberTreeLoader, which reads /PageLabels when the document is read (every tool works
    // on it): a root, which lists itself, and a leaf listed twice (ISO 32000-2, 12.4.2)
    {
        WalkTestPdf pdf = WalkTestPdf::createPages(5, "/PageLabels 20 0 R");
        pdf.set(20, "<< /Kids [21 0 R 20 0 R 22 0 R 22 0 R] >>");
        pdf.set(21, "<< /Nums [0 << /S /r >>] >>");
        pdf.set(22, "<< /Nums [3 << /S /D /St 10 >>] >>");
        const PDFDocument document = pdf.read();

        const std::vector<PDFPageLabel>& labels = document.getCatalog()->getPageLabels();
        QCOMPARE(labels.size(), size_t(2));
        QCOMPARE(labels[0].getPageIndex(), PDFInteger(0));
        QCOMPARE(labels[0].getNumberingStyle(), PDFPageLabel::NumberingStyle::LowercaseRoman);
        QCOMPARE(labels[1].getPageIndex(), PDFInteger(3));
        QCOMPARE(labels[1].getPageStartNumber(), PDFInteger(10));
    }

    // 16 levels of shared kids: one label
    {
        WalkTestPdf pdf = WalkTestPdf::createPages(1, "/PageLabels 20 0 R");
        pdf.setSharedChain(20, 16, "Kids", QByteArray(), "<< /Nums [0 << /S /A >>] >>");

        QElapsedTimer timer;
        timer.start();
        const PDFDocument document = pdf.read();
        QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());
        QCOMPARE(document.getCatalog()->getPageLabels().size(), size_t(1));
        QCOMPARE(document.getCatalog()->getPageLabels().front().getNumberingStyle(), PDFPageLabel::NumberingStyle::UppercaseLetters);
    }
}

void ObjectWalkTest::nameTree()
{
    // PDFNameTreeLoader, which reads the name trees of the catalog when the document is read
    // (a node, which was its own kid, ended the reader by a stack overflow: the former item 29
    // of SECURITY_TODO.md)
    {
        WalkTestPdf pdf = WalkTestPdf::createPages(2, "/Names << /Dests 20 0 R >>");
        pdf.set(20, "<< /Kids [20 0 R 21 0 R 21 0 R] >>");
        pdf.set(21, "<< /Names [(a) [3 0 R /Fit] (b) [4 0 R /Fit]] /Kids [20 0 R] >>");
        const PDFDocument document = pdf.read();
        QCOMPARE(document.getCatalog()->getPageCount(), size_t(2));

        const std::map<QByteArray, PDFDestination>& destinations = document.getCatalog()->getNamedDestinations();
        QCOMPARE(destinations.size(), size_t(2));
        QCOMPARE(destinations.at("b").getPageReference(), PDFObjectReference(4, 0));
    }

    // 26 levels of shared kids: 2^26 paths to one leaf, read once
    {
        WalkTestPdf pdf = WalkTestPdf::createPages(1, "/Names << /Dests 20 0 R >>");
        pdf.setSharedChain(20, 26, "Kids", QByteArray(), "<< /Names [(only) [3 0 R /Fit]] >>");

        QElapsedTimer timer;
        timer.start();
        const PDFDocument document = pdf.read();
        QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());
        QCOMPARE(document.getCatalog()->getNamedDestinations().size(), size_t(1));
        QVERIFY(document.getCatalog()->getNamedDestination("only"));
    }
}

void ObjectWalkTest::structureTree()
{
    // The structure tree, which the order Structure of Extract Text reads (ISO 32000-2, 14.7.2:
    // each element has one parent). The root lists itself, an element is its own kid, the
    // figure is listed by both sections: three elements, the figure below the first section.
    WalkTestPdf pdf = WalkTestPdf::createPages(1, "/StructTreeRoot 8 0 R /MarkInfo << /Marked true >>");
    pdf.set(8, "<< /Type /StructTreeRoot /K [9 0 R 10 0 R 8 0 R] >>");
    pdf.set(9, "<< /Type /StructElem /S /Sect /P 8 0 R /K [11 0 R 9 0 R] >>");
    pdf.set(10, "<< /Type /StructElem /S /Sect /P 8 0 R /K 11 0 R >>");
    pdf.set(11, "<< /Type /StructElem /S /Figure /P 9 0 R /Alt (Shared figure) >>");
    const PDFDocument document = pdf.read();

    const PDFStructureTree tree = PDFStructureTree::parse(&document.getStorage(), document.getCatalog()->getStructureTreeRoot());
    QCOMPARE(WalkTestHelper::getTree(&tree), QStringLiteral("Sect[Figure] Sect"));
    QCOMPARE(WalkTestHelper::countItems(&tree), qint64(3));
    QCOMPARE(tree.getChild(0)->getChild(0)->getSelfReference(), PDFObjectReference(11, 0));
}

void ObjectWalkTest::structureTreeSharedChain()
{
    // 16 levels, each element lists the next one twice: 17 elements of the reachable graph,
    // not 2^17 items of the paths
    WalkTestPdf pdf = WalkTestPdf::createPages(1, "/StructTreeRoot 8 0 R /MarkInfo << /Marked true >>");
    pdf.set(8, "<< /Type /StructTreeRoot /K [20 0 R] >>");
    pdf.setSharedChain(20, 16, "K", "/Type /StructElem /S /Div", "<< /Type /StructElem /S /P >>");
    const PDFDocument document = pdf.read();

    QElapsedTimer timer;
    timer.start();
    const PDFStructureTree tree = PDFStructureTree::parse(&document.getStorage(), document.getCatalog()->getStructureTreeRoot());
    QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());
    QCOMPARE(WalkTestHelper::countItems(&tree), qint64(17));
}

void ObjectWalkTest::structureParentTree()
{
    // The parent tree (a number tree, 14.7.5.4) and the ID tree (a name tree) of the structure,
    // which the order Structure reads: a node, which lists itself, and a leaf listed twice. The
    // marked content 0 of the page has one parent element (one entry, not two).
    WalkTestPdf pdf = WalkTestPdf::createPages(1, "/StructTreeRoot 8 0 R /MarkInfo << /Marked true >>");
    pdf.set(8, "<< /Type /StructTreeRoot /K [11 0 R] /ParentTree 12 0 R /IDTree 14 0 R >>");
    pdf.set(11, "<< /Type /StructElem /S /P /P 8 0 R /ID (id1) /Pg 3 0 R /K 0 >>");
    pdf.set(12, "<< /Kids [12 0 R 13 0 R 13 0 R] >>");
    pdf.set(13, "<< /Nums [0 [11 0 R]] >>");
    pdf.set(14, "<< /Kids [14 0 R 15 0 R] >>");
    pdf.set(15, "<< /Names [(id1) 11 0 R] >>");
    const PDFDocument document = pdf.read();

    QElapsedTimer timer;
    timer.start();
    const PDFStructureTree tree = PDFStructureTree::parse(&document.getStorage(), document.getCatalog()->getStructureTreeRoot());
    QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());
    QCOMPARE(tree.getParentTreeEntry(0).reference, PDFObjectReference(11, 0));
    QVERIFY(!tree.getParentTreeEntry(1).reference.isValid());
    QCOMPARE(WalkTestHelper::getTree(&tree), QStringLiteral("P"));
}

void ObjectWalkTest::structureTreeMoved()
{
    // The order Structure of Extract Text assigns the parsed tree to its own object and walks
    // up the parents for the inherited /Pg (ISO 32000-2, 14.7.2, Table 355: /P is the parent,
    // the kids of the root have the root). The kids of the tree pointed to the destroyed
    // temporary (P2TSAN: a crash in PDFStructureTreeTextFlowCollector::isOnProcessedPage).
    WalkTestPdf pdf = WalkTestPdf::createPages(1, "/StructTreeRoot 8 0 R /MarkInfo << /Marked true >>");
    pdf.set(8, "<< /Type /StructTreeRoot /K [9 0 R 10 0 R 8 0 R] >>");
    pdf.set(9, "<< /Type /StructElem /S /Sect /P 8 0 R /K [11 0 R 9 0 R] >>");
    pdf.set(10, "<< /Type /StructElem /S /Sect /P 8 0 R /K [11 0 R << /Type /MCR /Pg 3 0 R /MCID 0 >>] >>");
    pdf.set(11, "<< /Type /StructElem /S /Figure /P 9 0 R /Alt (Shared figure) >>");
    const PDFDocument document = pdf.read();

    PDFStructureTree assigned;
    assigned = PDFStructureTree::parse(&document.getStorage(), document.getCatalog()->getStructureTreeRoot());
    QCOMPARE(WalkTestHelper::getTree(&assigned), QStringLiteral("Sect[Figure] Sect[?]"));
    QCOMPARE(WalkTestHelper::countWrongLinks(&assigned, &assigned), qint64(0));
    QCOMPARE(assigned.getChild(0)->getParent(), static_cast<const PDFStructureItem*>(&assigned));

    // A moved tree hands its items to the new object, the old one is empty
    PDFStructureTree moved(std::move(assigned));
    QCOMPARE(WalkTestHelper::getTree(&moved), QStringLiteral("Sect[Figure] Sect[?]"));
    QCOMPARE(WalkTestHelper::countWrongLinks(&moved, &moved), qint64(0));
    QVERIFY(!assigned.isValid());
}

void ObjectWalkTest::extractTextSharedElement()
{
    // The text flow of the order Structure (Extract Text): the description of the figure,
    // which both sections list, is in the text once
    WalkTestPdf pdf = WalkTestPdf::createPages(1, "/StructTreeRoot 8 0 R /MarkInfo << /Marked true >>");
    pdf.set(8, "<< /Type /StructTreeRoot /K [9 0 R 10 0 R 8 0 R] >>");
    pdf.set(9, "<< /Type /StructElem /S /Sect /P 8 0 R /K [11 0 R 9 0 R] >>");
    pdf.set(10, "<< /Type /StructElem /S /Sect /P 8 0 R /K [11 0 R 10 0 R] >>");
    pdf.set(11, "<< /Type /StructElem /S /Figure /P 9 0 R /Alt (Shared figure) /K [9 0 R] >>");
    const PDFDocument document = pdf.read();

    QVERIFY(PDFDocumentTextFlowFactory::hasValidStructureTree(&document));
    PDFDocumentTextFlowFactory factory;
    const PDFDocumentTextFlow flow = factory.create(&document, PDFDocumentTextFlowFactory::Algorithm::Structure);

    int descriptions = 0;
    for (const PDFDocumentTextFlow::Item& item : flow.getItems())
    {
        if (item.flags.testFlag(PDFDocumentTextFlow::StructureAlternativeDescription))
        {
            QCOMPARE(item.text, QStringLiteral("Shared figure"));
            ++descriptions;
        }
    }
    QCOMPARE(descriptions, 1);
}

void ObjectWalkTest::removePagesTrees()
{
    // Remove Pages over hostile trees, which the reader now reads: the page labels with a root,
    // which lists itself, the named destinations with a node, which is its own kid, and a leaf
    // listed twice. Page 2 (object 4) is removed.
    WalkTestPdf pdf = WalkTestPdf::createPages(5, "/PageLabels 20 0 R /Names << /Dests 30 0 R >>");
    pdf.set(20, "<< /Kids [21 0 R 20 0 R 22 0 R] >>");
    pdf.set(21, "<< /Nums [0 << /S /r >>] >>");
    pdf.set(22, "<< /Nums [3 << /S /D >>] >>");
    pdf.set(30, "<< /Kids [30 0 R 31 0 R 31 0 R] >>");
    pdf.set(31, "<< /Names [(a) [4 0 R /Fit] (b) [3 0 R /Fit]] >>");
    const PDFDocument document = pdf.read();
    QCOMPARE(document.getCatalog()->getPageLabels().size(), size_t(2));

    QElapsedTimer timer;
    timer.start();
    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PDFDocumentPageOperations::removePages(&document, { 1 }, nullptr, &statistics);
    QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(4));

    // The destination of the removed page goes, counted once (PDF 2.0, 12.3.2.4)
    QCOMPARE(statistics.namedDestinationsRemoved, qint64(1));
    QCOMPARE(result.getCatalog()->getNamedDestinations().size(), size_t(1));
    QVERIFY(result.getCatalog()->getNamedDestination("b"));

    // Every page keeps its label (12.4.2): i, iii, 1, 2 - the third page starts again
    const std::vector<PDFPageLabel>& labels = result.getCatalog()->getPageLabels();
    QVERIFY(statistics.pageLabelsRewritten);
    QCOMPARE(labels.size(), size_t(3));
    QCOMPARE(labels[0].getPageIndex(), PDFInteger(0));
    QCOMPARE(labels[0].getNumberingStyle(), PDFPageLabel::NumberingStyle::LowercaseRoman);
    QCOMPARE(labels[1].getPageIndex(), PDFInteger(1));
    QCOMPARE(labels[1].getPageStartNumber(), PDFInteger(3));
    QCOMPARE(labels[2].getPageIndex(), PDFInteger(2));
    QCOMPARE(labels[2].getNumberingStyle(), PDFPageLabel::NumberingStyle::DecimalArabic);
}

void ObjectWalkTest::sanitizerFormsAndFields()
{
    // Remove Hidden Data, the invisible text and the signature fields. Both pages share their
    // content stream and their form, the form draws itself; the field tree has a field, which
    // lists itself twice, with the signature field; a widget, whose /Parent chain is a cycle
    // without /FT, is no signature field (ISO 32000-2, Table 226: /FT is inherited).
    WalkTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [61 0 R] /SigFlags 3 >> >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 5 0 R /Resources << /XObject << /Fm 10 0 R >> >> /Annots [70 0 R 60 0 R] >>");
    pdf.set(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 5 0 R /Resources << /XObject << /Fm 10 0 R >> >> >>");
    pdf.setStream(5, QByteArray(), "/Fm Do BT 3 Tr (MarkerPage) Tj ET");
    pdf.setStream(10, "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << /XObject << /Fm 10 0 R >> >>", "BT 3 Tr (MarkerForm) Tj ET /Fm Do");
    pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /Parent 61 0 R /Rect [0 0 1 1] /P 3 0 R >>");
    pdf.set(61, "<< /T (group) /Kids [61 0 R 61 0 R 60 0 R] >>");
    pdf.set(70, "<< /Type /Annot /Subtype /Widget /T (loop) /Parent 71 0 R /Rect [0 0 1 1] /P 3 0 R >>");
    pdf.set(71, "<< /T (loopParent) /Parent 70 0 R >>");
    const PDFDocument document = pdf.read();
    QCOMPARE(WalkTestHelper::countStreamsWith(document.getStorage(), "MarkerPage"), 1);
    QCOMPARE(WalkTestHelper::countStreamsWith(document.getStorage(), "MarkerForm"), 1);

    PDFDocumentSanitizer sanitizer(PDFDocumentSanitizer::SanitizationFlag(PDFDocumentSanitizer::InvisibleText | PDFDocumentSanitizer::SignatureFields), nullptr);
    QSignalSpy progress(&sanitizer, &PDFDocumentSanitizer::sanitizationProgress);
    sanitizer.setDocument(&document);

    QElapsedTimer timer;
    timer.start();
    sanitizer.sanitize();
    QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());

    // Each shared stream is rewritten once: the content stream and the form
    QStringList messages;
    for (const QList<QVariant>& arguments : progress)
    {
        messages << arguments.front().toString();
    }
    QVERIFY2(messages.contains(QStringLiteral("Invisible text content streams sanitized: 2.")), qPrintable(messages.join(QLatin1Char('|'))));
    QVERIFY2(messages.contains(QStringLiteral("Signature fields removed: 1.")), qPrintable(messages.join(QLatin1Char('|'))));

    const PDFObjectStorage& storage = sanitizer.getStorage();
    QCOMPARE(WalkTestHelper::countStreamsWith(storage, "MarkerPage"), 0);
    QCOMPARE(WalkTestHelper::countStreamsWith(storage, "MarkerForm"), 0);

    // The page keeps the widget of the cycle, the signature widget goes (the objects are
    // renumbered by the sanitizer: the widget is found by its name)
    const PDFDocument output(PDFObjectStorage(storage), document.getInfo()->version, QByteArray());
    const std::vector<PDFObjectReference>& annotations = output.getCatalog()->getPage(0)->getAnnotations();
    QCOMPARE(annotations.size(), size_t(1));
    QCOMPARE(output.getDictionaryFromObject(output.getObjectByReference(annotations.front()))->get("T").getString(), QByteArray("loop"));
}

void ObjectWalkTest::sanitizerDeepForms()
{
    // 20000 forms, each draws the next one: the walk of the invisible text ends at its depth
    // (the stack of the thread stays bounded), the text of the first form is removed
    constexpr int FORM_COUNT = 20000;
    WalkTestPdf pdf = WalkTestPdf::createPages(1);
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 5 0 R /Resources << /XObject << /F 10 0 R >> >> >>");
    pdf.setStream(5, QByteArray(), "/F Do");
    for (int i = 0; i < FORM_COUNT; ++i)
    {
        const QByteArray content = i == 0 ? QByteArray("BT 3 Tr (MarkerTop) Tj ET /F Do") : QByteArray("/F Do");
        pdf.setStream(10 + i, "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << /XObject << /F " + QByteArray::number(11 + i) + " 0 R >> >>", content);
    }
    const PDFDocument document = pdf.read();
    QCOMPARE(WalkTestHelper::countStreamsWith(document.getStorage(), "MarkerTop"), 1);

    PDFDocumentSanitizer sanitizer(PDFDocumentSanitizer::InvisibleText, nullptr);
    sanitizer.setDocument(&document);

    QElapsedTimer timer;
    timer.start();
    sanitizer.sanitize();
    QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());
    QCOMPARE(WalkTestHelper::countStreamsWith(sanitizer.getStorage(), "MarkerTop"), 0);
}

void ObjectWalkTest::hiddenDataScan()
{
    // The scan of Remove Hidden Data: the signature field once (its parent lists itself twice
    // and the field), each page with the invisible text of the shared form once, the metadata
    // of a dictionary shared by both pages once
    WalkTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [61 0 R] >> /Metadata 40 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 5 0 R /Resources 41 0 R /Annots [60 0 R] >>");
    pdf.set(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 5 0 R /Resources 41 0 R >>");
    pdf.setStream(5, QByteArray(), "/Fm Do");
    pdf.setStream(10, "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources 41 0 R", "BT 3 Tr (MarkerForm) Tj ET /Fm Do");
    pdf.setStream(40, "/Type /Metadata /Subtype /XML", "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"/>");
    pdf.set(41, "<< /XObject << /Fm 10 0 R >> /Metadata 40 0 R >>");
    pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /Parent 61 0 R /Rect [0 0 1 1] /P 3 0 R >>");
    pdf.set(61, "<< /T (group) /Kids [61 0 R 61 0 R 60 0 R] >>");
    const PDFDocument document = pdf.read();

    QElapsedTimer timer;
    timer.start();
    const PDFDocumentHiddenDataScan::Result result = PDFDocumentHiddenDataScan::scan(&document, true, nullptr, nullptr);
    QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());

    QCOMPARE(result.get(PDFDocumentHiddenDataScan::Kind::SignatureFields).count, qint64(1));
    QCOMPARE(result.get(PDFDocumentHiddenDataScan::Kind::InvisibleText).count, qint64(2));
    QCOMPARE(result.get(PDFDocumentHiddenDataScan::Kind::Metadata).count, qint64(2));
}

QTEST_MAIN(ObjectWalkTest)

#include "tst_objectwalktest.moc"
