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

// PDFDocumentManipulator of the old PageMaster as the adapter of PDFDocumentMerger (plan of step
// 2 of phase 2, 5.1, R3, B1-B4, B13, B14): its API is the same, the merge is the one of the tool
// Merge. The expectations are those of ISO 32000-2 (the clause at the test) and of the bugs of
// the old code, which each test reproduces.

#include "mergetestpdf.h"

#include "pdfdocumentmanipulator.h"
#include "pdfdocumentmerger.h"

#include <QtTest>

#include <cmath>
#include <set>

using namespace pdf;
using mergetest::MergeTestPdf;
using mergetest::MergeTestRead;
using Manipulator = PDFDocumentManipulator;
using Read = MergeTestRead;

class DocumentManipulatorTest : public QObject
{
    Q_OBJECT

private slots:
    void reorderKeepsCatalog();
    void cloneOfPage();
    void unselectedPageOfOneDocument();
    void unitedDocuments();
    void outlineModes();
    void cropMarginsFromCropBox();
    void cropMarginsOfRotatedPage();
    void imagePage();
    void blankPage();
    void permissionsChecked();
    void assembleRepeated();

private:
    static Manipulator::AssembledPage documentPage(const PDFDocument& document, int documentIndex, int pageIndex, PageRotation turn = PageRotation::None);
    static PDFObjectReference pageReference(const PDFDocument& document, size_t index) { return document.getCatalog()->getPage(index)->getPageReference(); }
    static bool isClose(const QRectF& left, const QRectF& right) { return std::abs(left.left() - right.left()) < 0.01 && std::abs(left.top() - right.top()) < 0.01 && std::abs(left.width() - right.width()) < 0.01 && std::abs(left.height() - right.height()) < 0.01; }
};

Manipulator::AssembledPage DocumentManipulatorTest::documentPage(const PDFDocument& document, int documentIndex, int pageIndex, PageRotation turn)
{
    // As PageItemModel::createAssembledPage: the rotation of the page combined with its turn
    const PDFPage* page = document.getCatalog()->getPage(size_t(pageIndex));
    Manipulator::AssembledPage assembledPage = Manipulator::createDocumentPage(documentIndex, pageIndex, page->getRotatedMediaBoxMM().size(), getPageRotationCombined(page->getPageRotation(), turn));
    assembledPage.sourcePageRotation = page->getPageRotation();
    return assembledPage;
}

void DocumentManipulatorTest::reorderKeepsCatalog()
{
    // R3: reordering one document keeps its form, its scripts and its structure (the old
    // PageMaster kept the catalog of one document)
    MergeTestPdf a = MergeTestPdf::createPages("A", 3, "/AcroForm << /Fields [30 0 R] >> /Names << /JavaScript << /Names [(init) << /S /JavaScript /JS (SCRIPTMARK) >>] >> >> /StructTreeRoot 50 0 R",
                                               { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    a.set(50, "<< /Type /StructTreeRoot /K 51 0 R >>");
    a.set(51, "<< /Type /StructElem /S /Document /P 50 0 R >>");
    const PDFDocument document = a.read();

    Manipulator manipulator;
    manipulator.addDocument(0, &document);
    QVERIFY(manipulator.assemble({ documentPage(document, 0, 2), documentPage(document, 0, 0, PageRotation::Rotate90), documentPage(document, 0, 1) }));
    const PDFDocument result = MergeTestPdf::roundTrip(manipulator.getAssembledDocument());

    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));
    const QByteArray contents = Read::pageContents(result);
    QVERIFY(contents.indexOf("APAGE3") < contents.indexOf("APAGE1"));
    QVERIFY(contents.indexOf("APAGE1") < contents.indexOf("APAGE2"));
    QCOMPARE(result.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate90);
    QVERIFY(Read::catalog(result)->hasKey("StructTreeRoot"));
    QCOMPARE(Read::items(result, Read::path(result, { "AcroForm" })->get("Fields")).size(), size_t(1));
    QVERIFY(MergeTestPdf::writeDocument(result).contains("SCRIPTMARK"));
}

