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

#include <algorithm>
#include <iterator>
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

    /// The ranges of the leaves of the document parts in the order of a depth-first walk (14.12.3)
    /// as page numbers ("1-2", "3"), and what breaks Table 409 and 14.12.3: "End of one page" (/End
    /// only for more pages), "invalid node", "page n not in its part" (/DPart of a page is the leaf,
    /// whose range has it)
    static QStringList getDocumentParts(const PDFDocument& document)
    {
        QStringList parts;
        const PDFDictionary* root = document.getDictionaryFromObject(getCatalog(document)->get("DPartRoot"));
        if (!root)
        {
            return parts;
        }
        std::map<PDFObjectReference, size_t> pageIndices;
        const size_t pageCount = document.getCatalog()->getPageCount();
        for (size_t i = 0; i < pageCount; ++i)
        {
            pageIndices[document.getCatalog()->getPage(i)->getPageReference()] = i;
        }
        std::set<PDFObjectReference> visited;
        std::vector<PDFObjectReference> leafOfPage(pageCount);
        readDocumentPart(document, root->get("DPartRootNode"), pageIndices, &visited, &leafOfPage, &parts);
        for (size_t i = 0; i < pageCount; ++i)
        {
            const PDFObject& part = getDictionary(document, document.getCatalog()->getPage(i)->getPageReference())->get("DPart");
            if (!part.isReference() || part.getReference() != leafOfPage[i])
            {
                parts << QStringLiteral("page %1 not in its part").arg(qint64(i + 1));
            }
        }
        return parts;
    }

    static void readDocumentPart(const PDFDocument& document, const PDFObject& node, const std::map<PDFObjectReference, size_t>& pageIndices, std::set<PDFObjectReference>* visited,
                                 std::vector<PDFObjectReference>* leafOfPage, QStringList* parts)
    {
        const PDFDictionary* dictionary = document.getDictionaryFromObject(node);
        if (!node.isReference() || !visited->insert(node.getReference()).second || !dictionary)
        {
            *parts << QStringLiteral("invalid node");
            return;
        }
        if (dictionary->hasKey("DParts"))
        {
            for (const PDFObject& children : *document.getObject(dictionary->get("DParts")).getArray())
            {
                for (const PDFObject& child : *document.getObject(children).getArray())
                {
                    readDocumentPart(document, child, pageIndices, visited, leafOfPage, parts);
                }
            }
            return;
        }
        const PDFObject& start = dictionary->get("Start");
        const PDFObject& end = dictionary->hasKey("End") ? dictionary->get("End") : start;
        if (!start.isReference() || !end.isReference() || !pageIndices.count(start.getReference()) || !pageIndices.count(end.getReference()))
        {
            *parts << QStringLiteral("invalid node");
            return;
        }
        const size_t first = pageIndices.at(start.getReference());
        const size_t last = pageIndices.at(end.getReference());
        *parts << (first == last ? QString::number(qint64(first + 1)) : QStringLiteral("%1-%2").arg(qint64(first + 1)).arg(qint64(last + 1)));
        if (dictionary->hasKey("End") && first == last)
        {
            *parts << QStringLiteral("End of one page");
        }
        for (size_t i = first; i <= last && i < leafOfPage->size(); ++i)
        {
            (*leafOfPage)[i] = node.getReference();
        }
    }

    /// Five pages in the document parts 62 (pages 1-2) and 63, which has 64 (page 3) and 65 (pages
    /// 4-5, with its metadata /DPM); three levels named by /NodeNameList, /RecordLevel 2
    static PagesTestPdf createDocumentParts()
    {
        PagesTestPdf pdf = PagesTestPdf::createPages("/DPartRoot 60 0 R", { { 1, "/DPart 62 0 R" }, { 2, "/DPart 62 0 R" }, { 3, "/DPart 64 0 R" }, { 4, "/DPart 65 0 R" }, { 5, "/DPart 65 0 R" } });
        pdf.set(60, "<< /Type /DPartRoot /DPartRootNode 61 0 R /NodeNameList [/Document /Chapter /Range] /RecordLevel 2 >>");
        pdf.set(61, "<< /Type /DPart /Parent 60 0 R /DParts [[62 0 R 63 0 R]] >>");
        pdf.set(62, "<< /Type /DPart /Parent 61 0 R /Start 3 0 R /End 4 0 R >>");
        pdf.set(63, "<< /Type /DPart /Parent 61 0 R /DParts [[64 0 R] [65 0 R]] >>");
        pdf.set(64, "<< /Type /DPart /Parent 63 0 R /Start 5 0 R >>");
        pdf.set(65, "<< /Type /DPart /Parent 63 0 R /Start 6 0 R /End 7 0 R /DPM << /Name (Annex) >> >>");
        return pdf;
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
    void removeOutlineCountsAbove();
    void removeNamedDestinations();
    void removeLinksAndActions();
    void removeFormFields();
    void removeFieldsWithWidgetKids();
    void removeNestedFieldWithWidgetKids();
    void removePageLabels();
    void removeStructureThreadsActions();
    void removeLayers();
    void removeDocumentParts();
    void removeDocumentPartsDamaged_data();
    void removeDocumentPartsDamaged();
    void removeLeavesNoMarker();
    void removeSignedIncremental();
    void removeKeepsOcrLayer();
    void removeEveryPageRefused();
    void removeNoPageRefused();
    void removeToolFacts();
    void removeHostile_data();
    void removeHostile();
    void formatPageNumbers();

    void followPagesMap();
    void followPagesTools();
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
    QCOMPARE(uniform.facts[0].value, QStringLiteral("3 pages · 2–3, 5"));
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

void DocumentToolPagesTest::removeOutlineCountsAbove()
{
    // A (open) > A1 (open) > A1a -> page 2; B (closed) > B1 -> page 1, B2 -> page 2. /Count of an
    // open item is the number of its visible descendants, of a closed one the negative number of
    // the descendants, which would be visible when it opens; the root counts its visible items
    // (ISO 32000-2, Table 152 and 153). Before: A, A1, A1a and B are visible.
    PagesTestPdf pdf = PagesTestPdf::createPages("/Outlines 20 0 R");
    pdf.set(20, "<< /Type /Outlines /First 21 0 R /Last 24 0 R /Count 4 >>");
    pdf.set(21, "<< /Title (A) /Parent 20 0 R /Next 24 0 R /First 22 0 R /Last 22 0 R /Count 2 /Dest [3 0 R /Fit] >>");
    pdf.set(22, "<< /Title (A1) /Parent 21 0 R /First 23 0 R /Last 23 0 R /Count 1 /Dest [3 0 R /Fit] >>");
    pdf.set(23, "<< /Title (A1a) /Parent 22 0 R /Dest [4 0 R /Fit] >>");
    pdf.set(24, "<< /Title (B) /Parent 20 0 R /Prev 21 0 R /First 25 0 R /Last 26 0 R /Count -2 /Dest [3 0 R /Fit] >>");
    pdf.set(25, "<< /Title (B1) /Parent 24 0 R /Next 26 0 R /Dest [3 0 R /Fit] >>");
    pdf.set(26, "<< /Title (B2) /Parent 24 0 R /Prev 25 0 R /Dest [4 0 R /Fit] >>");
    const PDFDocument document = pdf.read();
    QCOMPARE(PagesTestHelper::getOutline(document.getCatalog()->getOutlineRootPtr().data()), QStringLiteral("A[A1[A1a]] B[B1 B2]"));

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);
    QCOMPARE(PagesTestHelper::getOutline(result.getCatalog()->getOutlineRootPtr().data()), QStringLiteral("A[A1] B[B1]"));
    QCOMPARE(statistics.outlineItemsRemoved, 2);

    // A1 has no item below it; A has one visible descendant (A1); B, closed, would show B1
    QVERIFY(!PagesTestHelper::getDictionary(result, PDFObjectReference(22, 0))->hasKey("Count"));
    QCOMPARE(PagesTestHelper::getDictionary(result, PDFObjectReference(21, 0))->get("Count").getInteger(), 1);
    QCOMPARE(PagesTestHelper::getDictionary(result, PDFObjectReference(24, 0))->get("Count").getInteger(), -1);

    // The root: A, A1 and B are visible
    QCOMPARE(PagesTestHelper::getDictionary(result, PDFObjectReference(20, 0))->get("Count").getInteger(), 3);
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());
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

