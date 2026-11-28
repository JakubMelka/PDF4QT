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

// Edit Content, stage E1, package P13, the core: the geometry of Align, Distribute and Match size
// and its command (pdfcontentarrange.*; zadani_jadro.md 4.13, zadani_overeni.md 8).
//
// Boxes are QRectF(x, y, width, height) in a space with the y axis up (the rules of the class
// PDFContentArrange): the left edge is x, the right edge x + width, the lower edge y and the upper
// edge y + height. Every expected box is computed by hand from these rules in the comments.
//
// The command is checked by the content the stream writer writes (tokens joined by one space).
// Matrices are written as in PDF: [a b c d e f] maps (x, y) to (a x + c y + e, b x + d y + f).

#include "contenttestdocuments.h"

#include "pdfcms.h"
#include "pdfcontentarrange.h"
#include "pdfcontentcommands.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentstreamwriter.h"
#include "pdfcontentundostack.h"
#include "pdfdocument.h"

#include <QtTest>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>

#include <memory>
#include <optional>
#include <tuple>
#include <vector>

using namespace pdf;

using Arrange = PDFContentArrange;
using Operation = PDFContentArrange::Operation;
using Documents = pdftest::ContentTestDocuments;
using Key = PDFContentContainerKey;
using Kind = PDFContentNodeKind;

/// A session over a document of the test
class ContentArrangeFixture
{
public:
    explicit ContentArrangeFixture(const QByteArray& content) :
        m_document(new PDFDocument(Documents::createDocument(content, &Documents::createNoResources))),
        m_session(new PDFContentEditSession(m_document, PDFCMSPointer(), nullptr))
    {
        const Key key = m_session->getPageKey(0);
        QString errorText;
        m_key = m_session->loadContainer(key, nullptr, &errorText) ? key : Key();
    }

    PDFContentEditSession* session() const { return m_session.get(); }
    const Key& key() const { return m_key; }

    /// The node of a kind by its order in the tree (not valid, when there is none)
    PDFContentNodeReference node(Kind kind, size_t index) const
    {
        std::vector<PDFContentNodeId> ids;
        const PDFContentContainerPointer container = m_session->getContainer(m_key);
        if (container && container->getRoot())
        {
            collect(container->getRoot(), kind, &ids);
        }
        return index < ids.size() ? PDFContentNodeReference{ m_key, ids[index] } : PDFContentNodeReference();
    }

    /// The written content of the page, white space collapsed
    QByteArray written() const
    {
        const PDFContentContainerPointer container = m_session->getContainer(m_key);
        if (!container)
        {
            return QByteArray("<not loaded>");
        }

        const PDFContentStreamWriteResult result = PDFContentStreamWriter::write(*container);
        return result.isSuccess() ? result.content.simplified() : QByteArray("<error>");
    }

    bool execute(const PDFContentCommandPointer& command, QString* errorText = nullptr)
    {
        QString text;
        const bool result = m_session->execute(command, &text);
        if (errorText)
        {
            *errorText = text;
        }
        return result;
    }

private:
    static void collect(const PDFContentNodePointer& node, Kind kind, std::vector<PDFContentNodeId>* result)
    {
        if (node->getKind() == kind)
        {
            result->push_back(node->getId());
        }
        for (const PDFContentNodePointer& child : node->getChildren())
        {
            collect(child, kind, result);
        }
    }

    PDFDocumentPointer m_document;
    std::unique_ptr<PDFContentEditSession> m_session;
    Key m_key;
};

class ContentArrangeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The geometry
    void operationNames();
    void align_data();
    void align();
    void degenerateBoxes();
    void centerOnPage_data();
    void centerOnPage();
    void distributeInRow();
    void distributeInColumn();
    void distributeAsForm();
    void distributeAsFormTies();
    void distributeAsFormLimit();
    void distributeAsGrid();
    void distributeAsGridTiming();
    void matchSize_data();
    void matchSize();
    void matchSizeOfLines();
    void refusals();

    // The command
    void commandAlign();
    void commandJsonReplay_data();
    void commandJsonReplay();
    void commandJsonErrors();
    void commandRefusedAsUnchanged();
    void commandGroupMovesOnce();
    void commandContainerToPage();
    void commandRefusals();

    // Stage E1, fix package FX1: the space the user sees (review R5-6)
    void displayedPageMatrix();
    void commandOnRotatedPage();

private:
    /// The boxes of the operation after their matrices (normalized boxes mapped), no value when
    /// computeMatrices refuses
    static std::optional<QList<QRectF>> arrange(Operation operation, const QList<QRectF>& boxes, size_t keyIndex, const QRectF& pageBox = QRectF(), QString* errorText = nullptr)
    {
        const std::vector<QRectF> input(boxes.cbegin(), boxes.cend());
        const std::optional<std::vector<QTransform>> matrices = Arrange::computeMatrices(operation, input, keyIndex, pageBox, errorText);
        if (!matrices || matrices->size() != input.size())
        {
            return std::nullopt;
        }

        QList<QRectF> result;
        for (size_t i = 0; i < input.size(); ++i)
        {
            result.push_back((*matrices)[i].mapRect(input[i].normalized()));
        }
        return result;
    }

    static QByteArray describe(const QList<QRectF>& boxes)
    {
        QByteArray result;
        for (const QRectF& box : boxes)
        {
            result += "(" + QByteArray::number(box.x()) + " " + QByteArray::number(box.y()) + " " + QByteArray::number(box.width()) + " " + QByteArray::number(box.height()) + ") ";
        }
        return result;
    }

    static QByteArray describe(const std::optional<QList<QRectF>>& boxes)
    {
        return boxes ? describe(*boxes) : QByteArray("<refused>");
    }

    /// A time limit in milliseconds scaled by PDF4QT_TEST_TIMEOUT_FACTOR
    static qint64 timeout(qint64 milliseconds)
    {
        bool isValid = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &isValid);
        return milliseconds * (isValid && factor > 1 ? qMin(factor, 1000) : 1);
    }

    static QJsonArray matrixArray(qreal a, qreal b, qreal c, qreal d, qreal e, qreal f) { return QJsonArray({ a, b, c, d, e, f }); }

    /// The three boxes of the geometry tests:
    /// b0: left 10, right 30, lower 10, upper 20, center (20, 15)
    /// b1: left 50, right 90, lower 40, upper 60, center (70, 50) - the key box (index 1)
    /// b2: left 100, right 110, lower 0, upper 30, center (105, 15)
    static QList<QRectF> threeBoxes() { return { QRectF(10, 10, 20, 10), QRectF(50, 40, 40, 20), QRectF(100, 0, 10, 30) }; }
};

#define ARRANGE_COMPARE(actual, expected) \
    do \
    { \
        const std::optional<QList<QRectF>> arrangeResult = (actual); \
        const QList<QRectF> arrangeExpected = (expected); \
        QVERIFY2(arrangeResult.has_value() && *arrangeResult == arrangeExpected, (describe(arrangeResult) + "!= expected " + describe(arrangeExpected)).constData()); \
    } while (false)

// -------------------------------------------------------------------------------------------------
//  The geometry
// -------------------------------------------------------------------------------------------------

void ContentArrangeTest::operationNames()
{
    // The names of the header, written out
    const std::vector<std::tuple<Operation, QByteArray, QByteArray>> table = {
        { Operation::AlignLeft, "align", "left" },
        { Operation::AlignHorizontalCenter, "align", "horizontal-center" },
        { Operation::AlignRight, "align", "right" },
        { Operation::AlignTop, "align", "top" },
        { Operation::AlignVerticalCenter, "align", "vertical-center" },
        { Operation::AlignBottom, "align", "bottom" },
        { Operation::CenterHorizontallyOnPage, "align", "page-horizontal-center" },
        { Operation::CenterVerticallyOnPage, "align", "page-vertical-center" },
        { Operation::CenterOnPage, "align", "page-center" },
        { Operation::DistributeInRow, "distribute", "row" },
        { Operation::DistributeInColumn, "distribute", "column" },
        { Operation::DistributeAsForm, "distribute", "form" },
        { Operation::DistributeAsGrid, "distribute", "grid" },
        { Operation::MatchWidth, "match_size", "width" },
        { Operation::MatchHeight, "match_size", "height" },
        { Operation::MatchSize, "match_size", "size" }
    };

    for (const auto& [operation, type, name] : table)
    {
        QCOMPARE(Arrange::getType(operation), type);
        QCOMPARE(Arrange::getOperationName(operation), name);
        QVERIFY(Arrange::getOperation(type, name) == operation);

        const bool isCenter = operation == Operation::CenterHorizontallyOnPage || operation == Operation::CenterVerticallyOnPage || operation == Operation::CenterOnPage;
        QCOMPARE(Arrange::getMinimumBoxCount(operation), size_t(isCenter ? 1 : 2));
    }

    // A name of another type is not an operation of the type
    QVERIFY(!Arrange::getOperation("align", "row").has_value());
    QVERIFY(!Arrange::getOperation("distribute", "left").has_value());
    QVERIFY(!Arrange::getOperation("match_size", "diagonal").has_value());
    QVERIFY(!Arrange::getOperation("transform", "left").has_value());
}

