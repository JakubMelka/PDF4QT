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

// The tools Rotate Pages and Remove Pages of the core (PDFDocumentPageOperations,
// PDFDocumentToolRotatePages, PDFDocumentToolRemovePages; plan of the first batch of phase
// 2, chapters 6 and 7). The documents are written as PDF text by the test, so every object,
// which points at a page, is known; the expected values come from ISO 32000-2 (Rotate in
// steps of 90 clockwise, 7.7.3.3; the outline 12.3.3; name trees 7.9.6; page labels 12.4.2;
// fields 12.7.4) and from the table of chapter 7 of the plan.

#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentpageoperations.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_pages.h"
#include "pdfdocumentwriter.h"
#include "pdfform.h"
#include "pdfobjectutils.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfoutline.h"

#include <QBuffer>
#include <QtTest>

#include <map>

using namespace pdf;

/// Writes PDF text of numbered objects (a classic cross-reference table) and reads it
class PagesTestPdf
{
public:
    /// Sets the object with its text (a dictionary, an array, a stream by stream())
    void set(int number, const QByteArray& text) { m_objects[number] = text; }

    /// A stream object of the content with the extra entries of its dictionary
    static QByteArray stream(const QByteArray& content, const QByteArray& entries = QByteArray())
    {
        return "<< /Length " + QByteArray::number(content.size()) + " " + entries + " >>\nstream\n" + content + "\nendstream";
    }

    QByteArray write() const
    {
        QByteArray data = "%PDF-1.7\n";
        const int size = m_objects.empty() ? 1 : m_objects.rbegin()->first + 1;
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

    /// Five pages (objects 3 - 7, contents 13 - 17 with the markers PAGEMARKER<n>) below the
    /// root of the page tree 2, which gives the media box; the catalog 1 with \p catalogEntries.
    /// \p pageEntries are the extra entries of each page.
    static PagesTestPdf createPages(const QByteArray& catalogEntries = QByteArray(), const std::map<int, QByteArray>& pageEntries = {})
    {
        PagesTestPdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R " + catalogEntries + " >>");
        pdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R 6 0 R 7 0 R] /Count 5 /MediaBox [0 0 600 800] >>");
        for (int page = 1; page <= 5; ++page)
        {
            const auto it = pageEntries.find(page);
            pdf.set(page + 2, "<< /Type /Page /Parent 2 0 R /Contents " + QByteArray::number(page + 12) + " 0 R /Resources << >> " + (it != pageEntries.end() ? it->second : QByteArray()) + " >>");
            pdf.set(page + 12, stream("% PAGEMARKER" + QByteArray::number(page) + "\n0 0 m 10 10 l S"));
        }
        return pdf;
    }

private:
    std::map<int, QByteArray> m_objects;
};

/// Helpers of the test
class PagesTestHelper
{
public:
    PagesTestHelper() = delete;

    static PDFDocumentToolContext context(const PDFDocument& document)
    {
        return PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(document)), QByteArray(), QStringLiteral("Handbook.pdf"), true));
    }

    static QVariantMap pages(const QString& range)
    {
        PDFDocumentToolPageSelection selection;
        selection.pages = range;
        QVariantMap parameters;
        parameters.insert(QStringLiteral("pages"), selection.toVariant());
        return parameters;
    }

    static int getRotation(const PDFDocument& document, size_t page)
    {
        return int(document.getCatalog()->getPage(page)->getPageRotation()) * 90;
    }

    static QStringList getRotations(const PDFDocument& document)
    {
        QStringList rotations;
        for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
        {
            rotations << QString::number(getRotation(document, i));
        }
        return rotations;
    }

    static const PDFDictionary* getDictionary(const PDFDocument& document, PDFObjectReference reference)
    {
        return document.getDictionaryFromObject(document.getObjectByReference(reference));
    }

    static const PDFDictionary* getCatalog(const PDFDocument& document)
    {
        return document.getDictionaryFromObject(document.getTrailerDictionary()->get("Root"));
    }

    /// The titles of the outline as "A[A1] B C"
    static QString getOutline(const PDFOutlineItem* item)
    {
        QStringList parts;
        for (size_t i = 0; item && i < item->getChildCount(); ++i)
        {
            const PDFOutlineItem* child = item->getChild(i);
            parts << (child->getChildCount() > 0 ? child->getTitle() + QLatin1Char('[') + getOutline(child) + QLatin1Char(']') : child->getTitle());
        }
        return parts.join(QLatin1Char(' '));
    }

    /// Every reference reachable from the trailer leads to an object (the result never points at nothing)
    static QStringList getDanglingReferences(const PDFDocument& document)
    {
        QStringList dangling;
        const std::set<PDFObjectReference> references = PDFObjectUtils::getReferences({ document.getStorage().getTrailerDictionary() }, document.getStorage());
        for (const PDFObjectReference& reference : references)
        {
            if (document.getObjectByReference(reference).isNull())
            {
                dangling << QStringLiteral("%1 %2 R").arg(reference.objectNumber).arg(reference.generation);
            }
        }
        return dangling;
    }

    static QByteArray writeWhole(const PDFDocument& document)
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        PDFDocumentWriter writer(nullptr);
        const PDFOperationResult result = writer.write(&buffer, &document);
        if (!result)
        {
            qWarning() << result.getErrorMessage();
        }
        return buffer.data();
    }

    static PDFDocument readBack(const QByteArray& data)
    {
        PDFDocumentReader reader(nullptr, nullptr, false, false);
        return reader.readFromBuffer(data);
    }

    /// An own OCR layer with the word "Layer" on the page (the writer of PDF4QT)
    static PDFDocument writeLayer(const PDFDocument& document, PDFInteger pageIndex)
    {
        PDFOCRPageResult result;
        result.pageIndex = pageIndex;
        result.state = PDFOCRPageState::Done;
        result.provenance.engineId = QStringLiteral("test");
        PDFOCRBlock block;
        block.id = result.allocateId();
        PDFOCRLine line;
        line.id = result.allocateId();
        PDFOCRWord word;
        word.id = result.allocateId();
        word.text = QStringLiteral("Layer");
        word.originalText = word.text;
        word.quad = PDFOCRQuad::fromRect(QRectF(40, 40, 60, 20));
        line.words.push_back(word);
        line.updateGeometryFromWords();
        block.lines.push_back(line);
        block.updateGeometryFromLines();
        result.blocks.push_back(block);

        PDFDocumentModifier modifier(&document);
        PDFOCRTextLayerWriter::PageRequest request;
        request.pageIndex = pageIndex;
        request.result = result;
        PDFOCRTextLayerWriter::apply(modifier.getBuilder(), &document, { request }, PDFOCRTextLayerWriter::Options());
        modifier.finalize();
        return *modifier.getDocument();
    }

    /// Indices of the removal from page numbers from 1
    static PDFDocument remove(const PDFDocument& document, std::vector<PDFInteger> pageNumbers, PDFDocumentPageOperations::RemovalStatistics* statistics = nullptr)
    {
        for (PDFInteger& page : pageNumbers)
        {
            --page;
        }
        return PDFDocumentPageOperations::removePages(&document, pageNumbers, nullptr, statistics);
    }
};

