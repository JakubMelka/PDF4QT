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

// The comparison of documents (plan of step 7 of phase 2, 4.9, 8; package P5): PDFDiff::compare (B160 page
// selection "3-10", a moved page, B191 the budget of the text with 1 000 pages, B192 an empty document, the
// cancellation in the middle of the compilation), PDFDiffReportBuilder (the polygon in the color of its kind, a
// restricted input without its owner refused) and PDFDocumentToolCompare.
//
// The documents are written by the test: a Type 3 font (ISO 32000-2, 9.6.4) of three glyphs - "A" a square of
// its em, "B" the left half of it, the space empty - with a /ToUnicode map, so the text of a page is known
// exactly; the vector graphics are rectangles of known coordinates.

#include "pdfalgorithmlcs.h"
#include "pdfdiff.h"
#include "pdfdiffreport.h"
#include "pdfdocumenttool_compare.h"
#include "pdfdocumentmerger.h"
#include "pdfencoding.h"
#include "pdfoperationcontrol.h"
#include "pdfpainter.h"

#include "mergetestpdf.h"

#include <QtTest>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <atomic>
#include <cstring>
#include <numeric>

using namespace pdf;
using mergetest::MergeTestPdf;

/// Cancels the operation after the given count of its checks (and then always)
class DiffTestCancelAfter : public PDFOperationControl
{
public:
    explicit DiffTestCancelAfter(int count) : m_remaining(count) { }

    virtual bool isOperationCanceled() const override { return m_remaining.fetch_sub(1) <= 0; }

private:
    mutable std::atomic_int m_remaining;
};

class DiffTest : public QObject
{
    Q_OBJECT

private slots:
    void lcsBudget();
    void identicalDocuments();
    void pageSelectionMovedPage();
    void insertedPageIsNotMoved();
    void pairingOnePieceOnce();
    void textReplaced();
    void textOverBudget();
    void thousandPages();
    void emptyDocument();
    void rangeOutsideDocument();
    void cancellation();
    void hostileContent();
    void navigatorClamp();
    void combinedDocumentViews();
    void combinedDocumentForeignResult();
    void reportPolygons();
    void reportRuleOfMerge();
    void toolSummaryData();
    void toolTruncated();
    void toolTextPermission();
    void toolXmlRectangles();
    void toolXmlWellFormed();
    void toolReport();
    void toolValidatePages();
    void pdfToolDiff();
    void imageHashIgnoresScanLinePadding();

private:
    /// A document of the pages of the contents (media box 600 x 800, the font /F1 of the glyphs A, B, space); the
    /// /ToUnicode of B is \p unicodeOfB (four hexadecimal digits of UTF-16BE)
    static MergeTestPdf createPdf(const QByteArrayList& pageContents, const QByteArray& unicodeOfB = QByteArray("0042"));
    static PDFDocument createDocument(const QByteArrayList& pageContents) { return createPdf(pageContents).read(); }
    static PDFDocumentPointer createPointer(const QByteArrayList& pageContents) { return createPdf(pageContents).readPointer(); }

    /// A text of the font /F1 of the size at the position
    static QByteArray text(const QByteArray& characters, int x, int y, int size = 10);

    /// A blue filled rectangle
    static QByteArray rect(double x, double y, double width, double height);

    /// The options of the tool Compare (every kind of the content matches the pages, words)
    static PDFDiff::Options getOptions(bool isCharacters = false);

    /// All pages of a document (indices from 0)
    static PDFClosedIntervalSet allPages(const PDFDocument& document);

    /// The comparison of all pages
    static PDFDiffResult compare(const PDFDocument& left, const PDFDocument& right, bool isCharacters = false);

    /// The indices of the differences of the type
    static std::vector<size_t> findDifferences(const PDFDiffResult& result, PDFDiffResult::Type type);

    /// The context of the tool for two documents
    static PDFDocumentToolContext createContext(PDFDocumentPointer original, PDFDocumentPointer revised, bool isOwnerPasswordVerified = false);

    /// The two documents of most tests: page 1 the same text and a removed and an added rectangle,
    /// page 2 a replaced word
    static std::pair<PDFDocumentPointer, PDFDocumentPointer> createPair();

    /// A file of the document encrypted with an empty user password, the owner password "owner" and the permissions
    static PDFDocumentPointer createRestricted(const PDFDocument& document, uint32_t permissions);