void ContentArrangeTest::align_data()
{
    QTest::addColumn<int>("operation");
    QTest::addColumn<QList<QRectF>>("expected");

    // The key b1 stays; b0 and b2 move to its edge or center
    // left 50: b0 x 10 -> 50, b2 x 100 -> 50
    QTest::newRow("left") << int(Operation::AlignLeft) << QList<QRectF>({ QRectF(50, 10, 20, 10), QRectF(50, 40, 40, 20), QRectF(50, 0, 10, 30) });
    // center x 70: b0 width 20 -> left 60, b2 width 10 -> left 65
    QTest::newRow("horizontal center") << int(Operation::AlignHorizontalCenter) << QList<QRectF>({ QRectF(60, 10, 20, 10), QRectF(50, 40, 40, 20), QRectF(65, 0, 10, 30) });
    // right 90: b0 left 90 - 20 = 70, b2 left 90 - 10 = 80
    QTest::newRow("right") << int(Operation::AlignRight) << QList<QRectF>({ QRectF(70, 10, 20, 10), QRectF(50, 40, 40, 20), QRectF(80, 0, 10, 30) });
    // upper edge 60: b0 lower 60 - 10 = 50, b2 lower 60 - 30 = 30
    QTest::newRow("top") << int(Operation::AlignTop) << QList<QRectF>({ QRectF(10, 50, 20, 10), QRectF(50, 40, 40, 20), QRectF(100, 30, 10, 30) });
    // center y 50: b0 lower 50 - 5 = 45, b2 lower 50 - 15 = 35
    QTest::newRow("vertical center") << int(Operation::AlignVerticalCenter) << QList<QRectF>({ QRectF(10, 45, 20, 10), QRectF(50, 40, 40, 20), QRectF(100, 35, 10, 30) });
    // lower edge 40: b0 and b2 lower 40
    QTest::newRow("bottom") << int(Operation::AlignBottom) << QList<QRectF>({ QRectF(10, 40, 20, 10), QRectF(50, 40, 40, 20), QRectF(100, 40, 10, 30) });
}

void ContentArrangeTest::align()
{
    QFETCH(int, operation);
    QFETCH(QList<QRectF>, expected);

    ARRANGE_COMPARE(arrange(Operation(operation), threeBoxes(), 1), expected);

    // The key box does not move: its matrix is the identity
    const std::vector<QRectF> boxes = { QRectF(10, 10, 20, 10), QRectF(50, 40, 40, 20), QRectF(100, 0, 10, 30) };
    const std::optional<std::vector<QTransform>> matrices = Arrange::computeMatrices(Operation(operation), boxes, 1, QRectF(), nullptr);
    QVERIFY(matrices.has_value());
    QVERIFY((*matrices)[1].isIdentity());

    // Another key: b0 (index 0) is the reference then and b1 moves to it
    // (left: b1 x 50 -> 10)
    if (Operation(operation) == Operation::AlignLeft)
    {
        ARRANGE_COMPARE(arrange(Operation::AlignLeft, threeBoxes(), 0), QList<QRectF>({ QRectF(10, 10, 20, 10), QRectF(10, 40, 40, 20), QRectF(10, 0, 10, 30) }));
    }
}

void ContentArrangeTest::degenerateBoxes()
{
    // n0 has a negative width: normalized it is (10, 10, 20, 10). A null box, a box with NaN and
    // a box of an infinite width take part in nothing (identity, not in the union); the vertical
    // line v (0, 0, 0, 40) has a height, so it takes part.
    const QList<QRectF> boxes = { QRectF(30, 10, -20, 10), QRectF(1000, 1000, 0, 0), QRectF(qQNaN(), 0, 10, 10), QRectF(0, 0, 0, 40), QRectF(0, 0, qInf(), 10) };

    // Center on the page (0, 0, 200, 100), center (100, 50). The union of n0 and v: left 0,
    // right 30, lower 0, upper 40, center (15, 20): the offset (85, 30).
    // n0 -> (95, 40, 20, 10), v -> (85, 30, 0, 40)
    const std::vector<QRectF> input(boxes.cbegin(), boxes.cend());
    const std::optional<std::vector<QTransform>> matrices = Arrange::computeMatrices(Operation::CenterOnPage, input, 0, QRectF(0, 0, 200, 100), nullptr);
    QVERIFY(matrices.has_value());
    QCOMPARE((*matrices)[0].mapRect(QRectF(10, 10, 20, 10)), QRectF(95, 40, 20, 10));
    QCOMPARE((*matrices)[3].mapRect(QRectF(0, 0, 0, 40)), QRectF(85, 30, 0, 40));
    QVERIFY((*matrices)[1].isIdentity());
    QVERIFY((*matrices)[2].isIdentity());
    QVERIFY((*matrices)[4].isIdentity());

    // Align right to the key (50, 40, 40, 20) (right 90): n0 normalized gets the left edge 70;
    // the line v gets the left edge 90
    const std::vector<QRectF> alignInput = { QRectF(30, 10, -20, 10), QRectF(50, 40, 40, 20), QRectF(1000, 1000, 0, 0), QRectF(0, 0, 0, 40) };
    const std::optional<std::vector<QTransform>> aligned = Arrange::computeMatrices(Operation::AlignRight, alignInput, 1, QRectF(), nullptr);
    QVERIFY(aligned.has_value());
    QCOMPARE((*aligned)[0].mapRect(QRectF(10, 10, 20, 10)), QRectF(70, 10, 20, 10));
    QCOMPARE((*aligned)[3].mapRect(QRectF(0, 0, 0, 40)), QRectF(90, 0, 0, 40));
    QVERIFY((*aligned)[2].isIdentity());
}

void ContentArrangeTest::centerOnPage_data()
{
    QTest::addColumn<int>("operation");
    QTest::addColumn<QList<QRectF>>("expected");

    // The page (0, 0, 200, 100): center (100, 50). The union of the three boxes: left 10,
    // right 110, lower 0, upper 60, center (60, 30). The offset (40, 20); every box by the same.
    QTest::newRow("horizontally") << int(Operation::CenterHorizontallyOnPage) << QList<QRectF>({ QRectF(50, 10, 20, 10), QRectF(90, 40, 40, 20), QRectF(140, 0, 10, 30) });
    QTest::newRow("vertically") << int(Operation::CenterVerticallyOnPage) << QList<QRectF>({ QRectF(10, 30, 20, 10), QRectF(50, 60, 40, 20), QRectF(100, 20, 10, 30) });
    QTest::newRow("both") << int(Operation::CenterOnPage) << QList<QRectF>({ QRectF(50, 30, 20, 10), QRectF(90, 60, 40, 20), QRectF(140, 20, 10, 30) });
}

void ContentArrangeTest::centerOnPage()
{
    QFETCH(int, operation);
    QFETCH(QList<QRectF>, expected);

    // The key index is ignored (even one out of range)
    ARRANGE_COMPARE(arrange(Operation(operation), threeBoxes(), 99, QRectF(0, 0, 200, 100)), expected);

    // The page box with negative sizes is normalized: (200, 100, -200, -100) is the same page
    ARRANGE_COMPARE(arrange(Operation(operation), threeBoxes(), 0, QRectF(200, 100, -200, -100)), expected);

    // One box is enough: b0 (center (20, 15)) alone to (100, 50) by (80, 35) in both directions
    if (Operation(operation) == Operation::CenterOnPage)
    {
        ARRANGE_COMPARE(arrange(Operation::CenterOnPage, { QRectF(10, 10, 20, 10) }, 0, QRectF(0, 0, 200, 100)), QList<QRectF>({ QRectF(90, 45, 20, 10) }));
    }
}

