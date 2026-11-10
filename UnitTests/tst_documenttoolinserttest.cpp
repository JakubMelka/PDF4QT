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

// The insertion mode of the merger and the tools Insert Pages, Add Image as Page of the core
// (package C of step 3a of phase 2, plan 5.7, 5.8, 8, Q3 a, R8, Q12, Q18): the object numbers,
// /ID, the encryption and the signatures of the target, its page tree with the inherited values
// (ISO 32000-2, Table 31, 7.7.3.4), the page labels (12.4.2), the fields (12.7.4), the structure
// (14.7), the output intents (14.11.5), DocMDP (12.8.2.2, Table 257).

#include "mergetestpdf.h"

#include "pdfcatalog.h"
#include "pdfcertificatemanager.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentsigner.h"
#include "pdfdocumenttool_insert.h"
#include "pdfdocumenttool_pages.h"
#include "pdfform.h"
#include "pdfobjectutils.h"
#include "pdfocrapplyprocessor.h"
#include "pdfoperationcontrol.h"
#include "pdfsignaturehandler.h"

#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QTemporaryDir>

using namespace pdf;
using mergetest::MergeTestPdf;
using mergetest::MergeTestRead;

/// A canceled operation
class InsertTestCanceledControl : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

/// Helpers of the tests of the insertion
class InsertTestHelper
{
public:
    /// The context of the tool: the target and the sources (documents with the ids "s1", "s2", ...)
    static PDFDocumentToolContext context(const PDFDocument& target, const std::vector<PDFDocument>& sources = {}, const std::vector<QByteArray>& images = {})
    {
        PDFDocumentToolContext context = PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(target)), QByteArray(), QStringLiteral("Target.pdf"), true));
        for (size_t i = 0; i < sources.size(); ++i)
        {
            PDFDocumentToolInputDocument source = PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(sources[i])), QByteArray(), QStringLiteral("Source%1.pdf").arg(i + 1), true);
            source.inputId = QStringLiteral("s%1").arg(i + 1);
            context.documents.push_back(std::move(source));
        }
        for (size_t i = 0; i < images.size(); ++i)
        {
            PDFDocumentToolInputImage image;
            image.data = images[i];
            image.fileName = QStringLiteral("Image%1.png").arg(i + 1);
            image.inputId = QStringLiteral("i%1").arg(i + 1);
            context.images.push_back(std::move(image));
        }
        return context;
    }

    static QVariantMap parameters(const QString& position, qint64 page = 1)
    {
        QVariantMap parameters;
        parameters.insert(QStringLiteral("position"), position);
        parameters.insert(QStringLiteral("page"), page);
        return parameters;
    }

    /// The values as a tool gets them (the defaults of the missing ones)
    static QVariantMap normalize(const PDFDocumentTool& tool, const QVariantMap& values)
    {
        QVariantMap normalized;
        const std::vector<PDFDocumentToolParameterError> errors = PDFDocumentToolParameters::normalize(tool.getParameters(), values, &normalized);
        if (!errors.empty())
        {
            qWarning() << "Invalid parameters:" << errors.front().message;
        }
        return normalized;
    }

    /// The only document of a successful result
    static PDFDocument output(const PDFDocumentToolResult& result)
    {
        if (result.documents.size() != 1 || !result.documents.front().document)
        {
            qWarning() << "The result has no document:" << result.message;
            return PDFDocument();
        }
        return *result.documents.front().document;
    }

    static QString fact(const PDFDocumentToolResult& result, const QString& id)
    {
        for (const PDFDocumentToolFact& fact : result.facts)
        {
            if (fact.id == id)
            {
                return fact.value;
            }
        }
        return QString();
    }

    /// Insertion of all pages of the documents by the merger
    static PDFDocumentMerger::Result insert(const PDFDocument& target, PDFInteger position, const std::vector<PDFDocument>& sources, std::vector<PDFInteger> replacedPages = {})
    {
        std::vector<PDFDocumentMerger::Source> mergerSources;
        std::vector<PDFDocumentMerger::Item> items;
        for (size_t i = 0; i < sources.size(); ++i)
        {
            mergerSources.push_back(PDFDocumentMerger::Source::createDocument(PDFDocumentPointer(new PDFDocument(sources[i])), QStringLiteral("Source%1.pdf").arg(i + 1)));
            for (size_t page = 0; page < sources[i].getCatalog()->getPageCount(); ++page)
            {
                PDFDocumentMerger::Item item;
                item.source = i;
                item.pageIndex = PDFInteger(page);
                items.push_back(item);
            }
        }
        PDFDocumentMerger::Settings settings;
        settings.outline = PDFDocumentMerger::OutlineMode::NoOutline;
        return PDFDocumentMerger::insert(target, position, std::move(replacedPages), mergerSources, items, settings, nullptr, nullptr);
    }

    static QByteArray writeIncremental(const QByteArray& original, const PDFDocument& document, QString* error)
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        const PDFOperationResult written = PDFDocumentWriter(nullptr).writeIncrementalUpdate(&buffer, original, &document);
        if (!written && error)
        {
            *error = written.getErrorMessage();
        }
        return written ? buffer.data() : QByteArray();
    }

    /// A PNG image of the size in pixels
    static QByteArray png(int width, int height)
    {
        QImage image(width, height, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return data;
    }

    /// The number of the objects of the document, which are not null
    static size_t objectCount(const PDFDocument& document)
    {
        size_t count = 0;
        for (const PDFObjectStorage::Entry& entry : document.getStorage().getObjects())
        {
            count += entry.object.isNull() ? 0 : 1;
        }
        return count;
    }

    /// A fake certification signature: the field "cert" with its value 61 and /Perms /DocMDP
    static MergeTestPdf createCertified(const QByteArray& references)
    {
        MergeTestPdf pdf = MergeTestPdf::createPages("T", 3, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >> /Perms << /DocMDP 61 0 R >> /DSS << /Certs [] >>", { { 1, "/Annots [60 0 R]" } });
        pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (cert) /V 61 0 R /Rect [0 0 0 0] /P 3 0 R /AP << /N 62 0 R >> >>");
        pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> /Reference [" + references + "] >>");
        pdf.set(62, MergeTestPdf::stream("", "/BBox [0 0 0 0]"));
        return pdf;
    }

    /// A tagged document: the root 20, its Document element 21 with a paragraph 23 of page 1,
    /// /StructParents 0 ... count - 1 of the pages
    static MergeTestPdf createTagged(const QByteArray& prefix, int count)
    {
        std::map<int, QByteArray> pageEntries;
        QByteArray nums;
        for (int page = 1; page <= count; ++page)
        {
            pageEntries[page] = "/StructParents " + QByteArray::number(page - 1);
            nums += QByteArray::number(page - 1) + (page == 1 ? " [23 0 R] " : " [] ");
        }
        MergeTestPdf pdf = MergeTestPdf::createPages(prefix, count, "/StructTreeRoot 20 0 R /MarkInfo << /Marked true >> /Lang (en)", pageEntries);
        pdf.set(20, "<< /Type /StructTreeRoot /K 21 0 R /ParentTree 22 0 R /ParentTreeNextKey " + QByteArray::number(count) + " >>");
        pdf.set(21, "<< /Type /StructElem /S /Document /P 20 0 R /K [23 0 R] >>");
        pdf.set(22, "<< /Nums [" + nums + "] >>");
        pdf.set(23, "<< /Type /StructElem /S /P /P 21 0 R /K 0 /Pg 3 0 R >>");
        return pdf;
    }
};

class DocumentToolInsertTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void insertKeepsObjectNumbersAndInheritance();
    void insertIntoNestedTree();
    void insertIntoDamagedTreeFlattens();
    void insertPositions();
    void incrementalUpdateOfTheResult();
    void encryptedTargetStaysEncrypted();
    void signedTargetSignatureStaysValid();
    void certificationLevels();
    void certifiedTargetOnlyCopy();
    void rotateAndRemoveOfCertifiedDocument();
    void replaceSelectedPages();
    void replaceFailureChangesNothing();
    void pageLabelsOfTarget();
    void formFieldsCollideInSource();
    void structureJoinedAsFragment();
    void structureNotJoined();
    void outputIntentsAndDeclarationsKept();
    void unreachableSourceObjectsLeftOut();
    void orderedSourcePages();
    void blankPagesAsNeighbor();
    void imageSourcesAndAddImage();
    void addImageValidation();
    void placementValidation();
    void sourcePermissions();
    void documentPartsTakeInsertedPages();
    void canceledInsertion();
    void documentInsertedIntoItself();
};