class DocumentToolPagesTest : public QObject
{
    Q_OBJECT

private slots:
    void rotateTurns_data();
    void rotateTurns();
    void rotatePlan();
    void rotatePlanErrors();
    void rotateGivenParameters();
    void rotateNothingToDo();
    void rotateKeepsObjectsAndOtherPages();
    void rotateKeepsOcrLayer();
    void rotatePlanText();

    void removeOutline();
    void removeNamedDestinations();
    void removeLinksAndActions();
    void removeFormFields();
    void removePageLabels();
    void removeStructureThreadsActions();
    void removeLayers();
    void removeLeavesNoMarker();
    void removeSignedIncremental();
    void removeKeepsOcrLayer();
    void removeEveryPageRefused();
    void removeNoPageRefused();
    void removeToolFacts();
    void removeHostile_data();
    void removeHostile();
    void formatPageNumbers();
};

void DocumentToolPagesTest::rotateTurns_data()
{
    QTest::addColumn<QString>("rotation");
    QTest::addColumn<QString>("pages");
    QTest::addColumn<QStringList>("expected");

    // Page 1: no /Rotate, page 2: /Rotate 90, page 3: /Rotate -90 (= 270, a multiple of 90), page 4: /Rotate 450 (= 90)
    QTest::newRow("right") << QStringLiteral("right") << QStringLiteral("1-4") << QStringList({ "90", "180", "0", "180", "0" });
    QTest::newRow("left") << QStringLiteral("left") << QStringLiteral("1-4") << QStringList({ "270", "0", "180", "0", "0" });
    QTest::newRow("half") << QStringLiteral("half") << QStringLiteral("1-4") << QStringList({ "180", "270", "90", "270", "0" });
    QTest::newRow("reset") << QStringLiteral("reset") << QStringLiteral("2-4") << QStringList({ "0", "0", "0", "0", "0" });
    QTest::newRow("one page") << QStringLiteral("right") << QStringLiteral("5") << QStringList({ "0", "90", "270", "90", "90" });
}

void DocumentToolPagesTest::rotateTurns()
{
    QFETCH(QString, rotation);
    QFETCH(QString, pages);
    QFETCH(QStringList, expected);

    const PDFDocument document = PagesTestPdf::createPages(QByteArray(), { { 2, "/Rotate 90" }, { 3, "/Rotate -90" }, { 4, "/Rotate 450" } }).read();
    QCOMPARE(PagesTestHelper::getRotations(document), QStringList({ "0", "90", "270", "90", "0" }));

    PDFDocumentToolRotatePages tool;
    QVariantMap parameters = PagesTestHelper::pages(pages);
    parameters.insert(QStringLiteral("rotation"), rotation);
    const PDFDocumentToolResult result = tool.run(PagesTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.documents.size(), size_t(1));
    QCOMPARE(PagesTestHelper::getRotations(*result.documents.front().document), expected);
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("Handbook_rotated.pdf"));
}

void DocumentToolPagesTest::rotatePlan()
{
    const PDFDocument document = PagesTestPdf::createPages(QByteArray(), { { 2, "/Rotate 90" } }).read();

    // The plan of the panels: different turns of the pages in one step
    PDFDocumentToolRotatePages tool;
    QVariantMap parameters;
    parameters.insert(QStringLiteral("plan"), QStringLiteral("1,3:right; 2:half; 4-5:left"));
    const PDFDocumentToolResult result = tool.run(PagesTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(PagesTestHelper::getRotations(*result.documents.front().document), QStringList({ "90", "270", "90", "270", "270" }));

    // The facts of 389:21590: "Rotated 5 pages · 1–5", Rotation Mixed (K14)
    QCOMPARE(result.facts.size(), size_t(2));
    QCOMPARE(result.facts[0].id, QStringLiteral("rotated"));
    QCOMPARE(result.facts[0].value, QStringLiteral("5 pages · 1–5"));
    QCOMPARE(result.facts[1].value, QStringLiteral("Mixed"));
    QCOMPARE(result.data.value(QStringLiteral("rotation")).toString(), QStringLiteral("mixed"));

    // One turn of all pages: its text
    parameters.insert(QStringLiteral("plan"), QStringLiteral("9-10:right; 15 : right"));
    const PDFDocument longDocument = PagesTestPdf::createPages().read();
    QString error;
    QVERIFY(!PDFDocumentToolRotatePages::parsePlan(QStringLiteral("9-10:right"), &longDocument, &error));
    QVERIFY(!error.isEmpty());

    parameters.insert(QStringLiteral("plan"), QStringLiteral("2,3:right; 5:right"));
    const PDFDocumentToolResult uniform = tool.run(PagesTestHelper::context(longDocument), parameters, nullptr, nullptr);
    QCOMPARE(uniform.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(uniform.facts[0].value, QStringLiteral("3 pages · 2, 3, 5"));
    QCOMPARE(uniform.facts[1].value, QStringLiteral("90° right"));
}

void DocumentToolPagesTest::rotatePlanErrors()
{
    const PDFDocument document = PagesTestPdf::createPages().read();
    PDFDocumentToolRotatePages tool;
    for (const QString& plan : { QStringLiteral("1:right; 1:left"), QStringLiteral("6:right"), QStringLiteral("1:sideways"), QStringLiteral(":right"), QStringLiteral("1-2"),
                                 QStringLiteral("0:right"), QStringLiteral("1-999999999999:right"), QStringLiteral(";;"), QString(65537, QLatin1Char('1')) })
    {
        QVariantMap parameters;
        parameters.insert(QStringLiteral("plan"), plan);
        const PDFDocumentToolResult result = tool.run(PagesTestHelper::context(document), parameters, nullptr, nullptr);
        QVERIFY2(result.status == PDFDocumentToolResultStatus::Failed, qPrintable(plan.left(40)));
        QVERIFY(result.documents.empty());
        QVERIFY(!result.message.isEmpty());
    }
}

void DocumentToolPagesTest::rotateGivenParameters()
{
    // An assistant: pages and rotation are ignored with a plan, nothing given asks for nothing
    PDFDocumentToolRotatePages tool;
    QVariantMap given;
    given.insert(QStringLiteral("plan"), QStringLiteral("1:right"));
    given.insert(QStringLiteral("rotation"), QStringLiteral("left"));
    std::vector<PDFDocumentToolParameterError> errors = tool.checkGivenParameters(given);
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors.front().parameter, QStringLiteral("rotation"));

    given.insert(QStringLiteral("pages"), QStringLiteral("1"));
    QCOMPARE(tool.checkGivenParameters(given).size(), size_t(2));

    QVariantMap pagesOnly;
    pagesOnly.insert(QStringLiteral("pages"), QStringLiteral("1"));
    errors = tool.checkGivenParameters(pagesOnly);
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors.front().parameter, QStringLiteral("rotation"));

    QVariantMap valid;
    valid.insert(QStringLiteral("rotation"), QStringLiteral("half"));
    QVERIFY(tool.checkGivenParameters(valid).empty());
}