void DocumentManipulatorTest::cloneOfPage()
{
    // B4: a page twice - the second is a new page object with its own annotations; a field of a
    // merged widget is split into a field and two widgets (R8, 12.7.4.1)
    MergeTestPdf a = MergeTestPdf::createPages("A", 2, "/AcroForm << /Fields [30 0 R] >>", { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (value) /Rect [0 0 10 10] /P 3 0 R >>");
    const PDFDocument document = a.read();

    Manipulator manipulator;
    manipulator.addDocument(0, &document);
    QVERIFY(manipulator.assemble({ documentPage(document, 0, 0), documentPage(document, 0, 1), documentPage(document, 0, 0) }));
    const PDFDocument result = MergeTestPdf::roundTrip(manipulator.getAssembledDocument());
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));
    QVERIFY(pageReference(result, 0) != pageReference(result, 2));
    QCOMPARE(Read::count(MergeTestPdf::writeDocument(result), "APAGE1"), 1);

    const std::vector<PDFObject> fields = Read::items(result, Read::path(result, { "AcroForm" })->get("Fields"));
    QCOMPARE(fields.size(), size_t(1));
    const PDFDictionary* field = Read::dictionary(result, fields.front());
    QCOMPARE(Read::text(result, field->get("T")), QStringLiteral("Name"));
    QCOMPARE(Read::text(result, field->get("V")), QStringLiteral("value"));
    const std::vector<PDFObject> widgets = Read::items(result, field->get("Kids"));
    QCOMPARE(widgets.size(), size_t(2));
    std::set<PDFObjectReference> pages;
    for (const PDFObject& widget : widgets)
    {
        const PDFDictionary* dictionary = Read::dictionary(result, widget);
        QCOMPARE(Read::dictionary(result, dictionary->get("Parent")), field);
        QVERIFY(!dictionary->hasKey("T"));
        pages.insert(dictionary->get("P").getReference());
    }
    QCOMPARE(pages, std::set<PDFObjectReference>({ pageReference(result, 0), pageReference(result, 2) }));
}