void DocumentToolPagesTest::removeFieldsWithWidgetKids()
{
    // A terminal field, whose kids are its widgets (ISO 32000-2, 12.7.4.1): the signature "sig"
    // with its widget on page 2 and /Perms /DocMDP of its signature; the text field "twice" with
    // both widgets on page 2. Both fields go with their last widget.
    PagesTestPdf pdf = PagesTestPdf::createPages("/AcroForm << /Fields [50 0 R 53 0 R 56 0 R] /SigFlags 3 >> /Perms << /DocMDP 52 0 R >>",
                                                 { { 2, "/Annots [51 0 R 54 0 R 55 0 R]" }, { 3, "/Annots [57 0 R]" } });
    pdf.set(50, "<< /FT /Sig /T (sig) /V 52 0 R /Kids [51 0 R] >>");
    pdf.set(51, "<< /Type /Annot /Subtype /Widget /Parent 50 0 R /Rect [0 0 0 0] /P 4 0 R >>");
    pdf.set(52, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> /Name (SIGNERMARKER) >>");
    pdf.set(53, "<< /FT /Tx /T (twice) /V (FIELDMARKER) /Kids [54 0 R 55 0 R] >>");
    pdf.set(54, "<< /Type /Annot /Subtype /Widget /Parent 53 0 R /Rect [0 0 10 10] /P 4 0 R >>");
    pdf.set(55, "<< /Type /Annot /Subtype /Widget /Parent 53 0 R /Rect [20 0 30 10] /P 4 0 R >>");
    pdf.set(56, "<< /FT /Tx /T (keep) /Kids [57 0 R] >>");
    pdf.set(57, "<< /Type /Annot /Subtype /Widget /Parent 56 0 R /Rect [0 0 10 10] /P 5 0 R >>");
    const PDFDocument document = pdf.read();

    // The form of PDF4QT counts the widget of the signature as a second signature field
    QVERIFY(PDFForm::getSignatureCount(&document) > 0);

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);
    QCOMPARE(statistics.fieldsRemoved, 2);
    QCOMPARE(statistics.signaturesRemoved, 1);
    QCOMPARE(PDFForm::getSignatureCount(&result), 0);

    // Only "keep" stays; no signature field: no signature flags, no permissions of the signature
    const PDFDictionary* catalog = PagesTestHelper::getCatalog(result);
    const PDFDictionary* form = result.getDictionaryFromObject(catalog->get("AcroForm"));
    const PDFArray* fields = form->get("Fields").getArray();
    QCOMPARE(fields->getCount(), size_t(1));
    QCOMPARE(fields->getItem(0), PDFObject::createReference(PDFObjectReference(56, 0)));
    QVERIFY(!form->hasKey("SigFlags"));
    QVERIFY(!catalog->hasKey("Perms"));
    QVERIFY(result.getObjectByReference(PDFObjectReference(52, 0)).isNull());
    QVERIFY(result.getObjectByReference(PDFObjectReference(53, 0)).isNull());
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());

    // The written file has neither the signer nor the value of the removed field
    const QByteArray written = PagesTestHelper::writeWhole(result);
    QVERIFY(!written.contains("SIGNERMARKER"));
    QVERIFY(!written.contains("FIELDMARKER"));

    // The tool says the signature is gone with its page
    PDFDocumentToolRemovePages tool;
    const PDFDocumentToolResult toolResult = tool.run(PagesTestHelper::context(document), PagesTestHelper::pages(QStringLiteral("2")), nullptr, nullptr);
    QVERIFY2(toolResult.isSuccess(), qPrintable(toolResult.message));
    QCOMPARE(toolResult.data.value(QStringLiteral("signaturesRemoved")).toInteger(), 1);
    QCOMPARE(toolResult.data.value(QStringLiteral("fieldsRemoved")).toInteger(), 2);
}