void DocumentToolInsertTest::insertKeepsObjectNumbersAndInheritance()
{
    // R8: /MediaBox and /Resources only in the root /Pages of the target (Table 31: inheritable);
    // after the insertion every page of the target still inherits them and every object of the
    // target keeps its number and generation (an incremental update, batch 1 chapter 12)
    const MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 4);
    const PDFDocument target = targetPdf.read();
    MergeTestPdf sourcePdf = MergeTestPdf::createPages("S", 2);
    sourcePdf.set(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 300 400] /Resources << /ProcSet [/PDF] >> /Rotate 90 >>");
    const PDFDocument source = sourcePdf.read();

    const PDFDocumentMerger::Result result = InsertTestHelper::insert(target, 2, { source });
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QCOMPARE(MergeTestRead::pageContents(result.document), QByteArray("% TPAGE1\n0 0 m 10 10 l S|% TPAGE2\n0 0 m 10 10 l S|% SPAGE1\n0 0 m 10 10 l S|% SPAGE2\n0 0 m 10 10 l S|% TPAGE3\n0 0 m 10 10 l S|% TPAGE4\n0 0 m 10 10 l S|"));
    QCOMPARE(result.statistics.insertedPageCount, 2);
    QVERIFY(!result.statistics.isPageTreeFlattened);

    // Every object of the target has its number and generation; a page of the target is unchanged
    const PDFObjectStorage::PDFObjects& before = target.getStorage().getObjects();
    const PDFObjectStorage::PDFObjects& after = result.document.getStorage().getObjects();
    QVERIFY(after.size() > before.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        QCOMPARE(after[i].generation, before[i].generation);
        QCOMPARE(after[i].object.isNull(), before[i].object.isNull());
    }
    for (int page = 1; page <= 4; ++page)
    {
        const PDFObjectReference reference(page + 2, 0);
        QCOMPARE(result.document.getObjectByReference(reference), target.getObjectByReference(reference));
    }

    // The pages of the target inherit the media box and the resources of the root (one root, 7.7.3.4)
    const PDFCatalog* catalog = result.document.getCatalog();
    for (size_t i : { size_t(0), size_t(1), size_t(4), size_t(5) })
    {
        QCOMPARE(catalog->getPage(i)->getMediaBox(), QRectF(0, 0, 600, 800));
        QCOMPARE(catalog->getPage(i)->getPageRotation(), PageRotation::None);
        QVERIFY(!result.document.getDictionaryFromObject(PDFObject::createReference(catalog->getPage(i)->getPageReference()))->hasKey("MediaBox"));
    }
    // The inserted pages keep their own values (inherited in their source)
    QCOMPARE(catalog->getPage(2)->getMediaBox(), QRectF(0, 0, 300, 400));
    QCOMPARE(catalog->getPage(2)->getPageRotation(), PageRotation::Rotate90);

    // The root keeps its number, /Count is 6, the new pages have it as /Parent
    const PDFDictionary* root = MergeTestRead::dictionary(result.document, PDFObject::createReference(PDFObjectReference(2, 0)));
    QCOMPARE(MergeTestRead::integer(result.document, root->get("Count")), 6);
    QCOMPARE(MergeTestRead::catalog(result.document)->get("Pages").getReference(), PDFObjectReference(2, 0));
    const PDFDictionary* inserted = result.document.getDictionaryFromObject(PDFObject::createReference(catalog->getPage(2)->getPageReference()));
    QCOMPARE(inserted->get("Parent").getReference(), PDFObjectReference(2, 0));
}

void DocumentToolInsertTest::insertIntoNestedTree()
{
    // R8: the pages go into the node of the page at the position; /Count of every node above
    // them grows; an inherited /Rotate and /CropBox of that node do not reach the new pages
    MergeTestPdf targetPdf;
    targetPdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    targetPdf.set(2, "<< /Type /Pages /Kids [10 0 R 11 0 R] /Count 4 /MediaBox [0 0 600 800] /Resources << >> >>");
    targetPdf.set(10, "<< /Type /Pages /Parent 2 0 R /Kids [3 0 R 4 0 R] /Count 2 /Rotate 90 /CropBox [10 10 500 700] >>");
    targetPdf.set(11, "<< /Type /Pages /Parent 2 0 R /Kids [5 0 R 6 0 R] /Count 2 >>");
    for (int page = 1; page <= 4; ++page)
    {
        targetPdf.set(page + 2, "<< /Type /Page /Parent " + QByteArray(page <= 2 ? "10" : "11") + " 0 R /Contents " + QByteArray::number(page + 100) + " 0 R >>");
        targetPdf.set(page + 100, MergeTestPdf::stream("% TPAGE" + QByteArray::number(page)));
    }
    const PDFDocument target = targetPdf.read();
    const PDFDocument source = MergeTestPdf::createPages("S", 1).read();

    // Before page 2: into node 10 at index 1
    const PDFDocumentMerger::Result result = InsertTestHelper::insert(target, 1, { source });
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QVERIFY(!result.statistics.isPageTreeFlattened);
    const PDFDocument& document = result.document;
    QCOMPARE(MergeTestRead::integer(document, MergeTestRead::dictionary(document, PDFObject::createReference(PDFObjectReference(2, 0)))->get("Count")), 5);
    const PDFDictionary* node = MergeTestRead::dictionary(document, PDFObject::createReference(PDFObjectReference(10, 0)));
    QCOMPARE(MergeTestRead::integer(document, node->get("Count")), 3);
    const std::vector<PDFObject> kids = MergeTestRead::items(document, node->get("Kids"));
    QCOMPARE(kids.size(), size_t(3));
    QCOMPARE(kids[0].getReference(), PDFObjectReference(3, 0));
    QCOMPARE(kids[2].getReference(), PDFObjectReference(4, 0));
    QCOMPARE(document.getObjectByReference(PDFObjectReference(11, 0)), target.getObjectByReference(PDFObjectReference(11, 0)));

    const PDFCatalog* catalog = document.getCatalog();
    QCOMPARE(catalog->getPageCount(), size_t(5));
    QCOMPARE(catalog->getPage(1)->getPageReference(), kids[1].getReference());
    QCOMPARE(catalog->getPage(1)->getPageRotation(), PageRotation::None);
    QCOMPARE(catalog->getPage(1)->getCropBox(), QRectF(0, 0, 600, 800));
    // The pages of the target keep the inherited values
    QCOMPARE(catalog->getPage(0)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(catalog->getPage(2)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(catalog->getPage(2)->getCropBox(), QRectF(10, 10, 490, 690));
    QCOMPARE(catalog->getPage(3)->getPageRotation(), PageRotation::None);

    // At the end: after the last page, into node 11
    const PDFDocumentMerger::Result atEnd = InsertTestHelper::insert(target, 4, { source });
    QVERIFY(atEnd.isSuccess);
    const PDFDictionary* lastNode = MergeTestRead::dictionary(atEnd.document, PDFObject::createReference(PDFObjectReference(11, 0)));
    QCOMPARE(MergeTestRead::items(atEnd.document, lastNode->get("Kids")).size(), size_t(3));
    QCOMPARE(MergeTestRead::integer(atEnd.document, lastNode->get("Count")), 3);
    QCOMPARE(MergeTestRead::pageContents(atEnd.document).right(16), QByteArray("0 0 m 10 10 l S|"));
}

void DocumentToolInsertTest::insertIntoDamagedTreeFlattens()
{
    // Hostile tree: wrong /Count of the nodes (7.7.3.2: the number of the leaves below) - the
    // counts of the nodes above the new pages are counted from their leaves
    MergeTestPdf targetPdf;
    targetPdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    targetPdf.set(2, "<< /Type /Pages /Kids [10 0 R] /Count 7 /MediaBox [0 0 600 800] /Resources << >> >>");
    targetPdf.set(10, "<< /Type /Pages /Parent 2 0 R /Kids [3 0 R 4 0 R] /Count 1 >>");
    targetPdf.set(3, "<< /Type /Page /Parent 10 0 R >>");
    targetPdf.set(4, "<< /Type /Page /Parent 10 0 R >>");
    const PDFDocument target = targetPdf.read();
    QCOMPARE(target.getCatalog()->getPageCount(), size_t(2));
    const PDFDocument source = MergeTestPdf::createPages("S", 1).read();

    const PDFDocumentMerger::Result result = InsertTestHelper::insert(target, 2, { source });
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QVERIFY(!result.statistics.isPageTreeFlattened);
    QCOMPARE(result.document.getCatalog()->getPageCount(), size_t(3));
    QCOMPARE(MergeTestRead::integer(result.document, MergeTestRead::dictionary(result.document, PDFObject::createReference(PDFObjectReference(2, 0)))->get("Count")), 3);
    QCOMPARE(MergeTestRead::integer(result.document, MergeTestRead::dictionary(result.document, PDFObject::createReference(PDFObjectReference(10, 0)))->get("Count")), 3);
    for (size_t i = 0; i < 2; ++i)
    {
        QCOMPARE(result.document.getCatalog()->getPage(i)->getMediaBox(), QRectF(0, 0, 600, 800));
    }
}

void DocumentToolInsertTest::insertPositions()
{
    // The tool: before / after the page, at the start, at the end (388:16103 Position)
    const PDFDocument target = MergeTestPdf::createPages("T", 3).read();
    const PDFDocument source = MergeTestPdf::createPages("S", 1).read();
    PDFDocumentToolInsertPages tool;

    struct Case
    {
        QString position;
        qint64 page;
        int sourceIndex;
        QString fact;
    };
    const Case cases[] = {
        { QStringLiteral("before"), 2, 1, QStringLiteral("1 pages before page 2") },
        { QStringLiteral("after"), 2, 2, QStringLiteral("1 pages after page 2") },
        { QStringLiteral("start"), 1, 0, QStringLiteral("1 pages at the beginning") },
        { QStringLiteral("end"), 1, 3, QStringLiteral("1 pages at the end") },
    };
    for (const Case& testCase : cases)
    {
        QVariantMap parameters = InsertTestHelper::parameters(testCase.position, testCase.page);
        const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { source }), parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(testCase.position + QStringLiteral(": ") + result.message));
        const PDFDocument document = InsertTestHelper::output(result);
        QCOMPARE(document.getCatalog()->getPageCount(), size_t(4));
        const QList<QByteArray> contents = MergeTestRead::pageContents(document).split('|');
        QVERIFY2(contents[testCase.sourceIndex].contains("SPAGE1"), qPrintable(testCase.position));
        QCOMPARE(InsertTestHelper::fact(result, QStringLiteral("document")), QStringLiteral("4 pages (was 3)"));
        QVERIFY2(InsertTestHelper::fact(result, QStringLiteral("inserted")).endsWith(testCase.fact.mid(8)), qPrintable(InsertTestHelper::fact(result, QStringLiteral("inserted"))));
        QCOMPARE(result.data.value(QStringLiteral("insertedPages")).toArray().first().toInteger(), qint64(testCase.sourceIndex + 1));
        QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("incremental-save"));
        QCOMPARE(result.documents.front().kind, PDFDocumentToolOutputDocument::Kind::Changed);
    }
}

