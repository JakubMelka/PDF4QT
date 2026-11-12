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

// The arrangement of a document, which stays itself (step 3b of phase 2, package M2; plan 4.5, 5,
// 8, RB1, RB2, RB4, RB5, RB13, QB3): PDFDocumentMerger::arrange, Settings::parts of the merge and
// the locks of the signed fields of PDFDocMDPPermissions. The expectations are of ISO 32000-2: the
// page tree and its inherited values (7.7.3.2, 7.7.3.4, Table 31), the incremental update (7.5.6),
// the encryption (7.6), the page labels (12.4.2), the outline (12.3.3), the fields and signatures
// (12.7.4, 12.7.5.5, 12.8), the structure (14.7.5.4), the document parts (14.12).

#include "mergetestpdf.h"

#include "pdfcatalog.h"
#include "pdfcertificatemanager.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentsigner.h"
#include "pdfdocumenttool_pages.h"
#include "pdfform.h"
#include "pdfoperationcontrol.h"
#include "pdfsignaturehandler.h"

#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <QTemporaryDir>

using namespace pdf;
using mergetest::MergeTestPdf;
using mergetest::MergeTestRead;
using Merger = PDFDocumentMerger;

/// A canceled operation
class ArrangeTestCanceledControl : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return true; }
};

/// Helpers of the tests of the arrangement
class ArrangeTestHelper
{
public:
    /// The page objects of the document in its order
    static std::vector<PDFObjectReference> pages(const PDFDocument& document)
    {
        std::vector<PDFObjectReference> result;
        for (size_t i = 0; i < document.getCatalog()->getPageCount(); ++i)
        {
            result.push_back(document.getCatalog()->getPage(i)->getPageReference());
        }
        return result;
    }

    /// The items of the pages of the target numbered from 1; a negative number is a clone of the page
    static std::vector<Merger::Item> items(std::initializer_list<int> pageNumbers)
    {
        std::vector<Merger::Item> result;
        for (const int number : pageNumbers)
        {
            Merger::Item item;
            item.source = 0;
            item.pageIndex = PDFInteger(qAbs(number) - 1);
            item.isClone = number < 0;
            result.push_back(item);
        }
        return result;
    }

    /// The settings of a plan made for the document as it is (RB2)
    static Merger::Settings settings(const PDFDocument& target)
    {
        Merger::Settings settings;
        settings.outline = Merger::OutlineMode::NoOutline;
        settings.targetPages = pages(target);
        return settings;
    }

    /// The arrangement of the target (the source 0) with the other sources after it
    static Merger::Result arrange(const PDFDocument& target, const std::vector<Merger::Item>& items, const std::vector<Merger::Source>& others = {}, bool isOwnerPasswordVerified = false)
    {
        std::vector<Merger::Source> sources;
        sources.push_back(Merger::Source::createDocument(PDFDocumentPointer(new PDFDocument(target)), QStringLiteral("Target.pdf")));
        sources.front().isOwnerPasswordVerified = isOwnerPasswordVerified;
        sources.insert(sources.end(), others.cbegin(), others.cend());
        return Merger::arrange(target, sources, items, settings(target), nullptr, nullptr);
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

    /// The markers of the contents of the pages ("TPAGE2"), one per page with a content
    static QStringList contents(const PDFDocument& document)
    {
        QStringList result;
        for (const QByteArray& content : MergeTestRead::pageContents(document).split('|'))
        {
            if (!content.isEmpty())
            {
                result << QString::fromLatin1(content.mid(2, content.indexOf('\n') - 2));
            }
        }
        return result;
    }

    static const PDFDictionary* pageDictionary(const PDFDocument& document, size_t pageIndex)
    {
        return document.getDictionaryFromObject(PDFObject::createReference(document.getCatalog()->getPage(pageIndex)->getPageReference()));
    }

    /// A widget of a signature field (its /FT or of a parent, Table 226)
    static bool isSignatureWidget(const PDFDocument& document, const PDFObject& annotation)
    {
        const PDFDictionary* dictionary = document.getDictionaryFromObject(annotation);
        if (!dictionary || MergeTestRead::name(document, dictionary->get("Subtype")) != "Widget")
        {
            return false;
        }
        PDFObject current = annotation;
        for (int depth = 0; depth < 32; ++depth)
        {
            const PDFDictionary* node = document.getDictionaryFromObject(current);
            if (!node)
            {
                return false;
            }
            if (node->hasKey("FT"))
            {
                return MergeTestRead::name(document, node->get("FT")) == "Sig";
            }
            current = node->get("Parent");
        }
        return false;
    }

    /// The page tree of 7.7.3.4 with inherited values: the root 2 (/MediaBox 600 x 800, /Resources
    /// with the font /F1), the node 10 (/Rotate 90, /CropBox [10 10 500 700]) with the pages 1, 2
    /// (objects 3, 4), the node 11 (/MediaBox 300 x 400) with the pages 3, 4 (objects 5, 6)
    static MergeTestPdf createNestedTree()
    {
        MergeTestPdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
        pdf.set(2, "<< /Type /Pages /Kids [10 0 R 11 0 R] /Count 4 /MediaBox [0 0 600 800] /Resources << /Font << /F1 50 0 R >> >> >>");
        pdf.set(10, "<< /Type /Pages /Parent 2 0 R /Kids [3 0 R 4 0 R] /Count 2 /Rotate 90 /CropBox [10 10 500 700] >>");
        pdf.set(11, "<< /Type /Pages /Parent 2 0 R /Kids [5 0 R 6 0 R] /Count 2 /MediaBox [0 0 300 400] >>");
        for (int page = 1; page <= 4; ++page)
        {
            pdf.set(page + 2, "<< /Type /Page /Parent " + QByteArray(page <= 2 ? "10" : "11") + " 0 R /Contents " + QByteArray::number(page + 100) + " 0 R >>");
            pdf.set(page + 100, MergeTestPdf::stream("% TPAGE" + QByteArray::number(page) + "\n0 0 m 10 10 l S"));
        }
        pdf.set(50, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        return pdf;
    }

    /// The file of the document encrypted by the algorithm with an empty user password
    static QByteArray encrypt(const PDFDocument& document, PDFSecurityHandlerFactory::Algorithm algorithm, uint32_t permissions)
    {
        PDFDocumentBuilder builder(&document);
        PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = algorithm;
        settings.ownerPassword = QStringLiteral("owner");
        settings.permissions = permissions;
        builder.setSecurityHandler(PDFSecurityHandlerFactory::createSecurityHandler(settings));
        return MergeTestPdf::writeDocument(builder.build());
    }

    /// Every bit of Table 22
    static uint32_t allPermissions()
    {
        using Permission = PDFSecurityHandler::Permission;
        uint32_t permissions = 0;
        for (const Permission permission : { Permission::PrintLowResolution, Permission::Modify, Permission::CopyContent, Permission::ModifyInteractiveItems,
                                             Permission::ModifyFormFields, Permission::Accessibility, Permission::Assemble, Permission::PrintHighResolution })
        {
            permissions |= uint32_t(permission);
        }
        return permissions;
    }

    /// The context of a tool of the pages over the document
    static PDFDocumentToolContext context(const PDFDocument& document)
    {
        return PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(document)), QByteArray(), QStringLiteral("Target.pdf"), true));
    }

    /// A document of 2 pages with the signature field 60 (a merged field and widget of page 1)
    /// with the extra entries \p fieldEntries and the catalog entries \p catalogEntries
    static PDFDocument createSignedField(const QByteArray& fieldEntries, const QByteArray& catalogEntries = QByteArray())
    {
        MergeTestPdf pdf = MergeTestPdf::createPages("T", 2, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >> " + catalogEntries, { { 1, "/Annots [60 0 R]" } });
        pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /Rect [0 0 0 0] /P 3 0 R " + fieldEntries + " >>");
        pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> >>");
        pdf.set(62, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> /Reference [<< /TransformMethod /DocMDP /TransformParams << /P 3 >> >>] >>");
        pdf.set(63, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> /Reference [<< /TransformMethod /DocMDP /TransformParams << /P 1 >> >>] >>");
        return pdf.read();
    }
};

class DocumentMergerArrangeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reverseKeepsObjectNumbers();
    void inheritedValuesAfterReorder();
    void insertionKeepsPageTree();
    void cloneBeforeOriginalKeepsIdentity();
    void signedPageCloneKeepsSignature();
    void signatureWidgetsNotCloned();
    void lockedFieldWidgetsNotCloned_data();
    void lockedFieldWidgetsNotCloned();
    void planOfAnotherVersionRefused();
    void encryptionKept_data();
    void encryptionKept();
    void permissionsOfTarget();
    void cropFromVisibleArea();
    void pageLabelsStayWithPages();
    void documentParts();
    void unusedPagesRemoved();
    void invalidPlans();
    void damagedPageTree();
    void otherSourcesJoined();
    void canceledArrangement();
    void mergeParts();
    void mergePartsOfOneDocument();
    void fieldLockLevels();
    void fieldLockRefusesPageChanges();
};

void DocumentMergerArrangeTest::reverseKeepsObjectNumbers()
{
    // Plan 4.5, table 5: the pages reversed - every object keeps its number and generation, the
    // page objects are the same objects in the new order, /ID stays (14.4); the contents are not
    // changed, so an incremental update (7.5.6) does not write them
    MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 4);
    targetPdf.setTrailerEntries("/ID [<0102> <0304>]");
    const QByteArray original = targetPdf.write();
    const PDFDocument target = targetPdf.read();

    const Merger::Result result = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 4, 3, 2, 1 }));
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFDocument& document = result.document;
    QCOMPARE(ArrangeTestHelper::contents(document), QStringList({ "TPAGE4", "TPAGE3", "TPAGE2", "TPAGE1" }));
    const std::vector<PDFObjectReference> pages = ArrangeTestHelper::pages(document);
    const std::vector<PDFObjectReference> expectedPages = { PDFObjectReference(6, 0), PDFObjectReference(5, 0), PDFObjectReference(4, 0), PDFObjectReference(3, 0) };
    QVERIFY(pages == expectedPages);
    QCOMPARE(MergeTestRead::trailer(document)->get("ID"), MergeTestRead::trailer(target)->get("ID"));
    QCOMPARE(result.statistics.insertedPageCount, 0);
    QCOMPARE(result.statistics.replacedPageCount, 0);
    QCOMPARE(result.statistics.pageCount, 4);

    const PDFObjectStorage::PDFObjects& before = target.getStorage().getObjects();
    const PDFObjectStorage::PDFObjects& after = document.getStorage().getObjects();
    QVERIFY(after.size() >= before.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        QCOMPARE(after[i].generation, before[i].generation);
        QCOMPARE(after[i].object.isNull(), before[i].object.isNull());
    }
    for (int content = 101; content <= 104; ++content)
    {
        QCOMPARE(document.getObjectByReference(PDFObjectReference(content, 0)), target.getObjectByReference(PDFObjectReference(content, 0)));
    }

    QString error;
    const QByteArray written = ArrangeTestHelper::writeIncremental(original, document, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QVERIFY(written.startsWith(original));
    QVERIFY(!written.mid(original.size()).contains("TPAGE"));
    const PDFDocument reread = MergeTestPdf::readBytes(written);
    QCOMPARE(ArrangeTestHelper::contents(reread), QStringList({ "TPAGE4", "TPAGE3", "TPAGE2", "TPAGE1" }));
}