void DocumentToolPagesTest::removeNestedFieldWithWidgetKids()
{
    // A terminal field below a non-terminal one (ISO 32000-2, 12.7.4.1), whose kids are its two
    // widgets, both on page 2; it is in the calculation order /CO (12.7.3, Table 224) with its
    // sibling on page 3. The field goes with its widgets: also from /CO, its value too.
    PagesTestPdf pdf = PagesTestPdf::createPages("/AcroForm << /Fields [60 0 R] /CO [61 0 R 64 0 R] >>",
                                                 { { 2, "/Annots [62 0 R 63 0 R]" }, { 3, "/Annots [65 0 R]" } });
    pdf.set(60, "<< /T (parent) /Kids [61 0 R 64 0 R] >>");
    pdf.set(61, "<< /FT /Tx /T (child) /Parent 60 0 R /V (NESTEDMARKER) /Kids [62 0 R 63 0 R] >>");
    pdf.set(62, "<< /Type /Annot /Subtype /Widget /Parent 61 0 R /Rect [0 0 10 10] /P 4 0 R >>");
    pdf.set(63, "<< /Type /Annot /Subtype /Widget /Parent 61 0 R /Rect [20 0 30 10] /P 4 0 R >>");
    pdf.set(64, "<< /FT /Tx /T (other) /Parent 60 0 R /Kids [65 0 R] >>");
    pdf.set(65, "<< /Type /Annot /Subtype /Widget /Parent 64 0 R /Rect [0 0 10 10] /P 5 0 R >>");
    const PDFDocument document = pdf.read();

    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);
    QCOMPARE(statistics.fieldsRemoved, 1);

    // The parent keeps its other kid, /CO has only the field, which stays
    const PDFDictionary* catalog = PagesTestHelper::getCatalog(result);
    const PDFDictionary* form = result.getDictionaryFromObject(catalog->get("AcroForm"));
    const PDFObject& calculationOrder = result.getObject(form->get("CO"));
    QVERIFY(calculationOrder.isArray());
    QCOMPARE(calculationOrder.getArray()->getCount(), size_t(1));
    QCOMPARE(calculationOrder.getArray()->getItem(0), PDFObject::createReference(PDFObjectReference(64, 0)));
    const PDFDictionary* parent = result.getDictionaryFromObject(result.getObjectByReference(PDFObjectReference(60, 0)));
    QVERIFY(parent);
    const PDFObject& parentKids = result.getObject(parent->get("Kids"));
    QCOMPARE(parentKids.getArray()->getCount(), size_t(1));
    QCOMPARE(parentKids.getArray()->getItem(0), PDFObject::createReference(PDFObjectReference(64, 0)));
    QVERIFY(result.getObjectByReference(PDFObjectReference(61, 0)).isNull());
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());
    QVERIFY(!PagesTestHelper::writeWhole(result).contains("NESTEDMARKER"));
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