void DocumentToolInsertTest::incrementalUpdateOfTheResult()
{
    // The result is written as an incremental update of the bytes of the target: the original
    // bytes stay, only the changed and the new objects follow (Q3 a), and the file reads back
    const MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 3);
    const QByteArray original = targetPdf.write();
    const PDFDocument target = targetPdf.read();
    const PDFDocumentMerger::Result result = InsertTestHelper::insert(target, 1, { MergeTestPdf::createPages("S", 2).read() });
    QVERIFY(result.isSuccess);

    QString error;
    const QByteArray written = InsertTestHelper::writeIncremental(original, result.document, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QVERIFY(written.startsWith(original));
    const QByteArray update = written.mid(original.size());
    QVERIFY(!update.contains("TPAGE"));
    QCOMPARE(MergeTestRead::count(update, "SPAGE"), 2);
    const PDFDocument reread = MergeTestPdf::readBytes(written);
    QCOMPARE(reread.getCatalog()->getPageCount(), size_t(5));
    QVERIFY(MergeTestRead::pageContents(reread).startsWith("% TPAGE1\n0 0 m 10 10 l S|% SPAGE1"));
}

void DocumentToolInsertTest::encryptedTargetStaysEncrypted()
{
    // Q3 a: the target keeps its encryption and /ID; the inserted pages are written encrypted by
    // the handler of the target (7.6)
    const PDFDocument plain = MergeTestPdf::createPages("T", 2).read();
    const QByteArray encrypted = MergeTestPdf::encrypt(plain, QString(), QStringLiteral("owner"), 0xFFFFFFFC);
    const PDFDocument target = MergeTestPdf::readBytes(encrypted);
    QVERIFY(target.getStorage().getSecurityHandler()->getMode() != EncryptionMode::None);
    const PDFObject id = MergeTestRead::trailer(target)->get("ID");

    PDFDocumentToolInsertPages tool;
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { MergeTestPdf::createPages("S", 1).read() }), InsertTestHelper::parameters(QStringLiteral("end")), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const PDFDocument document = InsertTestHelper::output(result);
    QVERIFY(document.getStorage().getSecurityHandler()->getMode() != EncryptionMode::None);
    QCOMPARE(MergeTestRead::trailer(document)->get("ID"), id);
    QCOMPARE(MergeTestRead::trailer(document)->get("Encrypt"), MergeTestRead::trailer(target)->get("Encrypt"));

    QString error;
    const QByteArray written = InsertTestHelper::writeIncremental(encrypted, document, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QVERIFY(!written.mid(encrypted.size()).contains("SPAGE1"));
    const PDFDocument reread = MergeTestPdf::readBytes(written);
    QCOMPARE(reread.getCatalog()->getPageCount(), size_t(3));
    QVERIFY(MergeTestRead::pageContents(reread).contains("SPAGE1"));
}

void DocumentToolInsertTest::signedTargetSignatureStaysValid()
{
    // Q3 a, 12.8: a signed (not certified) target - the insertion is saved incrementally and the
    // signature verified by the reader stays valid (its byte range is the unchanged original)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateManager::NewCertificateInfo info;
    info.fileName = directory.filePath(QStringLiteral("insert-test.p12"));
    info.privateKeyPasword = QStringLiteral("test-password");
    info.certCommonName = QStringLiteral("PDF4QT insert test");
    info.rsaKeyLength = 2048;
    PDFCertificateManager().createCertificate(info);
    QFile file(info.fileName);
    QVERIFY(file.open(QIODevice::ReadOnly));
    PDFCertificateEntry certificate;
    certificate.pkcs12 = file.readAll();
    file.close();
    QVERIFY(!certificate.pkcs12.isEmpty());

    const MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 2);
    const QByteArray original = targetPdf.write();
    const PDFDocument unsigned_ = targetPdf.read();
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &unsigned_;
    parameters.originalDocumentData = original;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature) { return PDFSignatureFactory::sign(certificate, info.privateKeyPasword, data, signature); };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary)
    {
        return builder.createSignatureField(QStringLiteral("Signature"), signatureDictionary, unsigned_.getCatalog()->getPage(0)->getPageReference());
    };
    QByteArray signedData;
    QCOMPARE(PDFDocumentSigner::sign(parameters, signedData), PDFDocumentSigner::Result::OK);
    const PDFDocument target = MergeTestPdf::readBytes(signedData);
    QCOMPARE(PDFForm::getSignatureCount(&target), 1);
    QCOMPARE(PDFDocMDPPermissions::getLevel(&target), PDFDocMDPPermissions::Level::NotCertified);

    PDFDocumentToolInsertPages tool;
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { MergeTestPdf::createPages("S", 2).read() }), InsertTestHelper::parameters(QStringLiteral("after"), 1), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("incremental-save"));
    QCOMPARE(result.data.value(QStringLiteral("signatureCount")).toInt(), 1);
    const PDFDocument document = InsertTestHelper::output(result);
    QCOMPARE(PDFForm::getSignatureCount(&document), 1);

    QString error;
    const QByteArray written = InsertTestHelper::writeIncremental(signedData, document, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    const PDFDocument reread = MergeTestPdf::readBytes(written);
    QCOMPARE(reread.getCatalog()->getPageCount(), size_t(4));
    const PDFForm form = PDFForm::parse(&reread, reread.getCatalog()->getFormObject());
    PDFSignatureHandler::Parameters verification;
    verification.useSystemCertificateStore = false;
    const std::vector<PDFSignatureVerificationResult> results = PDFSignatureHandler::verifySignatures(form, written, verification);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
}

void DocumentToolInsertTest::certificationLevels()
{
    // 12.8.2.2, Table 257: /P of the DocMDP reference; default 2; a wrong type is the default;
    // the reference of the method DocMDP is read, not the first one (UR3 below)
    using Level = PDFDocMDPPermissions::Level;
    const PDFDocument plain = MergeTestPdf::createPages("T", 1).read();
    QCOMPARE(PDFDocMDPPermissions::getLevel(&plain), Level::NotCertified);
    QCOMPARE(PDFDocMDPPermissions::getLevel(nullptr), Level::NotCertified);
    QVERIFY(PDFDocMDPPermissions::isPageChangeAllowed(&plain));

    struct Case
    {
        QByteArray references;
        Level level;
    };
    const Case cases[] = {
        { "<< /TransformMethod /DocMDP /TransformParams << /P 1 >> >>", Level::NoChanges },
        { "<< /TransformMethod /DocMDP /TransformParams << /P 3 >> >>", Level::FormFillingAndAnnotations },
        { "<< /TransformMethod /DocMDP /TransformParams << >> >>", Level::FormFilling },
        { "<< /TransformMethod /DocMDP /TransformParams << /P (1) >> >>", Level::FormFilling },
        { "<< /TransformMethod /DocMDP /TransformParams << /P 7 >> >>", Level::FormFilling },
        { "", Level::FormFilling },
        { "<< /TransformMethod /UR3 /TransformParams << /P 1 >> >> << /TransformMethod /DocMDP /TransformParams << /P 3 >> >>", Level::FormFillingAndAnnotations },
    };
    for (const Case& testCase : cases)
    {
        const PDFDocument document = InsertTestHelper::createCertified(testCase.references).read();
        QCOMPARE(PDFDocMDPPermissions::getLevel(&document), testCase.level);
        QVERIFY(!PDFDocMDPPermissions::isPageChangeAllowed(&document));
        // The OCR processor reads the same level (one reading)
        QCOMPARE(int(PDFOCRApplyProcessor::getCertificationPermissions(&document)), int(testCase.level));
    }

    // /DocMDP of a wrong type or a reference to nothing is no certification
    const PDFDocument broken = MergeTestPdf::createPages("T", 1, "/Perms << /DocMDP 99 0 R >>").read();
    QCOMPARE(PDFDocMDPPermissions::getLevel(&broken), Level::NotCertified);
}