    /// Time limit of a test step scaled by PDF4QT_TEST_TIMEOUT_FACTOR
    static qint64 getTimeLimit(qint64 milliseconds) { return milliseconds * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")); }
};

MergeTestPdf DiffTest::createPdf(const QByteArrayList& pageContents, const QByteArray& unicodeOfB)
{
    MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");

    const QByteArray squareGlyph = MergeTestPdf::stream("1000 0 0 0 1000 1000 d1 0 0 1000 1000 re f");
    const QByteArray halfGlyph = MergeTestPdf::stream("1000 0 0 0 500 1000 d1 0 0 500 1000 re f");
    const QByteArray spaceGlyph = MergeTestPdf::stream("1000 0 d0");
    const QByteArray toUnicode = MergeTestPdf::stream("/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def "
                                                      "/CMapName /Adobe-Identity-UCS def /CMapType 2 def 1 begincodespacerange <00> <FF> endcodespacerange "
                                                      "3 beginbfchar <20> <0020> <41> <0041> <42> <" + unicodeOfB + "> endbfchar endcmap CMapName currentdict /CMap defineresource pop end end");
    QByteArray widths = "1000";
    for (int code = 33; code < 65; ++code)
    {
        widths += " 0";
    }
    widths += " 1000 1000";
    pdf.set(5, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /A 6 0 R /B 7 0 R /space 8 0 R >> "
               "/Encoding << /Type /Encoding /Differences [32 /space 65 /A /B] >> /FirstChar 32 /LastChar 66 /Widths [" + widths + "] /Resources << >> /ToUnicode 9 0 R >>");
    pdf.set(6, squareGlyph);
    pdf.set(7, halfGlyph);
    pdf.set(8, spaceGlyph);
    pdf.set(9, toUnicode);

    QByteArray kids;
    for (qsizetype i = 0; i < pageContents.size(); ++i)
    {
        const int pageObject = 10 + int(i) * 2;
        kids += QByteArray::number(pageObject) + " 0 R ";
        pdf.set(pageObject, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /Font << /F1 5 0 R >> >> /Contents " + QByteArray::number(pageObject + 1) + " 0 R >>");
        pdf.set(pageObject + 1, MergeTestPdf::stream(pageContents[i]));
    }
    pdf.set(2, "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pageContents.size()) + " >>");
    return pdf;
}

QByteArray DiffTest::text(const QByteArray& characters, int x, int y, int size)
{
    return "BT /F1 " + QByteArray::number(size) + " Tf " + QByteArray::number(x) + " " + QByteArray::number(y) + " Td (" + characters + ") Tj ET ";
}

QByteArray DiffTest::rect(double x, double y, double width, double height)
{
    return "0 0 1 rg " + QByteArray::number(x, 'f', 3) + " " + QByteArray::number(y, 'f', 3) + " " + QByteArray::number(width, 'f', 3) + " " + QByteArray::number(height, 'f', 3) + " re f ";
}

PDFDiff::Options DiffTest::getOptions(bool isCharacters)
{
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_COMPARE_CHARACTERS), isCharacters);
    return PDFDocumentToolCompare::getOptions(parameters);
}

PDFClosedIntervalSet DiffTest::allPages(const PDFDocument& document)
{
    PDFClosedIntervalSet pages;
    pages.addInterval(0, PDFInteger(document.getCatalog()->getPageCount()) - 1);
    return pages;
}

PDFDiffResult DiffTest::compare(const PDFDocument& left, const PDFDocument& right, bool isCharacters)
{
    return PDFDiff::compare(&left, &right, allPages(left), allPages(right), getOptions(isCharacters), nullptr, nullptr);
}

std::vector<size_t> DiffTest::findDifferences(const PDFDiffResult& result, PDFDiffResult::Type type)
{
    std::vector<size_t> indices;
    for (size_t i = 0; i < result.getDifferencesCount(); ++i)
    {
        if (result.getType(i) == type)
        {
            indices.push_back(i);
        }
    }
    return indices;
}

PDFDocumentToolContext DiffTest::createContext(PDFDocumentPointer original, PDFDocumentPointer revised, bool isOwnerPasswordVerified)
{
    PDFDocumentToolContext context;
    PDFDocumentToolInputDocument originalInput = PDFDocumentToolInputDocument::create(std::move(original), QByteArray(), QStringLiteral("Report_v1.pdf"), false);
    PDFDocumentToolInputDocument revisedInput = PDFDocumentToolInputDocument::create(std::move(revised), QByteArray(), QStringLiteral("Report_v2.pdf"), false);
    originalInput.isOwnerPasswordVerified = isOwnerPasswordVerified;
    revisedInput.isOwnerPasswordVerified = isOwnerPasswordVerified;
    context.documents = { originalInput, revisedInput };
    context.authorName = QStringLiteral("Reviewer");
    return context;
}

std::pair<PDFDocumentPointer, PDFDocumentPointer> DiffTest::createPair()
{
    // Page 1: the same word "AB", the original has a rectangle at (100, 100), the revised one at (300, 500)
    // instead; page 2: the word "AAAA" became "BBBB" (the second word "AB" stays)
    PDFDocumentPointer original = createPointer({ text("AB", 50, 700) + rect(100, 100, 50, 20), text("AAAA AB", 50, 700) });
    PDFDocumentPointer revised = createPointer({ text("AB", 50, 700) + rect(300, 500, 40, 30), text("BBBB AB", 50, 700) });
    return { original, revised };
}

PDFDocumentPointer DiffTest::createRestricted(const PDFDocument& document, uint32_t permissions)
{
    return PDFDocumentPointer(new PDFDocument(MergeTestPdf::readBytes(MergeTestPdf::encrypt(document, QString(), QStringLiteral("owner"), permissions))));
}

void DiffTest::lcsBudget()
{
    // B191, R7-6: the cells of the matrix are the product of the lengths without the common beginning and
    // end; up to MAXIMUM_CELL_COUNT the sequence is computed, over it nothing is allocated
    using LCS = PDFAlgorithmLongestCommonSubsequenceBase;
    QCOMPARE(LCS::MAXIMUM_CELL_COUNT, size_t(64) * 1024 * 1024);
    QVERIFY(LCS::isWithinBudget(8192, 8192));
    QVERIFY(!LCS::isWithinBudget(8192, 8193));
    QVERIFY(!LCS::isWithinBudget(size_t(1) << 33, size_t(1) << 33));   // the product would overflow 64 bits

    auto equal = [](int a, int b) { return a == b; };

    // Just under the budget: 8 192 x 8 192 different values, no common item
    std::vector<int> left(8192);
    std::vector<int> right(8192);
    std::iota(left.begin(), left.end(), 0);
    std::iota(right.begin(), right.end(), 100000);
    PDFAlgorithmLongestCommonSubsequence under(left.cbegin(), left.cend(), right.cbegin(), right.cend(), equal);
    QVERIFY(under.perform());
    QVERIFY(!under.isOverBudget());
    QCOMPARE(under.getSequence().size(), size_t(8192 * 2));
    QVERIFY(std::none_of(under.getSequence().cbegin(), under.getSequence().cend(), [](const LCS::SequenceItem& item) { return item.isMatch(); }));

    // Just over the budget: one item more
    right.push_back(200000);
    PDFAlgorithmLongestCommonSubsequence over(left.cbegin(), left.cend(), right.cbegin(), right.cend(), equal);
    QVERIFY(!over.perform());
    QVERIFY(over.isOverBudget());
    QVERIFY(over.getSequence().empty());

    // A long common beginning and end are no cells: 100 000 items, 3 differ in the middle
    std::vector<int> longLeft(100000);
    std::iota(longLeft.begin(), longLeft.end(), 0);
    std::vector<int> longRight = longLeft;
    longRight[50000] = -1;
    longRight[50001] = -2;
    longRight.erase(longRight.begin() + 50002);
    PDFAlgorithmLongestCommonSubsequence trimmed(longLeft.cbegin(), longLeft.cend(), longRight.cbegin(), longRight.cend(), equal);
    QVERIFY(trimmed.perform());
    const LCS::Sequence& sequence = trimmed.getSequence();
    QCOMPARE(qsizetype(std::count_if(sequence.cbegin(), sequence.cend(), [](const LCS::SequenceItem& item) { return item.isMatch(); })), qsizetype(99997));
    QCOMPARE(qsizetype(std::count_if(sequence.cbegin(), sequence.cend(), [](const LCS::SequenceItem& item) { return item.isLeft(); })), qsizetype(3));
    QCOMPARE(qsizetype(std::count_if(sequence.cbegin(), sequence.cend(), [](const LCS::SequenceItem& item) { return item.isRight(); })), qsizetype(2));
    QCOMPARE(sequence.front().index1, size_t(0));
    QCOMPARE(sequence.back().index1, size_t(99999));
    QCOMPARE(sequence.back().index2, size_t(99998));

    // The cancellation is checked per row: nothing is returned
    DiffTestCancelAfter canceled(0);
    PDFAlgorithmLongestCommonSubsequence canceledRun(left.cbegin(), left.cbegin() + 100, right.cbegin(), right.cbegin() + 100, equal);
    QVERIFY(!canceledRun.perform(&canceled));
    QVERIFY(!canceledRun.isOverBudget());
    QVERIFY(canceledRun.getSequence().empty());
}

void DiffTest::identicalDocuments()
{
    // The same document on both sides has no difference; every page is a pair of the sequence
    const PDFDocument document = createDocument({ text("AB AB", 50, 700) + rect(10, 10, 20, 20), text("BA", 50, 700), rect(1, 2, 3, 4) });
    const PDFDiffResult result = compare(document, document);
    QVERIFY(result.getResult());
    QVERIFY(result.isSame());
    QCOMPARE(result.getPageSequence().size(), size_t(3));
    for (PDFInteger i = 0; i < 3; ++i)
    {
        QCOMPARE(result.getPageSequence()[size_t(i)].leftPage, i);
        QCOMPARE(result.getPageSequence()[size_t(i)].rightPage, i);
    }
}

void DiffTest::pageSelectionMovedPage()
{
    // B160: pages 3-10 of both documents; the revised one has the pages 5 and 6 swapped. The moved pages
    // are positions in the compared pages, not page numbers: exactly one page is moved, no content differs
    QByteArrayList originalPages;
    for (int page = 1; page <= 12; ++page)
    {
        originalPages << rect(10, 10 + page * 40, 30, 30);
    }
    QByteArrayList revisedPages = originalPages;
    std::swap(revisedPages[4], revisedPages[5]);

    const PDFDocument original = createDocument(originalPages);
    const PDFDocument revised = createDocument(revisedPages);

    PDFClosedIntervalSet pages;
    pages.addInterval(2, 9);
    const PDFDiffResult result = PDFDiff::compare(&original, &revised, pages, pages, getOptions(), nullptr, nullptr);
    QVERIFY2(result.getResult(), qPrintable(result.getResult().getErrorMessage()));
    QCOMPARE(result.getDifferencesCount(), size_t(1));
    QCOMPARE(result.getType(0), PDFDiffResult::Type::PageMoved);

    // Page 5 moved to 6, or page 6 moved to 5 (either of the two swapped pages is the moved one)
    const std::pair<PDFInteger, PDFInteger> pair(result.getLeftPage(0), result.getRightPage(0));
    QVERIFY(pair == std::make_pair(PDFInteger(4), PDFInteger(5)) || pair == std::make_pair(PDFInteger(5), PDFInteger(4)));

    // The sequence has the 8 compared pages of each document, from page 3 (index 2)
    PDFInteger leftCount = 0;
    PDFInteger rightCount = 0;
    for (const PDFDiffResult::PageSequenceItem& item : result.getPageSequence())
    {
        leftCount += item.leftPage != -1 ? 1 : 0;
        rightCount += item.rightPage != -1 ? 1 : 0;
        QVERIFY(item.leftPage == -1 || (item.leftPage >= 2 && item.leftPage <= 9));
        QVERIFY(item.rightPage == -1 || (item.rightPage >= 2 && item.rightPage <= 9));
    }
    QCOMPARE(leftCount, PDFInteger(8));
    QCOMPARE(rightCount, PDFInteger(8));
    QCOMPARE(result.getPageSequence().front().leftPage, PDFInteger(2));
}

void DiffTest::insertedPageIsNotMoved()
{
    // A page inserted at the beginning: one page added; the pages after it are only renumbered, they are
    // not moved (a moved page is a page out of the order of the other document)
    QByteArrayList originalPages;
    for (int page = 1; page <= 5; ++page)
    {
        originalPages << rect(10, 10 + page * 40, 30, 30);
    }
    QByteArrayList revisedPages = originalPages;
    revisedPages.prepend(rect(300, 300, 100, 100));

    const PDFDiffResult result = compare(createDocument(originalPages), createDocument(revisedPages));
    QVERIFY(result.getResult());
    QCOMPARE(result.getDifferencesCount(), size_t(1));
    QCOMPARE(result.getType(0), PDFDiffResult::Type::PageAdded);
    QCOMPARE(result.getRightPage(0), PDFInteger(0));
    QVERIFY(findDifferences(result, PDFDiffResult::Type::PageMoved).empty());
}

void DiffTest::pairingOnePieceOnce()
{
    // B172: a piece is paired within the tolerance with one piece only. The original has one rectangle, the
    // revised one two rectangles 0.02 and 0.03 pt from it (the tolerance is 0.001 of the page, 0.8 pt; the
    // positions lie across a step of the rounding of the hashes, so no hash is equal): one of them is the
    // original rectangle, the other one is added
    const PDFDocument original = createDocument({ rect(12.79, 100, 30, 30) });
    const PDFDocument revised = createDocument({ rect(12.81, 100, 30, 30) + rect(12.82, 100, 30, 30) });
    const PDFDiffResult result = compare(original, revised);
    QVERIFY(result.getResult());
    QCOMPARE(result.getDifferencesCount(), size_t(1));
    QCOMPARE(result.getType(0), PDFDiffResult::Type::AddedVectorGraphicContent);
    QCOMPARE(result.getRightPage(0), PDFInteger(0));
}

void DiffTest::textReplaced()
{
    // The word "AAAA" replaced by "BBBB" (the word "AB" after it stays); a rectangle removed, one added
    const auto [original, revised] = createPair();
    const PDFDiffResult result = compare(*original, *revised);
    QVERIFY(result.getResult());

    const std::vector<size_t> replaced = findDifferences(result, PDFDiffResult::Type::TextReplaced);
    QCOMPARE(replaced.size(), size_t(1));
    const size_t index = replaced.front();
    QCOMPARE(result.getLeftPage(index), PDFInteger(1));
    QCOMPARE(result.getRightPage(index), PDFInteger(1));
    QCOMPARE(result.getRemovedText(index), QStringLiteral("AAAA"));
    QCOMPARE(result.getAddedText(index), QStringLiteral("BBBB"));
    QVERIFY(!result.isTextTooLarge(index));

    // The message has the texts, the description (Q7.2: without bit 5) has none
    QVERIFY(result.getMessage(index).contains(QStringLiteral("AAAA")));
    QVERIFY(result.getMessage(index).contains(QStringLiteral("BBBB")));
    QVERIFY(!result.getDescription(index).contains(QStringLiteral("AAAA")));
    QVERIFY(!result.getDescription(index).contains(QStringLiteral("BBBB")));
    QVERIFY(result.getDescription(index).contains(QStringLiteral("2")));

    // The rectangles of the text: the word of 4 glyphs of 10 pt at (50, 700)
    auto rectangles = result.getLeftRectangles(index);
    QVERIFY(rectangles.first != rectangles.second);
    QRectF united;
    for (auto it = rectangles.first; it != rectangles.second; ++it)
    {
        QCOMPARE(it->first, PDFInteger(1));
        united = united.united(it->second);
    }
    QVERIFY(united.contains(QPointF(70, 705)));
    QVERIFY(united.left() >= 49.0 && united.right() <= 91.0);

    // Page 1: the removed rectangle (100, 100, 50, 20) and the added one (300, 500, 40, 30). The glyphs of a
    // Type 3 font are content streams (ISO 32000-2, 9.6.4): their shapes are vector graphics as well, so page
    // 2 has the 4 squares of "AAAA" removed and the 4 halves of "BBBB" added
    std::vector<size_t> removed = findDifferences(result, PDFDiffResult::Type::RemovedVectorGraphicContent);
    std::vector<size_t> added = findDifferences(result, PDFDiffResult::Type::AddedVectorGraphicContent);
    QCOMPARE(removed.size(), size_t(5));
    QCOMPARE(added.size(), size_t(5));
    auto isOnFirstPage = [&result](size_t difference) { return result.getLeftPage(difference) == 0 || result.getRightPage(difference) == 0; };
    removed.erase(std::remove_if(removed.begin(), removed.end(), [&](size_t difference) { return !isOnFirstPage(difference); }), removed.end());
    added.erase(std::remove_if(added.begin(), added.end(), [&](size_t difference) { return !isOnFirstPage(difference); }), added.end());
    QCOMPARE(removed.size(), size_t(1));
    QCOMPARE(added.size(), size_t(1));
    QCOMPARE(result.getLeftRectangles(removed.front()).first->second, QRectF(100, 100, 50, 20));
    QCOMPARE(result.getRightRectangles(added.front()).first->second, QRectF(300, 500, 40, 30));
    QCOMPARE(result.getDifferencesCount(), size_t(11));
}

void DiffTest::textOverBudget()
{
    // B191, R7-6: page 1 has 8 200 characters on each side, all of them different (compared by characters):
    // 8 200 x 8 200 cells are over the budget of 64 Mi - one difference "too large to compare word by word"
    // with the rectangle of the text of each page; page 2 of the same range is compared alone as before
    QByteArray original = "BT /F1 6 Tf 7 TL 20 760 Td ";
    QByteArray revised = original;
    for (int line = 0; line < 100; ++line)
    {
        original += "(" + QByteArray(82, 'A') + ") ' ";
        revised += "(" + QByteArray(82, 'B') + ") ' ";
    }
    original += "ET";
    revised += "ET";

    const PDFDocument originalDocument = createDocument({ original, text("AAAA", 50, 700) });
    const PDFDocument revisedDocument = createDocument({ revised, text("BBBB", 50, 700) });

    QElapsedTimer timer;
    timer.start();
    const PDFDiffResult result = compare(originalDocument, revisedDocument, true);
    QVERIFY(timer.elapsed() < getTimeLimit(60000));
    QVERIFY(result.getResult());

    const std::vector<size_t> replaced = findDifferences(result, PDFDiffResult::Type::TextReplaced);
    QCOMPARE(replaced.size(), size_t(2));

    size_t tooLargeCount = 0;
    for (const size_t index : replaced)
    {
        if (result.isTextTooLarge(index))
        {
            ++tooLargeCount;
            QCOMPARE(result.getLeftPage(index), PDFInteger(0));
            QCOMPARE(result.getRightPage(index), PDFInteger(0));
            QVERIFY(result.getRemovedText(index).isEmpty());
            QVERIFY(result.getMessage(index).contains(QStringLiteral("too large to compare word by word")));

            // One rectangle per side over the text of the page (20, 60) - (512, 766)
            auto left = result.getLeftRectangles(index);
            auto right = result.getRightRectangles(index);
            QCOMPARE(qsizetype(std::distance(left.first, left.second)), qsizetype(1));
            QCOMPARE(qsizetype(std::distance(right.first, right.second)), qsizetype(1));
            QVERIFY(left.first->second.contains(QPointF(260, 400)));
            QVERIFY(right.first->second.contains(QPointF(260, 400)));
            QVERIFY(QRectF(0, 0, 600, 800).contains(left.first->second));
        }
        else
        {
            QCOMPARE(result.getLeftPage(index), PDFInteger(1));
            QCOMPARE(result.getRemovedText(index), QStringLiteral("AAAA"));
            QCOMPARE(result.getAddedText(index), QStringLiteral("BBBB"));
        }
    }
    QCOMPARE(tooLargeCount, size_t(1));
    QCOMPARE(result.getTextTooLargeLeftPageIndices(), std::vector<PDFInteger>({ 0 }));

    // The tool says it by its fact
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_COMPARE_CHARACTERS), true);
    const PDFDocumentToolCompare tool;
    const PDFDocumentToolResult toolResult = tool.run(createContext(PDFDocumentPointer(new PDFDocument(originalDocument)), PDFDocumentPointer(new PDFDocument(revisedDocument))), parameters, nullptr, nullptr);
    QVERIFY(toolResult.isSuccess());
    auto fact = std::find_if(toolResult.facts.cbegin(), toolResult.facts.cend(), [](const PDFDocumentToolFact& item) { return item.id == QLatin1String("textTooLarge"); });
    QVERIFY(fact != toolResult.facts.cend());
    QCOMPARE(fact->value, QStringLiteral("Text of page 1 differs (too large to compare word by word)"));
}