void ContentArrangeTest::distributeInRow()
{
    // Sorted by the left edges: b0 (10), b1 (50), b2 (100); U.left = 10. b0 at 10 (stays),
    // b1 at 10 + 20 = 30, b2 at 30 + 40 = 70; only x changes
    ARRANGE_COMPARE(arrange(Operation::DistributeInRow, threeBoxes(), 0), QList<QRectF>({ QRectF(10, 10, 20, 10), QRectF(30, 40, 40, 20), QRectF(70, 0, 10, 30) }));

    // A tie: c0 and c2 have the same left edge 20, so they keep the order of the input.
    // Sorted: c1 (5), c0 (20), c2 (20); U.left = 5. c1 at 5, c0 at 5 + 30 = 35, c2 at 35 + 10 = 45.
    // (The other order of the tie would put c2 at 35 and c0 at 40.)
    const QList<QRectF> tie = { QRectF(20, 0, 10, 10), QRectF(5, 0, 30, 10), QRectF(20, 50, 5, 5) };
    ARRANGE_COMPARE(arrange(Operation::DistributeInRow, tie, 0), QList<QRectF>({ QRectF(35, 0, 10, 10), QRectF(5, 0, 30, 10), QRectF(45, 50, 5, 5) }));
}

void ContentArrangeTest::distributeInColumn()
{
    // The old rules (LayoutVertically of PDFPageContentElementManipulator, review R5-4): sorted by
    // the lower edges (QRectF::y), the highest first: b1 (40), b0 (10), b2 (0); U.upper = 60.
    // b1 upper 60 (stays); b0 upper 60 - 20 = 40 -> lower 30; b2 upper 40 - 10 = 30 -> lower 0
    // (stays). (Sorted by the upper edges b2 would come before b0.)
    ARRANGE_COMPARE(arrange(Operation::DistributeInColumn, threeBoxes(), 0), QList<QRectF>({ QRectF(10, 30, 20, 10), QRectF(50, 40, 40, 20), QRectF(100, 0, 10, 30) }));

    // A tie: d0 and d1 have the same lower edge 0 and keep the order of the input.
    // Sorted: d2 (20), d0 (0), d1 (0); U.upper = 25. d2 stays; d0 upper 25 - 5 = 20 -> lower 10;
    // d1 upper 20 - 10 = 10 -> lower 5. The other order would give d1 lower 15, d0 lower 5.
    const QList<QRectF> tie = { QRectF(0, 0, 10, 10), QRectF(50, 0, 10, 5), QRectF(20, 20, 10, 5) };
    ARRANGE_COMPARE(arrange(Operation::DistributeInColumn, tie, 0), QList<QRectF>({ QRectF(0, 10, 10, 10), QRectF(50, 5, 10, 5), QRectF(20, 20, 10, 5) }));

    // Boxes, which overlap in y (the example of R5-4): A (0, 0, 10, 100), B (20, 50, 10, 10). By the
    // lower edges B (50) is first: B upper 100 -> lower 90; A upper 90 -> lower -10. (By the upper
    // edges A would stay and B would go to -10.)
    ARRANGE_COMPARE(arrange(Operation::DistributeInColumn, { QRectF(0, 0, 10, 100), QRectF(20, 50, 10, 10) }, 0),
                    QList<QRectF>({ QRectF(0, -10, 10, 100), QRectF(20, 90, 10, 10) }));
}

void ContentArrangeTest::distributeAsForm()
{
    // L0 (0, 80, 30, 10)   x 0..30,   y 80..90
    // R0 (100, 78, 50, 10) x 100..150, y 78..88
    // L1 (0, 50, 40, 10)   x 0..40,   y 50..60
    // R1 (110, 52, 40, 20) x 110..150, y 52..72
    // L2 (5, 20, 20, 10)   x 5..25,   y 20..30
    // R2 (90, 0, 60, 10)   x 90..150, y 0..10
    // U: left 0, right 150, lower 0, upper 90.
    // Columns (distance of the left edge from 0 against the right edge from 150): L0 0 < 120,
    // L1 0 < 110, L2 5 < 125 -> left; R0 100 >= 0, R1 110 >= 0, R2 90 >= 0 -> right. W = 60.
    // Overlaps: L0-R0 88 - 80 = 8; L1-R1 60 - 52 = 8; no other pair overlaps (L2 [20, 30] and
    // R2 [0, 10] do not). Pairs (L0, R0), (L1, R1); L2 and R2 are rows of their own.
    // Row centers: (85 + 83) / 2 = 84; (55 + 62) / 2 = 58.5; L2 25; R2 5 - the order stays.
    // Placed from y = 90, left boxes at x 0, right boxes at x 150 - 60 = 90:
    //  row 0: L0 upper 90 -> (0, 80); R0 upper 90 -> (90, 80); height 10 -> y 80
    //  row 1: L1 upper 80 -> (0, 70); R1 upper 80, height 20 -> (90, 60); height 20 -> y 60
    //  row 2: L2 upper 60 -> (0, 50); y 50
    //  row 3: R2 upper 50 -> (90, 40)
    const QList<QRectF> boxes = { QRectF(0, 80, 30, 10), QRectF(100, 78, 50, 10), QRectF(0, 50, 40, 10), QRectF(110, 52, 40, 20), QRectF(5, 20, 20, 10), QRectF(90, 0, 60, 10) };
    ARRANGE_COMPARE(arrange(Operation::DistributeAsForm, boxes, 0),
                    QList<QRectF>({ QRectF(0, 80, 30, 10), QRectF(90, 80, 50, 10), QRectF(0, 70, 40, 10), QRectF(90, 60, 40, 20), QRectF(0, 50, 20, 10), QRectF(90, 40, 60, 10) }));

    // The rows are sorted by their centers: the same boxes in the reverse order of the input
    // give the same places (the pairs and the single rows are found by the geometry)
    QList<QRectF> reversed(boxes.crbegin(), boxes.crend());
    ARRANGE_COMPARE(arrange(Operation::DistributeAsForm, reversed, 0),
                    QList<QRectF>({ QRectF(90, 40, 60, 10), QRectF(0, 50, 20, 10), QRectF(90, 60, 40, 20), QRectF(0, 70, 40, 10), QRectF(90, 80, 50, 10), QRectF(0, 80, 30, 10) }));
}

void ContentArrangeTest::distributeAsFormTies()
{
    // A tie of two right boxes: P0 (0, 50, 20, 10) y 50..60 (left); Q0 (100, 55, 20, 10)
    // y 55..65 and Q1 (100, 45, 20, 10) y 45..55 (right). Both overlaps are 5: the smaller
    // index of the right box wins, the pair is (P0, Q0); Q1 is a row of its own.
    // U: left 0, right 120, lower 45, upper 65; W = 20, right x = 100.
    // Rows: (P0, Q0) center (55 + 60) / 2 = 57.5, Q1 center 50.
    //  row 0 from 65: P0 -> (0, 55), Q0 -> (100, 55); height 10 -> y 55
    //  row 1: Q1 upper 55 -> (100, 45)
    // (The pair (P0, Q1) would put P0 at (0, 45).)
    ARRANGE_COMPARE(arrange(Operation::DistributeAsForm, { QRectF(0, 50, 20, 10), QRectF(100, 55, 20, 10), QRectF(100, 45, 20, 10) }, 0),
                    QList<QRectF>({ QRectF(0, 55, 20, 10), QRectF(100, 55, 20, 10), QRectF(100, 45, 20, 10) }));

    // A tie of two left boxes: Q (100, 50, 20, 10) y 50..60 (right, index 0); P0 (0, 55, 20, 10)
    // y 55..65 (index 1) and P1 (0, 45, 20, 10) y 45..55 (index 2). Both overlaps are 5: the
    // smaller index of the left box wins, the pair is (P0, Q); P1 is a row of its own.
    // Rows: (P0, Q) center (60 + 55) / 2 = 57.5, P1 center 50.
    //  row 0 from 65: P0 -> (0, 55), Q -> (100, 55); y 55
    //  row 1: P1 -> (0, 45)
    // (The pair (P1, Q) would put Q at (100, 45).)
    ARRANGE_COMPARE(arrange(Operation::DistributeAsForm, { QRectF(100, 50, 20, 10), QRectF(0, 55, 20, 10), QRectF(0, 45, 20, 10) }, 0),
                    QList<QRectF>({ QRectF(100, 55, 20, 10), QRectF(0, 55, 20, 10), QRectF(0, 45, 20, 10) }));

    // A larger overlap wins over a smaller index: P0 y 50..60 overlaps Q0 y 58..68 by 2 and
    // Q1 y 51..61 by 9: the pair is (P0, Q1), Q0 is a row of its own.
    // U: left 0, right 120, lower 50, upper 68. Rows: Q0 center 63, (P0, Q1) (55 + 56) / 2 = 55.5.
    //  row 0 from 68: Q0 -> (100, 58) (stays); y 58
    //  row 1: P0 upper 58 -> (0, 48), Q1 upper 58 -> (100, 48)
    ARRANGE_COMPARE(arrange(Operation::DistributeAsForm, { QRectF(0, 50, 20, 10), QRectF(100, 58, 20, 10), QRectF(100, 51, 20, 10) }, 0),
                    QList<QRectF>({ QRectF(0, 48, 20, 10), QRectF(100, 58, 20, 10), QRectF(100, 48, 20, 10) }));

    // Boxes, which only touch (the overlap is zero), make no pair: P (0, 0, 20, 10) y 0..10,
    // Q (100, 10, 20, 10) y 10..20. U: left 0, right 120, lower 0, upper 20. Rows: Q (15), P (5).
    //  row 0: Q upper 20 (stays); row 1: P upper 10 (stays) - nothing moves
    ARRANGE_COMPARE(arrange(Operation::DistributeAsForm, { QRectF(0, 0, 20, 10), QRectF(100, 10, 20, 10) }, 0),
                    QList<QRectF>({ QRectF(0, 0, 20, 10), QRectF(100, 10, 20, 10) }));
}