void DocumentToolPagesTest::rotateNothingToDo()
{
    // Reset of pages with the rotation 0 changes nothing: no document, it counts as a use
    const PDFDocument document = PagesTestPdf::createPages().read();
    PDFDocumentToolRotatePages tool;
    QVariantMap parameters = PagesTestHelper::pages(QStringLiteral("1-2"));
    parameters.insert(QStringLiteral("rotation"), QStringLiteral("reset"));
    const PDFDocumentToolResult result = tool.run(PagesTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::NothingToDo);
    QVERIFY(result.documents.empty());
    QVERIFY(result.isUse());
}

void DocumentToolPagesTest::rotateKeepsObjectsAndOtherPages()
{
    // A page below a node with /Rotate 270 inherits it (7.7.3.4); the rotated page gets its own
    // value, its neighbor keeps the inherited one; the numbers of the objects stay (an
    // incremental update of a signed document)
    PagesTestPdf pdf = PagesTestPdf::createPages();
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 8 0 R 6 0 R 7 0 R] /Count 5 /MediaBox [0 0 600 800] >>");
    pdf.set(8, "<< /Type /Pages /Parent 2 0 R /Kids [4 0 R 5 0 R] /Count 2 /Rotate 270 >>");
    pdf.set(4, "<< /Type /Page /Parent 8 0 R /Contents 14 0 R /Resources << >> >>");
    pdf.set(5, "<< /Type /Page /Parent 8 0 R /Contents 15 0 R /Resources << >> >>");
    const PDFDocument document = pdf.read();
    QCOMPARE(PagesTestHelper::getRotations(document), QStringList({ "0", "270", "270", "0", "0" }));

    const PDFDocument rotated = PDFDocumentPageOperations::rotatePages(&document, PDFDocumentPageOperations::createTurnChanges(&document, { 1 }, PageRotation::Rotate90));
    QCOMPARE(PagesTestHelper::getRotations(rotated), QStringList({ "0", "0", "270", "0", "0" }));
    for (size_t i = 0; i < 5; ++i)
    {
        QCOMPARE(rotated.getCatalog()->getPage(i)->getPageReference(), document.getCatalog()->getPage(i)->getPageReference());
    }

    // Only the rotated page is another object: the page 3 and the node are unchanged
    QVERIFY(rotated.getObjectByReference(PDFObjectReference(5, 0)) == document.getObjectByReference(PDFObjectReference(5, 0)));
    QVERIFY(rotated.getObjectByReference(PDFObjectReference(8, 0)) == document.getObjectByReference(PDFObjectReference(8, 0)));
    QVERIFY(!(rotated.getObjectByReference(PDFObjectReference(4, 0)) == document.getObjectByReference(PDFObjectReference(4, 0))));
    QCOMPARE(rotated.getInfo()->version, document.getInfo()->version);
}

void DocumentToolPagesTest::rotateKeepsOcrLayer()
{
    // An own OCR layer bound to its page stays bound after the rotation (/PieceInfo keeps
    // working); its words are in the unrotated space of the page
    const PDFDocument document = PagesTestHelper::writeLayer(PagesTestPdf::createPages().read(), 0);
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(&document, 0).fingerprintMatches);

    const PDFDocument rotated = PDFDocumentPageOperations::rotatePages(&document, PDFDocumentPageOperations::createTurnChanges(&document, { 0 }, PageRotation::Rotate90));
    QCOMPARE(PagesTestHelper::getRotation(rotated, 0), 90);
    const PDFOCRTextLayerWriter::LayerInfo info = PDFOCRTextLayerWriter::readLayerInfo(&rotated, 0);
    QVERIFY(info.isPresent);
    QVERIFY(info.fingerprintMatches);
    QVERIFY(info.isContentOwn);
    const std::optional<PDFOCRPageResult> layer = PDFOCRTextLayerWriter::readLayer(&rotated, 0, nullptr);
    QVERIFY(layer.has_value());
    QCOMPARE(layer->blocks.size(), size_t(1));
    QCOMPARE(layer->blocks.front().lines.front().words.front().text, QStringLiteral("Layer"));
    QCOMPARE(layer->blocks.front().lines.front().words.front().quad.boundingRect(), QRectF(40, 40, 60, 20));

    // A layer, which another tool has unbound (the content of the page changed), stays unbound
    PDFDocumentModifier modifier(&document);
    PDFDictionaryBuilder page = PDFObjectUtils::copyDictionary(modifier.getBuilder()->getStorage(), document.getObjectByReference(document.getCatalog()->getPage(0)->getPageReference()));
    PDFArrayBuilder mediaBox;
    for (const int value : { 0, 0, 500, 800 })
    {
        mediaBox.appendItem(PDFObject::createInteger(value));
    }
    page.setEntry(PDFInplaceOrMemoryString("MediaBox"), PDFObject::createArray(std::move(mediaBox)));
    modifier.getBuilder()->setObject(document.getCatalog()->getPage(0)->getPageReference(), PDFObject::createDictionary(std::move(page)));
    modifier.finalize();
    const PDFDocument foreign = *modifier.getDocument();
    QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(&foreign, 0).fingerprintMatches);
    const PDFDocument foreignRotated = PDFDocumentPageOperations::rotatePages(&foreign, PDFDocumentPageOperations::createTurnChanges(&foreign, { 0 }, PageRotation::Rotate90));
    QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(&foreignRotated, 0).fingerprintMatches);
}