void DocumentToolInsertTest::certifiedTargetOnlyCopy()
{
    // Q12, R10: no level allows to insert pages - the result is a copy without the values of the
    // signatures, /Perms and /DSS; the empty field stays for a new signature (Q4 of step 2)
    const PDFDocument target = InsertTestHelper::createCertified("<< /TransformMethod /DocMDP /TransformParams << /P 2 >> >>").read();
    QCOMPARE(PDFForm::getSignatureCount(&target), 1);

    PDFDocumentToolInsertPages tool;
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { MergeTestPdf::createPages("S", 1).read() }), InsertTestHelper::parameters(QStringLiteral("end")), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));
    QVERIFY(result.data.value(QStringLiteral("certificationRemoved")).toBool());
    QCOMPARE(result.data.value(QStringLiteral("certificationSignaturesRemoved")).toInt(), 1);
    QVERIFY(!InsertTestHelper::fact(result, QStringLiteral("certification")).isEmpty());

    const PDFDocument document = InsertTestHelper::output(result);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(4));
    QCOMPARE(PDFForm::getSignatureCount(&document), 0);
    QCOMPARE(PDFDocMDPPermissions::getLevel(&document), PDFDocMDPPermissions::Level::NotCertified);
    const PDFDictionary* catalog = MergeTestRead::catalog(document);
    QVERIFY(!catalog->hasKey("Perms"));
    QVERIFY(!catalog->hasKey("DSS"));
    const PDFDictionary* field = MergeTestRead::dictionary(document, PDFObject::createReference(PDFObjectReference(60, 0)));
    QVERIFY(field && !field->hasKey("V") && !field->hasKey("AP"));
    QCOMPARE(MergeTestRead::integer(document, MergeTestRead::path(document, { "AcroForm" })->get("SigFlags")), 1);

    // A certification, whose signature is in no field (hostile): /Perms goes all the same, else
    // the copy would still say it is certified
    MergeTestPdf orphanPdf = MergeTestPdf::createPages("T", 1, "/Perms << /DocMDP 61 0 R >>");
    orphanPdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> /Reference [<< /TransformMethod /DocMDP >>] >>");
    const PDFDocument orphan = orphanPdf.read();
    QCOMPARE(PDFDocMDPPermissions::getLevel(&orphan), PDFDocMDPPermissions::Level::FormFilling);
    PDFInteger removed = -1;
    const PDFDocument uncertified = PDFDocMDPPermissions::removeCertification(orphan, &removed);
    QCOMPARE(removed, 0);
    QCOMPARE(PDFDocMDPPermissions::getLevel(&uncertified), PDFDocMDPPermissions::Level::NotCertified);
    QVERIFY(!MergeTestRead::catalog(uncertified)->hasKey("Perms"));
}

void DocumentToolInsertTest::rotateAndRemoveOfCertifiedDocument()
{
    // Q12: Rotate Pages and Remove Pages of a certified document - the same copy as Insert
    const PDFDocument target = InsertTestHelper::createCertified("<< /TransformMethod /DocMDP /TransformParams << /P 3 >> >>").read();

    PDFDocumentToolRotatePages rotate;
    QVariantMap rotation;
    rotation.insert(QStringLiteral("pages"), PDFDocumentToolPageSelection().toVariant());
    rotation.insert(QStringLiteral("rotation"), QStringLiteral("right"));
    const PDFDocumentToolResult rotated = rotate.run(InsertTestHelper::context(target), rotation, nullptr, nullptr);
    QVERIFY2(rotated.isSuccess(), qPrintable(rotated.message));
    QCOMPARE(rotated.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));
    QCOMPARE(rotated.documents.size(), size_t(1));
    QCOMPARE(PDFForm::getSignatureCount(rotated.documents.front().document.data()), 0);
    QVERIFY(!MergeTestRead::catalog(*rotated.documents.front().document)->hasKey("Perms"));

    PDFDocumentToolRemovePages remove;
    PDFDocumentToolPageSelection page3;
    page3.pages = QStringLiteral("3");
    QVariantMap removal;
    removal.insert(QStringLiteral("pages"), page3.toVariant());
    const PDFDocumentToolResult removed = remove.run(InsertTestHelper::context(target), removal, nullptr, nullptr);
    QVERIFY2(removed.isSuccess(), qPrintable(removed.message));
    QCOMPARE(removed.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));
    QVERIFY(!InsertTestHelper::fact(removed, QStringLiteral("certification")).isEmpty());
    QVERIFY(!MergeTestRead::catalog(*removed.documents.front().document)->hasKey("Perms"));

    // A document, which is only signed, keeps its signature (incremental save)
    MergeTestPdf signedPdf = MergeTestPdf::createPages("T", 2, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >>", { { 1, "/Annots [60 0 R]" } });
    signedPdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /V 61 0 R /Rect [0 0 0 0] /P 3 0 R >>");
    signedPdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> >>");
    const PDFDocumentToolResult signedResult = rotate.run(InsertTestHelper::context(signedPdf.read()), rotation, nullptr, nullptr);
    QVERIFY(signedResult.isSuccess());
    QCOMPARE(signedResult.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("incremental-save"));
    QCOMPARE(PDFForm::getSignatureCount(signedResult.documents.front().document.data()), 1);
}

void DocumentToolInsertTest::replaceSelectedPages()
{
    // 388:17167: pages 2-3 of 5 replaced by 2 pages of the source - one operation, the new pages
    // in the place of the first replaced page, "48 -> 52" style counts
    const PDFDocument target = MergeTestPdf::createPages("T", 5).read();
    PDFDocumentToolInsertPages tool;
    QVariantMap parameters = InsertTestHelper::parameters(QStringLiteral("replace"));
    PDFDocumentToolPageSelection replaced;
    replaced.pages = QStringLiteral("2-3");
    parameters.insert(QStringLiteral("replacePages"), replaced.toVariant());
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { MergeTestPdf::createPages("S", 2).read() }), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const PDFDocument document = InsertTestHelper::output(result);
    const QList<QByteArray> contents = MergeTestRead::pageContents(document).split('|');
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(5));
    QVERIFY(contents[0].contains("TPAGE1"));
    QVERIFY(contents[1].contains("SPAGE1"));
    QVERIFY(contents[2].contains("SPAGE2"));
    QVERIFY(contents[3].contains("TPAGE4"));
    QVERIFY(contents[4].contains("TPAGE5"));
    QCOMPARE(InsertTestHelper::fact(result, QStringLiteral("document")), QStringLiteral("5 pages (was 5)"));
    // The pages as the frame says them (388:17167 "Replace pages 13–14": a run of two pages is a
    // range, one helper with the GUI)
    QCOMPARE(InsertTestHelper::fact(result, QStringLiteral("replaced")), QStringLiteral("2 pages · 2–3"));
    QCOMPARE(PDFDocumentToolInsertBase::formatPageRuns({ 12, 13 }), QStringLiteral("13–14"));
    QCOMPARE(PDFDocumentToolInsertBase::formatPageRuns({ 1, 4, 5, 6, 8 }), QStringLiteral("2, 5–7, 9"));
    QCOMPARE(PDFDocumentToolInsertBase::formatPageRuns({}), QString());
    QCOMPARE(PDFDocumentToolInsertBase::formatPageRuns({ 0, 1 }, [](PDFInteger number) { return QStringLiteral("<%1>").arg(number); }), QStringLiteral("<1>–<2>"));
    QCOMPARE(result.data.value(QStringLiteral("replacedPages")).toArray(), QJsonArray({ 2, 3 }));
    QCOMPARE(result.data.value(QStringLiteral("insertedPages")).toArray(), QJsonArray({ 2, 3 }));
    // The objects of the pages, which stay, keep their numbers
    QCOMPARE(document.getCatalog()->getPage(3)->getPageReference(), target.getCatalog()->getPage(3)->getPageReference());

    // The merger: the place counts the pages before the removal - before page 5 with pages 2-3
    // replaced is before page 5 of the target (now the third page)
    const PDFDocumentMerger::Result merged = InsertTestHelper::insert(target, 4, { MergeTestPdf::createPages("S", 1).read() }, { 1, 2 });
    QVERIFY2(merged.isSuccess, qPrintable(merged.errorMessage));
    const QList<QByteArray> mergedContents = MergeTestRead::pageContents(merged.document).split('|');
    QCOMPARE(merged.document.getCatalog()->getPageCount(), size_t(4));
    QVERIFY(mergedContents[1].contains("TPAGE4"));
    QVERIFY(mergedContents[2].contains("SPAGE1"));
    QVERIFY(mergedContents[3].contains("TPAGE5"));
    QCOMPARE(merged.statistics.replacedPageCount, 2);
}

void DocumentToolInsertTest::replaceFailureChangesNothing()
{
    // B20: a failed insertion (a page the source has not) never removes the replaced pages; every
    // page replaced is refused (a document keeps a page to anchor the place)
    const PDFDocument target = MergeTestPdf::createPages("T", 3).read();
    PDFDocumentToolInsertPages tool;
    QVariantMap parameters = InsertTestHelper::parameters(QStringLiteral("replace"));
    PDFDocumentToolPageSelection replaced;
    replaced.pages = QStringLiteral("2");
    parameters.insert(QStringLiteral("replacePages"), replaced.toVariant());
    parameters.insert(QStringLiteral("sourcePages"), QStringLiteral("9"));
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { MergeTestPdf::createPages("S", 2).read() }), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.documents.empty());
    QVERIFY2(result.message.contains(QStringLiteral("Source1.pdf")), qPrintable(result.message));

    replaced.pages = QStringLiteral("1-3");
    parameters.insert(QStringLiteral("replacePages"), replaced.toVariant());
    parameters.remove(QStringLiteral("sourcePages"));
    const PDFDocumentToolResult every = tool.run(InsertTestHelper::context(target, { MergeTestPdf::createPages("S", 2).read() }), parameters, nullptr, nullptr);
    QCOMPARE(every.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(every.message.contains(QStringLiteral("Every page")));
    QVERIFY(!InsertTestHelper::insert(target, 0, { MergeTestPdf::createPages("S", 1).read() }, { 0, 1, 2 }).isSuccess);
}