void ContentArrangeTest::distributeAsFormLimit()
{
    // MAXIMUM_FORM_BOXES boxes are a form; one more is refused. The boxes alternate between the
    // columns (x 0..10 and 100..110) and no two overlap in y (box i: y 20 i .. 20 i + 10).
    QList<QRectF> boxes;
    for (int i = 0; i < int(Arrange::MAXIMUM_FORM_BOXES); ++i)
    {
        boxes.push_back(QRectF(i % 2 ? 100 : 0, 20 * i, 10, 10));
    }

    QString errorText;
    QVERIFY(arrange(Operation::DistributeAsForm, boxes, 0, QRectF(), &errorText).has_value());

    boxes.push_back(QRectF(0, 20.0 * qreal(boxes.size()), 10, 10));
    QVERIFY(!arrange(Operation::DistributeAsForm, boxes, 0, QRectF(), &errorText).has_value());
    QVERIFY(!errorText.isEmpty());

    // The other distributions have no such bound
    QVERIFY(arrange(Operation::DistributeInRow, boxes, 0).has_value());
}

void ContentArrangeTest::distributeAsGrid()
{
    // 3 rows x 2 columns of irregular boxes:
    // g0 (0, 100, 30, 20)  x 0..30,  y 100..120
    // g1 (60, 105, 20, 30) x 60..80, y 105..135
    // g2 (5, 50, 40, 25)   x 5..45,  y 50..75
    // g3 (55, 60, 30, 10)  x 55..85, y 60..70
    // g4 (2, 0, 20, 15)    x 2..22,  y 0..15
    // g5 (70, 10, 15, 10)  x 70..85, y 10..20
    // U: left 0, right 85, lower 0, upper 135.
    // Rows (the old rules of LayoutGrid, review R5-4: ascending by the lower edges g4 0, g5 10,
    // g2 50, g3 60, g0 100, g1 105; the last starts a row and takes the boxes, whose upper edge is
    // above its lower edge):
    //  g1 starts row 0 (lower 105): g0 (upper 120 > 105) joins; height max(30, 20) = 30
    //  g3 starts row 1 (lower 60): g2 (75 > 60) joins; height max(10, 25) = 25
    //  g5 starts row 2 (lower 10): g4 (15 > 10) joins; height max(10, 15) = 15
    // Columns (descending by the left edges: g5 70, g1 60, g3 55, g2 5, g4 2, g0 0):
    //  g0 starts column 0 (right 30): g4 (2 < 30), g2 (5 < 30) join; g3 (55) does not; width 40
    //  g3 starts column 1 (right 85): g1, g5 join; width max(30, 20, 15) = 30
    // Cells: column x 0 / 40; row upper edges 135 / 105 / 80.
    //  g0 (0, 115), g1 (40, 105), g2 (0, 80), g3 (40, 95), g4 (0, 65), g5 (40, 70)
    const QList<QRectF> boxes = { QRectF(0, 100, 30, 20), QRectF(60, 105, 20, 30), QRectF(5, 50, 40, 25), QRectF(55, 60, 30, 10), QRectF(2, 0, 20, 15), QRectF(70, 10, 15, 10) };
    ARRANGE_COMPARE(arrange(Operation::DistributeAsGrid, boxes, 0),
                    QList<QRectF>({ QRectF(0, 115, 30, 20), QRectF(40, 105, 20, 30), QRectF(0, 80, 40, 25), QRectF(40, 95, 30, 10), QRectF(0, 65, 20, 15), QRectF(40, 70, 15, 10) }));

    // Boxes, which only touch, are in different rows and columns: h1 (0, 0, 10, 10) and
    // h2 (10, 10, 10, 10) - y 0..10 and 10..20 overlap by zero, x 0..10 and 10..20 too.
    // U: left 0, upper 20. Rows: h2 (row 0), h1 (row 1); columns: h1 (column 0), h2 (column 1).
    // h1: left 0, upper 20 - 10 = 10 (stays); h2: left 10, upper 20 (stays). Shifted: h2
    // (20, 10) gets the left edge 10.
    ARRANGE_COMPARE(arrange(Operation::DistributeAsGrid, { QRectF(0, 0, 10, 10), QRectF(20, 10, 10, 10) }, 0),
                    QList<QRectF>({ QRectF(0, 0, 10, 10), QRectF(10, 10, 10, 10) }));

    // A horizontal line overlaps nothing in y (its interval has no length): a (0, 0, 10, 20)
    // y 0..20, the line z (20, 10, 10, 0) at y 10, x 20..30. By the lower edges z (10) is the last:
    // it starts row 0 (height 0) and takes nothing; a is row 1 (height 20). Columns: a (column 0,
    // width 10), z (column 1, width 10). U: left 0, upper 20. Row 0 is 0 high, so both rows have
    // their upper edges at 20: a stays; z: left 10, upper 20 -> (10, 20, 10, 0). (The rule of
    // P13, which started the rows at the highest upper edge, put z into row 1, at 0.)
    ARRANGE_COMPARE(arrange(Operation::DistributeAsGrid, { QRectF(0, 0, 10, 20), QRectF(20, 10, 10, 0) }, 0),
                    QList<QRectF>({ QRectF(0, 0, 10, 20), QRectF(10, 20, 10, 0) }));

    // The rows of the example of R5-4: X (0, 80, 10, 10) y 80..90, Y (20, 0, 10, 100) y 0..100,
    // Z (40, 95, 10, 4) y 95..99, each in a column of its own. By the lower edges (Y 0, X 80, Z 95)
    // Z starts row 0 and takes Y (upper 100 > 95), not X (90); row 0 is 100 high, X is row 1, 10
    // high. Columns X, Y, Z (widths 10). U: left 0, upper 100.
    //  X: row 1, column 0 -> left 0, upper 100 - 100 = 0: (0, -10)
    //  Y: row 0, column 1 -> left 10, upper 100: (10, 0)
    //  Z: row 0, column 2 -> left 20, upper 100: (20, 96)
    // (Started at the highest upper edge, Y would take X and Z into one row.)
    ARRANGE_COMPARE(arrange(Operation::DistributeAsGrid, { QRectF(0, 80, 10, 10), QRectF(20, 0, 10, 100), QRectF(40, 95, 10, 4) }, 0),
                    QList<QRectF>({ QRectF(0, -10, 10, 10), QRectF(10, 0, 10, 100), QRectF(20, 96, 10, 4) }));

    // A tie of the lower edges: the last of the input starts the row (the stable sort). The line p
    // (0, 0, 10, 0) and q (20, 0, 10, 30) have the lower edge 0. Columns: p (column 0), q (column 1),
    // widths 10; U: left 0, upper 30.
    //  [p, q]: q starts row 0 (30 high) and does not take p (its upper edge 0 is not above 0); p is
    //  row 1 (0 high): p upper 30 - 30 = 0 (stays), q (10, 0)
    //  [q, p]: p starts row 0 (0 high, it takes nothing), q is row 1: both upper edges at 30 - q
    //  (10, 0), p (0, 30)
    ARRANGE_COMPARE(arrange(Operation::DistributeAsGrid, { QRectF(0, 0, 10, 0), QRectF(20, 0, 10, 30) }, 0),
                    QList<QRectF>({ QRectF(0, 0, 10, 0), QRectF(10, 0, 10, 30) }));
    ARRANGE_COMPARE(arrange(Operation::DistributeAsGrid, { QRectF(20, 0, 10, 30), QRectF(0, 0, 10, 0) }, 0),
                    QList<QRectF>({ QRectF(10, 0, 10, 30), QRectF(0, 30, 10, 0) }));

    // A vertical line overlaps nothing in x: a (0, 0, 10, 10) and the line v (5, 0, 0, 10). Rows: one
    // (v, the last of the tie of the lower edges 0, takes a: the upper edge 10 is above 0), 10 high.
    // Columns: a starts column 0 (smallest left edge) and does not take v (no width); v is column 1,
    // 0 wide. U: left 0, upper 10. a stays; v: left 0 + 10 = 10 -> (10, 0, 0, 10).
    ARRANGE_COMPARE(arrange(Operation::DistributeAsGrid, { QRectF(0, 0, 10, 10), QRectF(5, 0, 0, 10) }, 0),
                    QList<QRectF>({ QRectF(0, 0, 10, 10), QRectF(10, 0, 0, 10) }));

    // A line, which starts a row, takes nothing: a (0, 0, 10, 20), the line z (20, 10, 10, 0) and d
    // (40, -5, 10, 7) y -5..2. By the lower edges (d -5, a 0, z 10) z starts row 0 (0 high) alone;
    // a starts row 1 and takes d (upper 2 > 0), 20 high. Columns a, z, d (widths 10). U: left 0,
    // upper 20; both rows begin at 20. a stays; z (10, 20, 10, 0); d upper 20 -> (20, 13, 10, 7).
    // (If z took a, d would be a row of its own below a: (20, -7).)
    ARRANGE_COMPARE(arrange(Operation::DistributeAsGrid, { QRectF(0, 0, 10, 20), QRectF(20, 10, 10, 0), QRectF(40, -5, 10, 7) }, 0),
                    QList<QRectF>({ QRectF(0, 0, 10, 20), QRectF(10, 20, 10, 0), QRectF(20, 13, 10, 7) }));
}