void DocumentMergerArrangeTest::inheritedValuesAfterReorder()
{
    // 7.7.3.4, Table 31: /MediaBox, /CropBox, /Resources and /Rotate only in the nodes of the
    // tree - after another order each page has the same values as before; the nodes, which the
    // flat tree does not reach any more, are null (as removePages leaves them)
    const PDFDocument target = ArrangeTestHelper::createNestedTree().read();
    const Merger::Result result = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 4, 2, 3, 1 }));
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QVERIFY(result.statistics.isPageTreeFlattened);
    const PDFDocument& document = result.document;
    const PDFCatalog* catalog = document.getCatalog();
    QCOMPARE(catalog->getPageCount(), size_t(4));
    QCOMPARE(ArrangeTestHelper::contents(document), QStringList({ "TPAGE4", "TPAGE2", "TPAGE3", "TPAGE1" }));

    struct Expected
    {
        PDFObjectReference page;
        QRectF mediaBox;
        QRectF cropBox;
        PageRotation rotation;
    };
    const Expected expected[] = {
        { PDFObjectReference(6, 0), QRectF(0, 0, 300, 400), QRectF(0, 0, 300, 400), PageRotation::None },
        { PDFObjectReference(4, 0), QRectF(0, 0, 600, 800), QRectF(10, 10, 490, 690), PageRotation::Rotate90 },
        { PDFObjectReference(5, 0), QRectF(0, 0, 300, 400), QRectF(0, 0, 300, 400), PageRotation::None },
        { PDFObjectReference(3, 0), QRectF(0, 0, 600, 800), QRectF(10, 10, 490, 690), PageRotation::Rotate90 },
    };
    for (size_t i = 0; i < 4; ++i)
    {
        const PDFPage* page = catalog->getPage(i);
        QCOMPARE(page->getPageReference(), expected[i].page);
        QCOMPARE(page->getMediaBox(), expected[i].mediaBox);
        QCOMPARE(page->getCropBox(), expected[i].cropBox);
        QCOMPARE(page->getPageRotation(), expected[i].rotation);
        const PDFDictionary* resources = document.getDictionaryFromObject(page->getResources());
        QVERIFY(resources && MergeTestRead::dictionary(document, resources->get("Font")) && MergeTestRead::dictionary(document, resources->get("Font"))->hasKey("F1"));
        QCOMPARE(MergeTestRead::dictionary(document, PDFObject::createReference(page->getPageReference()))->get("Parent").getReference(), PDFObjectReference(2, 0));
    }

    // The root keeps its number; the nodes 10 and 11 are reached by nothing
    QCOMPARE(MergeTestRead::catalog(document)->get("Pages").getReference(), PDFObjectReference(2, 0));
    QVERIFY(document.getObjectByReference(PDFObjectReference(10, 0)).isNull());
    QVERIFY(document.getObjectByReference(PDFObjectReference(11, 0)).isNull());
    QVERIFY(!document.getObjectByReference(PDFObjectReference(50, 0)).isNull());
}

void DocumentMergerArrangeTest::insertionKeepsPageTree()
{
    // R8, table 5: the order of the pages of the target kept, clones and a blank page added - the
    // tree stays (the new pages go into the node of the page after them, /Count of the nodes grows)
    // and the pages of the target are not changed at all. A clone keeps the values of its original
    // (Table 31), although it is in another node: the clone of page 3 in node 10 is not turned and
    // has no crop box of node 10, the clone of page 1 in node 11 keeps /Rotate 90, the crop box and
    // the media box 600 x 800 of the nodes of page 1. Page 1 turned left in the node of /Rotate 90
    // is not turned (the turn is composed with the inherited rotation, Table 31: 90 + 270 = 0)
    const MergeTestPdf targetPdf = ArrangeTestHelper::createNestedTree();
    const QByteArray original = targetPdf.write();
    const PDFDocument target = targetPdf.read();
    std::vector<Merger::Item> items = ArrangeTestHelper::items({ -3, 1, 2, -1, 3, 4 });
    items[1].turn = PageRotation::Rotate270;
    Merger::Item blank;
    blank.source = 1;
    blank.pageSize = QSizeF(200, 200);
    items.push_back(blank);

    const Merger::Result result = ArrangeTestHelper::arrange(target, items, { Merger::Source::createBlank() });
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QVERIFY(!result.statistics.isPageTreeFlattened);
    QCOMPARE(result.statistics.insertedPageCount, 3);
    const PDFDocument& document = result.document;
    const PDFCatalog* catalog = document.getCatalog();
    QCOMPARE(catalog->getPageCount(), size_t(7));
    QCOMPARE(ArrangeTestHelper::contents(document), QStringList({ "TPAGE3", "TPAGE1", "TPAGE2", "TPAGE1", "TPAGE3", "TPAGE4" }));

    for (int object : { 4, 5, 6 })
    {
        const PDFObjectReference reference(object, 0);
        QCOMPARE(document.getObjectByReference(reference), target.getObjectByReference(reference));
    }
    QCOMPARE(catalog->getPage(1)->getPageReference(), PDFObjectReference(3, 0));
    QCOMPARE(catalog->getPage(1)->getPageRotation(), PageRotation::None);
    QCOMPARE(catalog->getPage(1)->getCropBox(), QRectF(10, 10, 490, 690));
    const PDFDictionary* root = MergeTestRead::dictionary(document, PDFObject::createReference(PDFObjectReference(2, 0)));
    QCOMPARE(MergeTestRead::integer(document, root->get("Count")), 7);
    QCOMPARE(MergeTestRead::items(document, root->get("Kids")).size(), size_t(2));
    const PDFDictionary* node10 = MergeTestRead::dictionary(document, PDFObject::createReference(PDFObjectReference(10, 0)));
    const PDFDictionary* node11 = MergeTestRead::dictionary(document, PDFObject::createReference(PDFObjectReference(11, 0)));
    QCOMPARE(MergeTestRead::integer(document, node10->get("Count")), 3);
    QCOMPARE(MergeTestRead::integer(document, node11->get("Count")), 4);
    const std::vector<PDFObject> kids10 = MergeTestRead::items(document, node10->get("Kids"));
    const std::vector<PDFObject> kids11 = MergeTestRead::items(document, node11->get("Kids"));
    QCOMPARE(kids10.size(), size_t(3));
    QCOMPARE(kids11.size(), size_t(4));
    QCOMPARE(kids10[0].getReference(), catalog->getPage(0)->getPageReference());
    QCOMPARE(kids10[1].getReference(), PDFObjectReference(3, 0));
    QCOMPARE(kids10[2].getReference(), PDFObjectReference(4, 0));
    QCOMPARE(kids11[0].getReference(), catalog->getPage(3)->getPageReference());
    QCOMPARE(kids11[1].getReference(), PDFObjectReference(5, 0));
    QCOMPARE(kids11[2].getReference(), PDFObjectReference(6, 0));
    QCOMPARE(kids11[3].getReference(), catalog->getPage(6)->getPageReference());

    // The clones and the blank page with their values
    QCOMPARE(catalog->getPage(0)->getPageRotation(), PageRotation::None);
    QCOMPARE(catalog->getPage(0)->getMediaBox(), QRectF(0, 0, 300, 400));
    QCOMPARE(catalog->getPage(0)->getCropBox(), QRectF(0, 0, 300, 400));
    QCOMPARE(catalog->getPage(3)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(catalog->getPage(3)->getMediaBox(), QRectF(0, 0, 600, 800));
    QCOMPARE(catalog->getPage(3)->getCropBox(), QRectF(10, 10, 490, 690));
    QCOMPARE(catalog->getPage(6)->getPageRotation(), PageRotation::None);
    QCOMPARE(catalog->getPage(6)->getMediaBox(), QRectF(0, 0, 200, 200));
    for (size_t i : { size_t(0), size_t(3) })
    {
        const PDFDictionary* resources = document.getDictionaryFromObject(catalog->getPage(i)->getResources());
        QVERIFY(resources && MergeTestRead::dictionary(document, resources->get("Font")));
    }
    // The other pages of the target keep their inherited values
    QCOMPARE(catalog->getPage(2)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(catalog->getPage(5)->getMediaBox(), QRectF(0, 0, 300, 400));

    QString error;
    const QByteArray written = ArrangeTestHelper::writeIncremental(original, document, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QVERIFY(!written.mid(original.size()).contains("TPAGE"));
    QCOMPARE(MergeTestPdf::readBytes(written).getCatalog()->getPageCount(), size_t(7));
}

void DocumentMergerArrangeTest::cloneBeforeOriginalKeepsIdentity()
{
    // RB4: the clone of page 2 placed before it - the page itself (its object, the bookmark, the
    // named destination, /StructParents, its annotation) stays with the item without the clone
    // flag; the clone is a new page without /StructParents (14.7.5.4, RB13) with a new annotation
    MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 3, "/StructTreeRoot 20 0 R /MarkInfo << /Marked true >> /Outlines 30 0 R /Names << /Dests 35 0 R >>",
                                                       { { 1, "/StructParents 0" }, { 2, "/StructParents 1 /Annots [40 0 R]" }, { 3, "/StructParents 2" } });
    targetPdf.set(20, "<< /Type /StructTreeRoot /K 21 0 R /ParentTree 22 0 R /ParentTreeNextKey 3 >>");
    targetPdf.set(21, "<< /Type /StructElem /S /Document /P 20 0 R /K [23 0 R] >>");
    targetPdf.set(22, "<< /Nums [0 [] 1 [23 0 R] 2 []] >>");
    targetPdf.set(23, "<< /Type /StructElem /S /P /P 21 0 R /K 0 /Pg 4 0 R >>");
    targetPdf.set(30, "<< /Type /Outlines /First 31 0 R /Last 31 0 R /Count 1 >>");
    targetPdf.set(31, "<< /Title (Two) /Parent 30 0 R /Dest [4 0 R /Fit] >>");
    targetPdf.set(35, "<< /Names [(two) [4 0 R /Fit]] >>");
    targetPdf.set(40, "<< /Type /Annot /Subtype /Text /Rect [0 0 10 10] /P 4 0 R /Contents (note) >>");
    const PDFDocument target = targetPdf.read();
    const size_t targetObjectCount = target.getStorage().getObjects().size();

    const Merger::Result result = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ -2, 1, 2, 3 }));
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFDocument& document = result.document;
    QCOMPARE(ArrangeTestHelper::contents(document), QStringList({ "TPAGE2", "TPAGE1", "TPAGE2", "TPAGE3" }));
    const std::vector<PDFObjectReference> pages = ArrangeTestHelper::pages(document);
    QCOMPARE(pages[2], PDFObjectReference(4, 0));
    QVERIFY(size_t(pages[0].objectNumber) >= targetObjectCount);

    // The page itself, its bookmark, destination, structure and annotation are as they were
    QCOMPARE(document.getObjectByReference(PDFObjectReference(4, 0)), target.getObjectByReference(PDFObjectReference(4, 0)));
    for (int object : { 20, 21, 22, 23, 31, 35, 40 })
    {
        QCOMPARE(document.getObjectByReference(PDFObjectReference(object, 0)), target.getObjectByReference(PDFObjectReference(object, 0)));
    }
    QCOMPARE(MergeTestRead::catalog(document)->get("StructTreeRoot").getReference(), PDFObjectReference(20, 0));

    // The clone: no /StructParents, its own annotation of it
    const PDFDictionary* clone = ArrangeTestHelper::pageDictionary(document, 0);
    QVERIFY(!clone->hasKey("StructParents"));
    const std::vector<PDFObject> annotations = MergeTestRead::items(document, clone->get("Annots"));
    QCOMPARE(annotations.size(), size_t(1));
    QVERIFY(annotations.front().isReference() && annotations.front().getReference() != PDFObjectReference(40, 0));
    QCOMPARE(MergeTestRead::dictionary(document, annotations.front())->get("P").getReference(), pages[0]);
    QCOMPARE(MergeTestRead::text(document, MergeTestRead::dictionary(document, annotations.front())->get("Contents")), QStringLiteral("note"));
    QVERIFY(result.statistics.insertedPagesUntagged);
    QCOMPARE(result.statistics.insertedPageCount, 1);
}