void DocumentToolPagesTest::removeDocumentParts()
{
    // 14.12 (RS3BCORE, owner fix wave 2026-11-12): the leaves of the document parts are ranges of
    // pages in the order of the page tree, every page in one of them (14.12.2, 14.12.3). The pages,
    // which stay, keep their order, so a part keeps the pages of its range, which stay: /Start and
    // /End move to the first and the last of them, /End goes for one page (Table 409); a part
    // without a page goes and so does a node without a part; /DPM of a node, which stays, is kept;
    // /NodeNameList keeps a name per level, which stays, /RecordLevel only an existing level
    // (Table 408)
    const PDFDocument document = PagesTestHelper::createDocumentParts().read();
    QCOMPARE(PagesTestHelper::getDocumentParts(document), QStringList({ QStringLiteral("1-2"), QStringLiteral("3"), QStringLiteral("4-5") }));

    // The first and the last page: /Start of 62 and /End of 65 move
    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument ends = PagesTestHelper::remove(document, { 1, 5 }, &statistics);
    QCOMPARE(PagesTestHelper::getDocumentParts(ends), QStringList({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3") }));
    QVERIFY(!statistics.documentPartsRemoved);
    QCOMPARE(PagesTestHelper::getDictionary(ends, PDFObjectReference(62, 0))->get("Start").getReference(), PDFObjectReference(4, 0));
    QCOMPARE(PagesTestHelper::getDictionary(ends, PDFObjectReference(65, 0))->get("Start").getReference(), PDFObjectReference(6, 0));
    QVERIFY(ends.getDictionaryFromObject(PagesTestHelper::getDictionary(ends, PDFObjectReference(65, 0))->get("DPM")));
    const PDFDictionary* endsRoot = PagesTestHelper::getDictionary(ends, PDFObjectReference(60, 0));
    QCOMPARE(endsRoot->get("NodeNameList").getArray()->getCount(), size_t(3));
    QCOMPARE(endsRoot->get("RecordLevel"), PDFObject::createInteger(2));
    QVERIFY(PagesTestHelper::getDanglingReferences(ends).isEmpty());

    // The middle page of 62..65: the part 64 goes, its inner array of 63 too (an array of DParts is never empty)
    const PDFDocument middle = PagesTestHelper::remove(document, { 3 });
    QCOMPARE(PagesTestHelper::getDocumentParts(middle), QStringList({ QStringLiteral("1-2"), QStringLiteral("3-4") }));
    const PDFArray* middleParts = PagesTestHelper::getDictionary(middle, PDFObjectReference(63, 0))->get("DParts").getArray();
    QCOMPARE(middleParts->getCount(), size_t(1));
    QCOMPARE(middleParts->getItem(0).getArray()->getCount(), size_t(1));
    QCOMPARE(middleParts->getItem(0).getArray()->getItem(0).getReference(), PDFObjectReference(65, 0));
    QVERIFY(middle.getObjectByReference(PDFObjectReference(64, 0)).isNull());
    QVERIFY(PagesTestHelper::getDanglingReferences(middle).isEmpty());

    // Every page of 63: 63, 64 and 65 go, the hierarchy has two levels
    const PDFDocument tail = PagesTestHelper::remove(document, { 3, 4, 5 }, &statistics);
    QVERIFY(!statistics.documentPartsRemoved);
    QCOMPARE(PagesTestHelper::getDocumentParts(tail), QStringList({ QStringLiteral("1-2") }));
    for (int object : { 63, 64, 65 })
    {
        QVERIFY(tail.getObjectByReference(PDFObjectReference(object, 0)).isNull());
    }
    const PDFDictionary* tailRoot = PagesTestHelper::getDictionary(tail, PDFObjectReference(60, 0));
    QCOMPARE(tailRoot->get("NodeNameList").getArray()->getCount(), size_t(2));
    QCOMPARE(tailRoot->get("NodeNameList").getArray()->getItem(1), PDFObject::createName(QByteArray("Chapter")));
    QVERIFY(!tailRoot->hasKey("RecordLevel"));
    QVERIFY(PagesTestHelper::getDanglingReferences(tail).isEmpty());

    // The tool says nothing of the parts, they stay
    const PDFDocumentToolResult result = PDFDocumentToolRemovePages().run(PagesTestHelper::context(document), PagesTestHelper::pages(QStringLiteral("2")), nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(PagesTestHelper::getDocumentParts(*result.documents.front().document), QStringList({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3-4") }));
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        QVERIFY2(fact.id != QLatin1String("documentParts"), qPrintable(fact.value));
    }
}

void DocumentToolPagesTest::removeDocumentPartsDamaged_data()
{
    QTest::addColumn<QVariantMap>("objects");

    // 14.12.2: a tree, whose node has one parent; Table 408, 409: /DPartRootNode and the nodes are
    // indirect, a node has /DParts (no array empty) or /Start, a range is of pages in their order.
    // A hierarchy, which is none of it, is removed whole - the walk keeps one visited set and a
    // depth limit (a cycle, a node shared by two paths, a chain deeper than 256 end)
    auto objects = [](std::initializer_list<std::pair<int, QByteArray>> list)
    {
        QVariantMap map;
        for (const auto& [number, text] : list)
        {
            map.insert(QString::number(number), text);
        }
        return map;
    };
    QTest::newRow("cycle") << objects({ { 63, "<< /Type /DPart /Parent 61 0 R /DParts [[64 0 R] [61 0 R]] >>" } });
    QTest::newRow("node of two parents") << objects({ { 63, "<< /Type /DPart /Parent 61 0 R /DParts [[64 0 R 62 0 R] [65 0 R]] >>" } });
    QTest::newRow("end before start") << objects({ { 62, "<< /Type /DPart /Parent 61 0 R /Start 4 0 R /End 3 0 R >>" } });
    QTest::newRow("start is no page") << objects({ { 64, "<< /Type /DPart /Parent 63 0 R /Start 15 0 R >>" } });
    QTest::newRow("start direct") << objects({ { 64, "<< /Type /DPart /Parent 63 0 R /Start << /Type /Page >> >>" } });
    QTest::newRow("leaf with children") << objects({ { 64, "<< /Type /DPart /Parent 63 0 R /Start 5 0 R /DParts [[65 0 R]] >>" } });
    QTest::newRow("no range, no children") << objects({ { 64, "<< /Type /DPart /Parent 63 0 R >>" } });
    QTest::newRow("empty children") << objects({ { 63, "<< /Type /DPart /Parent 61 0 R /DParts [] >>" } });
    QTest::newRow("empty array of children") << objects({ { 63, "<< /Type /DPart /Parent 61 0 R /DParts [[64 0 R] []] >>" } });
    QTest::newRow("root node direct") << objects({ { 60, "<< /Type /DPartRoot /DPartRootNode << /DParts [[62 0 R 63 0 R]] >> >>" } });
    QTest::newRow("root no dictionary") << objects({ { 60, "[61 0 R]" } });

    QVariantMap deep = objects({ { 63, "<< /Type /DPart /Parent 61 0 R /DParts [[200 0 R]] >>" } });
    for (int number = 200; number < 500; ++number)
    {
        deep.insert(QString::number(number), number + 1 < 500 ? "<< /Type /DPart /DParts [[" + QByteArray::number(number + 1) + " 0 R]] >>" : QByteArray("<< /Type /DPart /Start 5 0 R /End 7 0 R >>"));
    }
    QTest::newRow("deeper than 256") << deep;
}

void DocumentToolPagesTest::removeDocumentPartsDamaged()
{
    QFETCH(QVariantMap, objects);

    PagesTestPdf pdf = PagesTestHelper::createDocumentParts();
    for (auto it = objects.cbegin(); it != objects.cend(); ++it)
    {
        pdf.set(it.key().toInt(), it.value().toByteArray());
    }
    const PDFDocument document = pdf.read();

    // The parts and /DPart of every page, which stays, are removed; nothing points at nothing
    QElapsedTimer timer;
    timer.start();
    PDFDocumentPageOperations::RemovalStatistics statistics;
    const PDFDocument result = PagesTestHelper::remove(document, { 2 }, &statistics);
    QVERIFY(timer.elapsed() < 5000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
    QVERIFY(statistics.documentPartsRemoved);
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(4));
    QVERIFY(!PagesTestHelper::getCatalog(result)->hasKey("DPartRoot"));
    for (size_t i = 0; i < 4; ++i)
    {
        QVERIFY(!PagesTestHelper::getDictionary(result, result.getCatalog()->getPage(i)->getPageReference())->hasKey("DPart"));
    }
    QVERIFY(PagesTestHelper::getDanglingReferences(result).isEmpty());

    // The tool says so
    const PDFDocumentToolResult toolResult = PDFDocumentToolRemovePages().run(PagesTestHelper::context(document), PagesTestHelper::pages(QStringLiteral("2")), nullptr, nullptr);
    QCOMPARE(toolResult.status, PDFDocumentToolResultStatus::Success);
    QString partsFact;
    for (const PDFDocumentToolFact& fact : toolResult.facts)
    {
        partsFact = fact.id == QLatin1String("documentParts") ? fact.value : partsFact;
    }
    QCOMPARE(partsFact, QStringLiteral("Document parts removed"));
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
    // A kid of a name tree listed twice (a name tree, which is its own kid: UnitTestsObjectWalk)
    QTest::newRow("name tree shared kid") << QByteArray("/Names << /Dests 30 0 R >>") << 30 << QByteArray("<< /Kids [31 0 R 31 0 R] >>")
                                          << 31 << QByteArray("<< /Names [(a) [4 0 R /Fit]] >>");
    // An outline item reached by /Next, which is a page: the chain ends there, the page stays
    QTest::newRow("outline to page") << QByteArray("/Outlines 20 0 R") << 20 << QByteArray("<< /First 21 0 R /Last 21 0 R >>")
                                     << 21 << QByteArray("<< /Title (A) /Parent 20 0 R /Next 5 0 R >>");
    // Two items, whose /First leads back to the first of them: each path is a new outline of the
    // same two items, the walk of every path would take 2^256 steps (review RCORE). Each item is
    // read once, both items keep their targets of page 1.
    QTest::newRow("outline items shared by paths") << QByteArray("/Outlines << /First 21 0 R /Last 22 0 R /Count 2 >>")
                                                   << 21 << QByteArray("<< /Title (A) /Next 22 0 R /First 21 0 R /Last 22 0 R /Dest [3 0 R /Fit] >>")
                                                   << 22 << QByteArray("<< /Title (B) /Prev 21 0 R /First 21 0 R /Last 22 0 R /Dest [3 0 R /Fit] >>");
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
    if (QByteArray(QTest::currentDataTag()) == "outline items shared by paths")
    {
        // The items point at page 1, they stay
        QCOMPARE(PagesTestHelper::getOutline(result.getCatalog()->getOutlineRootPtr().data()), QStringLiteral("A B"));
    }
    if (QByteArray(QTest::currentDataTag()) == "lying widget")
    {
        // Page 1 keeps its widget, its field stays
        QCOMPARE(result.getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));
        QCOMPARE(result.getDictionaryFromObject(PagesTestHelper::getCatalog(result)->get("AcroForm"))->get("Fields").getArray()->getCount(), size_t(1));
    }
}

void DocumentToolPagesTest::formatPageNumbers()
{
    // The one text of the page lists (owner D2 / D3 2026-11-10): every run of consecutive pages is
    // a range with an en dash without spaces, also a run of two pages (388:17167 "13–14", 385:9405
    // "1–3, 8, 10–12", 388:21459 "4–6, 20"); the parts are separated by ", "
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 3, 4, 5, 19 }), QStringLiteral("4–6, 20"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 0, 1, 2, 7, 9, 10, 11 }), QStringLiteral("1–3, 8, 10–12"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 12, 13 }), QStringLiteral("13–14"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 8, 9, 14 }), QStringLiteral("9–10, 15"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 0, 1, 2, 4, 5 }), QStringLiteral("1–3, 5–6"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 6 }), QStringLiteral("7"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({}), QString());

    // At most so many parts, then "…" (a run counts once)
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 0, 2, 4, 6 }, 2), QStringLiteral("1, 3, …"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 0, 1, 2, 4, 5 }, 2), QStringLiteral("1–3, 5–6"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 0, 1, 2, 4, 5 }, 1), QStringLiteral("1–3, …"));
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 0, 2 }, 0), QStringLiteral("1, …"));

    // The numbers of a locale (the GUI): both bounds of a range are formatted
    QCOMPARE(PDFDocumentPageOperations::formatPageNumbers({ 0, 1, 1233, 1234 }, 6, [](PDFInteger number) { return QStringLiteral("<%1>").arg(number); }),
             QStringLiteral("<1>–<2>, <1234>–<1235>"));
}

