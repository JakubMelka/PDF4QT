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
#include "pdfoperationcontrol.h"
#include "pdfresourcewalk.h"
#include "pdfstructuretree.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
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
    void numberTreeLimit();
    void numberTreeSharedNums();
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
    void resourceWalkGraph();
    void resourceWalkLimits();
    void resourceWalkHostile();
    void resourceWalkSharedContainers();
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

void ObjectWalkTest::numberTreeLimit()
{
    // PDFObjectUtils::readNumberTree tells its caller, when a limit stopped its walk: the entries
    // are a part of the tree only then, and a caller, which writes the tree again from them, would
    // lose the rest (the structure model of Edit Content must not write such a parent tree back).
    // A node read already is no limit: its entries are in the result. Each call starts with the
    // flag set to the opposite of the expected value, so a flag, which is not written, fails.
    WalkTestPdf pdf = WalkTestPdf::createPages(1);

    // A cycle: a node, which is its own kid, and a kid, which leads back to the root
    pdf.set(20, "<< /Kids [20 0 R 21 0 R] >>");
    pdf.set(21, "<< /Nums [1 (a) 2 (b)] /Kids [20 0 R 21 0 R] >>");

    // A node shared by two paths: 20 levels, each lists the next one twice; the leaf with the
    // key 7 is the object 50 at the depth 20
    pdf.setSharedChain(30, 20, "Kids", QByteArray(), "<< /Nums [7 (x)] >>");

    // A tree with a leaf at the depth 1 (the key 3) and a leaf at the depth 2 (the key 4)
    pdf.set(60, "<< /Kids [61 0 R 62 0 R] >>");
    pdf.set(61, "<< /Nums [3 (c)] >>");
    pdf.set(62, "<< /Kids [63 0 R] >>");
    pdf.set(63, "<< /Nums [4 (d)] >>");

    // A root, which is no dictionary, and a root, which does not exist
    pdf.set(70, "[1 (a)]");
    const PDFDocument document = pdf.read();
    const PDFObjectStorage* storage = &document.getStorage();

    struct Row
    {
        const char* name;
        int root;
        int maximumDepth;
        const char* keys;
        bool isLimitReached;
    };

    const Row rows[] =
    {
        { "cycle", 20, 32, "1 2", false },
        { "node shared by two paths", 30, 32, "7", false },
        { "shared node at the depth limit", 30, 20, "7", false },
        { "shared node one level too deep", 30, 19, "", true },
        { "both leaves in the depth", 60, 2, "3 4", false },
        { "one leaf too deep, the other read", 60, 1, "3", true },
        { "kids too deep", 60, 0, "", true },
        { "root is not a dictionary", 70, 32, "", false },
        { "root does not exist", 99, 32, "", false },
    };

    for (const Row& row : rows)
    {
        bool isLimitReached = !row.isLimitReached;
        const std::vector<std::pair<PDFInteger, PDFObject>> entries = PDFObjectUtils::readNumberTree(storage, PDFObject::createReference(PDFObjectReference(row.root, 0)), row.maximumDepth, &isLimitReached);
        QVERIFY2(WalkTestHelper::getKeys(entries) == QLatin1String(row.keys), row.name);
        QVERIFY2(isLimitReached == row.isLimitReached, row.name);

        // The entries do not depend on the question
        QVERIFY2(WalkTestHelper::getKeys(PDFObjectUtils::readNumberTree(storage, PDFObject::createReference(PDFObjectReference(row.root, 0)), row.maximumDepth)) == QLatin1String(row.keys), row.name);
    }
}