void DiffTest::thousandPages()
{
    // B191, plan 8: 1 000 pages with a changed footer - the joined text of the range (9 000 words a side, 81
    // million cells) is over the budget, so each pair of pages is compared alone: 1 000 replaced words, each
    // on its own page, within the memory of the budget
    QByteArrayList originalPages;
    QByteArrayList revisedPages;
    for (int page = 0; page < 1000; ++page)
    {
        const QByteArray body = text("AB AB AB AB AB AB AB AB", 50, 700);
        originalPages << body + text("AAAA", 50, 50);
        revisedPages << body + text("BBBB", 50, 50);
    }

    const PDFDocument original = createDocument(originalPages);
    const PDFDocument revised = createDocument(revisedPages);

    QElapsedTimer timer;
    timer.start();
    const PDFDiffResult result = compare(original, revised);
    QVERIFY(timer.elapsed() < getTimeLimit(120000));
    QVERIFY(result.getResult());

    // Per page the replaced word and the shapes of its Type 3 glyphs (4 removed, 4 added vector graphics)
    QCOMPARE(result.getDifferencesCount(), size_t(9000));
    QCOMPARE(findDifferences(result, PDFDiffResult::Type::RemovedVectorGraphicContent).size(), size_t(4000));
    QCOMPARE(findDifferences(result, PDFDiffResult::Type::AddedVectorGraphicContent).size(), size_t(4000));

    std::vector<char> isPageFound(1000, 0);
    const std::vector<size_t> replaced = findDifferences(result, PDFDiffResult::Type::TextReplaced);
    QCOMPARE(replaced.size(), size_t(1000));
    for (const size_t i : replaced)
    {
        QVERIFY(!result.isTextTooLarge(i));
        const PDFInteger page = result.getLeftPage(i);
        QVERIFY(page >= 0 && page < 1000);
        QCOMPARE(result.getRightPage(i), page);
        QCOMPARE(result.getRemovedText(i), QStringLiteral("AAAA"));
        QCOMPARE(result.getAddedText(i), QStringLiteral("BBBB"));
        isPageFound[size_t(page)] = 1;
    }
    QVERIFY(std::all_of(isPageFound.cbegin(), isPageFound.cend(), [](char found) { return found != 0; }));
}