/// Versions of the five pages of createPages, which an assistant has made (owner 2026-11-06:
/// the marks follow the pages, not their numbers): page 2 removed, then the last page moved first
class PagesTestVersions
{
public:
    PagesTestVersions() :
        original(PagesTestPdf::createPages().read()),
        removed(PagesTestHelper::remove(original, { 2 }))
    {
        PDFDocumentBuilder builder(&removed);
        builder.flattenPageTree();
        std::vector<PDFObjectReference> pages = builder.getPages();
        std::rotate(pages.begin(), std::prev(pages.end()), pages.end());
        builder.setPages(pages);
        moved = builder.build();
    }

    /// The page objects of a version (by the pages of the original: "P1 P3 P4 P5")
    QStringList getPages(const PDFDocument& document) const
    {
        QStringList pages;
        for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
        {
            const PDFObjectReference reference = document.getCatalog()->getPage(i)->getPageReference();
            for (size_t j = 0; j < original.getCatalog()->getPageCount(); ++j)
            {
                if (original.getCatalog()->getPage(j)->getPageReference() == reference)
                {
                    pages << QStringLiteral("P%1").arg(j + 1);
                }
            }
        }
        return pages;
    }

    PDFDocument original;   ///< P1 P2 P3 P4 P5
    PDFDocument removed;    ///< P1 P3 P4 P5
    PDFDocument moved;      ///< P5 P1 P3 P4
};