void ObjectWalkTest::numberTreeSharedNums()
{
    // Many nodes of a number tree, which name one indirect /Nums array (review G2-1): the visited
    // set of the walk holds references only, so each direct node reads the whole array again. The
    // reading has one budget for the whole tree, PDFObjectWalk::DEFAULT_MAXIMUM_NODES, which every
    // entered node and every pair of /Nums read spends; when it is spent, the reading stops and
    // the limit is reported (the caller must not take the entries for the whole tree).
    //
    // The root 80 lists KID_COUNT direct nodes << /Nums 81 0 R >>; the array 81 has PAIR_COUNT
    // pairs, of which only the first one has an integer key (the others have the key 0.5, which
    // is no key of a number tree, ISO 32000-2, 7.9.7): the result stays small (one entry per
    // node read), the work is KID_COUNT x PAIR_COUNT = 163 840 000 pairs without the budget.
    constexpr int KID_COUNT = 40000;
    constexpr int PAIR_COUNT = 4096;

    WalkTestPdf pdf = WalkTestPdf::createPages(1);
    QByteArray kids;
    kids.reserve(KID_COUNT * 20);
    for (int i = 0; i < KID_COUNT; ++i)
    {
        kids += "<< /Nums 81 0 R >> ";
    }
    pdf.set(80, "<< /Kids [" + kids + "] >>");

    QByteArray numbers("0 (first)");
    for (int i = 1; i < PAIR_COUNT; ++i)
    {
        numbers += " 0.5 null";
    }
    pdf.set(81, "[" + numbers + "]");

    // Within the budget the same tree is read whole, each node with the shared array
    pdf.set(90, "<< /Kids [<< /Nums 91 0 R >> << /Nums 91 0 R >> << /Nums 91 0 R >>] >>");
    pdf.set(91, "[1 (a) 2 (b)]");
    const PDFDocument document = pdf.read();
    const PDFObjectStorage* storage = &document.getStorage();

    bool isLimitReached = true;
    QCOMPARE(WalkTestHelper::getKeys(PDFObjectUtils::readNumberTree(storage, PDFObject::createReference(PDFObjectReference(90, 0)), 32, &isLimitReached)), QStringLiteral("1 2 1 2 1 2"));
    QVERIFY(!isLimitReached);

    // The budget B: the root spends 1, each node 1 + PAIR_COUNT. The nodes read whole are the
    // largest k with 1 + k x (1 + PAIR_COUNT) <= B: (B - 1) / 4097 = 4095 (16 777 215 / 4097 is
    // exactly 4095), so the budget is spent with the last pair of the node 4095 and the node
    // 4096 is refused. Each node read gives its first pair: 4095 entries with the key 0.
    const qint64 budget = PDFObjectWalk::DEFAULT_MAXIMUM_NODES;
    const qint64 nodesRead = (budget - 1) / (1 + PAIR_COUNT);
    QCOMPARE(nodesRead, qint64(4095));

    QElapsedTimer timer;
    timer.start();
    isLimitReached = false;
    const std::vector<std::pair<PDFInteger, PDFObject>> entries = PDFObjectUtils::readNumberTree(storage, PDFObject::createReference(PDFObjectReference(80, 0)), 32, &isLimitReached);
    const qint64 elapsed = timer.elapsed();

    QVERIFY(isLimitReached);
    QCOMPARE(qint64(entries.size()), nodesRead);
    QVERIFY(std::all_of(entries.cbegin(), entries.cend(), [](const auto& entry) { return entry.first == 0 && entry.second.isString(); }));
    QVERIFY2(elapsed < WalkTestHelper::getTimeLimit(), qPrintable(QString::number(elapsed)));
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

    // The paragraph has one kid, the marked content of its /K 0 (Table 355: an integer kid is a
    // marked-content identifier); getTree shows a kid, which is not an element, as "?"
    QCOMPARE(WalkTestHelper::getTree(&tree), QStringLiteral("P[?]"));
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

/// The node of the walk of the resources, whose owner is the object \p number (nullptr = none)
class ResourceWalkTestHelper
{
public:
    ResourceWalkTestHelper() = delete;

    static const PDFResourceWalkNode* find(const PDFResourceWalkResult& result, int number, size_t* index = nullptr)
    {
        for (size_t i = 0; i < result.nodes.size(); ++i)
        {
            if (result.nodes[i].reference == PDFObjectReference(number, 0))
            {
                if (index)
                {
                    *index = i;
                }
                return &result.nodes[i];
            }
        }
        return nullptr;
    }

    static std::vector<PDFInteger> pagesOf(const PDFResourceWalkResult& result, int number)
    {
        size_t index = 0;
        return find(result, number, &index) ? result.getPageIndices(index) : std::vector<PDFInteger>();
    }

    /// Cancels from the start
    class Canceled : public PDFOperationControl
    {
    public:
        virtual bool isOperationCanceled() const override { return true; }
    };
};

void ObjectWalkTest::resourceWalkGraph()
{
    // Plan of step 7 of phase 2, 6.1, R7-17 (B167, B197): every owner of resources of ISO 32000-2 - the pages
    // (7.7.3.3), the forms (8.10.1), the tiling patterns (8.7.3.1, Table 75), the Type 3 fonts (9.6.4, Table
    // 110), the groups of the soft masks (11.6.5.2, Table 142) and the appearance streams of the annotations
    // with their states (12.5.5, Table 170). A cycle (the forms 11 and 12 draw each other) and a form shared
    // by two paths (12 from the form 11 and from the Type 3 font 14) and by both pages (a resource
    // dictionary shared by them): each owner once, the edges of every path kept.
    WalkTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources 10 0 R >>");
    pdf.set(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources 10 0 R /Annots [18 0 R] >>");
    pdf.set(10, "<< /XObject << /F1 11 0 R >> /Font << /T3 14 0 R >> /Pattern << /P1 15 0 R /P2 21 0 R >> /ExtGState << /G1 16 0 R >> >>");
    pdf.setStream(11, "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << /XObject << /F2 12 0 R >> >>", "/F2 Do");
    pdf.setStream(12, "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << /XObject << /F1 11 0 R /Im 13 0 R >> >>", "/F1 Do /Im Do");
    pdf.setStream(13, "/Type /XObject /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray /BitsPerComponent 8", QByteArray(1, '\0'));
    pdf.set(14, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1 1] /FontMatrix [1 0 0 1 0 0] /CharProcs << >> /Encoding << /Differences [] >> /FirstChar 0 /LastChar 0 /Widths [0] /Resources << /XObject << /F2 12 0 R >> >> >>");
    pdf.setStream(15, "/PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 1 1] /XStep 1 /YStep 1 /Resources << >>", "0 0 1 1 re f");
    pdf.set(21, "<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 1 0] /Function << /FunctionType 2 /Domain [0 1] /N 1 >> >> >>");
    pdf.set(16, "<< /Type /ExtGState /SMask << /S /Luminosity /G 17 0 R >> >>");
    pdf.setStream(17, "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Group << /S /Transparency >> /Resources << >>", "0 0 1 1 re f");
    pdf.set(18, "<< /Type /Annot /Subtype /Square /Rect [0 0 10 10] /AP << /N 19 0 R /D << /On 19 0 R /Off 20 0 R >> >> >>");
    pdf.setStream(19, "/Type /XObject /Subtype /Form /BBox [0 0 10 10] /Resources << /XObject << /F1 11 0 R >> >>", "/F1 Do");
    pdf.setStream(20, "/Type /XObject /Subtype /Form /BBox [0 0 10 10]", "0 0 1 1 re f");
    const PDFDocument document = pdf.read();

    QElapsedTimer timer;
    timer.start();
    const PDFResourceWalkResult result = PDFResourceWalk::walk(&document, { 0, 1, 1 }, PDFResourceWalk::Options(), nullptr);
    QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());
    QVERIFY(!result.isLimitReached);
    QVERIFY(!result.isCanceled);

    // The two pages, the forms 11 and 12, the Type 3 font, the tiling pattern (the shading pattern 21 and
    // the image 13 own no resources), the soft mask group, the two appearance streams: each once
    using H = ResourceWalkTestHelper;
    QCOMPARE(result.nodes.size(), size_t(9));
    QCOMPARE(result.pages.size(), size_t(2));
    QCOMPARE(H::find(result, 3)->kind, PDFResourceOwnerKind::Page);
    QCOMPARE(H::find(result, 4)->kind, PDFResourceOwnerKind::Page);
    QCOMPARE(H::find(result, 11)->kind, PDFResourceOwnerKind::Form);
    QCOMPARE(H::find(result, 12)->kind, PDFResourceOwnerKind::Form);
    QCOMPARE(H::find(result, 14)->kind, PDFResourceOwnerKind::Type3Font);
    QCOMPARE(H::find(result, 15)->kind, PDFResourceOwnerKind::Pattern);
    QCOMPARE(H::find(result, 17)->kind, PDFResourceOwnerKind::SoftMaskGroup);
    QCOMPARE(H::find(result, 19)->kind, PDFResourceOwnerKind::AppearanceStream);
    QCOMPARE(H::find(result, 20)->kind, PDFResourceOwnerKind::AppearanceStream);
    QVERIFY(!H::find(result, 13));
    QVERIFY(!H::find(result, 21));
    QCOMPARE(H::find(result, 19)->annotation, PDFObjectReference(18, 0));
    QVERIFY(H::find(result, 20)->resources.isNull());

    // The breadth first walk: a form of the page is at depth 1, its form at 2; the first page reached them
    QCOMPARE(H::find(result, 11)->depth, 1);
    QCOMPARE(H::find(result, 12)->depth, 2);
    QCOMPARE(H::find(result, 11)->pageIndex, PDFInteger(0));
    QCOMPARE(H::find(result, 19)->pageIndex, PDFInteger(1));

    // Every path is an edge: the form 12 has two parents (11 and the font 14), the cycle 12 -> 11 is an edge too
    size_t index11 = 0;
    size_t index12 = 0;
    size_t index14 = 0;
    H::find(result, 11, &index11);
    H::find(result, 12, &index12);
    H::find(result, 14, &index14);
    const std::vector<size_t>& children12 = result.nodes[index12].children;
    QVERIFY(std::find(children12.cbegin(), children12.cend(), index11) != children12.cend());
    const std::vector<size_t>& children14 = result.nodes[index14].children;
    QVERIFY(std::find(children14.cbegin(), children14.cend(), index12) != children14.cend());

    // The pages, which use a node: both pages share the dictionary of the resources; the appearance stream
    // only the second one - and through it the form 11 again
    QCOMPARE(H::pagesOf(result, 12), std::vector<PDFInteger>({ 0, 1 }));
    QCOMPARE(H::pagesOf(result, 19), std::vector<PDFInteger>({ 1 }));
    QCOMPARE(H::pagesOf(result, 20), std::vector<PDFInteger>({ 1 }));

    // Only the first page: the appearance streams of the second one are not walked
    const PDFResourceWalkResult first = PDFResourceWalk::walk(&document, { 0 }, PDFResourceWalk::Options(), nullptr);
    QCOMPARE(first.nodes.size(), size_t(6));
    QVERIFY(!H::find(first, 19));

    // Without the annotations
    PDFResourceWalk::Options options;
    options.includeAnnotations = false;
    QCOMPARE(PDFResourceWalk::walk(&document, { 0, 1 }, options, nullptr).nodes.size(), size_t(7));
}

void ObjectWalkTest::resourceWalkLimits()
{
    // A chain of 300 nested forms: the depth limit stops the walk and says so (never a recursion of the
    // caller - the walk is iterative); 2^40 paths of shared forms (each form lists the next one twice) are
    // walked in linear time; the cancellation stops the walk
    WalkTestPdf pdf = WalkTestPdf::createPages(1);
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /XObject << /F 100 0 R >> >> >>");
    constexpr int chainLength = 300;
    for (int i = 0; i < chainLength; ++i)
    {
        const QByteArray next = i + 1 < chainLength ? "/Resources << /XObject << /F " + QByteArray::number(101 + i) + " 0 R >> >>" : QByteArray();
        pdf.setStream(100 + i, "/Type /XObject /Subtype /Form /BBox [0 0 1 1] " + next, "0 0 1 1 re f");
    }
    const PDFDocument document = pdf.read();

    const PDFResourceWalkResult limited = PDFResourceWalk::walk(&document, { 0 }, PDFResourceWalk::Options(), nullptr);
    QVERIFY(limited.isLimitReached);
    QCOMPARE(limited.nodes.size(), size_t(1 + PDFObjectWalk::DEFAULT_MAXIMUM_DEPTH));

    PDFResourceWalk::Options deep;
    deep.maximumDepth = chainLength + 1;
    const PDFResourceWalkResult complete = PDFResourceWalk::walk(&document, { 0 }, deep, nullptr);
    QVERIFY(!complete.isLimitReached);
    QCOMPARE(complete.nodes.size(), size_t(1 + chainLength));

    // The budget of the nodes
    PDFResourceWalk::Options budget;
    budget.maximumNodes = 10;
    const PDFResourceWalkResult small = PDFResourceWalk::walk(&document, { 0 }, budget, nullptr);
    QVERIFY(small.isLimitReached);
    QCOMPARE(small.nodes.size(), size_t(10));

    // Shared forms: 40 levels, each form draws the next one by two names
    WalkTestPdf shared = WalkTestPdf::createPages(1);
    shared.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /XObject << /F 200 0 R >> >> >>");
    constexpr int levels = 40;
    for (int i = 0; i <= levels; ++i)
    {
        const QByteArray next = QByteArray::number(201 + i) + " 0 R";
        const QByteArray resources = i < levels ? "/Resources << /XObject << /A " + next + " /B " + next + " >> >>" : QByteArray();
        shared.setStream(200 + i, "/Type /XObject /Subtype /Form /BBox [0 0 1 1] " + resources, "0 0 1 1 re f");
    }
    const PDFDocument sharedDocument = shared.read();
    QElapsedTimer timer;
    timer.start();
    const PDFResourceWalkResult sharedResult = PDFResourceWalk::walk(&sharedDocument, { 0 }, PDFResourceWalk::Options(), nullptr);
    QVERIFY(timer.elapsed() < WalkTestHelper::getTimeLimit());
    QCOMPARE(sharedResult.nodes.size(), size_t(1 + levels + 1));
    QCOMPARE(ResourceWalkTestHelper::pagesOf(sharedResult, 200 + levels), std::vector<PDFInteger>({ 0 }));

    // Canceled: the pages are entered, their resources are not read
    const ResourceWalkTestHelper::Canceled canceled;
    const PDFResourceWalkResult stopped = PDFResourceWalk::walk(&document, { 0 }, PDFResourceWalk::Options(), &canceled);
    QVERIFY(stopped.isCanceled);
    QCOMPARE(stopped.nodes.size(), size_t(1));
}

void ObjectWalkTest::resourceWalkHostile()
{
    // Wrong types are skipped, never an error: /Resources a number, /XObject an array, a form, which is a
    // dictionary (not a stream), a reference to nothing, /SMask /None, /AP a name; pages out of range and an
    // empty document give nothing
    WalkTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources 5 /Annots [7 0 R 99 0 R] >>");
    pdf.set(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /XObject [1 2] /Font << /F 98 0 R /G 6 0 R >> /ExtGState << /S << /SMask /None >> >> /Pattern << /P 6 0 R >> >> >>");
    pdf.set(6, "<< /Type /XObject /Subtype /Form /PatternType 1 >>");
    pdf.set(7, "<< /Type /Annot /Subtype /Square /Rect [0 0 1 1] /AP /N >>");
    const PDFDocument document = pdf.read();

    const PDFResourceWalkResult result = PDFResourceWalk::walk(&document, { -1, 0, 1, 2, 1000 }, PDFResourceWalk::Options(), nullptr);
    QVERIFY(!result.isLimitReached);
    QCOMPARE(result.pages.size(), size_t(2));

    // The pattern 6 is a dictionary with /PatternType 1: an owner of resources (a pattern without a stream
    // is damaged, its /Resources are read as they are)
    QCOMPARE(result.nodes.size(), size_t(3));
    QCOMPARE(ResourceWalkTestHelper::find(result, 6)->kind, PDFResourceOwnerKind::Pattern);

    QCOMPARE(PDFResourceWalk::walk(nullptr, { 0 }, PDFResourceWalk::Options(), nullptr).nodes.size(), size_t(0));
    const PDFDocument empty = WalkTestPdf::createPages(0).read();
    QCOMPARE(PDFResourceWalk::walk(&empty, { 0 }, PDFResourceWalk::Options(), nullptr).nodes.size(), size_t(0));
}

void ObjectWalkTest::resourceWalkSharedContainers()
{
    // Review RS7CORE: a resource dictionary shared by its owners. (1) Two Type 3 fonts written directly into
    // a shared /Font dictionary (9.6.4: a font dictionary may be direct), whose /Resources lead back to that
    // dictionary: each direct font is one owner (an object of the document, as a referenced one) - a node
    // for each reading of its container doubled the nodes with each level up to the budget. Both pages share
    // the resources: 2 pages + 2 fonts, each font reached from both pages, every edge kept.
    WalkTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources 10 0 R >>");
    pdf.set(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources 10 0 R >>");
    pdf.set(10, "<< /Font 11 0 R >>");
    const QByteArray type3 = "/Subtype /Type3 /FontBBox [0 0 1 1] /FontMatrix [1 0 0 1 0 0] /CharProcs << >> /Encoding << /Differences [] >> /FirstChar 0 /LastChar 0 /Widths [0] /Resources 10 0 R";
    pdf.set(11, "<< /A << " + type3 + " >> /B << " + type3 + " >> >>");
    const PDFDocument document = pdf.read();

    // A small budget: the doubling would spend it (and say so) in moments
    PDFResourceWalk::Options options;
    options.maximumNodes = 1 << 16;
    const PDFResourceWalkResult result = PDFResourceWalk::walk(&document, { 0, 1 }, options, nullptr);
    QVERIFY(!result.isLimitReached);
    QCOMPARE(result.nodes.size(), size_t(4));
    std::vector<size_t> fonts;
    for (size_t i = 0; i < result.nodes.size(); ++i)
    {
        if (result.nodes[i].kind == PDFResourceOwnerKind::Type3Font)
        {
            QVERIFY(!result.nodes[i].reference.isValid());
            fonts.push_back(i);
        }
    }
    QCOMPARE(fonts.size(), size_t(2));
    for (const size_t font : fonts)
    {
        // Each font draws by the shared dictionary: an edge to both fonts (itself included)
        std::vector<size_t> children = result.nodes[font].children;
        std::sort(children.begin(), children.end());
        QCOMPARE(children, fonts);
        QCOMPARE(result.getPageIndices(font), std::vector<PDFInteger>({ 0, 1 }));
    }

    // (2) N forms sharing one resource dictionary, which lists all of them: N + 1 owners, (N + 1) * N edges;
    // the edges have the budget of the nodes (N * M edges of a file of a few megabytes are billions)
    constexpr int formCount = 200;
    WalkTestPdf shared = WalkTestPdf::createPages(1);
    shared.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources 20 0 R >>");
    QByteArray xObjects;
    for (int i = 0; i < formCount; ++i)
    {
        xObjects += "/X" + QByteArray::number(i) + " " + QByteArray::number(100 + i) + " 0 R ";
        shared.setStream(100 + i, "/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources 20 0 R", "0 0 1 1 re f");
    }
    shared.set(20, "<< /XObject << " + xObjects + " >> >>");
    const PDFDocument sharedDocument = shared.read();

    auto countEdges = [](const PDFResourceWalkResult& walk)
    {
        size_t count = 0;
        for (const PDFResourceWalkNode& node : walk.nodes)
        {
            count += node.children.size();
        }
        return count;
    };

    const PDFResourceWalkResult complete = PDFResourceWalk::walk(&sharedDocument, { 0 }, PDFResourceWalk::Options(), nullptr);
    QVERIFY(!complete.isLimitReached);
    QCOMPARE(complete.nodes.size(), size_t(formCount + 1));
    QCOMPARE(countEdges(complete), size_t(formCount + 1) * size_t(formCount));

    PDFResourceWalk::Options budget;
    budget.maximumNodes = 1000;
    const PDFResourceWalkResult limited = PDFResourceWalk::walk(&sharedDocument, { 0 }, budget, nullptr);
    QVERIFY(limited.isLimitReached);
    QCOMPARE(limited.nodes.size(), size_t(formCount + 1));
    QVERIFY(countEdges(limited) <= size_t(budget.maximumNodes));
}

QTEST_MAIN(ObjectWalkTest)

#include "tst_objectwalktest.moc"