void ContentArrangeTest::distributeAsGridTiming()
{
    // 100 000 boxes on a diagonal: box i (2 i, 2 i, 1, 1); no two overlap in x or in y, so each
    // is a row and a column of its own. An algorithm, which compares every box with every
    // remaining one, makes 5 x 10^9 comparisons here; O(n log n) is a few milliseconds.
    // Expected: box i is in the column i (left i x 1 = i) and in the row n - 1 - i (the highest
    // box is row 0); U.upper = 2 (n - 1) + 1 = 2 n - 1, so its upper edge is 2 n - 1 - (n - 1 - i)
    // = n + i: the box (i, n + i - 1, 1, 1).
    const size_t count = 100000;
    std::vector<QRectF> boxes;
    boxes.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        boxes.push_back(QRectF(2.0 * qreal(i), 2.0 * qreal(i), 1, 1));
    }

    QElapsedTimer timer;
    timer.start();
    const std::optional<std::vector<QTransform>> matrices = Arrange::computeMatrices(Operation::DistributeAsGrid, boxes, 0, QRectF(), nullptr);
    const qint64 elapsed = timer.elapsed();

    QVERIFY(matrices.has_value());
    QVERIFY2(elapsed < timeout(2000), qPrintable(QStringLiteral("%1 ms").arg(elapsed)));

    for (size_t i = 0; i < count; ++i)
    {
        const QRectF result = (*matrices)[i].mapRect(boxes[i]);
        if (result != QRectF(qreal(i), qreal(count + i - 1), 1, 1))
        {
            QFAIL(qPrintable(QStringLiteral("box %1: (%2, %3)").arg(i).arg(result.x()).arg(result.y())));
        }
    }
}

void ContentArrangeTest::matchSize_data()
{
    QTest::addColumn<int>("operation");
    QTest::addColumn<QList<QRectF>>("expected");

    // To the key b1 (width 40, height 20); the left and the upper edge of a box stay.
    // b0 (x 10, upper 20): width 20 -> 40, height 10 -> 20 (lower 20 - 20 = 0)
    // b2 (x 100, upper 30): width 10 -> 40, height 30 -> 20 (lower 30 - 20 = 10)
    QTest::newRow("width") << int(Operation::MatchWidth) << QList<QRectF>({ QRectF(10, 10, 40, 10), QRectF(50, 40, 40, 20), QRectF(100, 0, 40, 30) });
    QTest::newRow("height") << int(Operation::MatchHeight) << QList<QRectF>({ QRectF(10, 0, 20, 20), QRectF(50, 40, 40, 20), QRectF(100, 10, 10, 20) });
    QTest::newRow("size") << int(Operation::MatchSize) << QList<QRectF>({ QRectF(10, 0, 40, 20), QRectF(50, 40, 40, 20), QRectF(100, 10, 40, 20) });
}

void ContentArrangeTest::matchSize()
{
    QFETCH(int, operation);
    QFETCH(QList<QRectF>, expected);

    ARRANGE_COMPARE(arrange(Operation(operation), threeBoxes(), 1), expected);

    // The matrix of b0 for the size: x' = 2 (x - 10) + 10 = 2 x - 10, y' = 2 (y - 20) + 20 = 2 y - 20
    if (Operation(operation) == Operation::MatchSize)
    {
        const std::vector<QRectF> boxes = { QRectF(10, 10, 20, 10), QRectF(50, 40, 40, 20) };
        const std::optional<std::vector<QTransform>> matrices = Arrange::computeMatrices(Operation::MatchSize, boxes, 1, QRectF(), nullptr);
        QVERIFY(matrices.has_value());
        QCOMPARE((*matrices)[0], QTransform(2, 0, 0, 2, -10, -20));
        QVERIFY((*matrices)[1].isIdentity());
    }
}

void ContentArrangeTest::matchSizeOfLines()
{
    // A horizontal line l (0, 50, 30, 0) and a vertical line v (5, 0, 0, 25) to the key
    // (50, 40, 40, 20): the zero dimension is not scaled.
    // l: width 30 -> 40 (x 0 stays), height 0 stays -> (0, 50, 40, 0)
    // v: width 0 stays, height 25 -> 20 with the upper edge 25 -> (5, 5, 0, 20)
    const QList<QRectF> lines = { QRectF(0, 50, 30, 0), QRectF(50, 40, 40, 20), QRectF(5, 0, 0, 25) };
    ARRANGE_COMPARE(arrange(Operation::MatchSize, lines, 1), QList<QRectF>({ QRectF(0, 50, 40, 0), QRectF(50, 40, 40, 20), QRectF(5, 5, 0, 20) }));

    // Every matrix is regular (a zero scale would be singular)
    const std::vector<QRectF> input(lines.cbegin(), lines.cend());
    const std::optional<std::vector<QTransform>> matrices = Arrange::computeMatrices(Operation::MatchSize, input, 1, QRectF(), nullptr);
    QVERIFY(matrices.has_value());
    for (const QTransform& matrix : *matrices)
    {
        QVERIFY(matrix.isInvertible());
    }

    // The key is the vertical line (width 0): the widths are not matched at all, only the heights.
    // b0 (10, 10, 20, 10) upper 20: height 10 -> 25, lower 20 - 25 = -5
    ARRANGE_COMPARE(arrange(Operation::MatchWidth, { QRectF(10, 10, 20, 10), QRectF(5, 0, 0, 25) }, 1), QList<QRectF>({ QRectF(10, 10, 20, 10), QRectF(5, 0, 0, 25) }));
    ARRANGE_COMPARE(arrange(Operation::MatchSize, { QRectF(10, 10, 20, 10), QRectF(5, 0, 0, 25) }, 1), QList<QRectF>({ QRectF(10, -5, 20, 25), QRectF(5, 0, 0, 25) }));
}