void DocumentToolInsertTest::pageLabelsOfTarget()
{
    // 12.4.2, 5.7: the pages of the target keep their labels (the ranges after the place move); an
    // inserted page of a source without labels has the decimal number of its page
    const PDFDocument target = MergeTestPdf::createPages("T", 4, "/PageLabels << /Nums [0 << /S /r >> 2 << /S /D /St 13 >>] >>").read();
    const PDFDocumentMerger::Result result = InsertTestHelper::insert(target, 3, { MergeTestPdf::createPages("S", 2).read() });
    QVERIFY(result.isSuccess);
    const std::vector<PDFPageLabel>& labels = result.document.getCatalog()->getPageLabels();
    auto labelOf = [&labels](PDFInteger pageIndex)
    {
        auto it = std::upper_bound(labels.cbegin(), labels.cend(), pageIndex, [](PDFInteger index, const PDFPageLabel& label) { return index < label.getPageIndex(); });
        const PDFPageLabel& label = *std::prev(it);
        return std::make_pair(label.getNumberingStyle(), label.getPageStartNumber() + pageIndex - label.getPageIndex());
    };
    QCOMPARE(labelOf(0).first, PDFPageLabel::NumberingStyle::LowercaseRoman);
    QCOMPARE(labelOf(1).second, 2);
    QCOMPARE(labelOf(2), std::make_pair(PDFPageLabel::NumberingStyle::DecimalArabic, PDFInteger(13)));
    QCOMPARE(labelOf(3), std::make_pair(PDFPageLabel::NumberingStyle::DecimalArabic, PDFInteger(1)));
    QCOMPARE(labelOf(4), std::make_pair(PDFPageLabel::NumberingStyle::DecimalArabic, PDFInteger(2)));
    // Page 4 of the target ("14") is page 6 of the result and keeps "14"
    QCOMPARE(labelOf(5), std::make_pair(PDFPageLabel::NumberingStyle::DecimalArabic, PDFInteger(14)));

    // A target without labels gets none, also from a source with labels
    const PDFDocument withoutLabels = MergeTestPdf::createPages("T", 2).read();
    const PDFDocument labeledSource = MergeTestPdf::createPages("S", 1, "/PageLabels << /Nums [0 << /S /A >>] >>").read();
    const PDFDocumentMerger::Result unlabeled = InsertTestHelper::insert(withoutLabels, 1, { labeledSource });
    QVERIFY(unlabeled.isSuccess);
    QVERIFY(!MergeTestRead::catalog(unlabeled.document)->hasKey("PageLabels"));
}

void DocumentToolInsertTest::formFieldsCollideInSource()
{
    // 12.7.4, 5.7: a collision is renamed in the source ("Name_2"), the field of the target keeps
    // its name and object; a signature of the source loses its value (Q4 of step 2), the
    // signature of the target stays; the form of the target keeps its number and /SigFlags
    MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 2, "/AcroForm 50 0 R", { { 1, "/Annots [51 0 R 52 0 R]" } });
    targetPdf.set(50, "<< /Fields [51 0 R 52 0 R] /SigFlags 3 /DA (/Helv 0 Tf 0 g) >>");
    targetPdf.set(51, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (target) /Rect [0 0 10 10] /P 3 0 R >>");
    targetPdf.set(52, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (TargetSig) /V 53 0 R /Rect [0 0 0 0] /P 3 0 R >>");
    targetPdf.set(53, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> >>");
    const PDFDocument target = targetPdf.read();
    MergeTestPdf sourcePdf = MergeTestPdf::createPages("S", 1, "/AcroForm << /Fields [51 0 R 52 0 R] /SigFlags 3 >>", { { 1, "/Annots [51 0 R 52 0 R]" } });
    sourcePdf.set(51, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (source) /Rect [0 0 10 10] /P 3 0 R >>");
    sourcePdf.set(52, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (SourceSig) /V 53 0 R /Rect [0 0 0 0] /P 3 0 R >>");
    sourcePdf.set(53, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> >>");

    const PDFDocumentMerger::Result result = InsertTestHelper::insert(target, 2, { sourcePdf.read() });
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QCOMPARE(result.statistics.fieldsRenamed, 1);
    QCOMPARE(result.statistics.signaturesRemoved, 1);
    const PDFDocument& document = result.document;
    QCOMPARE(MergeTestRead::catalog(document)->get("AcroForm").getReference(), PDFObjectReference(50, 0));
    QCOMPARE(document.getObjectByReference(PDFObjectReference(51, 0)), target.getObjectByReference(PDFObjectReference(51, 0)));
    QCOMPARE(document.getObjectByReference(PDFObjectReference(52, 0)), target.getObjectByReference(PDFObjectReference(52, 0)));
    const PDFDictionary* form = MergeTestRead::path(document, { "AcroForm" });
    QCOMPARE(MergeTestRead::integer(document, form->get("SigFlags")), 3);
    const std::vector<PDFObject> fields = MergeTestRead::items(document, form->get("Fields"));
    QCOMPARE(fields.size(), size_t(4));
    QStringList names;
    for (const PDFObject& field : fields)
    {
        names << MergeTestRead::text(document, MergeTestRead::dictionary(document, field)->get("T"));
    }
    QCOMPARE(names, QStringList({ "Name", "TargetSig", "Name_2", "SourceSig" }));
    QCOMPARE(PDFForm::getSignatureCount(&document), 1);
}

void DocumentToolInsertTest::structureJoinedAsFragment()
{
    // 5.7, Q1 of step 2: a tagged target and a tagged source taken whole - the source is a
    // DocumentFragment below the Document element of the target, its keys of /ParentTree follow
    // the keys of the target (14.7.5.4), the root keeps its number; PDF 2.0 by the catalog
    const PDFDocument target = InsertTestHelper::createTagged("T", 2).read();
    const PDFDocument source = InsertTestHelper::createTagged("S", 2).read();
    const PDFDocumentMerger::Result result = InsertTestHelper::insert(target, 2, { source });
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QVERIFY(result.statistics.structureTreeMerged);
    QVERIFY(!result.statistics.insertedPagesUntagged);

    const PDFDocument& document = result.document;
    QCOMPARE(MergeTestRead::catalog(document)->get("StructTreeRoot").getReference(), PDFObjectReference(20, 0));
    const PDFDictionary* root = MergeTestRead::dictionary(document, PDFObject::createReference(PDFObjectReference(20, 0)));
    QCOMPARE(MergeTestRead::integer(document, root->get("ParentTreeNextKey")), 4);
    const PDFDictionary* documentElement = MergeTestRead::dictionary(document, PDFObject::createReference(PDFObjectReference(21, 0)));
    const std::vector<PDFObject> kids = MergeTestRead::items(document, documentElement->get("K"));
    QCOMPARE(kids.size(), size_t(2));
    QCOMPARE(kids[0].getReference(), PDFObjectReference(23, 0));
    const PDFDictionary* fragment = MergeTestRead::dictionary(document, kids[1]);
    QCOMPARE(MergeTestRead::name(document, fragment->get("S")), QByteArray("DocumentFragment"));
    QCOMPARE(fragment->get("P").getReference(), PDFObjectReference(21, 0));

    // The pages of the source have the keys 2 and 3, the pages of the target 0 and 1
    for (size_t i = 0; i < 4; ++i)
    {
        const PDFDictionary* page = document.getDictionaryFromObject(PDFObject::createReference(document.getCatalog()->getPage(i)->getPageReference()));
        QCOMPARE(MergeTestRead::integer(document, page->get("StructParents")), PDFInteger(i));
    }
    const std::vector<std::pair<PDFInteger, PDFObject>> parentTree = PDFObjectUtils::readNumberTree(&document.getStorage(), root->get("ParentTree"));
    QCOMPARE(parentTree.size(), size_t(4));
    QCOMPARE(parentTree[2].first, 2);
    QCOMPARE(MergeTestRead::name(document, MergeTestRead::catalog(document)->get("Version")), QByteArray("2.0"));
}

void DocumentToolInsertTest::structureNotJoined()
{
    // 5.7: a source not taken whole - the structure of the target stays as it is (its root
    // unchanged), the inserted pages are outside of it and the result says it
    const PDFDocument target = InsertTestHelper::createTagged("T", 2).read();
    const PDFDocument source = InsertTestHelper::createTagged("S", 2).read();
    PDFDocumentToolInsertPages tool;
    QVariantMap parameters = InsertTestHelper::parameters(QStringLiteral("end"));
    parameters.insert(QStringLiteral("sourcePages"), QStringLiteral("2"));
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { source }), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(InsertTestHelper::fact(result, QStringLiteral("tags")), QStringLiteral("Inserted pages are not tagged"));
    QVERIFY(result.data.value(QStringLiteral("insertedPagesUntagged")).toBool());

    const PDFDocument document = InsertTestHelper::output(result);
    for (int object : { 20, 21, 22, 23 })
    {
        QCOMPARE(document.getObjectByReference(PDFObjectReference(object, 0)), target.getObjectByReference(PDFObjectReference(object, 0)));
    }
    const PDFDictionary* inserted = document.getDictionaryFromObject(PDFObject::createReference(document.getCatalog()->getPage(2)->getPageReference()));
    QVERIFY(!inserted->hasKey("StructParents"));

    // An untagged target: nothing of the structure is added
    const PDFDocumentMerger::Result untagged = InsertTestHelper::insert(MergeTestPdf::createPages("T", 1).read(), 1, { source });
    QVERIFY(untagged.isSuccess);
    QVERIFY(!MergeTestRead::catalog(untagged.document)->hasKey("StructTreeRoot"));
    QVERIFY(!untagged.statistics.insertedPagesUntagged);
}

void DocumentToolInsertTest::outputIntentsAndDeclarationsKept()
{
    // R8: the output intents of the target stay (14.11.5); Q18: the declaration of PDF/X stays and
    // the result says that the inserted pages were not checked
    const QByteArray targetIntent = "/OutputIntents [<< /Type /OutputIntent /S /GTS_PDFX /OutputConditionIdentifier (FOGRA39) >>]";
    const PDFDocument target = MergeTestPdf::createPages("T", 2, targetIntent).read();
    const PDFDocument source = MergeTestPdf::createPages("S", 1, "/OutputIntents [<< /Type /OutputIntent /S /GTS_PDFA1 /OutputConditionIdentifier (sRGB) >>]").read();

    PDFDocumentToolInsertPages tool;
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { source }), InsertTestHelper::parameters(QStringLiteral("start")), nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const PDFDocument document = InsertTestHelper::output(result);
    QCOMPARE(MergeTestRead::catalog(document)->get("OutputIntents"), MergeTestRead::catalog(target)->get("OutputIntents"));
    QCOMPARE(InsertTestHelper::fact(result, QStringLiteral("conformance")), QStringLiteral("Inserted pages were not checked for PDF/X"));
    QCOMPARE(result.data.value(QStringLiteral("declarationsKept")).toArray(), QJsonArray({ QStringLiteral("PDF/X") }));

    // Without a declaration there is no such fact
    const PDFDocumentToolResult plain = tool.run(InsertTestHelper::context(source, { target }), InsertTestHelper::parameters(QStringLiteral("start")), nullptr, nullptr);
    QVERIFY(plain.isSuccess());
    QVERIFY(InsertTestHelper::fact(plain, QStringLiteral("conformance")).isEmpty());
}

void DocumentToolInsertTest::unreachableSourceObjectsLeftOut()
{
    // An incremental update adds only what the result reaches: the outline, the info and the
    // catalog of the source are not added, the new objects follow the target without a gap
    MergeTestPdf sourcePdf = MergeTestPdf::createPages("S", 1, "/Outlines 40 0 R");
    sourcePdf.set(40, "<< /Type /Outlines /First 41 0 R /Last 41 0 R /Count 1 >>");
    sourcePdf.set(41, "<< /Title (SOURCEOUTLINE) /Parent 40 0 R /Dest [3 0 R /Fit] >>");
    sourcePdf.set(42, "<< /Title (SOURCEINFO) >>");
    sourcePdf.setTrailerEntries("/Info 42 0 R");
    const PDFDocument target = MergeTestPdf::createPages("T", 2).read();
    const PDFDocumentMerger::Result result = InsertTestHelper::insert(target, 2, { sourcePdf.read() });
    QVERIFY(result.isSuccess);

    const QByteArray written = MergeTestPdf::writeDocument(result.document);
    QVERIFY(!written.contains("SOURCEOUTLINE"));
    QVERIFY(!written.contains("SOURCEINFO"));
    QVERIFY(written.contains("SPAGE1"));
    const PDFObjectStorage::PDFObjects& objects = result.document.getStorage().getObjects();
    for (size_t i = target.getStorage().getObjects().size(); i < objects.size(); ++i)
    {
        QVERIFY2(!objects[i].object.isNull(), qPrintable(QString::number(i)));
    }
    // The new page, its content stream and the information dictionary of the target, which had
    // none (the modification date, 14.3.3) - the trailer refers to its compacted number
    QCOMPARE(objects.size() - target.getStorage().getObjects().size(), size_t(3));
    const PDFObject info = MergeTestRead::trailer(result.document)->get("Info");
    QVERIFY(info.isReference() && size_t(info.getReference().objectNumber) < objects.size());
    QVERIFY(MergeTestRead::dictionary(result.document, info) && MergeTestRead::dictionary(result.document, info)->hasKey("ModDate"));
    QCOMPARE(MergeTestPdf::roundTrip(result.document).getCatalog()->getPageCount(), size_t(3));
}

void DocumentToolInsertTest::orderedSourcePages()
{
    // K23: the source pages in the order of the text, a page twice is a further copy (a clone),
    // odd / even, a descending range
    const PDFDocument target = MergeTestPdf::createPages("T", 1).read();
    const PDFDocument source = MergeTestPdf::createPages("S", 4).read();
    PDFDocumentToolInsertPages tool;
    struct Case
    {
        QString pages;
        QByteArray order;
    };
    const Case cases[] = {
        { QStringLiteral("3, 1-2"), "312" },
        { QStringLiteral("4-2"), "432" },
        { QStringLiteral("even"), "24" },
        { QStringLiteral("1,1"), "11" },
        { QStringLiteral("all"), "1234" },
    };
    for (const Case& testCase : cases)
    {
        QVariantMap parameters = InsertTestHelper::parameters(QStringLiteral("end"));
        parameters.insert(QStringLiteral("sourcePages"), testCase.pages);
        const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { source }), parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(testCase.pages + QStringLiteral(": ") + result.message));
        const QList<QByteArray> contents = MergeTestRead::pageContents(InsertTestHelper::output(result)).split('|');
        QByteArray order;
        for (int i = 1; i <= testCase.order.size(); ++i)
        {
            const qsizetype position = contents[i].indexOf("SPAGE");
            order += position >= 0 ? contents[i].mid(position + 5, 1) : QByteArray("?");
        }
        QCOMPARE(order, testCase.order);
    }

    // Two sources with the pages of each one; "inputs" orders them
    const PDFDocument second = MergeTestPdf::createPages("R", 2).read();
    QVariantMap parameters = InsertTestHelper::parameters(QStringLiteral("end"));
    parameters.insert(QStringLiteral("inputs"), QStringLiteral("s2,s1"));
    parameters.insert(QStringLiteral("sourcePages"), QStringLiteral("2; 1"));
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { source, second }), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const QList<QByteArray> contents = MergeTestRead::pageContents(InsertTestHelper::output(result)).split('|');
    QVERIFY(contents[1].contains("RPAGE2"));
    QVERIFY(contents[2].contains("SPAGE1"));
}