void DocumentToolPagesTest::rotatePlanText()
{
    // The plan of the panels: groups by the rotation, the pages in ranges, read back as written
    std::map<PDFInteger, PageRotation> turns = { { 8, PageRotation::Rotate90 }, { 9, PageRotation::Rotate90 }, { 14, PageRotation::Rotate90 }, { 19, PageRotation::Rotate180 }, { 0, PageRotation::Rotate270 } };
    QCOMPARE(PDFDocumentToolRotatePages::createPlan(turns), QStringLiteral("9-10,15:right; 1:left; 20:half"));

    // 2000 alternating pages: no group is longer than a page range may be, all of them read back
    PagesTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    QByteArray kids;
    constexpr int PAGE_COUNT = 2000;
    for (int i = 0; i < PAGE_COUNT; ++i)
    {
        kids += QByteArray::number(i + 3) + " 0 R ";
        pdf.set(i + 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 100 100] >>");
    }
    pdf.set(2, "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(PAGE_COUNT) + " >>");
    const PDFDocument document = pdf.read();

    std::map<PDFInteger, PageRotation> alternating;
    for (int i = 0; i < PAGE_COUNT; i += 2)
    {
        alternating[i] = PageRotation::Rotate90;
    }
    const QString plan = PDFDocumentToolRotatePages::createPlan(alternating);
    for (const QString& group : plan.split(QStringLiteral("; ")))
    {
        QVERIFY(group.size() <= PDFDocumentToolPageSelection::MAXIMUM_PAGES_LENGTH + 6);
    }
    QString error;
    const std::optional<std::map<PDFInteger, PDFDocumentToolRotatePages::Turn>> parsed = PDFDocumentToolRotatePages::parsePlan(plan, &document, &error);
    QVERIFY2(parsed.has_value(), qPrintable(error));
    QCOMPARE(parsed->size(), alternating.size());
    QVERIFY(parsed->count(1998) && !parsed->count(1999));
}

void DocumentToolPagesTest::removeOutline()
{
    // A -> page 2 with A1 -> page 1, A2 -> page 2; B -> the named destination chap3 (page 3);
    // C -> GoTo page 4; D without a target with D1 -> page 2
    PagesTestPdf pdf = PagesTestPdf::createPages("/Outlines 20 0 R /Names << /Dests << /Names [(chap3) [5 0 R /Fit]] >> >>");
    pdf.set(20, "<< /Type /Outlines /First 21 0 R /Last 24 0 R /Count 6 >>");
    pdf.set(21, "<< /Title (A) /Parent 20 0 R /Next 22 0 R /First 25 0 R /Last 26 0 R /Count 2 /Dest [4 0 R /Fit] >>");
    pdf.set(22, "<< /Title (B) /Parent 20 0 R /Prev 21 0 R /Next 23 0 R /Dest (chap3) >>");
    pdf.set(23, "<< /Title (C) /Parent 20 0 R /Prev 22 0 R /Next 24 0 R /A << /S /GoTo /D [6 0 R /XYZ 0 0 0] >> >>");
    pdf.set(24, "<< /Title (D) /Parent 20 0 R /Prev 23 0 R /First 27 0 R /Last 27 0 R /Count -1 >>");
    pdf.set(25, "<< /Title (A1) /Parent 21 0 R /Next 26 0 R /Dest [3 0 R /Fit] >>");
    pdf.set(26, "<< /Title (A2) /Parent 21 0 R /Prev 25 0 R /Dest [4 0 R /Fit] >>");
    pdf.set(27, "<< /Title (D1) /Parent 24 0 R /Dest [4 0 R /Fit] >>");
    const PDFDocument document = pdf.read();
    QCOMPARE(PagesTestHelper::getOutline(document.getCatalog()->getOutlineRootPtr().data()), QStringLiteral("A[A1 A2] B C D[D1]"));

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);

    // A keeps A1 and loses its own target; A2 and D (D1 was its only item) are removed
    QCOMPARE(PagesTestHelper::getOutline(result.getCatalog()->getOutlineRootPtr().data()), QStringLiteral("A[A1] B C"));
    QCOMPARE(statistics.outlineItemsRemoved, 3);
    QCOMPARE(statistics.outlineTargetsRemoved, 1);

    const PDFDictionary* a = PagesTestHelper::getDictionary(result, PDFObjectReference(21, 0));
    QVERIFY(!a->hasKey("Dest") && !a->hasKey("A"));
    QCOMPARE(a->get("First"), PDFObject::createReference(PDFObjectReference(25, 0)));
    QCOMPARE(a->get("Last"), PDFObject::createReference(PDFObjectReference(25, 0)));
    QCOMPARE(a->get("Count").getInteger(), 1);
    QVERIFY(!PagesTestHelper::getDictionary(result, PDFObjectReference(25, 0))->hasKey("Next"));
    QCOMPARE(PagesTestHelper::getDictionary(result, PDFObjectReference(23, 0))->get("Next").isNull(), true);

    // The root counts its visible items: A, A1 (A is open), B, C
    const PDFDictionary* root = PagesTestHelper::getDictionary(result, PDFObjectReference(20, 0));
    QCOMPARE(root->get("Last"), PDFObject::createReference(PDFObjectReference(23, 0)));
    QCOMPARE(root->get("Count").getInteger(), 4);
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());

    // The items, which nothing refers to, are gone
    QVERIFY(result.getObjectByReference(PDFObjectReference(26, 0)).isNull());
    QVERIFY(result.getObjectByReference(PDFObjectReference(27, 0)).isNull());
}