void DiffTest::emptyDocument()
{
    // B192: a document without pages is an error of the result, never an access to its first page
    MergeTestPdf emptyPdf;
    emptyPdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    emptyPdf.set(2, "<< /Type /Pages /Kids [] /Count 0 >>");
    const PDFDocument empty = emptyPdf.read();
    const PDFDocument document = createDocument({ rect(10, 10, 10, 10) });
    QCOMPARE(empty.getCatalog()->getPageCount(), size_t(0));

    PDFClosedIntervalSet firstPage;
    firstPage.addValue(0);
    const PDFDiffResult left = PDFDiff::compare(&empty, &document, firstPage, firstPage, getOptions(), nullptr, nullptr);
    QVERIFY(!left.getResult());
    QCOMPARE(left.getResult().getErrorMessage(), QStringLiteral("The original document has no pages."));
    QVERIFY(left.getPageSequence().empty());

    const PDFDiffResult right = PDFDiff::compare(&document, &empty, firstPage, firstPage, getOptions(), nullptr, nullptr);
    QVERIFY(!right.getResult());
    QCOMPARE(right.getResult().getErrorMessage(), QStringLiteral("The revised document has no pages."));
}

void DiffTest::rangeOutsideDocument()
{
    // B174: a range is checked against the pages before it is unfolded - a page past the end and a range of
    // a trillion pages are an error at once (an unfolded range of a trillion pages would never end)
    const PDFDocument document = createDocument({ rect(10, 10, 10, 10), rect(20, 20, 10, 10), rect(30, 30, 10, 10) });
    const PDFClosedIntervalSet all = allPages(document);

    PDFClosedIntervalSet pastEnd;
    pastEnd.addInterval(1, 3);
    PDFDiffResult result = PDFDiff::compare(&document, &document, pastEnd, all, getOptions(), nullptr, nullptr);
    QVERIFY(!result.getResult());
    QCOMPARE(result.getResult().getErrorMessage(), QStringLiteral("Invalid page range."));

    PDFClosedIntervalSet negative;
    negative.addInterval(-1, 0);
    result = PDFDiff::compare(&document, &document, all, negative, getOptions(), nullptr, nullptr);
    QVERIFY(!result.getResult());

    QElapsedTimer timer;
    timer.start();
    PDFClosedIntervalSet huge;
    huge.addInterval(0, PDFInteger(1) << 40);
    result = PDFDiff::compare(&document, &document, huge, all, getOptions(), nullptr, nullptr);
    QVERIFY(!result.getResult());
    QVERIFY(timer.elapsed() < getTimeLimit(5000));

    PDFClosedIntervalSet empty;
    result = PDFDiff::compare(&document, &document, empty, all, getOptions(), nullptr, nullptr);
    QVERIFY(!result.getResult());
    QCOMPARE(result.getResult().getErrorMessage(), QStringLiteral("No page to be compared."));
}

void DiffTest::cancellation()
{
    // B173, B193: a comparison canceled in the middle of the compilation of the pages returns at once with
    // an error - never a result "no differences"; the tool reports Stopped
    QByteArrayList originalPages;
    QByteArrayList revisedPages;
    for (int page = 0; page < 20; ++page)
    {
        QByteArray content;
        for (int i = 0; i < 50; ++i)
        {
            content += rect(10 + i * 5, 10 + page * 10, 3, 3);
        }
        originalPages << content;
        revisedPages << content + rect(500, 700, 10, 10);
    }
    const PDFDocument original = createDocument(originalPages);
    const PDFDocument revised = createDocument(revisedPages);

    DiffTestCancelAfter control(40);
    const PDFDiffResult result = PDFDiff::compare(&original, &revised, allPages(original), allPages(revised), getOptions(), &control, nullptr);
    QVERIFY(control.isOperationCanceled());
    QVERIFY(!result.getResult());
    QCOMPARE(result.getResult().getErrorMessage(), QStringLiteral("The comparison has been canceled."));

    // Without the cancellation the same documents have a difference on every page
    QCOMPARE(compare(original, revised).getDifferencesCount(), size_t(20));

    DiffTestCancelAfter toolControl(40);
    const PDFDocumentToolCompare tool;
    const PDFDocumentToolResult toolResult = tool.run(createContext(PDFDocumentPointer(new PDFDocument(original)), PDFDocumentPointer(new PDFDocument(revised))), QVariantMap(), &toolControl, nullptr);
    QCOMPARE(toolResult.status, PDFDocumentToolResultStatus::Stopped);
}