void DocumentToolInsertTest::blankPagesAsNeighbor()
{
    // 388:16811 "As page 12": the media box and the rotation of the neighboring page; a named size
    // in its orientation (A4 = 595.276 x 841.89 points, landscape swapped)
    const PDFDocument target = MergeTestPdf::createPages("T", 2, QByteArray(), { { 2, "/MediaBox [0 0 300 500] /Rotate 90" } }).read();
    PDFDocumentToolInsertPages tool;
    QVariantMap parameters = InsertTestHelper::parameters(QStringLiteral("after"), 2);
    parameters.insert(QStringLiteral("blankPages"), 2);
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target), parameters, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const PDFDocument document = InsertTestHelper::output(result);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(4));
    for (size_t i : { size_t(2), size_t(3) })
    {
        QCOMPARE(document.getCatalog()->getPage(i)->getMediaBox(), QRectF(0, 0, 300, 500));
        QCOMPARE(document.getCatalog()->getPage(i)->getPageRotation(), PageRotation::Rotate90);
    }

    parameters.insert(QStringLiteral("blankSize"), QStringLiteral("a4"));
    parameters.insert(QStringLiteral("blankOrientation"), QStringLiteral("landscape"));
    parameters.insert(QStringLiteral("blankPages"), 1);
    const PDFDocumentToolResult landscape = tool.run(InsertTestHelper::context(target), parameters, nullptr, nullptr);
    QVERIFY(landscape.isSuccess());
    const QRectF mediaBox = InsertTestHelper::output(landscape).getCatalog()->getPage(2)->getMediaBox();
    QVERIFY(qAbs(mediaBox.width() - 841.89) < 0.01 && qAbs(mediaBox.height() - 595.276) < 0.01);
    QCOMPARE(InsertTestHelper::output(landscape).getCatalog()->getPage(2)->getPageRotation(), PageRotation::None);

    // The place of the blank pages among the sources ("inputs" with the id "blank")
    QVariantMap ordered = InsertTestHelper::parameters(QStringLiteral("end"));
    ordered.insert(QStringLiteral("blankPages"), 1);
    ordered.insert(QStringLiteral("inputs"), QStringLiteral("blank,s1"));
    const PDFDocumentToolResult blankFirst = tool.run(InsertTestHelper::context(target, { MergeTestPdf::createPages("S", 1).read() }), ordered, nullptr, nullptr);
    QVERIFY2(blankFirst.isSuccess(), qPrintable(blankFirst.message));
    const QList<QByteArray> contents = MergeTestRead::pageContents(InsertTestHelper::output(blankFirst)).split('|');
    QVERIFY(contents[2].contains("SPAGE1"));

    // Nothing to insert
    const PDFDocumentToolResult nothing = tool.run(InsertTestHelper::context(target), InsertTestHelper::parameters(QStringLiteral("end")), nullptr, nullptr);
    QCOMPARE(nothing.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(nothing.message.contains(QStringLiteral("nothing to insert")));
}

/// Access to the plan of the tools (the layout given to the merger)
class AddImageTestTool : public PDFDocumentToolAddImage
{
public:
    std::optional<InsertionPlan> plan(const PDFDocumentToolContext& context, const QVariantMap& parameters, QString* error) const
    {
        const std::optional<Placement> placement = resolvePlacement(context.getDocument(), parameters, error);
        return placement ? createInsertionPlan(context, parameters, *placement, error) : std::nullopt;
    }
};