void DocumentMergerArrangeTest::signedPageCloneKeepsSignature()
{
    // RB1, RB5 point 1: the signed page cloned before the other page, the order changed. The
    // signature field and its widget are not changed, the clone has no widget of the signature;
    // the incremental update keeps the signed revision, the signature verifies (12.8)
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PDFCertificateManager::NewCertificateInfo info;
    info.fileName = directory.filePath(QStringLiteral("arrange-test.p12"));
    info.privateKeyPasword = QStringLiteral("test-password");
    info.certCommonName = QStringLiteral("PDF4QT arrange test");
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
    const PDFDocument unsignedDocument = targetPdf.read();
    PDFDocumentSigner::Parameters parameters;
    parameters.document = &unsignedDocument;
    parameters.originalDocumentData = original;
    parameters.signFunction = [&](const QByteArray& data, QByteArray& signature) { return PDFSignatureFactory::sign(certificate, info.privateKeyPasword, data, signature); };
    parameters.createSignatureFieldFunction = [&](PDFDocumentBuilder& builder, PDFObjectReference signatureDictionary)
    {
        return builder.createSignatureField(QStringLiteral("Signature"), signatureDictionary, unsignedDocument.getCatalog()->getPage(0)->getPageReference());
    };
    QByteArray signedData;
    QCOMPARE(PDFDocumentSigner::sign(parameters, signedData), PDFDocumentSigner::Result::OK);
    const PDFDocument target = MergeTestPdf::readBytes(signedData);
    QCOMPARE(PDFForm::getSignatureCount(&target), 1);

    // The objects of the field and of the widgets of page 1 before the arrangement
    std::vector<PDFObject> signatureObjects = MergeTestRead::items(target, MergeTestRead::path(target, { "AcroForm" })->get("Fields"));
    const std::vector<PDFObject> widgets = MergeTestRead::items(target, ArrangeTestHelper::pageDictionary(target, 0)->get("Annots"));
    QVERIFY(!widgets.empty());
    signatureObjects.insert(signatureObjects.end(), widgets.cbegin(), widgets.cend());

    const Merger::Result result = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ -1, 2, 1 }));
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFDocument& document = result.document;
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(3));
    for (const PDFObject& object : signatureObjects)
    {
        QVERIFY(object.isReference());
        QCOMPARE(document.getObjectByReference(object.getReference()), target.getObjectByReference(object.getReference()));
    }
    QCOMPARE(MergeTestRead::catalog(document)->get("AcroForm"), MergeTestRead::catalog(target)->get("AcroForm"));
    for (const PDFObject& annotation : MergeTestRead::items(document, ArrangeTestHelper::pageDictionary(document, 0)->get("Annots")))
    {
        QVERIFY(!ArrangeTestHelper::isSignatureWidget(document, annotation));
    }
    QCOMPARE(PDFForm::getSignatureCount(&document), 1);

    QString error;
    const QByteArray written = ArrangeTestHelper::writeIncremental(signedData, document, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QVERIFY(written.startsWith(signedData));
    const PDFDocument reread = MergeTestPdf::readBytes(written);
    QCOMPARE(reread.getCatalog()->getPageCount(), size_t(3));
    const PDFForm form = PDFForm::parse(&reread, reread.getCatalog()->getFormObject());
    PDFSignatureHandler::Parameters verification;
    verification.useSystemCertificateStore = false;
    const std::vector<PDFSignatureVerificationResult> results = PDFSignatureHandler::verifySignatures(form, written, verification);
    QCOMPARE(results.size(), size_t(1));
    QVERIFY2(results.front().isSignatureValid(), qPrintable(results.front().getErrors().join('\n')));
}

void DocumentMergerArrangeTest::signatureWidgetsNotCloned()
{
    // RB1: no widget of a signature field is cloned - also a kid of a field, whose /FT /Sig is
    // inherited (Table 226); a widget of a text field is cloned as a new widget of its field
    // (12.7.4.1), a widget with a cycle of /Parent and no /FT is an annotation as any other
    MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 1, "/AcroForm << /Fields [50 0 R 51 0 R] >>", { { 1, "/Annots [50 0 R 52 0 R 55 0 R]" } });
    targetPdf.set(50, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /Rect [0 0 50 10] /P 3 0 R >>");
    targetPdf.set(51, "<< /FT /Sig /T (sig) /V 53 0 R /Kids [52 0 R] >>");
    targetPdf.set(52, "<< /Type /Annot /Subtype /Widget /Parent 51 0 R /Rect [0 0 0 0] /P 3 0 R >>");
    targetPdf.set(53, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> >>");
    targetPdf.set(55, "<< /Type /Annot /Subtype /Widget /Parent 56 0 R /Rect [0 0 5 5] /P 3 0 R >>");
    targetPdf.set(56, "<< /Parent 55 0 R /Kids [55 0 R] >>");
    const PDFDocument target = targetPdf.read();

    const Merger::Result result = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 1, -1 }));
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFDocument& document = result.document;
    const std::vector<PDFObject> cloneAnnotations = MergeTestRead::items(document, ArrangeTestHelper::pageDictionary(document, 1)->get("Annots"));
    QCOMPARE(cloneAnnotations.size(), size_t(2));
    bool hasTextWidget = false;
    for (const PDFObject& annotation : cloneAnnotations)
    {
        QVERIFY(!ArrangeTestHelper::isSignatureWidget(document, annotation));
        QVERIFY(annotation.getReference() != PDFObjectReference(50, 0) && annotation.getReference() != PDFObjectReference(55, 0));
        const PDFDictionary* widget = MergeTestRead::dictionary(document, annotation);
        const PDFDictionary* parent = MergeTestRead::dictionary(document, widget->get("Parent"));
        hasTextWidget = hasTextWidget || (parent && MergeTestRead::name(document, parent->get("FT")) == "Tx");
    }
    QVERIFY(hasTextWidget);

    // The signature field and its widget are as they were, the page keeps its three annotations
    for (int object : { 51, 52, 53 })
    {
        QCOMPARE(document.getObjectByReference(PDFObjectReference(object, 0)), target.getObjectByReference(PDFObjectReference(object, 0)));
    }
    QCOMPARE(MergeTestRead::items(document, ArrangeTestHelper::pageDictionary(document, 0)->get("Annots")).size(), size_t(3));
}