void ContentArrangeTest::refusals()
{
    const QRectF b0(10, 10, 20, 10);
    const QRectF b1(50, 40, 40, 20);
    const QRectF null(5, 5, 0, 0);
    const QRectF page(0, 0, 200, 100);

    struct Case
    {
        const char* name;
        Operation operation;
        QList<QRectF> boxes;
        size_t keyIndex;
        QRectF pageBox;
    };

    const std::vector<Case> cases = {
        // Fewer boxes with a size than the operation needs (2; the center needs 1)
        { "align one box", Operation::AlignLeft, { b0 }, 0, page },
        { "align a box and a null box", Operation::AlignLeft, { b0, null }, 0, page },
        { "distribute one box", Operation::DistributeInRow, { b0 }, 0, page },
        { "grid of a box and a null box", Operation::DistributeAsGrid, { b0, null }, 0, page },
        { "match one box", Operation::MatchSize, { b1 }, 0, page },
        { "center no box", Operation::CenterOnPage, {}, 0, page },
        { "center a null box", Operation::CenterOnPage, { null }, 0, page },
        // The key index out of range or of a box, which takes part in nothing
        { "align key out of range", Operation::AlignLeft, { b0, b1 }, 2, page },
        { "align key of a null box", Operation::AlignTop, { b0, b1, null }, 2, page },
        { "match key out of range", Operation::MatchWidth, { b0, b1 }, 5, page },
        { "match key of a NaN box", Operation::MatchHeight, { b0, b1, QRectF(qQNaN(), 0, 1, 1) }, 2, page },
        // A page box, which is empty or not finite
        { "center on an empty page", Operation::CenterOnPage, { b0 }, 0, QRectF(0, 0, 0, 100) },
        { "center on a null page", Operation::CenterHorizontallyOnPage, { b0 }, 0, QRectF() },
        { "center on an infinite page", Operation::CenterVerticallyOnPage, { b0 }, 0, QRectF(0, 0, qInf(), 100) },
        // A result, which is not finite: 1e308 - (-1e308) overflows
        { "result not finite", Operation::AlignLeft, { QRectF(-1e308, 0, 10, 10), QRectF(1e308, 0, 10, 10) }, 1, page },
    };

    for (const Case& item : cases)
    {
        QString errorText;
        const std::optional<QList<QRectF>> result = arrange(item.operation, item.boxes, item.keyIndex, item.pageBox, &errorText);
        QVERIFY2(!result.has_value(), item.name);
        QVERIFY2(!errorText.isEmpty(), item.name);
    }

    // The same operations with enough boxes and a good key are accepted
    QVERIFY(arrange(Operation::AlignLeft, { b0, b1 }, 1, page).has_value());
    QVERIFY(arrange(Operation::AlignTop, { b0, b1, null }, 1, page).has_value());
    QVERIFY(arrange(Operation::CenterOnPage, { b0, null }, 0, page).has_value());
    QVERIFY(arrange(Operation::DistributeInRow, { b0, b1 }, 0, page).has_value());
    QVERIFY(arrange(Operation::MatchWidth, { b0, b1 }, 1, page).has_value());
}

// -------------------------------------------------------------------------------------------------
//  The command
// -------------------------------------------------------------------------------------------------

void ContentArrangeTest::commandAlign()
{
    // A (10, 10, 50, 50), B (80, 30, 40, 20), C (150, 100, 30, 30) in the space of the page (the
    // content has no matrix). Align left to A (left 10): B by -70, C by -140; A has no wrap.
    ContentArrangeFixture fixture("10 10 50 50 re f 80 30 40 20 re f 150 100 30 30 re f");
    PDFContentEditSession* session = fixture.session();
    const PDFContentContainerPointer before = session->getContainer(fixture.key());
    const QByteArray original = fixture.written();

    QString errorText;
    const PDFContentCommandPointer command = Arrange::createCommand(Operation::AlignLeft,
                                                                    { fixture.node(Kind::Path, 0), fixture.node(Kind::Path, 1), fixture.node(Kind::Path, 2) },
                                                                    { QRectF(10, 10, 50, 50), QRectF(80, 30, 40, 20), QRectF(150, 100, 30, 30) },
                                                                    0, QRectF(0, 0, 200, 200), QTransform(), &errorText);
    QVERIFY2(command, qPrintable(errorText));
    QCOMPARE(command->getType(), QByteArray("align"));
    QCOMPARE(command->getText(), QStringLiteral("Align 3 Object(s)"));
    QCOMPARE(command->getPageIndex(), PDFInteger(0));

    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    const QByteArray expected("10 10 50 50 re f q 1 0 0 1 -70 0 cm 80 30 40 20 re f Q q 1 0 0 1 -140 0 cm 150 100 30 30 re f Q");
    QCOMPARE(fixture.written(), expected);
    const PDFContentContainerPointer after = session->getContainer(fixture.key());

    // One undo step for the whole operation: undo gives the model before it, redo the one after
    QCOMPARE(session->getUndoStack()->getCount(), size_t(1));
    QVERIFY(session->undo().has_value());
    QCOMPARE(session->getContainer(fixture.key()), before);
    QCOMPARE(fixture.written(), original);
    QVERIFY(session->redo().has_value());
    QCOMPARE(session->getContainer(fixture.key()), after);
    QCOMPARE(fixture.written(), expected);
    QCOMPARE(session->getUndoStack()->getCount(), size_t(1));
}

void ContentArrangeTest::commandJsonReplay_data()
{
    QTest::addColumn<int>("operation");
    QTest::addColumn<int>("nodeCount");
    QTest::addColumn<int>("keyIndex");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QByteArray>("expected");

    // The page of commandAlign: A (10, 10, 50, 50), B (80, 30, 40, 20), C (150, 100, 30, 30)
    QTest::newRow("align") << int(Operation::AlignLeft) << 3 << 0 << QStringLiteral("Align 3 Object(s)")
                           << QByteArray("10 10 50 50 re f q 1 0 0 1 -70 0 cm 80 30 40 20 re f Q q 1 0 0 1 -140 0 cm 150 100 30 30 re f Q");

    // In a row from 10: A stays, B at 10 + 50 = 60 (by -20), C at 60 + 40 = 100 (by -50)
    QTest::newRow("distribute") << int(Operation::DistributeInRow) << 3 << 0 << QStringLiteral("Distribute 3 Object(s)")
                                << QByteArray("10 10 50 50 re f q 1 0 0 1 -20 0 cm 80 30 40 20 re f Q q 1 0 0 1 -50 0 cm 150 100 30 30 re f Q");

    // The height of A (50) for B (height 20, upper edge 50): y' = 2.5 (y - 50) + 50 = 2.5 y - 75
    QTest::newRow("match size") << int(Operation::MatchHeight) << 2 << 0 << QStringLiteral("Match Size of 2 Object(s)")
                                << QByteArray("10 10 50 50 re f q 1 0 0 2.5 0 -75 cm 80 30 40 20 re f Q 150 100 30 30 re f");
}

void ContentArrangeTest::commandJsonReplay()
{
    QFETCH(int, operation);
    QFETCH(int, nodeCount);
    QFETCH(int, keyIndex);
    QFETCH(QString, text);
    QFETCH(QByteArray, expected);

    const QByteArray content("10 10 50 50 re f 80 30 40 20 re f 150 100 30 30 re f");
    const std::vector<QRectF> allBoxes = { QRectF(10, 10, 50, 50), QRectF(80, 30, 40, 20), QRectF(150, 100, 30, 30) };

    ContentArrangeFixture live(content);
    std::vector<PDFContentNodeReference> nodes;
    for (int i = 0; i < nodeCount; ++i)
    {
        nodes.push_back(live.node(Kind::Path, size_t(i)));
    }

    QString errorText;
    const PDFContentCommandPointer command = Arrange::createCommand(Operation(operation), nodes, std::vector<QRectF>(allBoxes.cbegin(), allBoxes.cbegin() + nodeCount),
                                                                    size_t(keyIndex), QRectF(0, 0, 200, 200), QTransform(), &errorText);
    QVERIFY2(command, qPrintable(errorText));
    QCOMPARE(command->getText(), text);
    QVERIFY2(live.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(live.written(), expected);

    // The operation of the script: the type, the operation, the identifiers and one matrix per
    // identifier
    const QJsonObject json = command->toJson();
    QCOMPARE(json.value("type").toString(), QString::fromLatin1(Arrange::getType(Operation(operation))));
    QCOMPARE(json.value("operation").toString(), QString::fromLatin1(Arrange::getOperationName(Operation(operation))));
    QCOMPARE(json.value("ids").toArray().size(), qsizetype(nodeCount));
    QCOMPARE(json.value("matrices").toArray().size(), qsizetype(nodeCount));
    QCOMPARE(json.size(), qsizetype(4));
    if (Operation(operation) == Operation::AlignLeft)
    {
        QCOMPARE(json.value("matrices").toArray(), QJsonArray({ matrixArray(1, 0, 0, 1, 0, 0), matrixArray(1, 0, 0, 1, -70, 0), matrixArray(1, 0, 0, 1, -140, 0) }));
    }

    // The replay on another session of the same document gives the same content
    ContentArrangeFixture replay(content);
    std::vector<QString> errors;
    const PDFContentCommandPointer replayed = PDFContentCommandFactory::fromJson(json, replay.session(), QStringLiteral("operations.items[0]"), &errors);
    QVERIFY2(replayed && errors.empty(), errors.empty() ? "no command" : qPrintable(errors.front()));
    QCOMPARE(replayed->getType(), command->getType());
    QCOMPARE(replayed->toJson(), json);
    QVERIFY2(replay.execute(replayed, &errorText), qPrintable(errorText));
    QCOMPARE(replay.written(), expected);
}

void ContentArrangeTest::commandJsonErrors()
{
    ContentArrangeFixture fixture("10 10 50 50 re f 80 30 40 20 re f 150 100 30 30 re f");
    QString errorText;
    const PDFContentCommandPointer command = Arrange::createCommand(Operation::AlignLeft, { fixture.node(Kind::Path, 0), fixture.node(Kind::Path, 1) },
                                                                    { QRectF(10, 10, 50, 50), QRectF(80, 30, 40, 20) }, 0, QRectF(0, 0, 200, 200), QTransform(), &errorText);
    QVERIFY2(command, qPrintable(errorText));
    const QJsonObject good = command->toJson();
    const QString path = QStringLiteral("operations.items[3]");

    // The good operation is read without an error
    {
        std::vector<QString> errors;
        QVERIFY(PDFContentCommandFactory::fromJson(good, fixture.session(), path, &errors) != nullptr);
        QVERIFY(errors.empty());
    }

    const QJsonArray goodMatrices = good.value("matrices").toArray();
    const QJsonArray singular = QJsonArray({ goodMatrices[0], matrixArray(1, 2, 2, 4, 0, 0) });
    const QJsonArray five = QJsonArray({ goodMatrices[0], QJsonArray({ 1, 0, 0, 1, 0 }) });
    const QJsonArray infinite = QJsonArray({ goodMatrices[0], QJsonArray({ 1, 0, 0, 1, 0, qInf() }) });

    struct Case
    {
        const char* name;
        QJsonObject operation;
        QString prefix;     ///< What the first error starts with
    };

    auto changed = [&good](const char* key, const QJsonValue& value)
    {
        QJsonObject object = good;
        if (value.isUndefined())
        {
            object.remove(QLatin1String(key));
        }
        else
        {
            object.insert(QLatin1String(key), value);
        }
        return object;
    };

    const std::vector<Case> cases = {
        { "operation of another type", changed("type", "distribute"), path + ".operation: " },
        { "operation of match_size", changed("type", "match_size"), path + ".operation: " },
        { "unknown operation", changed("operation", "diagonal"), path + ".operation: " },
        { "no operation", changed("operation", QJsonValue::Undefined), path + ".operation: " },
        { "operation is a number", changed("operation", 1), path + ".operation: " },
        { "no matrices", changed("matrices", QJsonValue::Undefined), path + ".matrices: " },
        { "matrices are no array", changed("matrices", "identity"), path + ".matrices: " },
        { "fewer matrices than ids", changed("matrices", QJsonArray({ goodMatrices[0] })), path + ".matrices: " },
        { "more matrices than ids", changed("matrices", QJsonArray({ goodMatrices[0], goodMatrices[1], goodMatrices[1] })), path + ".matrices: " },
        { "singular matrix", changed("matrices", singular), path + ".matrices[1]: " },
        { "matrix of five", changed("matrices", five), path + ".matrices[1]: " },
        { "infinite matrix", changed("matrices", infinite), path + ".matrices[1]: " },
        { "unknown key", changed("key", 1), path + ": " },
        { "id instead of ids", changed("id", good.value("ids").toArray().at(0)), path + ": " },
    };

    for (const Case& item : cases)
    {
        std::vector<QString> errors;
        const PDFContentCommandPointer result = PDFContentCommandFactory::fromJson(item.operation, fixture.session(), path, &errors);
        QVERIFY2(result == nullptr, item.name);
        QVERIFY2(!errors.empty(), item.name);
        QVERIFY2(errors.front().startsWith(item.prefix), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(item.name), errors.front())));
    }
}