void DocumentManipulatorTest::unselectedPageOfOneDocument()
{
    // B3, B7, R4: a page of one document, which is not taken, is gone with its destination, the
    // widget of its field, the link to it and the outline item of the second level
    MergeTestPdf a = MergeTestPdf::createPages("A", 3, "/Names << /Dests << /Names [(toThree) [5 0 R /Fit]] >> >> /AcroForm << /Fields [30 0 R] >> /Outlines 40 0 R",
                                               { { 1, "/Annots [20 0 R 31 0 R]" }, { 3, "/Annots [32 0 R]" } });
    a.set(20, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [5 0 R /Fit] >>");
    a.set(30, "<< /FT /Tx /T (f) /Kids [31 0 R 32 0 R] >>");
    a.set(31, "<< /Type /Annot /Subtype /Widget /Rect [0 0 10 10] /Parent 30 0 R /P 3 0 R >>");
    a.set(32, "<< /Type /Annot /Subtype /Widget /Rect [0 0 10 10] /Parent 30 0 R /P 5 0 R >>");
    a.set(40, "<< /Type /Outlines /First 41 0 R /Last 41 0 R /Count 2 >>");
    a.set(41, "<< /Title (Top) /Parent 40 0 R /Dest [3 0 R /Fit] /First 42 0 R /Last 42 0 R /Count 1 >>");
    a.set(42, "<< /Title (Deep) /Parent 41 0 R /Dest [5 0 R /Fit] >>");
    const PDFDocument document = a.read();

    Manipulator manipulator;
    manipulator.addDocument(0, &document);
    QVERIFY(manipulator.assemble({ documentPage(document, 0, 1), documentPage(document, 0, 0) }));
    const PDFDocument result = MergeTestPdf::roundTrip(manipulator.getAssembledDocument());
    const QByteArray bytes = MergeTestPdf::writeDocument(result);
    QVERIFY(!bytes.contains("APAGE3"));
    QVERIFY(!bytes.contains("Deep"));
    QVERIFY(!bytes.contains("toThree"));
    QCOMPARE(Read::items(result, Read::dictionary(result, Read::items(result, Read::path(result, { "AcroForm" })->get("Fields")).front())->get("Kids")).size(), size_t(1));
}

void DocumentManipulatorTest::unitedDocuments()
{
    // B1, B2: the forms, the named destinations and the layers of two documents are in the result
    // (the old merge never wrote them); the same document twice is two inputs (R20)
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/Names << /Dests << /Names [(intro) [3 0 R /Fit]] >> >> /AcroForm << /Fields [30 0 R] >> /OCProperties << /OCGs [20 0 R] /D << >> >>",
                                               { { 1, "/Annots [30 0 R]" } });
    a.set(20, "<< /Type /OCG /Name (Layer) >>");
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    const PDFDocument document = a.read();
    const PDFDocument other = MergeTestPdf::createPages("B", 1).read();

    Manipulator manipulator;
    manipulator.setOutlineMode(Manipulator::OutlineMode::NoOutline);
    manipulator.addDocument(1, &document);
    manipulator.addDocument(2, &other);
    manipulator.addDocument(3, &document);
    QVERIFY(manipulator.assemble({ documentPage(document, 1, 0), documentPage(other, 2, 0), documentPage(document, 3, 0) }));
    const PDFDocument result = MergeTestPdf::roundTrip(manipulator.getAssembledDocument());

    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));
    QCOMPARE(Read::items(result, Read::path(result, { "AcroForm" })->get("Fields")).size(), size_t(2));
    QCOMPARE(Read::items(result, Read::path(result, { "OCProperties" })->get("OCGs")).size(), size_t(2));
    const std::vector<PDFObject> pairs = Read::items(result, Read::path(result, { "Names", "Dests" })->get("Names"));
    QCOMPARE(pairs.size(), size_t(4));
    QCOMPARE(Read::object(result, pairs[2]).getString(), QByteArray("intro_2"));
    QCOMPARE(Read::items(result, Read::object(result, pairs[3])).front().getReference(), pageReference(result, 2));
}

void DocumentManipulatorTest::outlineModes()
{
    // The three modes of PageMaster: Document parts (an item and a part per document, PDF 2.0),
    // Join (the outline of a document under its item), No outline (nothing)
    MergeTestPdf a = MergeTestPdf::createPages("A", 2, "/Outlines 20 0 R");
    a.set(20, "<< /Type /Outlines /First 21 0 R /Last 21 0 R /Count 1 >>");
    a.set(21, "<< /Title (Chapter) /Parent 20 0 R /Dest [4 0 R /Fit] >>");
    const PDFDocument document = a.read();
    const PDFDocument other = MergeTestPdf::createPages("B", 1).read();

    for (const Manipulator::OutlineMode mode : { Manipulator::OutlineMode::NoOutline, Manipulator::OutlineMode::Join, Manipulator::OutlineMode::DocumentParts })
    {
        Manipulator manipulator;
        manipulator.setOutlineMode(mode);
        manipulator.addDocument(0, &document);
        manipulator.addDocument(1, &other);
        QVERIFY(manipulator.assemble({ documentPage(document, 0, 0), documentPage(document, 0, 1), documentPage(other, 1, 0) }));
        const PDFDocument result = MergeTestPdf::roundTrip(manipulator.getAssembledDocument());
        const PDFDictionary* outline = Read::path(result, { "Outlines" });
        if (mode == Manipulator::OutlineMode::NoOutline)
        {
            QVERIFY(!outline);
            QVERIFY(!Read::catalog(result)->hasKey("DPartRoot"));
            continue;
        }

        QVERIFY(outline);
        QVERIFY(Read::catalog(result)->hasKey("DPartRoot"));
        QCOMPARE(result.getInfo()->version, PDFVersion(2, 0));
        const PDFDictionary* first = Read::dictionary(result, outline->get("First"));
        QCOMPARE(Read::text(result, first->get("Title")), QStringLiteral("Document 1"));
        QCOMPARE(Read::text(result, Read::dictionary(result, first->get("Next"))->get("Title")), QStringLiteral("Document 2"));
        if (mode == Manipulator::OutlineMode::Join)
        {
            const PDFDictionary* chapter = Read::dictionary(result, first->get("First"));
            QVERIFY(chapter);
            QCOMPARE(Read::text(result, chapter->get("Title")), QStringLiteral("Chapter"));
            QCOMPARE(Read::items(result, chapter->get("Dest")).front().getReference(), pageReference(result, 1));
        }
        else
        {
            QVERIFY(!first->hasKey("First"));
        }
    }
}