void DocumentToolPagesTest::removeNamedDestinations()
{
    // A name tree with kids (7.9.6) and the dictionary /Dests of PDF 1.1; /Names /Pages
    PagesTestPdf pdf = PagesTestPdf::createPages("/Names << /Dests 30 0 R /Pages << /Names [(pg2) 4 0 R (pg3) 5 0 R] >> >> /Dests << /old2 [4 0 R /Fit] /old3 [5 0 R /Fit] >>");
    pdf.set(30, "<< /Kids [31 0 R 32 0 R] >>");
    pdf.set(31, "<< /Limits [(chap1) (chap2)] /Names [(chap1) [3 0 R /Fit] (chap2) << /D [4 0 R /Fit] >>] >>");
    pdf.set(32, "<< /Limits [(chap3) (chap3)] /Names [(chap3) [5 0 R /Fit]] >>");
    const PDFDocument document = pdf.read();
    // The catalog joins the name tree and the dictionary of PDF 1.1
    QCOMPARE(document.getCatalog()->getNamedDestinations().size(), size_t(5));

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);
    QCOMPARE(statistics.namedDestinationsRemoved, 2);
    QCOMPARE(statistics.namedPagesRemoved, 1);

    // chap1, chap3 and old3 stay
    const auto& destinations = result.getCatalog()->getNamedDestinations();
    QCOMPARE(destinations.size(), size_t(3));
    QVERIFY(destinations.count("chap1") && destinations.count("chap3") && destinations.count("old3") && !destinations.count("chap2") && !destinations.count("old2"));
    QCOMPARE(destinations.at("chap3").getPageReference(), PDFObjectReference(5, 0));

    const PDFDictionary* catalog = PagesTestHelper::getCatalog(result);
    const PDFDictionary* dests = result.getDictionaryFromObject(catalog->get("Dests"));
    QVERIFY(dests && dests->hasKey("old3") && !dests->hasKey("old2"));
    const PDFDictionary* names = result.getDictionaryFromObject(catalog->get("Names"));
    QVERIFY(names);
    const PDFDictionary* namedPages = result.getDictionaryFromObject(names->get("Pages"));
    QVERIFY(namedPages);
    const PDFArray* namedPageEntries = namedPages->get("Names").getArray();
    QCOMPARE(namedPageEntries->getCount(), size_t(2));
    QCOMPARE(namedPageEntries->getItem(0).getString(), QByteArray("pg3"));
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());
}