class InsertPagesTestTool : public PDFDocumentToolInsertPages
{
public:
    std::optional<InsertionPlan> plan(const PDFDocumentToolContext& context, const QVariantMap& parameters, QString* error) const
    {
        const std::optional<Placement> placement = resolvePlacement(context.getDocument(), parameters, error);
        return placement ? createInsertionPlan(context, parameters, *placement, error) : std::nullopt;
    }
};

void DocumentToolInsertTest::imageSourcesAndAddImage()
{
    // 5.8, K35: an image of Insert has the layout of Add Image - the size of the neighboring page,
    // Fit, 10 mm margins (28.3465 points), the middle; Add Image takes its own values
    const PDFDocument target = MergeTestPdf::createPages("T", 2).read();
    const QByteArray image = InsertTestHelper::png(400, 300);

    InsertPagesTestTool insertTool;
    QVariantMap insertParameters = InsertTestHelper::parameters(QStringLiteral("after"), 1);
    insertParameters = InsertTestHelper::normalize(insertTool, insertParameters);
    QString error;
    const std::optional<PDFDocumentToolInsertBase::InsertionPlan> insertPlan = insertTool.plan(InsertTestHelper::context(target, {}, { image }), insertParameters, &error);
    QVERIFY2(insertPlan.has_value(), qPrintable(error));
    QCOMPARE(insertPlan->items.size(), size_t(1));
    const PDFImagePageLayout& insertLayout = *insertPlan->items.front().imageLayout;
    QCOMPARE(insertLayout.pageSize, QSizeF(600, 800));
    QCOMPARE(insertLayout.orientation, PDFImagePageLayout::Orientation::Auto);
    QCOMPARE(insertLayout.scaling, PDFImagePageLayout::Scaling::Fit);
    QCOMPARE(insertLayout.anchor, Qt::Alignment(Qt::AlignCenter));
    QVERIFY(qAbs(insertLayout.margins.left() - 28.3465) < 0.001 && insertLayout.margins.top() == insertLayout.margins.left());

    AddImageTestTool addTool;
    QVariantMap parameters = InsertTestHelper::parameters(QStringLiteral("before"), 2);
    parameters.insert(QStringLiteral("pageSize"), QStringLiteral("custom"));
    parameters.insert(QStringLiteral("width"), 100.0);
    parameters.insert(QStringLiteral("height"), 200.0);
    parameters.insert(QStringLiteral("margins"), 5.0);
    parameters.insert(QStringLiteral("imageScaling"), QStringLiteral("fill"));
    parameters.insert(QStringLiteral("anchor"), QStringLiteral("top-left"));
    parameters.insert(QStringLiteral("turns"), QStringLiteral("90,270"));
    parameters = InsertTestHelper::normalize(addTool, parameters);
    const std::optional<PDFDocumentToolInsertBase::InsertionPlan> plan = addTool.plan(InsertTestHelper::context(target, {}, { image, image }), parameters, &error);
    QVERIFY2(plan.has_value(), qPrintable(error));
    QCOMPARE(plan->items.size(), size_t(2));
    const PDFImagePageLayout& layout = *plan->items.front().imageLayout;
    QVERIFY(qAbs(layout.pageSize.width() - 283.4646) < 0.001 && qAbs(layout.pageSize.height() - 566.9291) < 0.001);
    QCOMPARE(layout.scaling, PDFImagePageLayout::Scaling::Fill);
    QCOMPARE(layout.anchor, Qt::Alignment(Qt::AlignTop | Qt::AlignLeft));
    QCOMPARE(layout.turn, PageRotation::Rotate90);
    QCOMPARE(plan->items.back().imageLayout->turn, PageRotation::Rotate270);
    QVERIFY(qAbs(layout.margins.right() - 14.1732) < 0.001);

    // Fit to image: no page size (the size by the resolution of the image)
    parameters.insert(QStringLiteral("pageSize"), QStringLiteral("fit-to-image"));
    parameters.insert(QStringLiteral("turns"), QString());
    const std::optional<PDFDocumentToolInsertBase::InsertionPlan> fitted = addTool.plan(InsertTestHelper::context(target, {}, { image }), parameters, &error);
    QVERIFY2(fitted.has_value(), qPrintable(error));
    QVERIFY(fitted->items.front().imageLayout->pageSize.isEmpty());

    // The run: two pages before page 2, the fact of 388:20121
    PDFDocumentToolAddImage tool;
    QVariantMap run = InsertTestHelper::parameters(QStringLiteral("after"), 1);
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, {}, { image, image }), run, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(InsertTestHelper::fact(result, QStringLiteral("added")), QStringLiteral("2 images after page 1"));
    QCOMPARE(InsertTestHelper::fact(result, QStringLiteral("document")), QStringLiteral("4 pages (was 2)"));
    const PDFDocument document = InsertTestHelper::output(result);
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(4));
    QVERIFY(MergeTestRead::pageContents(document).split('|')[3].contains("TPAGE2"));
    const PDFDictionary* resources = document.getDictionaryFromObject(document.getCatalog()->getPage(1)->getResources());
    QVERIFY(resources && resources->hasKey("XObject"));
}