void DocumentManipulatorTest::cropMarginsFromCropBox()
{
    // B14 (7.7.3.3 Table 31): the margins of PageMaster are cut from the crop box with its origin;
    // the old code put the crop box at the origin of the page (0, 0)
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, QByteArray(), { { 1, "/MediaBox [0 0 600 800] /CropBox [100 100 400 500]" } });
    const PDFDocument document = a.read();

    Manipulator::AssembledPage page = documentPage(document, 0, 0);
    page.cropMarginsMM = QMarginsF(10.0, 20.0, 5.0, 15.0);
    Manipulator manipulator;
    manipulator.addDocument(0, &document);
    QVERIFY(manipulator.assemble({ page }));
    const PDFDocument result = MergeTestPdf::roundTrip(manipulator.getAssembledDocument());

    const double mm = 72.0 / 25.4;
    const QRectF expected(100.0 + 10.0 * mm, 100.0 + 15.0 * mm, 300.0 - 15.0 * mm, 400.0 - 35.0 * mm);
    QVERIFY2(isClose(result.getCatalog()->getPage(0)->getCropBox(), expected), qPrintable(QStringLiteral("%1 %2 %3 %4").arg(result.getCatalog()->getPage(0)->getCropBox().left()).arg(result.getCatalog()->getPage(0)->getCropBox().top()).arg(result.getCatalog()->getPage(0)->getCropBox().width()).arg(result.getCatalog()->getPage(0)->getCropBox().height())));
}

void DocumentManipulatorTest::cropMarginsOfRotatedPage()
{
    // The margins are as the page is shown: on a page of /Rotate 90 the left margin of the view is
    // the bottom of the unrotated page (the mapping of PageMaster)
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, QByteArray(), { { 1, "/Rotate 90" } });
    const PDFDocument document = a.read();

    Manipulator::AssembledPage page = documentPage(document, 0, 0);
    page.cropMarginsMM = QMarginsF(10.0, 0.0, 0.0, 0.0);
    Manipulator manipulator;
    manipulator.addDocument(0, &document);
    QVERIFY(manipulator.assemble({ page }));
    const PDFDocument result = MergeTestPdf::roundTrip(manipulator.getAssembledDocument());
    const QRectF cropBox = result.getCatalog()->getPage(0)->getCropBox();
    const double mm = 72.0 / 25.4;
    QVERIFY(isClose(cropBox, QRectF(0.0, 10.0 * mm, 600.0, 800.0 - 10.0 * mm)));
    QCOMPARE(result.getCatalog()->getPage(0)->getPageRotation(), PageRotation::Rotate90);
}