void ContentArrangeTest::commandRefusedAsUnchanged()
{
    // Both rectangles have the left edge 10 already: every matrix is the identity
    ContentArrangeFixture fixture("10 10 50 50 re f 10 80 30 20 re f");
    const PDFContentContainerPointer before = fixture.session()->getContainer(fixture.key());
    QString errorText;
    const PDFContentCommandPointer command = Arrange::createCommand(Operation::AlignLeft, { fixture.node(Kind::Path, 0), fixture.node(Kind::Path, 1) },
                                                                    { QRectF(10, 10, 50, 50), QRectF(10, 80, 30, 20) }, 0, QRectF(0, 0, 200, 200), QTransform(), &errorText);
    QVERIFY2(command, qPrintable(errorText));
    QVERIFY(!fixture.execute(command, &errorText));
    QVERIFY(command->isRefusedAsUnchanged());
    QVERIFY(!errorText.isEmpty());
    QCOMPARE(fixture.session()->getContainer(fixture.key()), before);
    QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));

    // A matrix, which is not the identity, of an empty group: nothing moves
    ContentArrangeFixture empty("q Q 10 10 50 50 re f");
    const PDFContentCommandPointer emptyCommand = Arrange::createCommand(Operation::AlignLeft, { empty.node(Kind::Group, 0), empty.node(Kind::Path, 0) },
                                                                         { QTransform::fromTranslate(5, 0), QTransform() });
    QVERIFY(!empty.execute(emptyCommand, &errorText));
    QVERIFY(emptyCommand->isRefusedAsUnchanged());
    QCOMPARE(empty.session()->getUndoStack()->getCount(), size_t(0));
}

void ContentArrangeTest::commandGroupMovesOnce()
{
    // The group (10, 10, 50, 20) holds the paths P0 (10, 10, 20, 20) and P1 (40, 10, 20, 20);
    // P2 (100, 50, 30, 30) is outside. Align right to P2 (right 130): the group by 130 - 60 = 70,
    // P0 by 130 - 30 = 100 - but P0 is named together with its group, so it moves with the group
    // only (by 70, not by 170). A group gives its matrix to its leaves.
    ContentArrangeFixture fixture("q 10 10 20 20 re f 40 10 20 20 re f Q 100 50 30 30 re f");
    QString errorText;
    const PDFContentCommandPointer command = Arrange::createCommand(Operation::AlignRight,
                                                                    { fixture.node(Kind::Path, 0), fixture.node(Kind::Group, 0), fixture.node(Kind::Path, 2) },
                                                                    { QRectF(10, 10, 20, 20), QRectF(10, 10, 50, 20), QRectF(100, 50, 30, 30) },
                                                                    2, QRectF(0, 0, 200, 200), QTransform(), &errorText);
    QVERIFY2(command, qPrintable(errorText));
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(), QByteArray("q q 1 0 0 1 70 0 cm 10 10 20 20 re f Q q 1 0 0 1 70 0 cm 40 10 20 20 re f Q Q 100 50 30 30 re f"));
}

void ContentArrangeTest::commandContainerToPage()
{
    // The container maps to the page by C = [2 0 0 2 10 20]: (x, y) -> (2 x + 10, 2 y + 20).
    // A (0, 0, 10, 10) in the container is (10, 20, 20, 20) on the page; B (30, 5, 10, 20) is
    // x 70..90, y 30..70: (70, 30, 20, 40).
    const QByteArray content("0 0 10 10 re f 30 5 10 20 re f");
    const QTransform containerToPage(2, 0, 0, 2, 10, 20);
    const std::vector<QRectF> boxes = { QRectF(10, 20, 20, 20), QRectF(70, 30, 20, 40) };

    // Align left to A on the page: T = translate(-60, 0). In the container C x T x C^-1:
    // x -> 2 x + 10 -> 2 x - 50 -> (2 x - 50 - 10) / 2 = x - 30; y stays: [1 0 0 1 -30 0].
    // (C^-1 x T x C would give x - 120.)
    {
        ContentArrangeFixture fixture(content);
        QString errorText;
        const PDFContentCommandPointer command = Arrange::createCommand(Operation::AlignLeft, { fixture.node(Kind::Path, 0), fixture.node(Kind::Path, 1) },
                                                                        boxes, 0, QRectF(0, 0, 200, 200), containerToPage, &errorText);
        QVERIFY2(command, qPrintable(errorText));
        QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(), QByteArray("0 0 10 10 re f q 1 0 0 1 -30 0 cm 30 5 10 20 re f Q"));
    }

    // Match size to A (20 x 20 on the page): B 20 x 40 gets sy = 0.5 around its upper left
    // corner (70, 70): T: x' = x, y' = 0.5 (y - 70) + 70 = 0.5 y + 35. In the container:
    // y -> 2 y + 20 -> y + 10 + 35 = y + 45 -> (y + 45 - 20) / 2 = 0.5 y + 12.5: [1 0 0 0.5 0 12.5]
    // (the upper edge 25 of B stays: 12.5 + 12.5)
    {
        ContentArrangeFixture fixture(content);
        QString errorText;
        const PDFContentCommandPointer command = Arrange::createCommand(Operation::MatchSize, { fixture.node(Kind::Path, 0), fixture.node(Kind::Path, 1) },
                                                                        boxes, 0, QRectF(0, 0, 200, 200), containerToPage, &errorText);
        QVERIFY2(command, qPrintable(errorText));
        QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
        QCOMPARE(fixture.written(), QByteArray("0 0 10 10 re f q 1 0 0 0.5 0 12.5 cm 30 5 10 20 re f Q"));
        QCOMPARE(command->toJson().value("matrices").toArray().at(1).toArray(), matrixArray(1, 0, 0, 0.5, 0, 12.5));
    }
}