void DiffTest::hostileContent()
{
    // Two hostile documents never crash or hang: a broken content stream (unbalanced q / BT, a string not
    // closed, operators without operands) and a form, which draws itself (a cycle of the content) - the
    // renderer stops them; the comparison ends with the valid rectangle of the revised page as added
    MergeTestPdf brokenPdf = createPdf({ "q q q BT /F1 12 Tf ( 1 2 3 re f Q Q Q Q ET ET Tj cm re" });
    const PDFDocument broken = brokenPdf.read();

    MergeTestPdf cyclePdf = createPdf({ rect(100, 100, 50, 50) + "/Fm1 Do" });
    cyclePdf.set(10, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Resources << /XObject << /Fm1 50 0 R >> >> /Contents 11 0 R >>");
    cyclePdf.set(50, MergeTestPdf::stream("/Fm1 Do", "/Type /XObject /Subtype /Form /BBox [0 0 600 800] /Resources << /XObject << /Fm1 50 0 R >> >>"));
    const PDFDocument cycle = cyclePdf.read();

    QElapsedTimer timer;
    timer.start();
    const PDFDiffResult result = compare(broken, cycle);
    QVERIFY(timer.elapsed() < getTimeLimit(30000));
    QVERIFY(result.getResult());

    const std::vector<size_t> added = findDifferences(result, PDFDiffResult::Type::AddedVectorGraphicContent);
    QVERIFY(!added.empty());
    bool isRectangleFound = false;
    for (const size_t index : added)
    {
        auto rectangles = result.getRightRectangles(index);
        for (auto it = rectangles.first; it != rectangles.second; ++it)
        {
            isRectangleFound = isRectangleFound || it->second == QRectF(100, 100, 50, 50);
        }
    }
    QVERIFY(isRectangleFound);
}

void DiffTest::navigatorClamp()
{
    // B175: a new, shorter result never keeps the selection of the earlier one past its end
    const PDFDocument base = createDocument({ rect(10, 10, 10, 10) });
    const PDFDocument three = createDocument({ rect(10, 10, 10, 10) + rect(100, 100, 10, 10) + rect(200, 200, 10, 10) + rect(300, 300, 10, 10) });
    const PDFDocument one = createDocument({ rect(10, 10, 10, 10) + rect(100, 100, 10, 10) });
    const PDFDiffResult threeDifferences = compare(base, three);
    const PDFDiffResult oneDifference = compare(base, one);
    QCOMPARE(threeDifferences.getDifferencesCount(), size_t(3));
    QCOMPARE(oneDifference.getDifferencesCount(), size_t(1));

    PDFDiffResultNavigator navigator(nullptr);
    size_t lastSelection = 99;
    QObject::connect(&navigator, &PDFDiffResultNavigator::selectionChanged, &navigator, [&lastSelection](size_t index) { lastSelection = index; });

    navigator.setResult(&threeDifferences);
    navigator.select(2);
    QCOMPARE(lastSelection, size_t(2));
    QVERIFY(!navigator.canGoNext());

    navigator.setResult(&oneDifference);
    QCOMPARE(lastSelection, size_t(0));
    QVERIFY(navigator.isSelected());
    QVERIFY(!navigator.canGoNext());
    QVERIFY(!navigator.canGoPrevious());
}

void DiffTest::combinedDocumentViews()
{
    // Plan 4.9, 9.3: the document of each view - the pairs of the page sequence, one document, or the pairs
    // for the overlay (the view blends them); the pages are copies, the inputs never change
    QByteArrayList originalPages = { rect(10, 10, 10, 10), rect(20, 20, 10, 10) };
    QByteArrayList revisedPages = originalPages;
    revisedPages << rect(30, 30, 10, 10);
    const PDFDocumentPointer original = createPointer(originalPages);
    const PDFDocumentPointer revised = createPointer(revisedPages);
    const PDFDiffResult result = compare(*original, *revised);
    QCOMPARE(result.getPageSequence().size(), size_t(3));

    using View = PDFDiffReportView;
    const std::vector<std::pair<View, std::vector<std::pair<PDFInteger, PDFInteger>>>> expected = {
        { View::SideBySide, { { 0, -1 }, { -1, 0 }, { 1, -1 }, { -1, 1 }, { -1, 2 } } },
        { View::Overlay, { { 0, -1 }, { -1, 0 }, { 1, -1 }, { -1, 1 }, { -1, 2 } } },
        { View::Original, { { 0, -1 }, { 1, -1 } } },
        { View::Revised, { { -1, 0 }, { -1, 1 }, { -1, 2 } } }
    };

    for (const auto& [view, pages] : expected)
    {
        const PDFDiffCombinedDocument combined = PDFDiffReportBuilder::createCombinedDocument(view, original, revised, result, nullptr);
        QVERIFY2(combined.isSuccess(), qPrintable(combined.errorMessage));
        QCOMPARE(combined.document->getCatalog()->getPageCount(), pages.size());
        QCOMPARE(combined.pages.size(), pages.size());
        for (size_t i = 0; i < pages.size(); ++i)
        {
            QCOMPARE(combined.pages[i].originalPage, pages[i].first);
            QCOMPARE(combined.pages[i].revisedPage, pages[i].second);
        }

        // The page of the combined document is the page of its input (the same content, so no difference)
        for (size_t i = 0; i < pages.size(); ++i)
        {
            const PDFDocument& source = (pages[i].first != -1) ? *original : *revised;
            const PDFInteger sourcePage = (pages[i].first != -1) ? pages[i].first : pages[i].second;
            PDFClosedIntervalSet combinedPage;
            combinedPage.addValue(PDFInteger(i));
            PDFClosedIntervalSet inputPage;
            inputPage.addValue(sourcePage);
            const PDFDiffResult same = PDFDiff::compare(combined.document.data(), &source, combinedPage, inputPage, getOptions(), nullptr, nullptr);
            QVERIFY(same.getResult());
            QVERIFY(same.isSame());
        }
    }

    // Before a comparison (no page sequence) the pages are paired by their positions
    const PDFDiffCombinedDocument beforeComparison = PDFDiffReportBuilder::createCombinedDocument(View::SideBySide, original, revised, PDFDiffResult(), nullptr);
    QVERIFY(beforeComparison.isSuccess());
    QCOMPARE(beforeComparison.pages.size(), size_t(5));
}

void DiffTest::combinedDocumentForeignResult()
{
    // B162: a result, whose sequence names a page the documents do not have, is an error, never an empty
    // document with the page sequence of the result
    const PDFDocumentPointer large = createPointer({ rect(10, 10, 10, 10), rect(20, 20, 10, 10), rect(30, 30, 10, 10) });
    const PDFDocumentPointer small = createPointer({ rect(10, 10, 10, 10) });
    const PDFDiffResult result = compare(*large, *large);
    QCOMPARE(result.getPageSequence().size(), size_t(3));

    const PDFDiffCombinedDocument combined = PDFDiffReportBuilder::createCombinedDocument(PDFDiffReportView::SideBySide, small, small, result, nullptr);
    QVERIFY(!combined.isSuccess());
    QVERIFY(combined.document.isNull());
    QVERIFY(combined.pages.empty());
    QVERIFY(!combined.errorMessage.isEmpty());

    QString errorMessage;
    const std::optional<PDFDocument> report = PDFDiffReportBuilder::createReport(PDFDocumentToolInputDocument::create(small, QByteArray(), QString(), false),
                                                                                 PDFDocumentToolInputDocument::create(small, QByteArray(), QString(), false),
                                                                                 result, PDFDiffReportSettings(), nullptr, &errorMessage);
    QVERIFY(!report);
    QVERIFY(!errorMessage.isEmpty());
}

void DiffTest::reportPolygons()
{
    // Plan 4.9: the report has a /Polygon per rectangle in the color of its kind (the tokens diff/removed
    // #FF3232, diff/added #7DFA00, diff/replaced #FF781E), /CA 0.3, /Contents the description, /T the author
    const auto [original, revised] = createPair();
    const PDFDiffResult result = compare(*original, *revised);

    PDFDiffReportSettings settings;
    settings.author = QStringLiteral("Reviewer");
    QString errorMessage;
    const std::optional<PDFDocument> report = PDFDiffReportBuilder::createReport(PDFDocumentToolInputDocument::create(original, QByteArray(), QStringLiteral("a.pdf"), false),
                                                                                 PDFDocumentToolInputDocument::create(revised, QByteArray(), QStringLiteral("b.pdf"), false),
                                                                                 result, settings, nullptr, &errorMessage);
    QVERIFY2(report, qPrintable(errorMessage));
    const PDFDocument document = MergeTestPdf::roundTrip(*report);

    // Side by side: original 1, revised 1, original 2, revised 2
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(4));

    struct Polygon
    {
        QColor color;
        QRectF rect;
        double opacity = 0.0;
        QString contents;
        QString author;
    };
    auto readPolygons = [&document](size_t pageIndex)
    {
        std::vector<Polygon> polygons;
        for (const PDFObjectReference reference : document.getCatalog()->getPage(pageIndex)->getAnnotations())
        {
            const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(reference));
            if (!dictionary || dictionary->get("Subtype").getString() != "Polygon")
            {
                continue;
            }

            Polygon polygon;
            PDFDocumentDataLoaderDecorator loader(&document);
            const std::vector<PDFReal> color = loader.readNumberArrayFromDictionary(dictionary, "IC");
            if (color.size() == 3)
            {
                polygon.color = QColor::fromRgbF(float(color[0]), float(color[1]), float(color[2]));
            }
            const std::vector<PDFReal> vertices = loader.readNumberArrayFromDictionary(dictionary, "Vertices");
            QPolygonF points;
            for (size_t i = 0; i + 1 < vertices.size(); i += 2)
            {
                points << QPointF(vertices[i], vertices[i + 1]);
            }
            polygon.rect = points.boundingRect();
            polygon.opacity = loader.readNumberFromDictionary(dictionary, "CA", 1.0);
            polygon.contents = PDFEncoding::convertTextString(document.getObject(dictionary->get("Contents")).getString());
            polygon.author = PDFEncoding::convertTextString(document.getObject(dictionary->get("T")).getString());
            polygons.push_back(polygon);
        }
        return polygons;
    };

    // Original page 1: the removed rectangle in the color "removed"
    const std::vector<Polygon> originalPage1 = readPolygons(0);
    QCOMPARE(originalPage1.size(), size_t(1));
    QCOMPARE(originalPage1.front().color.name(), QStringLiteral("#ff3232"));
    QCOMPARE(originalPage1.front().rect, QRectF(100, 100, 50, 20));
    QVERIFY(qAbs(originalPage1.front().opacity - 0.3) < 1e-6);
    QCOMPARE(originalPage1.front().author, QStringLiteral("Reviewer"));
    QCOMPARE(originalPage1.front().contents, result.getMessage(findDifferences(result, PDFDiffResult::Type::RemovedVectorGraphicContent).front()));

    // Revised page 1: the added rectangle in the color "added"
    const std::vector<Polygon> revisedPage1 = readPolygons(1);
    QCOMPARE(revisedPage1.size(), size_t(1));
    QCOMPARE(revisedPage1.front().color.name(), QStringLiteral("#7dfa00"));
    QCOMPARE(revisedPage1.front().rect, QRectF(300, 500, 40, 30));

    // Page 2 of each: the replaced word in the color "replaced", its text in /Contents (beside the shapes of
    // the Type 3 glyphs - 4 removed squares, 4 added halves)
    auto findReplaced = [](const std::vector<Polygon>& polygons)
    {
        return std::find_if(polygons.cbegin(), polygons.cend(), [](const Polygon& polygon) { return polygon.color.name() == QLatin1String("#ff781e"); });
    };
    const std::vector<Polygon> originalPage2 = readPolygons(2);
    const std::vector<Polygon> revisedPage2 = readPolygons(3);
    QCOMPARE(originalPage2.size(), size_t(5));
    QCOMPARE(revisedPage2.size(), size_t(5));
    QVERIFY(findReplaced(originalPage2) != originalPage2.cend());
    QVERIFY(findReplaced(revisedPage2) != revisedPage2.cend());
    QVERIFY(findReplaced(originalPage2)->contents.contains(QStringLiteral("AAAA")));
    QVERIFY(findReplaced(originalPage2)->rect.contains(QPointF(70, 705)));

    // The view Original: only the pages of the original with its rectangles
    settings.view = PDFDiffReportView::Original;
    const std::optional<PDFDocument> originalReport = PDFDiffReportBuilder::createReport(PDFDocumentToolInputDocument::create(original, QByteArray(), QString(), false),
                                                                                         PDFDocumentToolInputDocument::create(revised, QByteArray(), QString(), false),
                                                                                         result, settings, nullptr, &errorMessage);
    QVERIFY(originalReport);
    QCOMPARE(originalReport->getCatalog()->getPageCount(), size_t(2));
}