void DocumentManipulatorTest::imagePage()
{
    // An image page of PageMaster: the size of its item in millimeters, the image as an XObject;
    // its crop margins cut the image (the page shows the rest)
    QImage image(200, 100, QImage::Format_RGB32);
    image.fill(Qt::red);

    Manipulator manipulator;
    manipulator.addImage(0, image);
    Manipulator::AssembledPage page = Manipulator::createImagePage(0, QSizeF(100.0, 50.0), PageRotation::None);
    page.sourcePageSize = QSizeF(200.0, 100.0);
    page.cropMarginsMM = QMarginsF(50.0, 0.0, 50.0, 0.0);
    page.pageSize = QSizeF(100.0, 100.0);
    QVERIFY(manipulator.assemble({ page, Manipulator::createImagePage(0, QSizeF(20.0, 10.0), PageRotation::Rotate90) }));
    const PDFDocument result = MergeTestPdf::roundTrip(manipulator.getAssembledDocument());

    QCOMPARE(result.getCatalog()->getPageCount(), size_t(2));
    const double mm = 72.0 / 25.4;
    QVERIFY(isClose(result.getCatalog()->getPage(0)->getMediaBox(), QRectF(0.0, 0.0, 100.0 * mm, 100.0 * mm)));
    QVERIFY(isClose(result.getCatalog()->getPage(1)->getMediaBox(), QRectF(0.0, 0.0, 20.0 * mm, 10.0 * mm)));
    QCOMPARE(result.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate90);

    // The middle half of the image fills the page: the image is twice the width of the page
    const PDFDictionary* dictionary = Read::dictionary(result, PDFObject::createReference(pageReference(result, 0)));
    const QByteArray content = *Read::object(result, dictionary->get("Contents")).getStream()->getContent();
    const QList<QByteArray> tokens = content.simplified().split(' ');
    const qsizetype cm = tokens.indexOf("cm");
    QVERIFY(cm >= 6);
    QVERIFY(std::abs(tokens[cm - 6].toDouble() - 200.0 * mm) < 0.01);
    QVERIFY(std::abs(tokens[cm - 2].toDouble() + 50.0 * mm) < 0.01);
}

void DocumentManipulatorTest::blankPage()
{
    // A blank page of PageMaster: its size in millimeters, its rotation
    const PDFDocument document = MergeTestPdf::createPages("A", 1).read();
    Manipulator manipulator;
    manipulator.addDocument(0, &document);
    QVERIFY(manipulator.assemble({ documentPage(document, 0, 0), Manipulator::createBlankPage(QSizeF(210.0, 297.0), PageRotation::Rotate270) }));
    const PDFDocument result = MergeTestPdf::roundTrip(manipulator.getAssembledDocument());
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(2));
    QVERIFY(isClose(result.getCatalog()->getPage(1)->getMediaBox(), QRectF(0.0, 0.0, 595.276, 841.890)));
    QCOMPARE(result.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate270);
}

void DocumentManipulatorTest::permissionsChecked()
{
    // R2, B13: the merge refuses a document without its permission for every caller - PageMaster
    // checked only when a file was added, never when a project was loaded
    using Permission = PDFSecurityHandler::Permission;
    const PDFDocument plain = MergeTestPdf::createPages("A", 1).read();
    const PDFDocument locked = MergeTestPdf::readBytes(MergeTestPdf::encrypt(plain, QString(), QStringLiteral("owner"), uint32_t(Permission::PrintLowResolution) | uint32_t(Permission::Modify)));
    QCOMPARE(PDFDocumentMerger::getPermission(&locked), PDFDocumentMerger::Permission::AssembleDenied);

    Manipulator manipulator;
    manipulator.addDocument(0, &locked);
    const PDFOperationResult result = manipulator.assemble({ documentPage(locked, 0, 0) });
    QVERIFY(!result);
    QVERIFY(!result.getErrorMessage().isEmpty());
}

void DocumentManipulatorTest::assembleRepeated()
{
    // Split of PageMaster: one manipulator assembles more documents one after another
    const PDFDocument document = MergeTestPdf::createPages("A", 3).read();
    Manipulator manipulator;
    manipulator.addDocument(0, &document);
    for (int i = 0; i < 3; ++i)
    {
        QVERIFY(manipulator.assemble({ documentPage(document, 0, i) }));
        const PDFDocument part = MergeTestPdf::roundTrip(manipulator.takeAssembledDocument());
        QCOMPARE(part.getCatalog()->getPageCount(), size_t(1));
        QVERIFY(Read::pageContents(part).contains("APAGE" + QByteArray::number(i + 1)));
    }
    QVERIFY(!manipulator.assemble({}));
}

QTEST_GUILESS_MAIN(DocumentManipulatorTest)

#include "tst_documentmanipulatortest.moc"