void DocumentMergerArrangeTest::lockedFieldWidgetsNotCloned_data()
{
    QTest::addColumn<QByteArray>("signatureEntries");
    QTest::addColumn<QStringList>("clonedFields");

    // 12.7.5.5, Table 236: /Action All locks every field, Include the fields of /Fields, Exclude
    // all but them; a name of /Fields is a fully qualified name (12.7.4.2): "addr" is the field of
    // its kid widget, "person" the parent field of "person.first" (a field contains its kids,
    // 12.7.4.1). The lock applies, when the signature field is signed (/V); unlocked fields are
    // cloned as new widgets of their fields (owner 2026-11-11)
    QTest::newRow("no lock") << QByteArray("/V 71 0 R") << QStringList({ "addr", "first", "name" });
    QTest::newRow("all") << QByteArray("/V 71 0 R /Lock << /Type /SigFieldLock /Action /All >>") << QStringList();
    QTest::newRow("include name") << QByteArray("/V 71 0 R /Lock << /Type /SigFieldLock /Action /Include /Fields [(name)] >>") << QStringList({ "addr", "first" });
    QTest::newRow("include field of a kid widget") << QByteArray("/V 71 0 R /Lock << /Type /SigFieldLock /Action /Include /Fields [(addr)] >>") << QStringList({ "first", "name" });
    QTest::newRow("include parent field") << QByteArray("/V 71 0 R /Lock << /Type /SigFieldLock /Action /Include /Fields [(person)] >>") << QStringList({ "addr", "name" });
    QTest::newRow("include qualified name") << QByteArray("/V 71 0 R /Lock << /Type /SigFieldLock /Action /Include /Fields [(person.first)] >>") << QStringList({ "addr", "name" });
    QTest::newRow("exclude name") << QByteArray("/V 71 0 R /Lock << /Type /SigFieldLock /Action /Exclude /Fields [(name)] >>") << QStringList({ "name" });
    QTest::newRow("unsigned all") << QByteArray("/Lock << /Type /SigFieldLock /Action /All >>") << QStringList({ "addr", "first", "name" });

    // 12.8.2.4, Table 259: the signature 72 carries the lock in its signature reference (the
    // FieldMDP transform parameters, Table 255 /Reference) without /Lock in the field - a validator
    // checks the fields of it, so they are locked the same way
    QTest::newRow("FieldMDP of the signature") << QByteArray("/V 72 0 R") << QStringList({ "addr", "first" });
}

void DocumentMergerArrangeTest::lockedFieldWidgetsNotCloned()
{
    // The clone of page 1 with the widgets of the text field "name" (a merged field and widget),
    // of the text field "addr" (its kid widget; the field tree has a cycle of /Kids), of the text
    // field "person.first" (a merged field and widget below the field "person") and of the
    // signature field "sig" with the lock of the row. A widget of a locked field is not cloned and
    // its field is not changed, as the signature field itself (RB1)
    QFETCH(QByteArray, signatureEntries);
    QFETCH(QStringList, clonedFields);

    MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 1, "/AcroForm << /Fields [50 0 R 51 0 R 53 0 R 70 0 R] /SigFlags 3 >>", { { 1, "/Annots [50 0 R 52 0 R 54 0 R 70 0 R]" } });
    targetPdf.set(50, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /Rect [0 0 50 10] /P 3 0 R >>");
    targetPdf.set(51, "<< /FT /Tx /T (addr) /Kids [52 0 R 51 0 R] >>");
    targetPdf.set(52, "<< /Type /Annot /Subtype /Widget /Parent 51 0 R /Rect [0 20 50 30] /P 3 0 R >>");
    targetPdf.set(53, "<< /FT /Tx /T (person) /Kids [54 0 R] >>");
    targetPdf.set(54, "<< /Type /Annot /Subtype /Widget /Parent 53 0 R /T (first) /Rect [0 40 50 50] /P 3 0 R >>");
    targetPdf.set(70, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (sig) /Rect [0 0 0 0] /P 3 0 R " + signatureEntries + " >>");
    targetPdf.set(71, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> >>");
    targetPdf.set(72, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> "
                      "/Reference [<< /Type /SigRef /TransformMethod /FieldMDP /TransformParams << /Type /TransformParams /Action /Include /Fields [(name)] /V /1.2 >> >>] >>");
    const PDFDocument target = targetPdf.read();

    const Merger::Result result = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 1, -1 }));
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFDocument& document = result.document;

    QStringList fields;
    for (const PDFObject& annotation : MergeTestRead::items(document, ArrangeTestHelper::pageDictionary(document, 1)->get("Annots")))
    {
        const PDFDictionary* widget = MergeTestRead::dictionary(document, annotation);
        QVERIFY(widget && !ArrangeTestHelper::isSignatureWidget(document, annotation));
        const PDFDictionary* field = widget->hasKey("T") ? widget : MergeTestRead::dictionary(document, widget->get("Parent"));
        QVERIFY(field);
        fields << MergeTestRead::text(document, field->get("T"));
    }
    fields.sort();
    QCOMPARE(fields, clonedFields);

    // The facts of Manage count what the clone left out: the widget of the signature field and
    // the widgets of the three text fields, which the lock names
    QCOMPARE(result.statistics.signatureWidgetsNotCloned, qint64(1));
    QCOMPARE(result.statistics.lockedWidgetsNotCloned, qint64(3 - clonedFields.size()));

    // A locked field keeps its objects (no new kid, no split of the merged field and widget)
    if (!clonedFields.contains(QStringLiteral("name")))
    {
        QCOMPARE(document.getObjectByReference(PDFObjectReference(50, 0)), target.getObjectByReference(PDFObjectReference(50, 0)));
    }
    if (!clonedFields.contains(QStringLiteral("addr")))
    {
        QCOMPARE(document.getObjectByReference(PDFObjectReference(51, 0)), target.getObjectByReference(PDFObjectReference(51, 0)));
    }
    if (!clonedFields.contains(QStringLiteral("first")))
    {
        QCOMPARE(document.getObjectByReference(PDFObjectReference(53, 0)), target.getObjectByReference(PDFObjectReference(53, 0)));
        QCOMPARE(document.getObjectByReference(PDFObjectReference(54, 0)), target.getObjectByReference(PDFObjectReference(54, 0)));
    }
    QCOMPARE(document.getObjectByReference(PDFObjectReference(70, 0)), target.getObjectByReference(PDFObjectReference(70, 0)));
    QCOMPARE(MergeTestRead::items(document, ArrangeTestHelper::pageDictionary(document, 0)->get("Annots")).size(), size_t(4));
}

void DocumentMergerArrangeTest::planOfAnotherVersionRefused()
{
    // RB2: a plan made for another version of the document is refused - a page inserted meanwhile
    // (the plan would remove it), a page removed, another order; the document is not changed.
    // A plan of this version removes the page it leaves out.
    const PDFDocument target = MergeTestPdf::createPages("T", 3).read();
    const std::vector<PDFObjectReference> pages = ArrangeTestHelper::pages(target);
    const std::vector<std::vector<PDFObjectReference>> otherVersions = {
        { pages[0], pages[1] },
        { pages[0], pages[1], pages[2], PDFObjectReference(99, 0) },
        { pages[1], pages[0], pages[2] },
        {},
    };
    for (const std::vector<PDFObjectReference>& version : otherVersions)
    {
        std::vector<Merger::Source> sources = { Merger::Source::createDocument(PDFDocumentPointer(new PDFDocument(target)), QStringLiteral("Target.pdf")) };
        Merger::Settings settings;
        settings.targetPages = version;
        const Merger::Result result = Merger::arrange(target, sources, ArrangeTestHelper::items({ 2, 1 }), settings, nullptr, nullptr);
        QVERIFY(!result.isSuccess);
        QVERIFY2(result.errorMessage.contains(QStringLiteral("changed")), qPrintable(result.errorMessage));
        QCOMPARE(result.document.getCatalog()->getPageCount(), size_t(0));
    }

    const Merger::Result current = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 2, 1 }));
    QVERIFY2(current.isSuccess, qPrintable(current.errorMessage));
    QCOMPARE(ArrangeTestHelper::contents(current.document), QStringList({ "TPAGE2", "TPAGE1" }));
    QCOMPARE(current.statistics.replacedPageCount, 1);
}

void DocumentMergerArrangeTest::encryptionKept_data()
{
    QTest::addColumn<int>("algorithm");
    QTest::newRow("aes-256") << int(PDFSecurityHandlerFactory::AES_256);
    QTest::newRow("rc4") << int(PDFSecurityHandlerFactory::RC4);
}