void DocumentToolPagesTest::removeLinksAndActions()
{
    // Page 1: links to page 2 by a destination, by a named destination, by a GoTo, a link to a
    // web page and one to page 3; a button with a GoTo to page 2; the page opens page 2 by /AA
    PagesTestPdf pdf = PagesTestPdf::createPages("/Names << /Dests << /Names [(chap2) [4 0 R /Fit]] >> >>",
                                                 { { 1, "/Annots [40 0 R 41 0 R 42 0 R 43 0 R 44 0 R 45 0 R] /AA << /O << /S /GoTo /D [4 0 R /Fit] >> >>" } });
    pdf.set(40, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [4 0 R /Fit] >>");
    pdf.set(41, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest /chap2 >>");
    pdf.set(42, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A << /S /GoTo /D [4 0 R /Fit] >> >>");
    pdf.set(43, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A << /S /URI /URI (https://example.com) >> >>");
    pdf.set(44, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [5 0 R /Fit] >>");
    pdf.set(45, "<< /Type /Annot /Subtype /Widget /FT /Btn /Ff 65536 /T (go) /Rect [0 0 10 10] /P 3 0 R /A << /S /GoTo /D [4 0 R /Fit] >> >>");
    const PDFDocument document = pdf.read();

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);
    QCOMPARE(statistics.linksRemoved, 3);
    QCOMPARE(PagesTestHelper::getDictionary(result, PDFObjectReference(3, 0))->get("Annots").getArray()->getCount(), size_t(3));
    const PDFArray* annots = PagesTestHelper::getDictionary(result, PDFObjectReference(3, 0))->get("Annots").getArray();
    QCOMPARE(annots->getItem(0), PDFObject::createReference(PDFObjectReference(43, 0)));
    QCOMPARE(annots->getItem(1), PDFObject::createReference(PDFObjectReference(44, 0)));
    QCOMPARE(annots->getItem(2), PDFObject::createReference(PDFObjectReference(45, 0)));

    // The button stays without its action, the page without its action of opening
    QVERIFY(!PagesTestHelper::getDictionary(result, PDFObjectReference(45, 0))->hasKey("A"));
    QVERIFY(!PagesTestHelper::getDictionary(result, PDFObjectReference(3, 0))->hasKey("AA"));
    QVERIFY(result.getObjectByReference(PDFObjectReference(40, 0)).isNull());
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());
}

void DocumentToolPagesTest::removeFormFields()
{
    // "name": its widget on page 2 only; "both": kids on pages 1 and 2; "sig": a signature on
    // page 2 with /Perms /DocMDP; "keep": a field of page 3; /CO lists name and keep
    PagesTestPdf pdf = PagesTestPdf::createPages("/AcroForm << /Fields [50 0 R 51 0 R 54 0 R 56 0 R] /SigFlags 3 /CO [50 0 R 56 0 R] >> /Perms << /DocMDP 55 0 R >>",
                                                 { { 1, "/Annots [52 0 R]" }, { 2, "/Annots [50 0 R 53 0 R 54 0 R]" }, { 3, "/Annots [56 0 R]" } });
    pdf.set(50, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /V (FIELDMARKER) /Rect [0 0 10 10] /P 4 0 R >>");
    pdf.set(51, "<< /FT /Tx /T (both) /Kids [52 0 R 53 0 R] >>");
    pdf.set(52, "<< /Type /Annot /Subtype /Widget /Parent 51 0 R /Rect [0 0 10 10] /P 3 0 R >>");
    pdf.set(53, "<< /Type /Annot /Subtype /Widget /Parent 51 0 R /Rect [0 0 10 10] /P 4 0 R >>");
    pdf.set(54, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /V 55 0 R /Rect [0 0 0 0] /P 4 0 R >>");
    pdf.set(55, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> >>");
    pdf.set(56, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (keep) /Rect [0 0 10 10] /P 5 0 R >>");
    const PDFDocument document = pdf.read();
    QCOMPARE(PDFForm::getSignatureCount(&document), 1);

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);
    QCOMPARE(statistics.fieldsRemoved, 2);
    QCOMPARE(statistics.signaturesRemoved, 1);
    QCOMPARE(PDFForm::getSignatureCount(&result), 0);

    const PDFDictionary* catalog = PagesTestHelper::getCatalog(result);
    const PDFDictionary* form = result.getDictionaryFromObject(catalog->get("AcroForm"));
    const PDFArray* fields = form->get("Fields").getArray();
    QCOMPARE(fields->getCount(), size_t(2));
    QCOMPARE(fields->getItem(0), PDFObject::createReference(PDFObjectReference(51, 0)));
    QCOMPARE(fields->getItem(1), PDFObject::createReference(PDFObjectReference(56, 0)));
    QCOMPARE(PagesTestHelper::getDictionary(result, PDFObjectReference(51, 0))->get("Kids").getArray()->getCount(), size_t(1));
    QCOMPARE(form->get("CO").getArray()->getCount(), size_t(1));
    QVERIFY(!form->hasKey("SigFlags"));
    QVERIFY(!catalog->hasKey("Perms"));
    QVERIFY(result.getObjectByReference(PDFObjectReference(55, 0)).isNull());
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());
}

void DocumentToolPagesTest::removePageLabels()
{
    // Labels i, ii, iii, 1, 2 (12.4.2): each page, which stays, keeps its label
    const PDFDocument document = PagesTestPdf::createPages("/PageLabels << /Nums [0 << /S /r >> 3 << /S /D /P (A-) >>] >>").read();
    QStringList labels;
    for (PDFInteger i = 0; i < 5; ++i)
    {
        labels << document.getCatalog()->getPageLabel(i);
    }
    QCOMPARE(labels, QStringList({ "i", "ii", "iii", "A-1", "A-2" }));

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2, 4 }, &statistics);
    QVERIFY(statistics.pageLabelsRewritten);
    labels.clear();
    for (PDFInteger i = 0; i < 3; ++i)
    {
        labels << result.getCatalog()->getPageLabel(i);
    }
    QCOMPARE(labels, QStringList({ "i", "iii", "A-2" }));
}

void DocumentToolPagesTest::removeStructureThreadsActions()
{
    // As PageMaster (owner answer Q8): the structure tree, the articles and the actions of
    // the document go; the beads of a page, which stays, go with the articles
    PagesTestPdf pdf = PagesTestPdf::createPages("/StructTreeRoot 60 0 R /MarkInfo << /Marked true >> /Threads [61 0 R] /OpenAction [4 0 R /Fit]",
                                                 { { 1, "/B [62 0 R]" }, { 2, "/B [63 0 R]" } });
    pdf.set(60, "<< /Type /StructTreeRoot /K << /S /P /Pg 4 0 R /K 0 >> >>");
    pdf.set(61, "<< /Type /Thread /F 62 0 R >>");
    pdf.set(62, "<< /Type /Bead /T 61 0 R /N 63 0 R /V 63 0 R /P 3 0 R /R [0 0 10 10] >>");
    pdf.set(63, "<< /Type /Bead /T 61 0 R /N 62 0 R /V 62 0 R /P 4 0 R /R [0 0 10 10] >>");
    const PDFDocument document = pdf.read();

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);
    QVERIFY(statistics.structureTreeRemoved);
    QVERIFY(statistics.threadsRemoved);
    QVERIFY(statistics.documentActionsRemoved);
    const PDFDictionary* catalog = PagesTestHelper::getCatalog(result);
    for (const char* key : { "StructTreeRoot", "MarkInfo", "Threads", "OpenAction" })
    {
        QVERIFY2(!catalog->hasKey(key), key);
    }
    QVERIFY(!PagesTestHelper::getDictionary(result, PDFObjectReference(3, 0))->hasKey("B"));
    for (const int number : { 60, 61, 62, 63 })
    {
        QVERIFY(result.getObjectByReference(PDFObjectReference(number, 0)).isNull());
    }
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());
}

void DocumentToolPagesTest::removeLayers()
{
    // "OnlyRemoved" is used only by page 2, "Shared" by pages 1 and 2, "Unused" by no page
    PagesTestPdf pdf = PagesTestPdf::createPages("/OCProperties << /OCGs [70 0 R 71 0 R 72 0 R] /D << /Order [70 0 R 71 0 R 72 0 R] /OFF [70 0 R] >> >>",
                                                 { { 1, "/Resources << /Properties << /L1 71 0 R >> >>" }, { 2, "/Resources << /Properties << /L1 70 0 R /L2 71 0 R >> >>" } });
    pdf.set(70, "<< /Type /OCG /Name (OnlyRemoved) >>");
    pdf.set(71, "<< /Type /OCG /Name (Shared) >>");
    pdf.set(72, "<< /Type /OCG /Name (Unused) >>");
    // The pages of createPages have empty resources: the extra entries replace them
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /Contents 13 0 R /Resources << /Properties << /L1 71 0 R >> >> >>");
    pdf.set(4, "<< /Type /Page /Parent 2 0 R /Contents 14 0 R /Resources << /Properties << /L1 70 0 R /L2 71 0 R >> >> >>");
    const PDFDocument document = pdf.read();

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);
    QCOMPARE(statistics.layersRemoved, 1);
    const PDFDictionary* properties = result.getDictionaryFromObject(PagesTestHelper::getCatalog(result)->get("OCProperties"));
    const PDFArray* groups = properties->get("OCGs").getArray();
    QCOMPARE(groups->getCount(), size_t(2));
    QCOMPARE(groups->getItem(0), PDFObject::createReference(PDFObjectReference(71, 0)));
    QCOMPARE(groups->getItem(1), PDFObject::createReference(PDFObjectReference(72, 0)));
    const PDFDictionary* configuration = result.getDictionaryFromObject(properties->get("D"));
    QCOMPARE(configuration->get("Order").getArray()->getCount(), size_t(2));
    QCOMPARE(configuration->get("OFF").getArray()->getCount(), size_t(0));
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());
}