void DocumentToolInsertTest::addImageValidation()
{
    // Images only, margins within half of the shorter side, the turns 0 / 90 / 180 / 270
    const PDFDocument target = MergeTestPdf::createPages("T", 1).read();
    const QByteArray image = InsertTestHelper::png(40, 30);
    PDFDocumentToolAddImage tool;

    const PDFDocumentToolResult withPdf = tool.run(InsertTestHelper::context(target, { target }, { image }), InsertTestHelper::parameters(QStringLiteral("end")), nullptr, nullptr);
    QCOMPARE(withPdf.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(withPdf.message.contains(QStringLiteral("images only")));

    const PDFDocumentToolResult empty = tool.run(InsertTestHelper::context(target), InsertTestHelper::parameters(QStringLiteral("end")), nullptr, nullptr);
    QCOMPARE(empty.status, PDFDocumentToolResultStatus::Failed);

    QVariantMap margins = InsertTestHelper::parameters(QStringLiteral("end"));
    margins.insert(QStringLiteral("pageSize"), QStringLiteral("a4"));
    margins.insert(QStringLiteral("margins"), 105.0);
    const PDFDocumentToolResult tooLarge = tool.run(InsertTestHelper::context(target, {}, { image }), margins, nullptr, nullptr);
    QCOMPARE(tooLarge.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY2(tooLarge.message.contains(QStringLiteral("105 mm")), qPrintable(tooLarge.message));

    QVariantMap turns = InsertTestHelper::parameters(QStringLiteral("end"));
    turns.insert(QStringLiteral("turns"), QStringLiteral("45"));
    QCOMPARE(tool.run(InsertTestHelper::context(target, {}, { image }), turns, nullptr, nullptr).status, PDFDocumentToolResultStatus::Failed);
    QCOMPARE(PDFDocumentToolAddImage::createTurns({ PageRotation::None, PageRotation::Rotate180 }), QStringLiteral("0,180"));
    QCOMPARE(PDFDocumentToolAddImage::createTurns({ PageRotation::None }), QString());

    QCOMPARE(PDFDocumentToolAddImage::getAnchorIds().size(), 9);
    QCOMPARE(*PDFDocumentToolAddImage::getAnchor(QStringLiteral("bottom-right")), Qt::Alignment(Qt::AlignBottom | Qt::AlignRight));
    QVERIFY(!PDFDocumentToolAddImage::getAnchor(QStringLiteral("center")).has_value());

    // An image, which cannot be read, names itself
    const PDFDocumentToolResult broken = tool.run(InsertTestHelper::context(target, {}, { QByteArray("not an image") }), InsertTestHelper::parameters(QStringLiteral("end")), nullptr, nullptr);
    QCOMPARE(broken.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(broken.message.contains(QStringLiteral("Image1.png")));

    // MCP: width and height belong to custom, page to before / after
    QVariantMap given;
    given.insert(QStringLiteral("width"), 10.0);
    QCOMPARE(tool.checkGivenParameters(given).size(), size_t(1));
    given.insert(QStringLiteral("position"), QStringLiteral("after"));
    QCOMPARE(tool.checkGivenParameters(given).size(), size_t(2));
}

void DocumentToolInsertTest::placementValidation()
{
    const PDFDocument target = MergeTestPdf::createPages("T", 4).read();
    PDFDocumentToolInsertPages tool;
    const PDFDocumentToolContext context = InsertTestHelper::context(target, { MergeTestPdf::createPages("S", 1).read() });

    const PDFDocumentToolResult outside = tool.run(context, InsertTestHelper::parameters(QStringLiteral("after"), 60), nullptr, nullptr);
    QCOMPARE(outside.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY2(outside.message.contains(QStringLiteral("Page 60 does not exist – the document has 4 pages.")), qPrintable(outside.message));

    QVariantMap replace = InsertTestHelper::parameters(QStringLiteral("replace"));
    const PDFDocumentToolResult noSelection = tool.run(context, replace, nullptr, nullptr);
    QCOMPARE(noSelection.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(noSelection.message.contains(QStringLiteral("Thumbnails")));

    // MCP: a value, which would be ignored, is an error
    QVariantMap given;
    given.insert(QStringLiteral("position"), QStringLiteral("before"));
    QCOMPARE(tool.checkGivenParameters(given).size(), size_t(1));
    given.insert(QStringLiteral("page"), 2);
    QVERIFY(tool.checkGivenParameters(given).empty());
    given.insert(QStringLiteral("blankSize"), QStringLiteral("a4"));
    QCOMPARE(tool.checkGivenParameters(given).size(), size_t(1));

    // The page follows its page object in another version of the document
    const PDFDocumentMerger::Result twoMore = InsertTestHelper::insert(target, 0, { MergeTestPdf::createPages("S", 2).read() });
    QVariantMap mapped = tool.mapParameters(InsertTestHelper::parameters(QStringLiteral("after"), 3), &target, &twoMore.document);
    QCOMPARE(mapped.value(QStringLiteral("page")).toLongLong(), 5);
}

void DocumentToolInsertTest::sourcePermissions()
{
    // Q4, R4: a source, which restricts printing (bit 3), loses it in the target - its owner
    // password is needed (rule Q2 of step 2); a target without Assemble (bit 11) is refused
    const PDFDocument plain = MergeTestPdf::createPages("S", 1).read();
    const uint32_t withoutPrint = 0xFFFFFFFC & ~uint32_t(PDFSecurityHandler::Permission::PrintLowResolution);
    const PDFDocument restricted = MergeTestPdf::readBytes(MergeTestPdf::encrypt(plain, QString(), QStringLiteral("owner"), withoutPrint));
    const PDFDocument target = MergeTestPdf::createPages("T", 1).read();

    PDFDocumentToolInsertPages tool;
    const QVariantMap atEnd = InsertTestHelper::normalize(tool, InsertTestHelper::parameters(QStringLiteral("end")));
    const std::vector<PDFDocumentToolParameterError> errors = tool.validate(InsertTestHelper::context(target, { restricted }), atEnd);
    QCOMPARE(errors.size(), size_t(1));
    QVERIFY2(errors.front().message.contains(QStringLiteral("Source1.pdf")), qPrintable(errors.front().message));

    PDFDocumentToolContext verified = InsertTestHelper::context(target, { restricted });
    verified.documents[1].isOwnerPasswordVerified = true;
    QVERIFY(tool.validate(verified, atEnd).empty());

    const uint32_t withoutAssemble = 0xFFFFFFFC & ~uint32_t(PDFSecurityHandler::Permission::Assemble);
    const PDFDocument lockedTarget = MergeTestPdf::readBytes(MergeTestPdf::encrypt(target, QString(), QStringLiteral("owner"), withoutAssemble));
    const PDFDocumentToolResult refused = tool.run(InsertTestHelper::context(lockedTarget, { plain }), InsertTestHelper::parameters(QStringLiteral("end")), nullptr, nullptr);
    QCOMPARE(refused.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(refused.message.contains(QStringLiteral("permissions")));
}

void DocumentToolInsertTest::documentPartsTakeInsertedPages()
{
    // 14.12: the pages of a part are a range from /Start to /End - the pages inserted after the last
    // page of a part join it (its /End moves), the pages inside of a part get its /DPart
    MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 4, "/DPartRoot 70 0 R", { { 1, "/DPart 71 0 R" }, { 2, "/DPart 71 0 R" }, { 3, "/DPart 72 0 R" }, { 4, "/DPart 72 0 R" } });
    targetPdf.set(70, "<< /Type /DPartRoot /DPartRootNode 73 0 R >>");
    targetPdf.set(73, "<< /Type /DPart /DParts [[71 0 R 72 0 R]] >>");
    targetPdf.set(71, "<< /Type /DPart /Parent 73 0 R /Start 3 0 R /End 4 0 R >>");
    targetPdf.set(72, "<< /Type /DPart /Parent 73 0 R /Start 5 0 R /End 6 0 R >>");
    const PDFDocument target = targetPdf.read();

    const PDFDocumentMerger::Result atBorder = InsertTestHelper::insert(target, 2, { MergeTestPdf::createPages("S", 1).read() });
    QVERIFY(atBorder.isSuccess);
    const PDFObjectReference inserted = atBorder.document.getCatalog()->getPage(2)->getPageReference();
    const PDFDictionary* page = atBorder.document.getDictionaryFromObject(PDFObject::createReference(inserted));
    QCOMPARE(page->get("DPart").getReference(), PDFObjectReference(71, 0));
    const PDFDictionary* part = MergeTestRead::dictionary(atBorder.document, PDFObject::createReference(PDFObjectReference(71, 0)));
    QCOMPARE(part->get("End").getReference(), inserted);

    const PDFDocumentMerger::Result inside = InsertTestHelper::insert(target, 3, { MergeTestPdf::createPages("S", 1).read() });
    QVERIFY(inside.isSuccess);
    const PDFDictionary* insidePage = inside.document.getDictionaryFromObject(PDFObject::createReference(inside.document.getCatalog()->getPage(3)->getPageReference()));
    QCOMPARE(insidePage->get("DPart").getReference(), PDFObjectReference(72, 0));
    QCOMPARE(inside.document.getObjectByReference(PDFObjectReference(72, 0)), target.getObjectByReference(PDFObjectReference(72, 0)));
}

void DocumentToolInsertTest::canceledInsertion()
{
    // A canceled run is Stopped, nothing of it is used
    const PDFDocument target = MergeTestPdf::createPages("T", 1).read();
    InsertTestCanceledControl control;
    PDFDocumentToolInsertPages tool;
    const PDFDocumentToolResult result = tool.run(InsertTestHelper::context(target, { MergeTestPdf::createPages("S", 3).read() }), InsertTestHelper::parameters(QStringLiteral("end")), &control, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Stopped);
    QVERIFY(result.documents.empty());
}

void DocumentToolInsertTest::documentInsertedIntoItself()
{
    // Hostile input of the review: the document of the tab inserted into itself (the same
    // objects are the target and the source). The target keeps its field, its named destination
    // and its links; the inserted copy gets its own field ("Name_2", 12.7.4.1: the fully qualified
    // names are unique), its own name of the destination (12.3.2.3: the keys of the name tree are
    // unique) and its links lead to the inserted copies of the pages, never to the target pages
    MergeTestPdf pdf = MergeTestPdf::createPages("T", 2, "/AcroForm << /Fields [51 0 R] >> /Names << /Dests 60 0 R >>", { { 1, "/Annots [51 0 R 52 0 R 53 0 R]" } });
    pdf.set(51, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    pdf.set(52, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest (chap) >>");
    pdf.set(53, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [4 0 R /Fit] >>");
    pdf.set(60, "<< /Names [(chap) [4 0 R /XYZ 0 800 0]] >>");
    const PDFDocument document = pdf.read();

    const PDFDocumentMerger::Result result = InsertTestHelper::insert(document, 2, { document });
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFDocument& output = result.document;
    const PDFCatalog* catalog = output.getCatalog();
    QCOMPARE(catalog->getPageCount(), size_t(4));
    QCOMPARE(MergeTestRead::pageContents(output), QByteArray("% TPAGE1\n0 0 m 10 10 l S|% TPAGE2\n0 0 m 10 10 l S|% TPAGE1\n0 0 m 10 10 l S|% TPAGE2\n0 0 m 10 10 l S|"));

    // The objects of the target stay; the copies are new objects
    for (const int number : { 3, 4, 51, 52, 53 })
    {
        QCOMPARE(output.getObjectByReference(PDFObjectReference(number, 0)), document.getObjectByReference(PDFObjectReference(number, 0)));
    }
    QCOMPARE(catalog->getPage(0)->getPageReference(), PDFObjectReference(3, 0));
    QCOMPARE(catalog->getPage(1)->getPageReference(), PDFObjectReference(4, 0));
    QVERIFY(catalog->getPage(2)->getPageReference() != PDFObjectReference(3, 0));
    QVERIFY(catalog->getPage(3)->getPageReference() != PDFObjectReference(4, 0));

    // The fields: the one of the target and its renamed copy
    QStringList names;
    for (const PDFObject& field : MergeTestRead::items(output, MergeTestRead::path(output, { "AcroForm" })->get("Fields")))
    {
        names << MergeTestRead::text(output, MergeTestRead::dictionary(output, field)->get("T"));
    }
    QCOMPARE(names, QStringList({ "Name", "Name_2" }));

    // The links of the target page lead to the target page 2 (by its name and explicitly)
    const PDFDestination* targetDestination = catalog->getNamedDestination("chap");
    QVERIFY(targetDestination);
    QCOMPARE(targetDestination->getPageReference(), PDFObjectReference(4, 0));

    // The links of the inserted copy of page 1 lead to the inserted copy of page 2
    const std::vector<PDFObjectReference>& annotations = catalog->getPage(2)->getAnnotations();
    QCOMPARE(annotations.size(), size_t(3));
    const PDFObjectReference copyOfPage2 = catalog->getPage(3)->getPageReference();
    const PDFObject& namedLink = MergeTestRead::dictionary(output, PDFObject::createReference(annotations[1]))->get("Dest");
    QVERIFY(namedLink.isString());
    QVERIFY(namedLink.getString() != QByteArray("chap"));
    const PDFDestination* copyDestination = catalog->getNamedDestination(namedLink.getString());
    QVERIFY(copyDestination);
    QCOMPARE(copyDestination->getPageReference(), copyOfPage2);
    const std::vector<PDFObject> explicitLink = MergeTestRead::items(output, MergeTestRead::dictionary(output, PDFObject::createReference(annotations[2]))->get("Dest"));
    QVERIFY(!explicitLink.empty() && explicitLink.front().isReference());
    QCOMPARE(explicitLink.front().getReference(), copyOfPage2);
    QCOMPARE(MergeTestPdf::roundTrip(output).getCatalog()->getPageCount(), size_t(4));
}

QTEST_GUILESS_MAIN(DocumentToolInsertTest)

#include "tst_documenttoolinserttest.moc"