void ContentArrangeTest::commandRefusals()
{
    const QByteArray content("10 10 50 50 re f 80 30 40 20 re f");
    const std::vector<QRectF> boxes = { QRectF(10, 10, 50, 50), QRectF(80, 30, 40, 20) };
    ContentArrangeFixture fixture(content);
    const std::vector<PDFContentNodeReference> nodes = { fixture.node(Kind::Path, 0), fixture.node(Kind::Path, 1) };

    // No command: the counts differ, the space is singular or not finite, the geometry refuses
    QString errorText;
    QVERIFY(!Arrange::createCommand(Operation::AlignLeft, nodes, { boxes[0] }, 0, QRectF(0, 0, 200, 200), QTransform(), &errorText));
    QVERIFY(!errorText.isEmpty());
    errorText.clear();
    QVERIFY(!Arrange::createCommand(Operation::AlignLeft, nodes, boxes, 0, QRectF(0, 0, 200, 200), QTransform(1, 2, 2, 4, 0, 0), &errorText));
    QVERIFY(!errorText.isEmpty());
    errorText.clear();
    QVERIFY(!Arrange::createCommand(Operation::AlignLeft, nodes, boxes, 0, QRectF(0, 0, 200, 200), QTransform(1, 0, 0, 1, qInf(), 0), &errorText));
    QVERIFY(!errorText.isEmpty());
    errorText.clear();
    // Also when no matrix would need the space (both boxes have the left edge 10 already)
    QVERIFY(!Arrange::createCommand(Operation::AlignLeft, nodes, { QRectF(10, 10, 50, 50), QRectF(10, 30, 40, 20) }, 0, QRectF(0, 0, 200, 200), QTransform(1, 2, 2, 4, 0, 0), &errorText));
    QVERIFY(!errorText.isEmpty());
    errorText.clear();
    QVERIFY(!Arrange::createCommand(Operation::AlignLeft, nodes, boxes, 7, QRectF(0, 0, 200, 200), QTransform(), &errorText));
    QVERIFY(!errorText.isEmpty());

    // The command of matrices refuses in apply what does not fit (not as unchanged)
    const PDFContentCommandPointer fewer = Arrange::createCommand(Operation::AlignLeft, nodes, { QTransform::fromTranslate(1, 0) });
    QVERIFY(!fixture.execute(fewer, &errorText));
    QVERIFY(!fewer->isRefusedAsUnchanged());
    QVERIFY(!errorText.isEmpty());

    const PDFContentCommandPointer singular = Arrange::createCommand(Operation::AlignLeft, nodes, { QTransform(), QTransform(1, 2, 2, 4, 0, 0) });
    QVERIFY(!fixture.execute(singular, &errorText));
    QVERIFY(!singular->isRefusedAsUnchanged());

    const PDFContentCommandPointer none = Arrange::createCommand(Operation::AlignLeft, {}, {});
    QVERIFY(!fixture.execute(none, &errorText));
    QVERIFY(!none->isRefusedAsUnchanged());

    const PDFContentCommandPointer twice = Arrange::createCommand(Operation::AlignLeft, { nodes[1], nodes[1] }, { QTransform::fromTranslate(1, 0), QTransform::fromTranslate(1, 0) });
    QVERIFY(!fixture.execute(twice, &errorText));
    QVERIFY(!twice->isRefusedAsUnchanged());

    QCOMPARE(fixture.session()->getUndoStack()->getCount(), size_t(0));
    QCOMPARE(fixture.written(), QByteArray(content));

    // An object, which the content does not close, is not moved (q without Q)
    ContentArrangeFixture unclosed("10 10 50 50 re f q 80 30 40 20 re f");
    const PDFContentCommandPointer group = Arrange::createCommand(Operation::AlignLeft, { unclosed.node(Kind::Path, 0), unclosed.node(Kind::Group, 0) },
                                                                  { QTransform(), QTransform::fromTranslate(-70, 0) });
    QVERIFY(!unclosed.execute(group, &errorText));
    QVERIFY(!group->isRefusedAsUnchanged());
}

// -------------------------------------------------------------------------------------------------
//  Stage E1, fix package FX1: the space the user sees (review R5-6)
// -------------------------------------------------------------------------------------------------

void ContentArrangeTest::displayedPageMatrix()
{
    // ISO 32000-2, 7.7.3.3: /Rotate turns the page clockwise when it is displayed. 90: the x axis
    // of the page points down on the screen, its y axis right - (x, y) -> (y, -x) in a space with
    // the y axis up; 180: (-x, -y); 270: the x axis up, the y axis left - (-y, x)
    QCOMPARE(Arrange::getDisplayedPageMatrix(PageRotation::None), QTransform());
    QCOMPARE(Arrange::getDisplayedPageMatrix(PageRotation::Rotate90).map(QPointF(3, 5)), QPointF(5, -3));
    QCOMPARE(Arrange::getDisplayedPageMatrix(PageRotation::Rotate180).map(QPointF(3, 5)), QPointF(-3, -5));
    QCOMPARE(Arrange::getDisplayedPageMatrix(PageRotation::Rotate270).map(QPointF(3, 5)), QPointF(-5, 3));

    // Clockwise: 90 four times is the identity, 90 then 180 is 270
    const QTransform quarter = Arrange::getDisplayedPageMatrix(PageRotation::Rotate90);
    QCOMPARE(quarter * quarter * quarter * quarter, QTransform());
    QCOMPARE(quarter * Arrange::getDisplayedPageMatrix(PageRotation::Rotate180), Arrange::getDisplayedPageMatrix(PageRotation::Rotate270));
}

void ContentArrangeTest::commandOnRotatedPage()
{
    // A page shown under /Rotate 90 with b0 (10, 10, 20, 10) and the key b1 (50, 40, 40, 20) in
    // its default space. Mapped by D = getDisplayedPageMatrix(90), (x, y) -> (y, -x): b0 is
    // x' 10..20, y' -30..-10 = (10, -30, 10, 20); b1 is (40, -90, 20, 40). Align left on the
    // screen: b0 by 40 - 10 = 30 along x'. The command with C = D: C x T x C^-1 moves (x, y) to
    // (y, -x) + (30, 0) = (y + 30, -x), back by D^-1, (x', y') -> (-y', x'): (x, y + 30) - b0 moves
    // up by 30 in the page, which is to the left on the screen.
    const QTransform displayed = Arrange::getDisplayedPageMatrix(PageRotation::Rotate90);
    const std::vector<QRectF> boxes = { displayed.mapRect(QRectF(10, 10, 20, 10)), displayed.mapRect(QRectF(50, 40, 40, 20)) };
    QCOMPARE(boxes[0], QRectF(10, -30, 10, 20));
    QCOMPARE(boxes[1], QRectF(40, -90, 20, 40));

    ContentArrangeFixture fixture("10 10 20 10 re f 50 40 40 20 re f");
    QString errorText;
    const PDFContentCommandPointer command = Arrange::createCommand(Operation::AlignLeft, { fixture.node(Kind::Path, 0), fixture.node(Kind::Path, 1) },
                                                                    boxes, 1, displayed.mapRect(QRectF(0, 0, 200, 200)), displayed, &errorText);
    QVERIFY2(command, qPrintable(errorText));
    QVERIFY2(fixture.execute(command, &errorText), qPrintable(errorText));
    QCOMPARE(fixture.written(), QByteArray("q 1 0 0 1 0 30 cm 10 10 20 10 re f Q 50 40 40 20 re f"));

    // Distribute in a row on the screen of the same page: sorted by x' b0 (10), b1 (40); b0 stays,
    // b1 goes to x' 10 + 10 = 20, by -20 along x': in the page by -20 along y
    ContentArrangeFixture rowFixture("10 10 20 10 re f 50 40 40 20 re f");
    const PDFContentCommandPointer row = Arrange::createCommand(Operation::DistributeInRow, { rowFixture.node(Kind::Path, 0), rowFixture.node(Kind::Path, 1) },
                                                                boxes, 0, displayed.mapRect(QRectF(0, 0, 200, 200)), displayed, &errorText);
    QVERIFY2(row, qPrintable(errorText));
    QVERIFY2(rowFixture.execute(row, &errorText), qPrintable(errorText));
    QCOMPARE(rowFixture.written(), QByteArray("10 10 20 10 re f q 1 0 0 1 0 -20 cm 50 40 40 20 re f Q"));
}

QTEST_MAIN(ContentArrangeTest)

#include "tst_contentarrangetest.moc"