void DocumentToolPagesTest::removeLeavesNoMarker()
{
    // Everything of page 2 is gone from the written file: its content, its annotation, its
    // field, the outline item and the named destination, which led to it (chapter 7)
    PagesTestPdf pdf = PagesTestPdf::createPages("/Outlines 20 0 R /AcroForm << /Fields [50 0 R] >> /Names << /Dests << /Names [(DESTMARKER) [4 0 R /Fit]] >> >>",
                                                 { { 2, "/Annots [50 0 R 80 0 R]" } });
    pdf.set(20, "<< /Type /Outlines /First 21 0 R /Last 21 0 R /Count 1 >>");
    pdf.set(21, "<< /Title (OUTLINEMARKER) /Parent 20 0 R /Dest [4 0 R /Fit] >>");
    pdf.set(50, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (FIELDMARKER) /Rect [0 0 10 10] /P 4 0 R >>");
    pdf.set(80, "<< /Type /Annot /Subtype /Text /Contents (ANNOTMARKER) /Rect [0 0 10 10] /P 4 0 R >>");
    const PDFDocument document = pdf.read();
    const QByteArray before = PagesTestHelper::writeWhole(document);
    for (const char* marker : { "PAGEMARKER2", "ANNOTMARKER", "FIELDMARKER", "OUTLINEMARKER", "DESTMARKER" })
    {
        QVERIFY2(before.contains(marker), marker);
    }

    const PDFDocument result = PagesTestHelper::remove(document, { 2 });
    const QByteArray after = PagesTestHelper::writeWhole(result);
    for (const char* marker : { "PAGEMARKER2", "ANNOTMARKER", "FIELDMARKER", "OUTLINEMARKER", "DESTMARKER" })
    {
        QVERIFY2(!after.contains(marker), marker);
    }
    QVERIFY(after.contains("PAGEMARKER1") && after.contains("PAGEMARKER3"));

    // The written file has the four pages in their order
    const PDFDocument reread = PagesTestHelper::readBack(after);
    QCOMPARE(reread.getCatalog()->getPageCount(), size_t(4));
    QVERIFY(PagesTestHelper::getDanglingReferences(reread).isEmpty());
    QCOMPARE(reread.getCatalog()->getPage(1)->getPageReference(), PDFObjectReference(5, 0));
}

void DocumentToolPagesTest::removeSignedIncremental()
{
    // A signature of page 1 stays: the result keeps the numbers of the objects, so its
    // incremental update after the signed bytes is written (chapter 12 of the plan)
    PagesTestPdf pdf = PagesTestPdf::createPages("/AcroForm << /Fields [54 0 R] /SigFlags 3 >>", { { 1, "/Annots [54 0 R]" } });
    pdf.set(54, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /V 55 0 R /Rect [0 0 0 0] /P 3 0 R >>");
    pdf.set(55, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> >>");
    const QByteArray original = pdf.write();
    const PDFDocument document = pdf.read();

    const PDFDocument result = PagesTestHelper::remove(document, { 4 });
    QCOMPARE(PDFForm::getSignatureCount(&result), 1);
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(4));
    for (size_t i = 0; i < 3; ++i)
    {
        QCOMPARE(result.getCatalog()->getPage(i)->getPageReference(), document.getCatalog()->getPage(i)->getPageReference());
    }

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    PDFDocumentWriter writer(nullptr);
    const PDFOperationResult written = writer.writeIncrementalUpdate(&buffer, original, &result);
    QVERIFY2(written, qPrintable(written.getErrorMessage()));
    QVERIFY(buffer.data().startsWith(original));
    const PDFDocument reread = PagesTestHelper::readBack(buffer.data());
    QCOMPARE(reread.getCatalog()->getPageCount(), size_t(4));
    QCOMPARE(PDFForm::getSignatureCount(&reread), 1);
}

void DocumentToolPagesTest::removeKeepsOcrLayer()
{
    // The layer of page 3 is bound to its page; the removal of page 2 takes away a link of
    // page 3 (the annotations are a part of the fingerprint) - the layer stays bound
    PagesTestPdf pdf = PagesTestPdf::createPages(QByteArray(), { { 3, "/Annots [40 0 R]" } });
    pdf.set(40, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [4 0 R /Fit] >>");
    const PDFDocument document = PagesTestHelper::writeLayer(pdf.read(), 2);
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(&document, 2).fingerprintMatches);

    const PDFDocument result = PagesTestHelper::remove(document, { 2 });
    QCOMPARE(result.getCatalog()->getPage(1)->getAnnotations().size(), size_t(0));
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(&result, 1).fingerprintMatches);
    QVERIFY(PDFOCRTextLayerWriter::readLayer(&result, 1, nullptr).has_value());
}

void DocumentToolPagesTest::removeEveryPageRefused()
{
    // A document keeps at least one page (388:21466): the core refuses, the tool fails
    const PDFDocument document = PagesTestPdf::createPages().read();
    QVERIFY_THROWS_EXCEPTION(PDFException, PDFDocumentPageOperations::removePages(&document, { 0, 1, 2, 3, 4 }, nullptr, nullptr));

    PDFDocumentToolRemovePages tool;
    const PDFDocumentToolResult result = tool.run(PagesTestHelper::context(document), PagesTestHelper::pages(QString()), nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.documents.empty());
    QVERIFY(result.message.contains(QStringLiteral("at least one page")));

    QVariantMap even = PagesTestHelper::pages(QStringLiteral("1-5"));
    QCOMPARE(tool.run(PagesTestHelper::context(document), even, nullptr, nullptr).status, PDFDocumentToolResultStatus::Failed);
}

void DocumentToolPagesTest::removeNoPageRefused()
{
    // No page given (MCP: pages is required) and the selection of no page of a panel
    const PDFDocument document = PagesTestPdf::createPages().read();
    PDFDocumentToolRemovePages tool;
    const PDFDocumentToolResult missing = tool.run(PagesTestHelper::context(document), QVariantMap(), nullptr, nullptr);
    QCOMPARE(missing.status, PDFDocumentToolResultStatus::Failed);

    PDFDocumentToolPageSelection none;
    none.isNone = true;
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), none.toVariant());
    const PDFDocumentToolResult result = tool.run(PagesTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("No page is selected")));

    // The value of no page survives the canonical value of the model
    QString error;
    const std::optional<PDFDocumentToolPageSelection> read = PDFDocumentToolPageSelection::fromVariant(none.toVariant(), &error);
    QVERIFY(read.has_value() && read->isNone);
}