void DiffTest::reportRuleOfMerge()
{
    // B161, B210 (Q7.2): the report is not encrypted - an input, which restricts anything besides bit 10,
    // needs its owner password (the rule of Merge); the verified owner password unlocks it
    const auto [original, revised] = createPair();
    using Permission = PDFSecurityHandler::Permission;
    const uint32_t noCopy = uint32_t(Permission::Assemble) | uint32_t(Permission::PrintLowResolution) | uint32_t(Permission::PrintHighResolution) |
                            uint32_t(Permission::Modify) | uint32_t(Permission::ModifyInteractiveItems) | uint32_t(Permission::ModifyFormFields);
    const PDFDocumentPointer restricted = createRestricted(*revised, noCopy);
    QVERIFY(restricted->getStorage().getSecurityHandler());
    const PDFDiffResult result = compare(*original, *restricted);
    QVERIFY(result.getResult());

    QString errorMessage;
    PDFDocumentToolInputDocument restrictedInput = PDFDocumentToolInputDocument::create(restricted, QByteArray(), QStringLiteral("secret.pdf"), false);
    const std::optional<PDFDocument> refused = PDFDiffReportBuilder::createReport(PDFDocumentToolInputDocument::create(original, QByteArray(), QStringLiteral("a.pdf"), false),
                                                                                  restrictedInput, result, PDFDiffReportSettings(), nullptr, &errorMessage);
    QVERIFY(!refused);
    QCOMPARE(errorMessage, PDFDocumentMerger::getPermissionText(PDFDocumentMerger::Permission::OwnerPasswordRequired, QStringLiteral("secret.pdf")));

    restrictedInput.isOwnerPasswordVerified = true;
    const std::optional<PDFDocument> permitted = PDFDiffReportBuilder::createReport(PDFDocumentToolInputDocument::create(original, QByteArray(), QStringLiteral("a.pdf"), false),
                                                                                    restrictedInput, result, PDFDiffReportSettings(), nullptr, &errorMessage);
    QVERIFY2(permitted, qPrintable(errorMessage));
    QVERIFY(!MergeTestPdf::roundTrip(*permitted).getStorage().getSecurityHandler() ||
            MergeTestPdf::roundTrip(*permitted).getStorage().getSecurityHandler()->getMode() == EncryptionMode::None);

    // The tool checks it in its validation (every caller: the GUI, MCP, PdfTool)
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_OUTPUT), QString::fromLatin1(PDFDocumentToolCompare::OUTPUT_REPORT));
    const PDFDocumentToolCompare tool;
    const PDFDocumentToolResult toolResult = tool.run(createContext(original, restricted), parameters, nullptr, nullptr);
    QCOMPARE(toolResult.status, PDFDocumentToolResultStatus::Failed);
}

void DiffTest::toolSummaryData()
{
    // Plan 4.9, 9.3: the data of the summary - the counts of the whole comparison, the shown differences
    // (filtered by include*), the pages from 1, the description without the text and the texts beside it
    const auto [original, revised] = createPair();
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_INCLUDE_VECTOR_GRAPHICS), false);

    const PDFDocumentToolCompare tool;
    std::shared_ptr<PDFDiffResultCollector> collector = std::make_shared<PDFDiffResultCollector>();
    QVariantMap normalized = parameters;
    for (const PDFDocumentToolParameterDefinition& definition : tool.getParameters())
    {
        if (!normalized.contains(definition.getName()))
        {
            normalized.insert(definition.getName(), definition.getDefaultValue());
        }
    }
    const PDFDocumentToolResult result = PDFDocumentToolCompare::compare(createContext(original, revised), normalized, nullptr, nullptr, collector.get());
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(tool.getResultKind(parameters), PDFDocumentToolResultKind::Analysis);

    // The collector has the whole result (11 differences: the word, the two rectangles, the 8 shapes of
    // the Type 3 glyphs), the data the shown ones (the text only)
    QVERIFY(collector->getResult());
    QCOMPARE(collector->getResult()->getDifferencesCount(), size_t(11));

    const QJsonObject& data = result.data;
    QCOMPARE(data.value(QStringLiteral("isSame")).toBool(), false);
    const QJsonObject counts = data.value(QStringLiteral("counts")).toObject();
    QCOMPARE(counts.value(QStringLiteral("total")).toInteger(), qint64(11));
    QCOMPARE(counts.value(QStringLiteral("shown")).toInteger(), qint64(1));
    QCOMPARE(counts.value(QStringLiteral("hidden")).toInteger(), qint64(10));
    QCOMPARE(counts.value(QStringLiteral("text")).toInteger(), qint64(1));
    QCOMPARE(counts.value(QStringLiteral("vectorGraphics")).toInteger(), qint64(10));
    QCOMPARE(counts.value(QStringLiteral("images")).toInteger(), qint64(0));
    QCOMPARE(counts.value(QStringLiteral("shading")).toInteger(), qint64(0));
    QCOMPARE(counts.value(QStringLiteral("pageMovement")).toInteger(), qint64(0));
    QCOMPARE(counts.value(QStringLiteral("removed")).toInteger(), qint64(5));
    QCOMPARE(counts.value(QStringLiteral("added")).toInteger(), qint64(5));
    QCOMPARE(counts.value(QStringLiteral("replaced")).toInteger(), qint64(1));
    QCOMPARE(counts.value(QStringLiteral("moved")).toInteger(), qint64(0));

    const QJsonArray differences = data.value(QStringLiteral("differences")).toArray();
    QCOMPARE(differences.size(), qsizetype(1));
    const QJsonObject difference = differences.first().toObject();
    QCOMPARE(difference.value(QStringLiteral("kind")).toString(), QStringLiteral("text-replaced"));
    QCOMPARE(difference.value(QStringLiteral("category")).toString(), QStringLiteral("text"));
    QCOMPARE(difference.value(QStringLiteral("change")).toString(), QStringLiteral("replaced"));
    QCOMPARE(difference.value(QStringLiteral("originalPage")).toInteger(), qint64(2));
    QCOMPARE(difference.value(QStringLiteral("revisedPage")).toInteger(), qint64(2));
    QCOMPARE(difference.value(QStringLiteral("originalText")).toString(), QStringLiteral("AAAA"));
    QCOMPARE(difference.value(QStringLiteral("revisedText")).toString(), QStringLiteral("BBBB"));
    QVERIFY(!difference.value(QStringLiteral("description")).toString().contains(QStringLiteral("AAAA")));
    QVERIFY(!difference.value(QStringLiteral("originalRects")).toArray().isEmpty());
    QCOMPARE(difference.value(QStringLiteral("originalRects")).toArray().first().toArray().size(), qsizetype(4));
    QCOMPARE(difference.value(QStringLiteral("originalRectPages")).toArray().first().toInteger(), qint64(2));
    QCOMPARE(data.value(QStringLiteral("truncated")).toBool(), false);
    QCOMPARE(data.value(QStringLiteral("isTextOmitted")).toBool(), false);
    QCOMPARE(data.value(QStringLiteral("originalPageCount")).toInteger(), qint64(2));
    QCOMPARE(data.value(QStringLiteral("revisedPageCount")).toInteger(), qint64(2));

    const QJsonArray pageSequence = data.value(QStringLiteral("pageSequence")).toArray();
    QCOMPARE(pageSequence.size(), qsizetype(2));
    QCOMPARE(pageSequence.at(0).toObject().value(QStringLiteral("originalPage")).toInteger(), qint64(1));
    QCOMPARE(pageSequence.at(1).toObject().value(QStringLiteral("revisedPage")).toInteger(), qint64(2));

    // The same documents: isSame and no difference
    const PDFDocumentToolResult same = tool.run(createContext(original, original), QVariantMap(), nullptr, nullptr);
    QVERIFY(same.isSuccess());
    QCOMPARE(same.data.value(QStringLiteral("isSame")).toBool(), true);
    QVERIFY(same.data.value(QStringLiteral("differences")).toArray().isEmpty());
}