void DocumentMergerArrangeTest::encryptionKept()
{
    // 7.6, table 5: the target keeps /Encrypt and /ID; the new objects (the clone and its
    // annotation) are encrypted by the handler of the target with the key of their numbers - the
    // update has no plain text of them and reads back by the password
    QFETCH(int, algorithm);
    MergeTestPdf annotated = MergeTestPdf::createPages("T", 2, QByteArray(), { { 1, "/Annots [40 0 R]" } });
    annotated.set(40, "<< /Type /Annot /Subtype /Text /Rect [0 0 10 10] /P 3 0 R /Contents (SECRETNOTE) >>");
    const QByteArray encrypted = ArrangeTestHelper::encrypt(annotated.read(), PDFSecurityHandlerFactory::Algorithm(algorithm), ArrangeTestHelper::allPermissions());
    QVERIFY(!encrypted.contains("SECRETNOTE"));
    const PDFDocument target = MergeTestPdf::readBytes(encrypted);
    QVERIFY(target.getStorage().getSecurityHandler()->getMode() != EncryptionMode::None);
    QCOMPARE(target.getCatalog()->getPageCount(), size_t(2));

    const Merger::Result result = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 2, 1, -1 }));
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFDocument& document = result.document;
    QVERIFY(document.getStorage().getSecurityHandler()->getMode() != EncryptionMode::None);
    QCOMPARE(MergeTestRead::trailer(document)->get("ID"), MergeTestRead::trailer(target)->get("ID"));
    QCOMPARE(MergeTestRead::trailer(document)->get("Encrypt"), MergeTestRead::trailer(target)->get("Encrypt"));

    QString error;
    const QByteArray written = ArrangeTestHelper::writeIncremental(encrypted, document, &error);
    QVERIFY2(!written.isEmpty(), qPrintable(error));
    QVERIFY(!written.mid(encrypted.size()).contains("SECRETNOTE"));
    const PDFDocument reread = MergeTestPdf::readBytes(written);
    QCOMPARE(ArrangeTestHelper::contents(reread), QStringList({ "TPAGE2", "TPAGE1", "TPAGE1" }));
    const std::vector<PDFObject> cloneAnnotations = MergeTestRead::items(reread, ArrangeTestHelper::pageDictionary(reread, 2)->get("Annots"));
    QCOMPARE(cloneAnnotations.size(), size_t(1));
    QVERIFY(cloneAnnotations.front().getReference() != PDFObjectReference(40, 0));
    QCOMPARE(MergeTestRead::text(reread, MergeTestRead::dictionary(reread, cloneAnnotations.front())->get("Contents")), QStringLiteral("SECRETNOTE"));
}

void DocumentMergerArrangeTest::permissionsOfTarget()
{
    // Table 22, plan 4.5: bit 11 (assemble) for the arrangement, bit 4 (modify) besides for a crop;
    // the owner password verified by the caller grants every bit (Q15 of step 2)
    using Permission = PDFSecurityHandler::Permission;
    const PDFDocument plain = MergeTestPdf::createPages("T", 2).read();
    const PDFDocument noAssemble = MergeTestPdf::readBytes(ArrangeTestHelper::encrypt(plain, PDFSecurityHandlerFactory::AES_256, ArrangeTestHelper::allPermissions() & ~uint32_t(Permission::Assemble)));
    const PDFDocument noModify = MergeTestPdf::readBytes(ArrangeTestHelper::encrypt(plain, PDFSecurityHandlerFactory::AES_256, ArrangeTestHelper::allPermissions() & ~uint32_t(Permission::Modify)));

    const Merger::Result refused = ArrangeTestHelper::arrange(noAssemble, ArrangeTestHelper::items({ 2, 1 }));
    QVERIFY(!refused.isSuccess);
    QVERIFY2(refused.errorMessage.contains(QStringLiteral("Target.pdf does not allow")), qPrintable(refused.errorMessage));
    const Merger::Result owner = ArrangeTestHelper::arrange(noAssemble, ArrangeTestHelper::items({ 2, 1 }), {}, true);
    QVERIFY2(owner.isSuccess, qPrintable(owner.errorMessage));

    const Merger::Result turned = ArrangeTestHelper::arrange(noModify, ArrangeTestHelper::items({ 2, 1 }));
    QVERIFY2(turned.isSuccess, qPrintable(turned.errorMessage));
    std::vector<Merger::Item> cropped = ArrangeTestHelper::items({ 2, 1 });
    cropped[0].cropMargins = QMarginsF(10, 0, 0, 0);
    QVERIFY(!ArrangeTestHelper::arrange(noModify, cropped).isSuccess);
    const Merger::Result croppedPlain = ArrangeTestHelper::arrange(plain, cropped);
    QVERIFY2(croppedPlain.isSuccess, qPrintable(croppedPlain.errorMessage));
    QCOMPARE(croppedPlain.document.getCatalog()->getPage(0)->getCropBox(), QRectF(10, 0, 590, 800));
}

void DocumentMergerArrangeTest::cropFromVisibleArea()
{
    // B28, 14.11.2: a crop box larger than the media box is effectively the media box - the page
    // shows 600 x 800 (the size the assembly shows, getEffectiveCropBox), so 10 points cut on the
    // left leave the media box less its left 10 points, in the document and in a new one (merge)
    const PDFDocument target = MergeTestPdf::createPages("T", 2, QByteArray(), { { 1, "/CropBox [-50 -50 650 850]" } }).read();
    QCOMPARE(target.getCatalog()->getPage(0)->getEffectiveCropBox(), QRectF(0, 0, 600, 800));
    std::vector<Merger::Item> items = ArrangeTestHelper::items({ 2, 1 });
    items[1].cropMargins = QMarginsF(10, 0, 0, 0);

    const Merger::Result arranged = ArrangeTestHelper::arrange(target, items);
    QVERIFY2(arranged.isSuccess, qPrintable(arranged.errorMessage));
    QCOMPARE(arranged.document.getCatalog()->getPage(1)->getCropBox(), QRectF(10, 0, 590, 800));

    std::vector<Merger::Source> sources = { Merger::Source::createDocument(PDFDocumentPointer(new PDFDocument(target)), QStringLiteral("Target.pdf")) };
    Merger::Settings settings;
    settings.outline = Merger::OutlineMode::NoOutline;
    const Merger::Result merged = Merger::merge(sources, items, settings, nullptr, nullptr);
    QVERIFY2(merged.isSuccess, qPrintable(merged.errorMessage));
    QCOMPARE(merged.document.getCatalog()->getPage(1)->getCropBox(), QRectF(10, 0, 590, 800));
}

void DocumentMergerArrangeTest::pageLabelsStayWithPages()
{
    // 12.4.2, Table 159: the labels i, ii, 1, 2 - every page keeps its label in the new order, the
    // clone gets the label of its original; a target without labels gets none
    const PDFDocument target = MergeTestPdf::createPages("T", 4, "/PageLabels << /Nums [0 << /S /r >> 2 << /S /D >>] >>").read();
    QCOMPARE(target.getCatalog()->getPageLabel(2), QStringLiteral("1"));
    const Merger::Result result = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 3, 1, 4, 2, -1 }));
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFCatalog* catalog = result.document.getCatalog();
    QStringList labels;
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        labels << catalog->getPageLabel(PDFInteger(i));
    }
    QCOMPARE(labels, QStringList({ "1", "i", "2", "ii", "i" }));

    const PDFDocument unlabeled = MergeTestPdf::createPages("T", 2).read();
    const Merger::Result plain = ArrangeTestHelper::arrange(unlabeled, ArrangeTestHelper::items({ 2, 1 }));
    QVERIFY(plain.isSuccess);
    QVERIFY(!MergeTestRead::catalog(plain.document)->hasKey("PageLabels"));
}