void DocumentToolPagesTest::removeToolFacts()
{
    // 388:22315: "Removed 4 pages · 4–6, 20" of 24 pages, "Document 20 pages (was 24)"
    PagesTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    QByteArray kids;
    for (int i = 0; i < 24; ++i)
    {
        kids += QByteArray::number(i + 3) + " 0 R ";
        pdf.set(i + 3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] >>");
    }
    pdf.set(2, "<< /Type /Pages /Kids [" + kids + "] /Count 24 >>");
    const PDFDocument document = pdf.read();

    PDFDocumentToolRemovePages tool;
    const PDFDocumentToolResult result = tool.run(PagesTestHelper::context(document), PagesTestHelper::pages(QStringLiteral("4-6, 20")), nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(result.facts.size(), size_t(2));
    QCOMPARE(result.facts[0].value, QStringLiteral("4 pages · 4–6, 20"));
    QCOMPARE(result.facts[1].value, QStringLiteral("20 pages (was 24)"));
    QCOMPARE(result.documents.front().document->getCatalog()->getPageCount(), size_t(20));
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("Handbook_removed.pdf"));
}

void DocumentToolPagesTest::removeHostile_data()
{
    QTest::addColumn<QByteArray>("catalog");
    QTest::addColumn<int>("extraNumber");
    QTest::addColumn<QByteArray>("extraObject");
    QTest::addColumn<int>("extraNumber2");
    QTest::addColumn<QByteArray>("extraObject2");

    // An outline, whose /Next comes back to its first item; an item, which is its own child (the
    // reader of the outline stops at an item seen already)
    QTest::newRow("outline cycle") << QByteArray("/Outlines 20 0 R") << 20 << QByteArray("<< /First 21 0 R /Last 21 0 R >>")
                                   << 21 << QByteArray("<< /Title (A) /Parent 20 0 R /Next 21 0 R /First 21 0 R /Dest [4 0 R /Fit] >>");
    // A field, which is its own kid, a name tree, which is its own kid
    QTest::newRow("field cycle") << QByteArray("/AcroForm << /Fields [50 0 R] >>") << 50 << QByteArray("<< /T (x) /Kids [50 0 R 51 0 R] >>")
                                 << 51 << QByteArray("<< /Type /Annot /Subtype /Widget /Parent 50 0 R /Rect [0 0 1 1] /P 4 0 R >>");
    // A kid of a name tree listed twice (a name tree, which is its own kid, ends the reader of the
    // document before the tool: SECURITY_TODO 29)
    QTest::newRow("name tree shared kid") << QByteArray("/Names << /Dests 30 0 R >>") << 30 << QByteArray("<< /Kids [31 0 R 31 0 R] >>")
                                          << 31 << QByteArray("<< /Names [(a) [4 0 R /Fit]] >>");
    // An outline item reached by /Next, which is a page: the chain ends there, the page stays
    QTest::newRow("outline to page") << QByteArray("/Outlines 20 0 R") << 20 << QByteArray("<< /First 21 0 R /Last 21 0 R >>")
                                     << 21 << QByteArray("<< /Title (A) /Parent 20 0 R /Next 5 0 R >>");
    // A widget, which says it is on the removed page, but page 1 lists it
    QTest::newRow("lying widget") << QByteArray("/AcroForm << /Fields [50 0 R] >>") << 50 << QByteArray("<< /Type /Annot /Subtype /Widget /FT /Tx /T (x) /Rect [0 0 1 1] /P 4 0 R >>")
                                  << 3 << QByteArray("<< /Type /Page /Parent 2 0 R /Contents 13 0 R /Resources << >> /Annots [50 0 R] >>");
}

void DocumentToolPagesTest::removeHostile()
{
    QFETCH(QByteArray, catalog);
    QFETCH(int, extraNumber);
    QFETCH(QByteArray, extraObject);
    QFETCH(int, extraNumber2);
    QFETCH(QByteArray, extraObject2);

    PagesTestPdf pdf = PagesTestPdf::createPages(catalog);
    pdf.set(extraNumber, extraObject);
    pdf.set(extraNumber2, extraObject2);
    const PDFDocument document = pdf.read();

    QElapsedTimer timer;
    timer.start();
    const PDFDocument result = PagesTestHelper::remove(document, { 2 });
    QVERIFY(timer.elapsed() < 5000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(4));
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());

    // The page reached by the hostile outline stays a page of the document
    QCOMPARE(result.getCatalog()->getPage(1)->getPageReference(), PDFObjectReference(5, 0));
    if (QByteArray(QTest::currentDataTag()) == "lying widget")
    {
        // Page 1 keeps its widget, its field stays
        QCOMPARE(result.getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));
        QCOMPARE(result.getDictionaryFromObject(PagesTestHelper::getCatalog(result)->get("AcroForm"))->get("Fields").getArray()->getCount(), size_t(1));
    }
}

void DocumentToolPagesTest::formatPageNumbers()
{
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 3, 4, 5, 19 }), QStringLiteral("4–6, 20"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 8, 9, 14 }), QStringLiteral("9, 10, 15"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 0, 1, 2, 4, 5 }), QStringLiteral("1–3, 5, 6"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 0, 2, 4, 6 }, 2), QStringLiteral("1, 3, …"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({}), QString());
}

QTEST_MAIN(DocumentToolPagesTest)

#include "tst_documenttoolpagestest.moc"