void DocumentToolPagesTest::followPagesMap()
{
    const PagesTestVersions versions;
    QCOMPARE(versions.getPages(versions.removed), QStringList({ "P1", "P3", "P4", "P5" }));
    QCOMPARE(versions.getPages(versions.moved), QStringList({ "P5", "P1", "P3", "P4" }));

    // Each page of the original where its page object is now, -1 for the removed one
    QCOMPARE(PDFDocumentToolPageSelection::mapPageIndices(&versions.original, &versions.removed), std::vector<PDFInteger>({ 0, -1, 1, 2, 3 }));
    const std::vector<PDFInteger> pageMap = PDFDocumentToolPageSelection::mapPageIndices(&versions.original, &versions.moved);
    QCOMPARE(pageMap, std::vector<PDFInteger>({ 1, -1, 2, 3, 0 }));

    const auto map = [&](const QString& range, PDFDocumentToolPageSelection::Filters filters)
    {
        PDFDocumentToolPageSelection selection;
        selection.pages = range;
        selection.filters = filters;
        return selection.mapPages(&versions.original, pageMap);
    };

    // A range names pages: P2 is gone, P3 and P4 are pages 3 and 4, P5 is page 1
    PDFDocumentToolPageSelection mapped = map(QStringLiteral("2-5"), PDFDocumentToolPageSelection::Filters());
    QCOMPARE(mapped.pages, QStringLiteral("1, 3-4"));
    QVERIFY(!mapped.isNone);
    QCOMPARE(mapped.getPageIndices(&versions.moved, nullptr), std::vector<PDFInteger>({ 0, 2, 3 }));

    // The filters decide on the version of the range (even: P2, P4), the result is a list
    mapped = map(QStringLiteral("1-5"), PDFDocumentToolPageSelection::Filter::Even);
    QCOMPARE(mapped.pages, QStringLiteral("4"));
    QCOMPARE(mapped.filters, PDFDocumentToolPageSelection::Filters());

    // A range, whose pages are all gone, selects none - never all pages
    mapped = map(QStringLiteral("2"), PDFDocumentToolPageSelection::Filters());
    QVERIFY(mapped.isNone);
    QVERIFY(mapped.pages.isEmpty());
    QVERIFY(mapped.getPageIndices(&versions.moved, nullptr).empty());

    // All pages is a rule over the version, not a list: it stays, with its filters
    mapped = map(QString(), PDFDocumentToolPageSelection::Filter::Odd);
    QVERIFY(!mapped.isNone);
    QVERIFY(mapped.pages.isEmpty());
    QCOMPARE(mapped.filters, PDFDocumentToolPageSelection::Filters(PDFDocumentToolPageSelection::Filter::Odd));

    // The range text reads back
    QCOMPARE(PDFDocumentToolPageSelection::createRangeText({ 0, 1, 2, 4, 7, 8 }), QStringLiteral("1-3, 5, 8-9"));
}