void DocumentMergerArrangeTest::documentParts()
{
    // 14.12: a part is a contiguous range of pages from /Start to /End. Another order breaks the
    // parts - /DPartRoot and /DPart of the pages are removed (plan 4.5 point 6), the nodes of the
    // parts are null. A clone inserted at the border of two parts joins the part before it.
    MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 4, "/DPartRoot 70 0 R", { { 1, "/DPart 72 0 R" }, { 2, "/DPart 72 0 R" }, { 3, "/DPart 73 0 R" }, { 4, "/DPart 73 0 R" } });
    targetPdf.set(70, "<< /Type /DPartRoot /DPartRootNode 71 0 R >>");
    targetPdf.set(71, "<< /Type /DPart /DParts [[72 0 R 73 0 R]] >>");
    targetPdf.set(72, "<< /Type /DPart /Parent 71 0 R /Start 3 0 R /End 4 0 R >>");
    targetPdf.set(73, "<< /Type /DPart /Parent 71 0 R /Start 5 0 R /End 6 0 R >>");
    const PDFDocument target = targetPdf.read();

    const Merger::Result reordered = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 2, 1, 3, 4 }));
    QVERIFY2(reordered.isSuccess, qPrintable(reordered.errorMessage));
    QVERIFY(!MergeTestRead::catalog(reordered.document)->hasKey("DPartRoot"));
    for (size_t i = 0; i < 4; ++i)
    {
        QVERIFY(!ArrangeTestHelper::pageDictionary(reordered.document, i)->hasKey("DPart"));
    }
    for (int object : { 70, 71, 72, 73 })
    {
        QVERIFY(reordered.document.getObjectByReference(PDFObjectReference(object, 0)).isNull());
    }

    const Merger::Result inserted = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 1, 2, -2, 3, 4 }));
    QVERIFY2(inserted.isSuccess, qPrintable(inserted.errorMessage));
    const PDFDocument& document = inserted.document;
    QCOMPARE(MergeTestRead::catalog(document)->get("DPartRoot").getReference(), PDFObjectReference(70, 0));
    const PDFObjectReference clone = document.getCatalog()->getPage(2)->getPageReference();
    const PDFObject clonePart = ArrangeTestHelper::pageDictionary(document, 2)->get("DPart");
    QVERIFY(clonePart.isReference());
    QCOMPARE(clonePart.getReference(), PDFObjectReference(72, 0));
    const PDFDictionary* first = MergeTestRead::dictionary(document, PDFObject::createReference(PDFObjectReference(72, 0)));
    QCOMPARE(first->get("Start").getReference(), PDFObjectReference(3, 0));
    QCOMPARE(first->get("End").getReference(), clone);
    QCOMPARE(document.getObjectByReference(PDFObjectReference(73, 0)), target.getObjectByReference(PDFObjectReference(73, 0)));

    // The page 1 (object 3, /Start of the part 72) removed, the order of the others kept: the part
    // keeps its page, which stays (removePages, 14.12.3) - /Start is the page 2 (object 4), /End
    // goes (Table 409: only for a range of more pages); the clone after it joins it
    const Merger::Result removed = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 2, -2, 3, 4 }));
    QVERIFY2(removed.isSuccess, qPrintable(removed.errorMessage));
    QCOMPARE(MergeTestRead::catalog(removed.document)->get("DPartRoot"), PDFObject::createReference(PDFObjectReference(70, 0)));
    const PDFDictionary* kept = MergeTestRead::dictionary(removed.document, PDFObject::createReference(PDFObjectReference(72, 0)));
    QVERIFY(kept);
    const PDFObjectReference removedClone = removed.document.getCatalog()->getPage(1)->getPageReference();
    QCOMPARE(kept->get("Start"), PDFObject::createReference(PDFObjectReference(4, 0)));
    QCOMPARE(kept->get("End"), PDFObject::createReference(removedClone));
    QCOMPARE(ArrangeTestHelper::pageDictionary(removed.document, 0)->get("DPart"), PDFObject::createReference(PDFObjectReference(72, 0)));
    QCOMPARE(ArrangeTestHelper::pageDictionary(removed.document, 1)->get("DPart"), PDFObject::createReference(PDFObjectReference(72, 0)));
    QCOMPARE(ArrangeTestHelper::pageDictionary(removed.document, 2)->get("DPart"), PDFObject::createReference(PDFObjectReference(73, 0)));
    QCOMPARE(removed.document.getObjectByReference(PDFObjectReference(73, 0)), target.getObjectByReference(PDFObjectReference(73, 0)));

    // The whole part 72 removed: its node goes, the root keeps the part 73
    const Merger::Result partRemoved = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 3, 4 }));
    QVERIFY2(partRemoved.isSuccess, qPrintable(partRemoved.errorMessage));
    const PDFDictionary* rootNode = MergeTestRead::path(partRemoved.document, { "DPartRoot", "DPartRootNode" });
    QVERIFY(rootNode);
    const std::vector<PDFObject> childArrays = MergeTestRead::items(partRemoved.document, rootNode->get("DParts"));
    QCOMPARE(childArrays.size(), size_t(1));
    const std::vector<PDFObject> children = MergeTestRead::items(partRemoved.document, childArrays.front());
    QCOMPARE(children.size(), size_t(1));
    QCOMPARE(children.front(), PDFObject::createReference(PDFObjectReference(73, 0)));
    QVERIFY(partRemoved.document.getObjectByReference(PDFObjectReference(72, 0)).isNull());
}

void DocumentMergerArrangeTest::unusedPagesRemoved()
{
    // Plan 4.5 point 1: a page, which the plan of this version leaves out, goes by the rules of
    // Remove Pages - the bookmark and the named destination of it with it, its object null; every
    // page removed is refused (a document has at least one page)
    MergeTestPdf targetPdf = MergeTestPdf::createPages("T", 3, "/Outlines 30 0 R /Names << /Dests 35 0 R >>");
    targetPdf.set(30, "<< /Type /Outlines /First 31 0 R /Last 31 0 R /Count 1 >>");
    targetPdf.set(31, "<< /Title (Two) /Parent 30 0 R /Dest [4 0 R /Fit] >>");
    targetPdf.set(35, "<< /Names [(two) [4 0 R /Fit]] >>");
    const PDFDocument target = targetPdf.read();

    const Merger::Result result = ArrangeTestHelper::arrange(target, ArrangeTestHelper::items({ 3, 1 }));
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    QCOMPARE(ArrangeTestHelper::contents(result.document), QStringList({ "TPAGE3", "TPAGE1" }));
    QVERIFY(result.document.getObjectByReference(PDFObjectReference(4, 0)).isNull());
    QCOMPARE(result.statistics.replacedPageCount, 1);
    QCOMPARE(result.statistics.replacement.outlineItemsRemoved, 1);
    QCOMPARE(result.statistics.replacement.namedDestinationsRemoved, 1);
    QVERIFY(result.statistics.isPageTreeFlattened);

    Merger::Item blank;
    blank.source = 1;
    blank.pageSize = QSizeF(100, 100);
    for (const std::vector<Merger::Item>& items : { std::vector<Merger::Item>(), std::vector<Merger::Item>({ blank }) })
    {
        const Merger::Result refused = ArrangeTestHelper::arrange(target, items, { Merger::Source::createBlank() });
        QVERIFY(!refused.isSuccess);
        QVERIFY2(refused.errorMessage.contains(QStringLiteral("at least one page")), qPrintable(refused.errorMessage));
    }
}

void DocumentMergerArrangeTest::invalidPlans()
{
    // A plan, which names no input, a page the document has not, the first input not the document,
    // a dynamic XFA form (12.7.9: no pages to arrange) - refused without a change
    const PDFDocument target = MergeTestPdf::createPages("T", 2).read();
    std::vector<Merger::Item> outside = ArrangeTestHelper::items({ 1, 3 });
    QVERIFY(ArrangeTestHelper::arrange(target, outside).errorMessage.contains(QStringLiteral("no page 3")));
    std::vector<Merger::Item> noInput = ArrangeTestHelper::items({ 1, 2 });
    noInput[1].source = 5;
    QVERIFY(!ArrangeTestHelper::arrange(target, noInput).isSuccess);

    std::vector<Merger::Source> blankFirst = { Merger::Source::createBlank() };
    QVERIFY(!Merger::arrange(target, blankFirst, ArrangeTestHelper::items({ 1, 2 }), ArrangeTestHelper::settings(target), nullptr, nullptr).isSuccess);

    MergeTestPdf xfaPdf = MergeTestPdf::createPages("T", 1, "/NeedsRendering true /AcroForm << /Fields [] /XFA 60 0 R >>");
    xfaPdf.set(60, MergeTestPdf::stream("<xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\"></xdp:xdp>"));
    const PDFDocument xfa = xfaPdf.read();
    const Merger::Result xfaResult = ArrangeTestHelper::arrange(xfa, ArrangeTestHelper::items({ 1 }));
    QVERIFY(!xfaResult.isSuccess);
    QVERIFY2(xfaResult.errorMessage.contains(QStringLiteral("XFA")), qPrintable(xfaResult.errorMessage));
}

void DocumentMergerArrangeTest::damagedPageTree()
{
    // 7.7.3.2: /Count of a node is the number of the leaves below it. A wrong /Count does not
    // reach the result - the counts of the nodes above the new pages are counted from the leaves;
    // a node without /Type, which the catalog does not read, flattens the tree on a new order
    MergeTestPdf countPdf;
    countPdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    countPdf.set(2, "<< /Type /Pages /Kids [10 0 R] /Count 7 /MediaBox [0 0 600 800] /Resources << >> >>");
    countPdf.set(10, "<< /Type /Pages /Parent 2 0 R /Kids [3 0 R 4 0 R] /Count 1 >>");
    countPdf.set(3, "<< /Type /Page /Parent 10 0 R >>");
    countPdf.set(4, "<< /Type /Page /Parent 10 0 R >>");
    const PDFDocument wrongCount = countPdf.read();
    QCOMPARE(wrongCount.getCatalog()->getPageCount(), size_t(2));
    const Merger::Result counted = ArrangeTestHelper::arrange(wrongCount, ArrangeTestHelper::items({ 1, -1, 2 }));
    QVERIFY2(counted.isSuccess, qPrintable(counted.errorMessage));
    QVERIFY(!counted.statistics.isPageTreeFlattened);
    QCOMPARE(MergeTestRead::integer(counted.document, MergeTestRead::dictionary(counted.document, PDFObject::createReference(PDFObjectReference(2, 0)))->get("Count")), 3);
    QCOMPARE(MergeTestRead::integer(counted.document, MergeTestRead::dictionary(counted.document, PDFObject::createReference(PDFObjectReference(10, 0)))->get("Count")), 3);

    // A node shared by two parents (a page is one leaf, 7.7.3.2) is refused by the reader of the
    // tree: the document has no pages, an arrangement of it is refused, nothing is removed
    MergeTestPdf sharedPdf;
    sharedPdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    sharedPdf.set(2, "<< /Type /Pages /Kids [10 0 R 10 0 R] /Count 4 /MediaBox [0 0 600 800] >>");
    sharedPdf.set(10, "<< /Type /Pages /Parent 2 0 R /Kids [3 0 R 4 0 R] /Count 2 >>");
    sharedPdf.set(3, "<< /Type /Page /Parent 10 0 R >>");
    sharedPdf.set(4, "<< /Type /Page /Parent 10 0 R >>");
    bool isRead = true;
    const PDFDocument shared = MergeTestPdf::readBytes(sharedPdf.write(), QString(), &isRead);
    QVERIFY(!isRead);
    QCOMPARE(shared.getCatalog()->getPageCount(), size_t(0));
    std::vector<Merger::Source> sources = { Merger::Source::createDocument(PDFDocumentPointer(new PDFDocument(shared)), QStringLiteral("Shared.pdf")) };
    const Merger::Result sharedResult = Merger::arrange(shared, sources, ArrangeTestHelper::items({ 2, 1 }), ArrangeTestHelper::settings(shared), nullptr, nullptr);
    QVERIFY(!sharedResult.isSuccess);
    QVERIFY(!sharedResult.errorMessage.isEmpty());
}