void DiffTest::toolTruncated()
{
    // Plan 4.9, 7.1: at most 1 000 differences in the data, then "truncated"; the counts are complete
    QByteArray revisedContent = rect(1, 1, 1, 1);
    for (int i = 0; i < 1001; ++i)
    {
        revisedContent += rect(10 + (i % 50) * 11, 10 + (i / 50) * 11, 5, 5);
    }
    const PDFDocumentPointer original = createPointer({ rect(1, 1, 1, 1) });
    const PDFDocumentPointer revised = createPointer({ revisedContent });

    const PDFDocumentToolCompare tool;
    const PDFDocumentToolResult result = tool.run(createContext(original, revised), QVariantMap(), nullptr, nullptr);
    QVERIFY(result.isSuccess());
    QCOMPARE(result.data.value(QStringLiteral("counts")).toObject().value(QStringLiteral("total")).toInteger(), qint64(1001));
    QCOMPARE(result.data.value(QStringLiteral("differences")).toArray().size(), qsizetype(PDFDocumentToolCompare::MAXIMUM_DIFFERENCES));
    QCOMPARE(result.data.value(QStringLiteral("truncated")).toBool(), true);
}

void DiffTest::toolTextPermission()
{
    // Q7.2: the texts of the differences are an extraction - an input without bit 5 leaves them out of the
    // summary, and the XML (which has them) needs bit 5 of every input
    const auto [original, revised] = createPair();
    using Permission = PDFSecurityHandler::Permission;
    const uint32_t noCopy = uint32_t(Permission::Assemble) | uint32_t(Permission::PrintLowResolution) | uint32_t(Permission::PrintHighResolution) |
                            uint32_t(Permission::Modify) | uint32_t(Permission::ModifyInteractiveItems) | uint32_t(Permission::ModifyFormFields);
    const PDFDocumentPointer restricted = createRestricted(*revised, noCopy);

    const PDFDocumentToolCompare tool;
    const PDFDocumentToolResult summary = tool.run(createContext(original, restricted), QVariantMap(), nullptr, nullptr);
    QVERIFY2(summary.isSuccess(), qPrintable(summary.message));
    QCOMPARE(summary.data.value(QStringLiteral("isTextOmitted")).toBool(), true);
    bool isTextDifferenceFound = false;
    for (const QJsonValue& value : summary.data.value(QStringLiteral("differences")).toArray())
    {
        const QJsonObject difference = value.toObject();
        QVERIFY(!difference.contains(QStringLiteral("originalText")));
        QVERIFY(!difference.contains(QStringLiteral("revisedText")));
        QVERIFY(!difference.value(QStringLiteral("description")).toString().contains(QStringLiteral("AAAA")));
        isTextDifferenceFound = isTextDifferenceFound || difference.value(QStringLiteral("category")).toString() == QLatin1String("text");
    }
    QVERIFY(isTextDifferenceFound);

    QVariantMap xml;
    xml.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_OUTPUT), QString::fromLatin1(PDFDocumentToolCompare::OUTPUT_XML));
    QCOMPARE(tool.getRequiredPermissions(xml), std::vector<Permission>({ Permission::CopyContent }));
    const PDFDocumentToolResult refused = tool.run(createContext(original, restricted), xml, nullptr, nullptr);
    QCOMPARE(refused.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(refused.outputs.empty());

    // The verified owner password: the texts are there
    const PDFDocumentToolResult owner = tool.run(createContext(original, restricted, true), QVariantMap(), nullptr, nullptr);
    QCOMPARE(owner.data.value(QStringLiteral("isTextOmitted")).toBool(), false);
}

void DiffTest::toolXmlRectangles()
{
    // Plan 4.9: the XML keeps its old form (type, left, right, text-added, text-removed, page-sequence) and has
    // the rectangles of each difference in points of their pages
    const auto [original, revised] = createPair();
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_OUTPUT), QString::fromLatin1(PDFDocumentToolCompare::OUTPUT_XML));

    const PDFDocumentToolCompare tool;
    const PDFDocumentToolResult result = tool.run(createContext(original, revised), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(tool.getResultKind(parameters), PDFDocumentToolResultKind::Analysis);
    QCOMPARE(result.outputs.size(), size_t(1));
    QCOMPARE(result.outputs.front().suggestedFileName, QStringLiteral("Report_v1 vs Report_v2 - differences.xml"));

    QXmlStreamReader reader(result.outputs.front().data);
    int differenceCount = 0;
    int pageSequenceCount = 0;
    bool isRemovedRectangleFound = false;
    bool isTextFound = false;
    QString currentType;
    while (!reader.atEnd())
    {
        reader.readNext();
        if (!reader.isStartElement())
        {
            continue;
        }

        if (reader.name() == QLatin1String("difference"))
        {
            ++differenceCount;
            currentType = reader.attributes().value(QStringLiteral("type")).toString();
        }
        else if (reader.name() == QLatin1String("rect") && currentType == QLatin1String("removed-vector-graphics"))
        {
            // The removed rectangle of page 1 (the others are the shapes of the glyphs of page 2)
            const QXmlStreamAttributes attributes = reader.attributes();
            QCOMPARE(attributes.value(QStringLiteral("side")).toString(), QStringLiteral("left"));
            if (attributes.value(QStringLiteral("page")) == QLatin1String("1"))
            {
                QCOMPARE(attributes.value(QStringLiteral("x")).toDouble(), 100.0);
                QCOMPARE(attributes.value(QStringLiteral("y")).toDouble(), 100.0);
                QCOMPARE(attributes.value(QStringLiteral("width")).toDouble(), 50.0);
                QCOMPARE(attributes.value(QStringLiteral("height")).toDouble(), 20.0);
                isRemovedRectangleFound = true;
            }
            else
            {
                QCOMPARE(attributes.value(QStringLiteral("page")).toString(), QStringLiteral("2"));
            }
        }
        else if (reader.name() == QLatin1String("text-removed"))
        {
            isTextFound = reader.readElementText() == QLatin1String("AAAA");
        }
        else if (reader.name() == QLatin1String("item"))
        {
            ++pageSequenceCount;
        }
    }
    QVERIFY(!reader.hasError());
    QCOMPARE(differenceCount, 11);
    QCOMPARE(pageSequenceCount, 2);
    QVERIFY(isRemovedRectangleFound);
    QVERIFY(isTextFound);
}

void DiffTest::toolXmlWellFormed()
{
    // Review RS7CCORE of step 7c: the texts of the differences come from untrusted documents - a glyph, whose
    // /ToUnicode is a control character (U+0001) or a lone surrogate (U+D800), which XML 1.0 (2.2 Characters) does not
    // allow. The XML of the differences stays well-formed: such a character is U+FFFD (the rule of the table writer)
    for (const QByteArray& unicodeOfB : { QByteArray("0001"), QByteArray("D800") })
    {
        const PDFDocumentPointer original = createPdf({ text("AAAA AB", 50, 700) }, unicodeOfB).readPointer();
        const PDFDocumentPointer revised = createPdf({ text("ABAB AB", 50, 700) }, unicodeOfB).readPointer();
        QVariantMap parameters;
        parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_OUTPUT), QString::fromLatin1(PDFDocumentToolCompare::OUTPUT_XML));

        const PDFDocumentToolCompare tool;
        const PDFDocumentToolResult result = tool.run(createContext(original, revised), parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        QCOMPARE(result.outputs.size(), size_t(1));

        QXmlStreamReader reader(result.outputs.front().data);
        QString addedText;
        while (!reader.atEnd())
        {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == QLatin1String("text-added"))
            {
                addedText = reader.readElementText();
            }
        }
        QVERIFY2(!reader.hasError(), qPrintable(QString::fromLatin1(unicodeOfB) + QStringLiteral(": ") + reader.errorString()));

        // The added text of the replaced word: "A" and the replacement character for each B
        const QString replacement(QChar::ReplacementCharacter);
        QCOMPARE(addedText, QStringLiteral("A") + replacement + QStringLiteral("A") + replacement);
    }
}