void DocumentToolPagesTest::followPagesTools()
{
    const PagesTestVersions versions;
    PDFDocumentToolRotatePages rotate;
    PDFDocumentToolRemovePages remove;

    // The plan of P2 right, P4 and P5 half, P1 reset, made for the moved version: P2 is gone,
    // P5 and P4 are pages 1 and 4, P1 page 2 (the groups right, left, half, reset)
    QVariantMap parameters;
    parameters.insert(QStringLiteral("plan"), QStringLiteral("2:right; 4-5:half; 1:reset"));
    QVariantMap mapped = rotate.mapParameters(parameters, &versions.original, &versions.moved);
    QCOMPARE(mapped.value(QStringLiteral("plan")).toString(), QStringLiteral("1,4:half; 2:reset"));
    PDFDocumentToolResult result = rotate.run(PagesTestHelper::context(versions.moved), mapped, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(PagesTestHelper::getRotations(*result.documents.front().document), QStringList({ "180", "0", "0", "180" }));

    // A plan, whose every page is gone: no page turns (without the plan the rotation would
    // turn all pages)
    parameters.insert(QStringLiteral("plan"), QStringLiteral("2:right"));
    mapped = rotate.mapParameters(parameters, &versions.original, &versions.moved);
    QVERIFY(mapped.value(QStringLiteral("plan")).toString().isEmpty());
    result = rotate.run(PagesTestHelper::context(versions.moved), mapped, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.documents.empty());

    // The pages and the rotation: P4, P5 turn right
    parameters = PagesTestHelper::pages(QStringLiteral("4-5"));
    parameters.insert(QStringLiteral("rotation"), QStringLiteral("right"));
    mapped = rotate.mapParameters(parameters, &versions.original, &versions.moved);
    result = rotate.run(PagesTestHelper::context(versions.moved), mapped, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(PagesTestHelper::getRotations(*result.documents.front().document), QStringList({ "90", "0", "0", "90" }));

    // Remove Pages: P2 and P5 marked - P2 is gone, P5 (page 1 now) goes
    mapped = remove.mapParameters(PagesTestHelper::pages(QStringLiteral("2, 5")), &versions.original, &versions.moved);
    result = remove.run(PagesTestHelper::context(versions.moved), mapped, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(versions.getPages(*result.documents.front().document), QStringList({ "P1", "P3", "P4" }));

    // Only the removed page marked: nothing goes
    mapped = remove.mapParameters(PagesTestHelper::pages(QStringLiteral("2")), &versions.original, &versions.moved);
    result = remove.run(PagesTestHelper::context(versions.moved), mapped, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.documents.empty());

    // The same version: nothing changes
    QCOMPARE(remove.mapParameters(PagesTestHelper::pages(QStringLiteral("2")), &versions.original, &versions.original), PagesTestHelper::pages(QStringLiteral("2")));
}

QTEST_MAIN(DocumentToolPagesTest)

#include "tst_documenttoolpagestest.moc"