void DocumentMergerArrangeTest::otherSourcesJoined()
{
    // Plan 4.5: pages of another PDF, an image and a blank page among the pages of the target in
    // another order - the source joins by the rules of insert (the field "name" collides, 12.7.4.2,
    // Q3 of step 2), the objects of the target keep their numbers
    const PDFDocument target = [](){
        MergeTestPdf pdf = MergeTestPdf::createPages("T", 2, "/AcroForm << /Fields [50 0 R] >>", { { 1, "/Annots [50 0 R]" } });
        pdf.set(50, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /Rect [0 0 50 10] /P 3 0 R >>");
        return pdf.read();
    }();
    const PDFDocument source = [](){
        MergeTestPdf pdf = MergeTestPdf::createPages("S", 2, "/AcroForm << /Fields [50 0 R] >>", { { 1, "/Annots [50 0 R]" } });
        pdf.set(50, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /Rect [0 0 50 10] /P 3 0 R >>");
        return pdf.read();
    }();
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");

    std::vector<Merger::Source> others = { Merger::Source::createDocument(PDFDocumentPointer(new PDFDocument(source)), QStringLiteral("Source.pdf")),
                                           Merger::Source::createBlank(),
                                           Merger::Source::createImage(png, "PNG", 72.0, QStringLiteral("image.png")) };
    std::vector<Merger::Item> items;
    Merger::Item sourcePage;
    sourcePage.source = 1;
    sourcePage.pageIndex = 0;
    Merger::Item blank;
    blank.source = 2;
    blank.pageSize = QSizeF(100, 100);
    Merger::Item imagePage;
    imagePage.source = 3;
    items = { sourcePage, ArrangeTestHelper::items({ 2 }).front(), blank, ArrangeTestHelper::items({ 1 }).front(), imagePage };

    const Merger::Result result = ArrangeTestHelper::arrange(target, items, others);
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFDocument& document = result.document;
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(5));
    QCOMPARE(ArrangeTestHelper::contents(document).mid(0, 3), QStringList({ "SPAGE1", "TPAGE2", "TPAGE1" }));
    QCOMPARE(result.statistics.insertedPageCount, 3);
    QCOMPARE(result.statistics.fieldsRenamed, 1);
    QCOMPARE(ArrangeTestHelper::pages(document)[1], PDFObjectReference(4, 0));
    QCOMPARE(ArrangeTestHelper::pages(document)[3], PDFObjectReference(3, 0));
    QCOMPARE(document.getCatalog()->getPage(2)->getMediaBox(), QRectF(0, 0, 100, 100));
    QCOMPARE(document.getCatalog()->getPage(4)->getMediaBox().size(), QSizeF(40, 20));
    QCOMPARE(MergeTestRead::items(document, MergeTestRead::path(document, { "AcroForm" })->get("Fields")).size(), size_t(2));
}

void DocumentMergerArrangeTest::canceledArrangement()
{
    // Cancellation: no document, the result says it
    const PDFDocument target = MergeTestPdf::createPages("T", 3).read();
    std::vector<Merger::Source> sources = { Merger::Source::createDocument(PDFDocumentPointer(new PDFDocument(target)), QStringLiteral("Target.pdf")) };
    ArrangeTestCanceledControl control;
    const Merger::Result result = Merger::arrange(target, sources, ArrangeTestHelper::items({ 3, 1 }), ArrangeTestHelper::settings(target), &control, nullptr);
    QVERIFY(result.isCanceled);
    QVERIFY(!result.isSuccess);
    QCOMPARE(result.document.getCatalog()->getPageCount(), size_t(0));
}

void DocumentMergerArrangeTest::mergeParts()
{
    // QB3 (b), plan 4.4: the parts of the settings instead of the runs - a bookmark per part with
    // its title to its first page (12.3.3), /DPartRoot with a part per entry over its pages (14.12);
    // Join puts the outlines of the inputs of the pages of a part below it, in the order of their
    // first pages; Document parts of more inputs has no outline of the inputs
    MergeTestPdf aPdf = MergeTestPdf::createPages("A", 3, "/Outlines 30 0 R");
    aPdf.set(30, "<< /Type /Outlines /First 31 0 R /Last 31 0 R /Count 1 >>");
    aPdf.set(31, "<< /Title (A-three) /Parent 30 0 R /Dest [5 0 R /Fit] >>");
    MergeTestPdf bPdf = MergeTestPdf::createPages("B", 2, "/Outlines 30 0 R");
    bPdf.set(30, "<< /Type /Outlines /First 31 0 R /Last 31 0 R /Count 1 >>");
    bPdf.set(31, "<< /Title (B-two) /Parent 30 0 R /Dest [4 0 R /Fit] >>");
    const std::vector<Merger::Source> sources = { Merger::Source::createDocument(aPdf.readPointer(), QStringLiteral("A.pdf")),
                                                  Merger::Source::createDocument(bPdf.readPointer(), QStringLiteral("B.pdf")),
                                                  Merger::Source::createBlank() };
    std::vector<Merger::Item> items;
    const std::vector<std::pair<size_t, PDFInteger>> pageList = { { 0, 0 }, { 0, 1 }, { 0, 2 }, { 1, 0 }, { 2, 0 }, { 1, 1 } };
    for (const auto& [source, page] : pageList)
    {
        Merger::Item item;
        item.source = source;
        item.pageIndex = page;
        item.pageSize = QSizeF(100, 100);
        items.push_back(item);
    }

    for (const Merger::OutlineMode mode : { Merger::OutlineMode::DocumentParts, Merger::OutlineMode::Join })
    {
        Merger::Settings settings;
        settings.outline = mode;
        settings.parts = { { 0, QStringLiteral("Introduction") }, { 2, QStringLiteral("Appendix") } };
        const Merger::Result result = Merger::merge(sources, items, settings, nullptr, nullptr);
        QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
        const PDFDocument& document = result.document;
        QCOMPARE(result.statistics.partCount, 2);

        const PDFDictionary* outline = MergeTestRead::path(document, { "Outlines" });
        QVERIFY(outline);
        const PDFDictionary* introduction = MergeTestRead::dictionary(document, outline->get("First"));
        const PDFDictionary* appendix = MergeTestRead::dictionary(document, introduction->get("Next"));
        QVERIFY(appendix && !appendix->hasKey("Next"));
        QCOMPARE(MergeTestRead::text(document, introduction->get("Title")), QStringLiteral("Introduction"));
        QCOMPARE(MergeTestRead::text(document, appendix->get("Title")), QStringLiteral("Appendix"));
        QCOMPARE(MergeTestRead::items(document, introduction->get("Dest")).front().getReference(), document.getCatalog()->getPage(0)->getPageReference());
        QCOMPARE(MergeTestRead::items(document, appendix->get("Dest")).front().getReference(), document.getCatalog()->getPage(2)->getPageReference());

        QStringList children;
        for (PDFObject child = appendix->get("First"); MergeTestRead::dictionary(document, child); child = MergeTestRead::dictionary(document, child)->get("Next"))
        {
            children << MergeTestRead::text(document, MergeTestRead::dictionary(document, child)->get("Title"));
        }
        QCOMPARE(children, mode == Merger::OutlineMode::Join ? QStringList({ "A-three", "B-two" }) : QStringList());
        QVERIFY(!introduction->hasKey("First"));

        // 14.12: two parts, pages 1-2 and 3-6
        const PDFCatalog* catalog = document.getCatalog();
        const PDFObjectReference firstPart = catalog->getPage(0)->getDocumentPart();
        const PDFObjectReference secondPart = catalog->getPage(2)->getDocumentPart();
        QVERIFY(firstPart.isValid() && secondPart.isValid() && firstPart != secondPart);
        QCOMPARE(catalog->getPage(1)->getDocumentPart(), firstPart);
        QCOMPARE(catalog->getPage(5)->getDocumentPart(), secondPart);
        const PDFDictionary* second = MergeTestRead::dictionary(document, PDFObject::createReference(secondPart));
        QCOMPARE(second->get("Start").getReference(), catalog->getPage(2)->getPageReference());
        QCOMPARE(second->get("End").getReference(), catalog->getPage(5)->getPageReference());
    }

    // Parts, which do not start at the first item, are not ascending or have no item, are refused
    const std::vector<std::vector<Merger::Part>> invalid = { { { 1, QStringLiteral("x") } },
                                                             { { 0, QStringLiteral("a") }, { 0, QStringLiteral("b") } },
                                                             { { 0, QStringLiteral("a") }, { 6, QStringLiteral("b") } } };
    for (const std::vector<Merger::Part>& parts : invalid)
    {
        Merger::Settings settings;
        settings.parts = parts;
        const Merger::Result result = Merger::merge(sources, items, settings, nullptr, nullptr);
        QVERIFY(!result.isSuccess);
        QVERIFY(!result.errorMessage.isEmpty());
    }
}

void DocumentMergerArrangeTest::mergePartsOfOneDocument()
{
    // QB3 (b) with one PDF input (its catalog stays, R3 of step 2): the parts apply the outline
    // mode - a bookmark per part, the outline of the document below the part of its pages, two
    // document parts (14.12)
    MergeTestPdf aPdf = MergeTestPdf::createPages("A", 3, "/Outlines 30 0 R");
    aPdf.set(30, "<< /Type /Outlines /First 31 0 R /Last 31 0 R /Count 1 >>");
    aPdf.set(31, "<< /Title (A-three) /Parent 30 0 R /Dest [5 0 R /Fit] >>");
    const std::vector<Merger::Source> sources = { Merger::Source::createDocument(aPdf.readPointer(), QStringLiteral("A.pdf")) };
    std::vector<Merger::Item> items;
    for (PDFInteger page = 0; page < 3; ++page)
    {
        Merger::Item item;
        item.pageIndex = page;
        items.push_back(item);
    }
    Merger::Settings settings;
    settings.outline = Merger::OutlineMode::DocumentParts;
    settings.parts = { { 0, QStringLiteral("One") }, { 1, QStringLiteral("Two") } };
    const Merger::Result result = Merger::merge(sources, items, settings, nullptr, nullptr);
    QVERIFY2(result.isSuccess, qPrintable(result.errorMessage));
    const PDFDocument& document = result.document;
    QCOMPARE(result.statistics.partCount, 2);

    const PDFDictionary* outline = MergeTestRead::path(document, { "Outlines" });
    const PDFDictionary* one = MergeTestRead::dictionary(document, outline->get("First"));
    const PDFDictionary* two = MergeTestRead::dictionary(document, one->get("Next"));
    QCOMPARE(MergeTestRead::text(document, one->get("Title")), QStringLiteral("One"));
    QCOMPARE(MergeTestRead::text(document, two->get("Title")), QStringLiteral("Two"));
    QVERIFY(!one->hasKey("First"));
    const PDFDictionary* child = MergeTestRead::dictionary(document, two->get("First"));
    QVERIFY(child);
    QCOMPARE(MergeTestRead::text(document, child->get("Title")), QStringLiteral("A-three"));
    QVERIFY(document.getCatalog()->getPage(0)->getDocumentPart() != document.getCatalog()->getPage(1)->getDocumentPart());
    QCOMPARE(document.getCatalog()->getPage(1)->getDocumentPart(), document.getCatalog()->getPage(2)->getDocumentPart());
}

void DocumentMergerArrangeTest::fieldLockLevels()
{
    // 12.7.5.5, RB5 point 3: /Lock /P of a signed signature field (PDF 2.0) restricts the document
    // with the values of DocMDP (Table 257); an unsigned field has not applied its lock, a lock
    // without /P changes no permission; the level is the strictest of DocMDP and the locks
    using Level = PDFDocMDPPermissions::Level;
    struct Case
    {
        const char* name;
        QByteArray fieldEntries;
        QByteArray catalogEntries;
        Level level;
    };
    const Case cases[] = {
        { "p1", "/V 61 0 R /Lock << /Type /SigFieldLock /Action /All /P 1 >>", "", Level::NoChanges },
        { "p2", "/V 61 0 R /Lock << /Type /SigFieldLock /Action /All /P 2 >>", "", Level::FormFilling },
        { "p3", "/V 61 0 R /Lock << /Type /SigFieldLock /Action /All /P 3 >>", "", Level::FormFillingAndAnnotations },
        { "wrong type", "/V 61 0 R /Lock << /Type /SigFieldLock /Action /All /P (1) >>", "", Level::FormFilling },
        { "unknown value", "/V 61 0 R /Lock << /Type /SigFieldLock /Action /All /P 7 >>", "", Level::FormFilling },
        { "no p", "/V 61 0 R /Lock << /Type /SigFieldLock /Action /All >>", "", Level::NotCertified },
        { "unsigned", "/Lock << /Type /SigFieldLock /Action /All /P 1 >>", "", Level::NotCertified },
        { "value of nothing", "/V 99 0 R /Lock << /Type /SigFieldLock /Action /All /P 1 >>", "", Level::NotCertified },
        { "docmdp 3, lock 1", "/V 61 0 R /Lock << /Type /SigFieldLock /Action /All /P 1 >>", "/Perms << /DocMDP 62 0 R >>", Level::NoChanges },
        { "docmdp 1, lock 3", "/V 61 0 R /Lock << /Type /SigFieldLock /Action /All /P 3 >>", "/Perms << /DocMDP 63 0 R >>", Level::NoChanges },
        { "docmdp 3, no lock", "/V 61 0 R", "/Perms << /DocMDP 62 0 R >>", Level::FormFillingAndAnnotations },
    };
    for (const Case& testCase : cases)
    {
        const PDFDocument document = ArrangeTestHelper::createSignedField(testCase.fieldEntries, testCase.catalogEntries);
        QVERIFY2(PDFDocMDPPermissions::getLevel(&document) == testCase.level, testCase.name);
        QCOMPARE(PDFDocMDPPermissions::isPageChangeAllowed(&document), testCase.level == Level::NotCertified);
    }

    // A text field with a lock is no signature field (Table 235: /Lock of a signature field)
    MergeTestPdf textPdf = MergeTestPdf::createPages("T", 1, "/AcroForm << /Fields [60 0 R] >>", { { 1, "/Annots [60 0 R]" } });
    textPdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (t) /V (x) /Rect [0 0 0 0] /P 3 0 R /Lock << /P 1 >> >>");
    const PDFDocument text = textPdf.read();
    QCOMPARE(PDFDocMDPPermissions::getLevel(&text), Level::NotCertified);

    // The type and the value inherited from the parent field (Table 226); a cycle of /Kids and a
    // field shared by two parents end at their second visit
    MergeTestPdf inheritedPdf = MergeTestPdf::createPages("T", 1, "/AcroForm << /Fields [60 0 R 64 0 R] >>", { { 1, "/Annots [61 0 R]" } });
    inheritedPdf.set(60, "<< /FT /Sig /T (s) /V 62 0 R /Kids [61 0 R 60 0 R 63 0 R] >>");
    inheritedPdf.set(61, "<< /Type /Annot /Subtype /Widget /Parent 60 0 R /Rect [0 0 0 0] /P 3 0 R /Lock << /P 2 >> >>");
    inheritedPdf.set(62, "<< /Type /Sig /Filter /Adobe.PPKLite /ByteRange [0 0 0 0] /Contents <00> >>");
    inheritedPdf.set(63, "<< /T (kid) /Kids [60 0 R] >>");
    inheritedPdf.set(64, "<< /T (other) /Kids [63 0 R] >>");
    const PDFDocument inherited = inheritedPdf.read();
    QCOMPARE(PDFDocMDPPermissions::getFieldLockLevel(&inherited), Level::FormFilling);
    QCOMPARE(PDFDocMDPPermissions::getCertificationLevel(&inherited), Level::NotCertified);
    QCOMPARE(PDFDocMDPPermissions::getLevel(&inherited), Level::FormFilling);
    QCOMPARE(PDFDocMDPPermissions::getLevel(nullptr), Level::NotCertified);
}

void DocumentMergerArrangeTest::fieldLockRefusesPageChanges()
{
    // RB5 point 3: a signed field locked with /P 1 to 3 allows no change of the pages as DocMDP -
    // Rotate Pages and Remove Pages of step 3a give a copy without the signatures (Q12); a lock
    // without /P keeps the incremental save
    const PDFDocument locked = ArrangeTestHelper::createSignedField("/V 61 0 R /Lock << /Type /SigFieldLock /Action /All /P 2 >>");
    const PDFDocument unlocked = ArrangeTestHelper::createSignedField("/V 61 0 R /Lock << /Type /SigFieldLock /Action /All >>");

    PDFDocumentToolRotatePages rotate;
    QVariantMap rotation;
    rotation.insert(QStringLiteral("pages"), PDFDocumentToolPageSelection().toVariant());
    rotation.insert(QStringLiteral("rotation"), QStringLiteral("right"));
    const PDFDocumentToolResult rotated = rotate.run(ArrangeTestHelper::context(locked), rotation, nullptr, nullptr);
    QVERIFY2(rotated.isSuccess(), qPrintable(rotated.message));
    QCOMPARE(rotated.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));
    QVERIFY(rotated.data.value(QStringLiteral("certificationRemoved")).toBool());
    QCOMPARE(PDFForm::getSignatureCount(rotated.documents.front().document.data()), 0);

    PDFDocumentToolRemovePages remove;
    PDFDocumentToolPageSelection page2;
    page2.pages = QStringLiteral("2");
    QVariantMap removal;
    removal.insert(QStringLiteral("pages"), page2.toVariant());
    const PDFDocumentToolResult removed = remove.run(ArrangeTestHelper::context(locked), removal, nullptr, nullptr);
    QVERIFY2(removed.isSuccess(), qPrintable(removed.message));
    QCOMPARE(removed.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));

    const PDFDocumentToolResult kept = rotate.run(ArrangeTestHelper::context(unlocked), rotation, nullptr, nullptr);
    QVERIFY2(kept.isSuccess(), qPrintable(kept.message));
    QCOMPARE(kept.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("incremental-save"));
    QCOMPARE(PDFForm::getSignatureCount(kept.documents.front().document.data()), 1);
}

QTEST_GUILESS_MAIN(DocumentMergerArrangeTest)

#include "tst_documentmergerarrangetest.moc"