void DiffTest::toolReport()
{
    // Plan 4.9, R7-13: the report is a new document (FileOnly: its written bytes, "Report_v1 vs Report_v2 -
    // comparison.pdf", 492:107143), the rule of Merge
    const auto [original, revised] = createPair();
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_OUTPUT), QString::fromLatin1(PDFDocumentToolCompare::OUTPUT_REPORT));
    parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_VIEW), QString::fromLatin1(PDFDocumentToolCompare::VIEW_REVISED));

    const PDFDocumentToolCompare tool;
    QCOMPARE(tool.getResultKind(parameters), PDFDocumentToolResultKind::NewDocument);
    QVERIFY(tool.isNewDocumentOfInputs(parameters));

    const PDFDocumentToolResult result = tool.run(createContext(original, revised), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.size(), size_t(1));
    const PDFDocumentToolOutputDocument& output = result.documents.front();
    QCOMPARE(output.kind, PDFDocumentToolOutputDocument::Kind::New);
    QCOMPARE(output.suggestedFileName, QStringLiteral("Report_v1 vs Report_v2 - comparison.pdf"));
    QVERIFY(!output.writtenData.isEmpty());

    // The written bytes are the report of the view Revised: two pages, the added rectangle on page 1
    const PDFDocument written = MergeTestPdf::readBytes(output.writtenData);
    QCOMPARE(written.getCatalog()->getPageCount(), size_t(2));
    QVERIFY(!written.getCatalog()->getPage(0)->getAnnotations().empty());
    QCOMPARE(output.document->getCatalog()->getPageCount(), size_t(2));
}

void DiffTest::toolValidatePages()
{
    // B174, B192: the ranges of both documents are checked by the validation, each by its parameter; a
    // document without pages is an error of its range
    const PDFDocumentPointer document = createPointer({ rect(10, 10, 10, 10), rect(20, 20, 10, 10) });
    MergeTestPdf emptyPdf;
    emptyPdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    emptyPdf.set(2, "<< /Type /Pages /Kids [] /Count 0 >>");
    const PDFDocumentPointer empty = emptyPdf.readPointer();

    const PDFDocumentToolCompare tool;
    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_REVISED_PAGES), QStringLiteral("2-3"));
    std::vector<PDFDocumentToolParameterError> errors = tool.validate(createContext(document, document), parameters);
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors.front().parameter, QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_REVISED_PAGES));

    errors = tool.validate(createContext(empty, document), QVariantMap());
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors.front().parameter, QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES));
    QCOMPARE(errors.front().message, QStringLiteral("The document has no pages."));

    parameters.insert(QString::fromLatin1(PDFDocumentToolCompare::PARAMETER_REVISED_PAGES), QStringLiteral("2"));
    QVERIFY(tool.validate(createContext(document, document), parameters).empty());

    // One document only is no comparison
    PDFDocumentToolContext one = createContext(document, document);
    one.documents.pop_back();
    QVERIFY(!tool.run(one, QVariantMap(), nullptr, nullptr).isSuccess());
}

void DiffTest::pdfToolDiff()
{
    // Plan 4.10: PdfTool diff runs the tool Compare - the table of the differences (with their texts) and the
    // XML of the tool
#ifndef PDF4QT_TEST_PDFTOOL
    QSKIP("PdfTool is not built.");
#else
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto writeFile = [&directory](const QString& name, const QByteArray& data)
    {
        QFile file(directory.filePath(name));
        if (!file.open(QFile::WriteOnly) || file.write(data) != data.size())
        {
            return QString();
        }
        return file.fileName();
    };
    auto diff = [](const QStringList& arguments, QByteArray* output)
    {
        QProcess process;
        process.setProgram(QStringLiteral(PDF4QT_TEST_PDFTOOL));
        process.setArguments(QStringList({ QStringLiteral("diff") }) + arguments);
        process.start();
        process.waitForFinished(int(getTimeLimit(60000)));
        *output = process.readAllStandardOutput();
        return process.exitCode();
    };

    const QString original = writeFile(QStringLiteral("a.pdf"), createPdf({ text("AB", 50, 700) + rect(100, 100, 50, 20), text("AAAA AB", 50, 700) }).write());
    const QString revised = writeFile(QStringLiteral("b.pdf"), createPdf({ text("AB", 50, 700) + rect(300, 500, 40, 30), text("BBBB AB", 50, 700) }).write());
    QVERIFY(!original.isEmpty() && !revised.isEmpty());

    QByteArray output;
    QCOMPARE(diff({ original, revised }, &output), 0);
    const QString listing = QString::fromUtf8(output);
    QVERIFY2(listing.contains(QStringLiteral("Text replaced")), output.constData());
    QVERIFY(listing.contains(QStringLiteral("Removed vector graphics")));
    QVERIFY(listing.contains(QStringLiteral("Added vector graphics")));
    QVERIFY(listing.contains(QStringLiteral("AAAA")));
    QVERIFY(listing.contains(QStringLiteral("BBBB")));

    QCOMPARE(diff({ QStringLiteral("--console-format"), QStringLiteral("xml"), original, revised }, &output), 0);
    QVERIFY(output.contains("<difference-report"));
    QVERIFY(output.contains("type=\"text-replaced\""));
    QVERIFY(output.contains("<rect "));

    // Review RS7CCORE: the command lists every difference as before step 7c (the data of the tool for MCP hold at
    // most 1 000 of them) - 1 001 added rectangles are 1 001 rows
    QByteArray manyContent;
    for (int i = 0; i < 1001; ++i)
    {
        manyContent += rect(10 + (i % 50) * 11, 10 + (i / 50) * 11, 5, 5);
    }
    const QString few = writeFile(QStringLiteral("few.pdf"), createPdf({ rect(1, 1, 1, 1) }).write());
    const QString many = writeFile(QStringLiteral("many.pdf"), createPdf({ rect(1, 1, 1, 1) + manyContent }).write());
    QVERIFY(!few.isEmpty() && !many.isEmpty());
    QCOMPARE(diff({ few, many }, &output), 0);
    QCOMPARE(QString::fromUtf8(output).count(QStringLiteral("Added vector graphics to page 1.")), qsizetype(1001));

    // A document without pages: an error, never a crash
    MergeTestPdf emptyPdf;
    emptyPdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    emptyPdf.set(2, "<< /Type /Pages /Kids [] /Count 0 >>");
    const QString empty = writeFile(QStringLiteral("empty.pdf"), emptyPdf.write());
    QVERIFY(diff({ empty, revised }, &output) != 0);
#endif
}

void DiffTest::imageHashIgnoresScanLinePadding()
{
    // QImage leaves the padding of a scan line (and the unused bits of a 1-bit line) uninitialized:
    // the same pixels must give the same hash whatever the padding holds, different pixels another
    using Hash = std::array<uint8_t, 64>;
    using Hashes = std::pair<Hash, Hash>;
    const Hash noHash = { };
    auto hashOf = [](const QImage& image)
    {
        PDFPrecompiledPage page;
        page.addImage(image);
        const PDFPrecompiledPage::GraphicPieceInfos infos = page.calculateGraphicPieceInfos(QRectF(0, 0, 600, 800), 0.001);
        return infos.size() == 1 ? Hashes(infos.front().hash, infos.front().imageHash) : Hashes();
    };

    for (const QImage::Format format : { QImage::Format_Alpha8, QImage::Format_Mono, QImage::Format_MonoLSB, QImage::Format_RGB888 })
    {
        // Width 3: 3 or 9 bytes of pixels in a line of 4 or 12 bytes; 3 of 8 bits of a 1-bit line
        QImage first(3, 2, format);
        QVERIFY(first.bytesPerLine() * 8 > first.width() * first.depth());
        if (first.depth() == 1)
        {
            first.setColorCount(2);
            first.setColor(0, qRgb(0, 0, 0));
            first.setColor(1, qRgb(255, 255, 255));
        }
        std::memset(first.bits(), 0x00, first.sizeInBytes());
        first.setPixel(1, 1, first.depth() == 1 ? 1 : qRgb(255, 255, 255));

        QImage second = first.copy();
        std::memset(second.bits(), 0xFF, second.sizeInBytes());
        for (int y = 0; y < first.height(); ++y)
        {
            for (int x = 0; x < first.width(); ++x)
            {
                second.setPixel(x, y, first.depth() == 1 ? first.pixelIndex(x, y) : first.pixel(x, y));
            }
        }
        QVERIFY(std::memcmp(first.constBits(), second.constBits(), first.sizeInBytes()) != 0);

        const auto firstHash = hashOf(first);
        QVERIFY(firstHash.second != noHash);
        QVERIFY2(hashOf(second) == firstHash, qPrintable(QStringLiteral("format %1").arg(int(format))));

        QImage changed = first.copy();
        changed.setPixel(0, 0, first.depth() == 1 ? 1 : qRgb(255, 255, 255));
        QVERIFY(hashOf(changed).second != firstHash.second);
    }
}

QTEST_GUILESS_MAIN(DiffTest)

#include "tst_difftest.moc"
