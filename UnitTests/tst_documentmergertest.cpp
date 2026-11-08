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

// The core of Merge and Blank PDF (PDFDocumentMerger, PDFImagePageFactory; plan of step 2 of
// phase 2, chapters 5 and 6, R1-R10, R15, owner answers Q1-Q7). Every expectation comes from
// ISO 32000-2 (the clause is named at the test) or from the decisions of the plan; the result
// is always written and read again, as a reader of the saved file sees it. The bugs of the old
// merge (B1-B16 of R5) have each a test, which fails with the old behavior.

#include "mergetestpdf.h"

#include "pdfdocumentmerger.h"
#include "pdfexception.h"
#include "pdfimagepagefactory.h"
#include "pdfnametreeloader.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfprogress.h"

#include <QBuffer>
#include <QColorSpace>
#include <QImageReader>
#include <QImageWriter>
#include <QtTest>

#include <atomic>
#include <cmath>
#include <set>
#include <thread>

using namespace pdf;
using mergetest::MergeTestPdf;
using mergetest::MergeTestRead;
using Merger = PDFDocumentMerger;
using Read = MergeTestRead;

/// Cancellation of a merge by a flag
class MergerTestControl : public PDFOperationControl
{
public:
    virtual bool isOperationCancelled() const override { return m_isCanceled.load(); }
    void cancel() { m_isCanceled = true; }

private:
    std::atomic_bool m_isCanceled = false;
};

/// Progress, which counts its steps (from any thread)
class MergerTestProgress : public PDFProgressSink
{
public:
    virtual void start(size_t stepCount, ProgressStartupInfo) override { m_stepCount = stepCount; }
    virtual void step() override { ++m_steps; }
    virtual void finish() override { m_isFinished = true; }

    std::atomic<size_t> m_stepCount = 0;
    std::atomic<size_t> m_steps = 0;
    std::atomic_bool m_isFinished = false;
};

class DocumentMergerTest : public QObject
{
    Q_OBJECT

private slots:
    void pageTreeInheritedAttributes();
    void crossReferenceStreamInput();
    void unselectedPagesNeverInResult();
    void cloneCopiesOnlyThePage();
    void namedDestinationsJoined();
    void formFieldsJoined();
    void formFieldsOfManyInputs();
    void formFieldNamesOfOneInput();
    void formResourcesAndAppearances();
    void clonedWidgetOfFieldKid();
    void signaturesRemoved();
    void layersKeepTheirStates_data();
    void layersKeepTheirStates();
    void structureTreeJoined();
    void structureTreeRemovedUnlessAllTagged();
    void structureHostileRoot();
    void structureRoleMapCollision();
    void pageLabelsKept();
    void outlineDocumentParts();
    void outlineRunsOfMovedPages();
    void outlineJoin();
    void outlineHostile();
    void noOutline();
    void attachmentsAndArticles();
    void documentScriptsRemoved();
    void outputIntents();
    void metadataAndIdentifier();
    void relativeUris();
    void extensionsAndViewerPreferences();
    void permissions();
    void singleDocumentKeepsCatalog();
    void singleDocumentWithImages_data();
    void singleDocumentWithImages();
    void singleDocumentWithOmittedPages();
    void sameDocumentTwice();
    void dynamicXfaRefused();
    void hostileFieldTree();
    void leftOutInput();
    void cancellationAndProgress();
    void parallelMerges();
    void blankPages();
    void imageJpegUnchanged();
    void imageJpegOrientation();
    void imageJpegCmyk();
    void imageAlphaAndProfile();
    void imageResolution();
    void imageLimits();
    void imageTiffFrames();
    void imageFrameCompacted();
    void imageTiffFramesInOnePass();

private:
    static Merger::Result mergeAll(const std::vector<PDFDocumentPointer>& documents, Merger::OutlineMode mode = Merger::OutlineMode::DocumentParts, const QStringList& names = QStringList());
    static std::vector<Merger::Item> allPages(const std::vector<Merger::Source>& sources);
    static PDFObjectReference pageReference(const PDFDocument& document, size_t index) { return document.getCatalog()->getPage(index)->getPageReference(); }
    static PDFDocument check(const Merger::Result& result);
    static void checkOutlineLinks(const PDFDocument& document, const PDFObject& parent, int depth);
    static PDFObjectReference resolveDestination(const PDFDocument& document, const PDFObject& destination);
    static QByteArray encodeImage(const QImage& image, const char* format, QImageIOHandler::Transformations transformation = QImageIOHandler::TransformationNone);
    static QByteArray createTiff(int frameCount);
    static const PDFDictionary* getPageImage(const PDFDocument& document, size_t pageIndex);
};

Merger::Result DocumentMergerTest::mergeAll(const std::vector<PDFDocumentPointer>& documents, Merger::OutlineMode mode, const QStringList& names)
{
    std::vector<Merger::Source> sources;
    for (size_t i = 0; i < documents.size(); ++i)
    {
        sources.push_back(Merger::Source::createDocument(documents[i], i < size_t(names.size()) ? names[int(i)] : QString()));
    }
    Merger::Settings settings;
    settings.outline = mode;
    return Merger::merge(sources, allPages(sources), settings, nullptr, nullptr);
}

std::vector<Merger::Item> DocumentMergerTest::allPages(const std::vector<Merger::Source>& sources)
{
    std::vector<Merger::Item> items;
    for (size_t source = 0; source < sources.size(); ++source)
    {
        const size_t count = sources[source].document ? sources[source].document->getCatalog()->getPageCount() : 1;
        for (size_t i = 0; i < count; ++i)
        {
            Merger::Item item;
            item.source = source;
            item.pageIndex = PDFInteger(i);
            items.push_back(item);
        }
    }
    return items;
}

PDFDocument DocumentMergerTest::check(const Merger::Result& result)
{
    if (!result.isSuccess)
    {
        qWarning() << "The merge failed:" << result.errorMessage;
        return PDFDocument();
    }
    return MergeTestPdf::roundTrip(result.document);
}

PDFObjectReference DocumentMergerTest::resolveDestination(const PDFDocument& document, const PDFObject& destination)
{
    // 12.3.2.3: a string names the tree /Names /Dests
    const PDFObject& dereferenced = document.getObject(destination);
    if (dereferenced.isString())
    {
        const PDFDictionary* names = Read::path(document, { "Names" });
        const auto entries = PDFNameTreeLoader<PDFObject>::parse(&document.getStorage(), names ? names->get("Dests") : PDFObject(), [](const PDFObjectStorage*, const PDFObject& object) { return object; });
        auto it = entries.find(dereferenced.getString());
        return it != entries.end() ? resolveDestination(document, it->second) : PDFObjectReference();
    }
    if (dereferenced.isArray() && dereferenced.getArray()->getCount() > 0 && dereferenced.getArray()->getItem(0).isReference())
    {
        return dereferenced.getArray()->getItem(0).getReference();
    }
    if (dereferenced.isDictionary())
    {
        return resolveDestination(document, dereferenced.getDictionary()->get("D"));
    }
    return PDFObjectReference();
}

void DocumentMergerTest::checkOutlineLinks(const PDFDocument& document, const PDFObject& parent, int depth)
{
    // Table 151 and 152: /First, /Last, /Prev, /Next and /Parent are consistent, /Count of an open
    // item is the number of its visible descendants, of a closed one its negative number
    QVERIFY(depth < 64);
    const PDFDictionary* parentDictionary = Read::dictionary(document, parent);
    QVERIFY(parentDictionary);
    std::vector<PDFObject> children;
    PDFObject current = parentDictionary->get("First");
    PDFObject previous;
    while (current.isReference())
    {
        const PDFDictionary* item = Read::dictionary(document, current);
        QVERIFY(item);
        QCOMPARE(item->get("Parent"), parent);
        QCOMPARE(item->get("Prev"), previous);
        children.push_back(current);
        QVERIFY(children.size() < 10000);
        previous = current;
        current = item->get("Next");
    }
    QCOMPARE(parentDictionary->get("Last"), children.empty() ? PDFObject() : children.back());

    PDFInteger visible = 0;
    for (const PDFObject& child : children)
    {
        checkOutlineLinks(document, child, depth + 1);
        const PDFInteger count = Read::integer(document, Read::dictionary(document, child)->get("Count"), 0);
        visible += 1 + qMax<PDFInteger>(0, count);
    }
    if (!children.empty())
    {
        QCOMPARE(std::abs(Read::integer(document, parentDictionary->get("Count"), 0)), visible);
    }
}

QByteArray DocumentMergerTest::encodeImage(const QImage& image, const char* format, QImageIOHandler::Transformations transformation)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QBuffer::WriteOnly);
    QImageWriter writer(&buffer, format);
    writer.setTransformation(transformation);
    if (!writer.write(image))
    {
        qWarning() << "The test image cannot be written:" << writer.errorString();
    }
    return data;
}

QByteArray DocumentMergerTest::createTiff(int frameCount)
{
    // Baseline TIFF 6.0, little endian: each frame is one 1 x 1 gray pixel without compression,
    // the frames are a chain of image file directories (section 2, "Multiple images per file")
    auto add16 = [](QByteArray& data, quint16 value) { data.append(char(value & 0xFF)); data.append(char(value >> 8)); };
    auto add32 = [](QByteArray& data, quint32 value) { for (int i = 0; i < 4; ++i) { data.append(char((value >> (8 * i)) & 0xFF)); } };

    QByteArray data("II*\0", 4);
    add32(data, 8);
    for (int frame = 0; frame < frameCount; ++frame)
    {
        const quint32 directory = quint32(data.size());
        const quint16 entryCount = 9;
        const quint32 pixel = directory + 2 + entryCount * 12 + 4;
        auto entry = [&](quint16 tag, quint16 type, quint32 value)
        {
            add16(data, tag);
            add16(data, type);
            add32(data, 1);
            if (type == 3)
            {
                add16(data, quint16(value));
                add16(data, 0);
            }
            else
            {
                add32(data, value);
            }
        };
        add16(data, entryCount);
        entry(256, 4, 1);           // ImageWidth
        entry(257, 4, 1);           // ImageLength
        entry(258, 3, 8);           // BitsPerSample
        entry(259, 3, 1);           // Compression: none
        entry(262, 3, 1);           // PhotometricInterpretation: black is zero
        entry(273, 4, pixel);       // StripOffsets
        entry(277, 3, 1);           // SamplesPerPixel
        entry(278, 4, 1);           // RowsPerStrip
        entry(279, 4, 1);           // StripByteCounts
        add32(data, frame + 1 < frameCount ? pixel + 1 + ((pixel + 1) & 1) : 0);
        data.append(char(frame % 256));
        if (data.size() & 1)
        {
            data.append('\0');
        }
    }
    return data;
}

const PDFDictionary* DocumentMergerTest::getPageImage(const PDFDocument& document, size_t pageIndex)
{
    const PDFDictionary* page = Read::dictionary(document, PDFObject::createReference(pageReference(document, pageIndex)));
    const PDFDictionary* resources = page ? Read::dictionary(document, page->get("Resources")) : nullptr;
    const PDFDictionary* xObjects = resources ? Read::dictionary(document, resources->get("XObject")) : nullptr;
    return xObjects && xObjects->getCount() == 1 ? Read::dictionary(document, xObjects->getValue(0)) : nullptr;
}

void DocumentMergerTest::pageTreeInheritedAttributes()
{
    // 7.7.3.3, 7.7.3.4, Table 31: MediaBox, Resources and Rotate are inheritable; a page of the
    // result has them itself, and the result has one flat page tree
    MergeTestPdf a;
    a.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    a.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 2 /MediaBox [0 0 300 400] /Resources << /ProcSet [/PDF] >> >>");
    a.set(3, "<< /Type /Pages /Parent 2 0 R /Kids [4 0 R 5 0 R] /Count 2 /Rotate 90 >>");
    a.set(4, "<< /Type /Page /Parent 3 0 R /Contents 101 0 R >>");
    a.set(5, "<< /Type /Page /Parent 3 0 R /MediaBox [0 0 100 200] /Contents 102 0 R >>");
    a.set(101, MergeTestPdf::stream("% APAGE1"));
    a.set(102, MergeTestPdf::stream("% APAGE2"));
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();

    const PDFDocument result = check(mergeAll({ a.readPointer(), b }));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));

    const PDFDictionary* root = Read::path(result, { "Pages" });
    QVERIFY(root);
    QCOMPARE(Read::integer(result, root->get("Count")), 3);
    for (size_t i = 0; i < 3; ++i)
    {
        const PDFDictionary* page = Read::dictionary(result, PDFObject::createReference(pageReference(result, i)));
        QCOMPARE(Read::name(result, page->get("Type")), QByteArray("Page"));
        QCOMPARE(Read::dictionary(result, page->get("Parent")), root);
        QVERIFY(page->hasKey("MediaBox"));
        QVERIFY(page->hasKey("Resources"));
    }

    const PDFDictionary* first = Read::dictionary(result, PDFObject::createReference(pageReference(result, 0)));
    QCOMPARE(result.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 300, 400));
    QCOMPARE(Read::integer(result, first->get("Rotate")), 90);
    QVERIFY(Read::dictionary(result, first->get("Resources"))->hasKey("ProcSet"));
    QCOMPARE(result.getCatalog()->getPage(1)->getMediaBox(), QRectF(0, 0, 100, 200));
    QCOMPARE(result.getCatalog()->getPage(1)->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(result.getCatalog()->getPage(2)->getMediaBox(), QRectF(0, 0, 600, 800));
    QCOMPARE(result.getCatalog()->getPage(2)->getPageRotation(), PageRotation::None);
}

void DocumentMergerTest::crossReferenceStreamInput()
{
    // 7.5.8: the trailer of a file with a cross-reference stream is the dictionary of the stream;
    // its catalog (here its layers and a form) is joined as of any other file (found by the corpus)
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/OCProperties << /OCGs [20 0 R] /D << /OFF [20 0 R] >> >> /AcroForm << /Fields [30 0 R] >>", { { 1, "/Annots [30 0 R]" } });
    a.set(20, "<< /Type /OCG /Name (Stream layer) >>");
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    const PDFDocument input = MergeTestPdf::readBytes(a.writeWithCrossReferenceStream());
    QCOMPARE(input.getCatalog()->getPageCount(), size_t(1));
    QVERIFY(Read::catalog(input));

    const Merger::Result merged = mergeAll({ PDFDocumentPointer(new PDFDocument(input)), MergeTestPdf::createPages("B", 1).readPointer() });
    QVERIFY2(merged.isSuccess, qPrintable(merged.errorMessage));
    const PDFDocument result = check(merged);
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(2));
    const PDFDictionary* configuration = Read::path(result, { "OCProperties", "D" });
    QVERIFY(configuration);
    QCOMPARE(Read::items(result, configuration->get("OFF")).size(), size_t(1));
    QCOMPARE(Read::items(result, Read::path(result, { "AcroForm" })->get("Fields")).size(), size_t(1));
}

void DocumentMergerTest::unselectedPagesNeverInResult()
{
    // B3, R4: a page, which is not taken, reaches the result through nothing - not through a link,
    // a named destination, a field with widgets on both pages, an item of the second level of the
    // outline (7.7.3.2, 12.3.2.2); the old merge copied it with the whole source through /Parent
    MergeTestPdf a = MergeTestPdf::createPages("A", 3, "/Names << /Dests 60 0 R >> /AcroForm << /Fields [30 0 R] >> /Outlines 40 0 R",
                                               { { 1, "/Annots [20 0 R 31 0 R]" }, { 3, "/Annots [32 0 R]" } });
    a.set(20, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest [5 0 R /Fit] >>");
    a.set(30, "<< /FT /Tx /T (f) /Kids [31 0 R 32 0 R] >>");
    a.set(31, "<< /Type /Annot /Subtype /Widget /Rect [0 0 10 10] /Parent 30 0 R /P 3 0 R >>");
    a.set(32, "<< /Type /Annot /Subtype /Widget /Rect [0 0 10 10] /Parent 30 0 R /P 5 0 R >>");
    a.set(40, "<< /Type /Outlines /First 41 0 R /Last 41 0 R /Count 2 >>");
    a.set(41, "<< /Title (Top) /Parent 40 0 R /Dest [3 0 R /Fit] /First 42 0 R /Last 42 0 R /Count 1 >>");
    a.set(42, "<< /Title (Deep) /Parent 41 0 R /Dest [5 0 R /Fit] >>");
    a.set(60, "<< /Names [(toThree) [5 0 R /Fit]] >>");
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();

    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a.readPointer(), QStringLiteral("A.pdf")), Merger::Source::createDocument(b, QStringLiteral("B.pdf")) };
    std::vector<Merger::Item> items(3);
    items[0].source = 0;
    items[0].pageIndex = 0;
    items[1].source = 0;
    items[1].pageIndex = 1;
    items[2].source = 1;
    items[2].pageIndex = 0;
    Merger::Settings settings;
    settings.outline = Merger::OutlineMode::Join;
    const Merger::Result merged = Merger::merge(sources, items, settings, nullptr, nullptr);
    const PDFDocument result = check(merged);
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));

    const QByteArray bytes = MergeTestPdf::writeDocument(result);
    QVERIFY(bytes.contains("APAGE1"));
    QVERIFY(bytes.contains("APAGE2"));
    QVERIFY(!bytes.contains("APAGE3"));
    QVERIFY(!bytes.contains("Deep"));
    QVERIFY(!bytes.contains("toThree"));

    // The field keeps the widget of the page, which stays
    const PDFDictionary* form = Read::path(result, { "AcroForm" });
    QVERIFY(form);
    const std::vector<PDFObject> fields = Read::items(result, form->get("Fields"));
    QCOMPARE(fields.size(), size_t(1));
    QCOMPARE(Read::items(result, Read::dictionary(result, fields.front())->get("Kids")).size(), size_t(1));

    // The first page lost the link to the removed page
    const PDFDictionary* firstPage = Read::dictionary(result, PDFObject::createReference(pageReference(result, 0)));
    for (const PDFObject& annotation : Read::items(result, firstPage->get("Annots")))
    {
        QVERIFY(Read::name(result, Read::dictionary(result, annotation)->get("Subtype")) != "Link");
    }
}

void DocumentMergerTest::cloneCopiesOnlyThePage()
{
    // B4: a further occurrence of a page is a copy of the page object and of its annotations, the
    // content is shared; the old clone copied the whole source again through /Parent
    MergeTestPdf a = MergeTestPdf::createPages("A", 2, QByteArray(), { { 1, "/Annots [20 0 R 21 0 R] /StructParents 0" } });
    a.set(20, "<< /Type /Annot /Subtype /Text /Rect [0 0 10 10] /Contents (note) /Popup 21 0 R /P 3 0 R /StructParent 1 >>");
    a.set(21, "<< /Type /Annot /Subtype /Popup /Rect [0 0 50 50] /Parent 20 0 R /P 3 0 R >>");
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();

    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a.readPointer(), QString()), Merger::Source::createDocument(b, QString()) };
    std::vector<Merger::Item> items = allPages(sources);
    Merger::Item clone;
    clone.source = 0;
    clone.pageIndex = 0;
    clone.turn = PageRotation::Rotate180;
    items.push_back(clone);
    const PDFDocument result = check(Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(4));

    const QByteArray bytes = MergeTestPdf::writeDocument(result);
    QCOMPARE(Read::count(bytes, "APAGE1"), 1);
    QCOMPARE(Read::count(bytes, "APAGE2"), 1);
    QCOMPARE(Read::count(bytes, "BPAGE1"), 1);

    const PDFObjectReference originalReference = pageReference(result, 0);
    const PDFObjectReference cloneReference = pageReference(result, 3);
    QVERIFY(originalReference != cloneReference);
    const PDFDictionary* original = Read::dictionary(result, PDFObject::createReference(originalReference));
    const PDFDictionary* cloned = Read::dictionary(result, PDFObject::createReference(cloneReference));
    QCOMPARE(cloned->get("Contents"), original->get("Contents"));
    QCOMPARE(result.getCatalog()->getPage(3)->getPageRotation(), PageRotation::Rotate180);
    QCOMPARE(result.getCatalog()->getPage(0)->getPageRotation(), PageRotation::None);

    // R9: the clone is content outside of the structure
    QVERIFY(!cloned->hasKey("StructParents"));

    // Its own annotations, the popup pair kept inside of the clone (12.5.6.14, SECURITY_TODO 30)
    const std::vector<PDFObject> annotations = Read::items(result, cloned->get("Annots"));
    QCOMPARE(annotations.size(), size_t(2));
    const std::vector<PDFObject> originalAnnotations = Read::items(result, original->get("Annots"));
    const PDFDictionary* text = Read::dictionary(result, annotations[0]);
    const PDFDictionary* popup = Read::dictionary(result, annotations[1]);
    QVERIFY(annotations[0] != originalAnnotations[0]);
    QCOMPARE(text->get("P").getReference(), cloneReference);
    QCOMPARE(popup->get("P").getReference(), cloneReference);
    QCOMPARE(text->get("Popup"), annotations[1]);
    QCOMPARE(popup->get("Parent"), annotations[0]);
    QVERIFY(!text->hasKey("StructParent"));
}

void DocumentMergerTest::namedDestinationsJoined()
{
    // B1, B2, 7.9.6 Table 36, 12.3.2.3: the keys of the tree are strings, sorted, the root has no
    // /Limits; a collision gets "intro_2" and the links of its input follow it; /Dests of PDF 1.1
    // joins the tree and its name references become strings. The first input has no /Names (the old
    // merge lost the trees then, B2).
    const PDFDocumentPointer c = MergeTestPdf::createPages("C", 1).readPointer();

    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/Names << /Dests 60 0 R >>", { { 1, "/Annots [20 0 R]" } });
    a.set(20, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest (intro) >>");
    a.set(60, "<< /Names [(intro) [3 0 R /Fit]] >>");

    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/Dests << /intro [3 0 R /FitH 500] /other [3 0 R /Fit] >>", { { 1, "/Annots [20 0 R 21 0 R]" } });
    b.set(20, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest /intro >>");
    b.set(21, "<< /Type /Annot /Subtype /Link /Rect [20 0 30 10] /A << /S /GoTo /D /other >> >>");

    const Merger::Result merged = mergeAll({ c, a.readPointer(), b.readPointer() });
    QCOMPARE(merged.statistics.destinationsRenamed, 1);
    const PDFDocument result = check(merged);

    const PDFDictionary* names = Read::path(result, { "Names" });
    QVERIFY(names);
    QVERIFY(!Read::catalog(result)->hasKey("Dests"));
    const PDFDictionary* tree = Read::dictionary(result, names->get("Dests"));
    QVERIFY(tree);
    QVERIFY(!tree->hasKey("Limits"));
    const std::vector<PDFObject> pairs = Read::items(result, tree->get("Names"));
    QCOMPARE(pairs.size(), size_t(6));
    QVERIFY(Read::object(result, pairs[0]).isString());
    QCOMPARE(Read::object(result, pairs[0]).getString(), QByteArray("intro"));
    QCOMPARE(Read::object(result, pairs[2]).getString(), QByteArray("intro_2"));
    QCOMPARE(Read::object(result, pairs[4]).getString(), QByteArray("other"));

    QCOMPARE(resolveDestination(result, PDFObject::createString("intro")), pageReference(result, 1));
    QCOMPARE(resolveDestination(result, PDFObject::createString("intro_2")), pageReference(result, 2));

    // The links of each input lead to its own page
    const PDFDictionary* pageA = Read::dictionary(result, PDFObject::createReference(pageReference(result, 1)));
    const PDFDictionary* linkA = Read::dictionary(result, Read::items(result, pageA->get("Annots")).front());
    QCOMPARE(resolveDestination(result, linkA->get("Dest")), pageReference(result, 1));
    const PDFDictionary* pageB = Read::dictionary(result, PDFObject::createReference(pageReference(result, 2)));
    const std::vector<PDFObject> annotationsB = Read::items(result, pageB->get("Annots"));
    const PDFDictionary* linkB = Read::dictionary(result, annotationsB[0]);
    QVERIFY(Read::object(result, linkB->get("Dest")).isString());
    QCOMPARE(Read::object(result, linkB->get("Dest")).getString(), QByteArray("intro_2"));
    QCOMPARE(resolveDestination(result, linkB->get("Dest")), pageReference(result, 2));
    const PDFDictionary* actionB = Read::dictionary(result, Read::dictionary(result, annotationsB[1])->get("A"));
    QVERIFY(Read::object(result, actionB->get("D")).isString());
    QCOMPARE(resolveDestination(result, actionB->get("D")), pageReference(result, 2));
}

void DocumentMergerTest::formFieldsJoined()
{
    // B1, 12.7.4.2, owner answer Q3: a field, whose fully qualified name is a name of an input
    // before, is renamed ("Name_2"); the others keep their names; the fields of one input are
    // its own (two top fields of one input with one name stay as they are)
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/AcroForm << /Fields [30 0 R 33 0 R] >>", { { 1, "/Annots [31 0 R 34 0 R]" } });
    a.set(30, "<< /FT /Tx /T (Name) /Kids [31 0 R] >>");
    a.set(31, "<< /Type /Annot /Subtype /Widget /Rect [0 0 10 10] /Parent 30 0 R /P 3 0 R >>");
    a.set(33, "<< /T (Group) /Kids [35 0 R] >>");
    a.set(35, "<< /FT /Tx /T (inner) /Parent 33 0 R /Kids [34 0 R] >>");
    a.set(34, "<< /Type /Annot /Subtype /Widget /Rect [0 0 10 10] /Parent 35 0 R /P 3 0 R >>");

    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/AcroForm << /Fields [30 0 R 32 0 R 36 0 R 37 0 R] >>", { { 1, "/Annots [30 0 R 32 0 R 36 0 R 37 0 R]" } });
    b.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    b.set(32, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Other) /Rect [0 0 10 10] /P 3 0 R >>");
    b.set(36, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Dup) /Rect [0 0 10 10] /P 3 0 R >>");
    b.set(37, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Dup) /Rect [20 0 30 10] /P 3 0 R >>");

    const Merger::Result merged = mergeAll({ a.readPointer(), b.readPointer() });
    QCOMPARE(merged.statistics.fieldsRenamed, 1);
    const PDFDocument result = check(merged);

    QStringList names;
    for (const PDFObject& field : Read::items(result, Read::path(result, { "AcroForm" })->get("Fields")))
    {
        names << Read::text(result, Read::dictionary(result, field)->get("T"));
    }
    QCOMPARE(names, QStringList({ QStringLiteral("Name"), QStringLiteral("Group"), QStringLiteral("Name_2"), QStringLiteral("Other"), QStringLiteral("Dup"), QStringLiteral("Dup") }));

    // Every widget of a page has its field in /Fields
    const std::vector<PDFObject> fields = Read::items(result, Read::path(result, { "AcroForm" })->get("Fields"));
    const PDFDictionary* pageB = Read::dictionary(result, PDFObject::createReference(pageReference(result, 1)));
    for (const PDFObject& widget : Read::items(result, pageB->get("Annots")))
    {
        QVERIFY(std::find(fields.cbegin(), fields.cend(), widget) != fields.cend());
    }
}

void DocumentMergerTest::formFieldsOfManyInputs()
{
    // Chapter 6: 100 inputs with the field "Name" give 100 different fully qualified names, linearly
    std::vector<PDFDocumentPointer> documents;
    for (int i = 0; i < 100; ++i)
    {
        MergeTestPdf pdf = MergeTestPdf::createPages("X", 1, "/AcroForm << /Fields [30 0 R] >>", { { 1, "/Annots [30 0 R]" } });
        pdf.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
        documents.push_back(pdf.readPointer());
    }

    QElapsedTimer timer;
    timer.start();
    const Merger::Result merged = mergeAll(documents, Merger::OutlineMode::NoOutline);
    QVERIFY(timer.elapsed() < 30000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
    QCOMPARE(merged.statistics.fieldsRenamed, 99);
    const PDFDocument result = check(merged);

    std::set<QString> names;
    for (const PDFObject& field : Read::items(result, Read::path(result, { "AcroForm" })->get("Fields")))
    {
        names.insert(Read::text(result, Read::dictionary(result, field)->get("T")));
    }
    QCOMPARE(names.size(), size_t(100));
    QVERIFY(names.count(QStringLiteral("Name")));
    QVERIFY(names.count(QStringLiteral("Name_100")));
}

void DocumentMergerTest::formFieldNamesOfOneInput()
{
    // 12.7.4.1, 12.7.4.2, owner answer Q3: the new name of a renamed field is no name of its own
    // input either - B has "Name" (collides with A) and its own "Name_2"; "Name_2" for its "Name"
    // would make the two different fields of B one field (one fully qualified name), so a text
    // typed into one would be the value of the other
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/AcroForm << /Fields [30 0 R] >>", { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/AcroForm << /Fields [30 0 R 31 0 R] >>", { { 1, "/Annots [30 0 R 31 0 R]" } });
    b.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (b-name) /Rect [0 0 10 10] /P 3 0 R >>");
    b.set(31, "<< /Type /Annot /Subtype /Widget /FT /Btn /T (Name_2) /Rect [20 0 30 10] /P 3 0 R >>");

    const Merger::Result merged = mergeAll({ a.readPointer(), b.readPointer() });
    QCOMPARE(merged.statistics.fieldsRenamed, 1);
    const PDFDocument result = check(merged);

    std::map<QString, QString> values;
    for (const PDFObject& field : Read::items(result, Read::path(result, { "AcroForm" })->get("Fields")))
    {
        const PDFDictionary* dictionary = Read::dictionary(result, field);
        const QString name = Read::text(result, dictionary->get("T"));
        QVERIFY2(!values.count(name), qPrintable(name));
        values[name] = Read::text(result, dictionary->get("V"));
    }
    QCOMPARE(values.size(), size_t(3));
    QVERIFY(values.count(QStringLiteral("Name")));
    QVERIFY(values.count(QStringLiteral("Name_2")));
    QVERIFY(values[QStringLiteral("Name_2")].isEmpty());
    QCOMPARE(values[QStringLiteral("Name_3")], QStringLiteral("b-name"));
}

void DocumentMergerTest::formResourcesAndAppearances()
{
    // 12.7.3, 12.7.4.3, R8: /DR joined, the same name of another font is renamed and the /DA of the
    // input follow it; a field of variable text of an input, whose AcroForm /DA and /Q lost, gets
    // its own; /NeedAppearances is an OR; /CO joined in the order of the inputs
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/AcroForm << /Fields [30 0 R] /DR << /Font << /Helv 50 0 R >> >> /DA (/Helv 0 Tf 0 g) /CO [30 0 R] >>", { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (a) /Rect [0 0 10 10] /P 3 0 R >>");
    a.set(50, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/AcroForm << /Fields [30 0 R 31 0 R] /DR << /Font << /Helv 50 0 R >> >> /DA (/Helv 10 Tf 1 g) /Q 2 /NeedAppearances true /CO [31 0 R] >>",
                                               { { 1, "/Annots [30 0 R 31 0 R]" } });
    b.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (b) /Rect [0 0 10 10] /P 3 0 R >>");
    b.set(31, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (c) /DA (/Helv 8 Tf 0 g) /Rect [0 0 10 10] /P 3 0 R >>");
    b.set(50, "<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman >>");

    const PDFDocument result = check(mergeAll({ a.readPointer(), b.readPointer() }));
    const PDFDictionary* form = Read::path(result, { "AcroForm" });
    QVERIFY(form);
    QCOMPARE(Read::object(result, form->get("DA")).getString(), QByteArray("/Helv 0 Tf 0 g"));
    QVERIFY(Read::object(result, form->get("NeedAppearances")).getBool());
    QVERIFY(!form->hasKey("Q"));

    const PDFDictionary* fonts = Read::path(result, { "AcroForm", "DR", "Font" });
    QVERIFY(fonts);
    QCOMPARE(fonts->getCount(), size_t(2));
    QCOMPARE(Read::name(result, Read::dictionary(result, fonts->get("Helv"))->get("BaseFont")), QByteArray("Helvetica"));
    QCOMPARE(Read::name(result, Read::dictionary(result, fonts->get("Helv_2"))->get("BaseFont")), QByteArray("Times-Roman"));

    const std::vector<PDFObject> fields = Read::items(result, form->get("Fields"));
    QCOMPARE(fields.size(), size_t(3));
    const PDFDictionary* fieldA = Read::dictionary(result, fields[0]);
    const PDFDictionary* fieldB = Read::dictionary(result, fields[1]);
    const PDFDictionary* fieldC = Read::dictionary(result, fields[2]);
    QVERIFY(!fieldA->hasKey("DA"));
    QCOMPARE(Read::object(result, fieldB->get("DA")).getString(), QByteArray("/Helv_2 10 Tf 1 g"));
    QCOMPARE(Read::integer(result, fieldB->get("Q")), 2);
    QCOMPARE(Read::object(result, fieldC->get("DA")).getString(), QByteArray("/Helv_2 8 Tf 0 g"));
    QCOMPARE(Read::integer(result, fieldC->get("Q")), 2);

    const std::vector<PDFObject> order = Read::items(result, form->get("CO"));
    QCOMPARE(order.size(), size_t(2));
    QCOMPARE(order[0], fields[0]);
    QCOMPARE(order[1], fields[2]);
}

void DocumentMergerTest::clonedWidgetOfFieldKid()
{
    // R8, 12.7.4.1, 12.7.4.3: a clone of a page splits a merged field and widget, which is a kid of
    // another field, into a field with two widgets; the split field keeps the /DA of the merged
    // dictionary - it follows the renamed font of /DR of its input as every field of the input
    // (else the clone would draw the text by the font of another input)
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/AcroForm << /Fields [30 0 R] /DR << /Font << /Helv 50 0 R >> >> >>", { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (a) /DA (/Helv 0 Tf 0 g) /Rect [0 0 10 10] /P 3 0 R >>");
    a.set(50, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/AcroForm << /Fields [40 0 R] /DR << /Font << /Helv 50 0 R >> >> >>", { { 1, "/Annots [41 0 R]" } });
    b.set(40, "<< /T (Group) /Kids [41 0 R] >>");
    b.set(41, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /DA (/Helv 9 Tf 0 g) /Parent 40 0 R /Rect [0 0 10 10] /P 3 0 R >>");
    b.set(50, "<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman >>");

    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a.readPointer(), QString()), Merger::Source::createDocument(b.readPointer(), QString()) };
    std::vector<Merger::Item> items = allPages(sources);
    Merger::Item clone;
    clone.source = 1;
    items.push_back(clone);
    const PDFDocument result = check(Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr));
    QCOMPARE(Read::name(result, Read::dictionary(result, Read::path(result, { "AcroForm", "DR", "Font" })->get("Helv_2"))->get("BaseFont")), QByteArray("Times-Roman"));

    const std::vector<PDFObject> fields = Read::items(result, Read::path(result, { "AcroForm" })->get("Fields"));
    QCOMPARE(fields.size(), size_t(2));
    const PDFDictionary* group = Read::dictionary(result, fields[1]);
    QCOMPARE(Read::text(result, group->get("T")), QStringLiteral("Group"));
    const std::vector<PDFObject> groupKids = Read::items(result, group->get("Kids"));
    QCOMPARE(groupKids.size(), size_t(1));
    const PDFDictionary* field = Read::dictionary(result, groupKids.front());
    QCOMPARE(Read::text(result, field->get("T")), QStringLiteral("Name"));
    QCOMPARE(Read::object(result, field->get("DA")).getString(), QByteArray("/Helv_2 9 Tf 0 g"));
    const std::vector<PDFObject> widgets = Read::items(result, field->get("Kids"));
    QCOMPARE(widgets.size(), size_t(2));
    for (const PDFObject& widget : widgets)
    {
        QVERIFY(!Read::dictionary(result, widget)->hasKey("DA"));
        QCOMPARE(Read::dictionary(result, Read::dictionary(result, widget)->get("Parent")), field);
    }
}

void DocumentMergerTest::signaturesRemoved()
{
    // 12.8, owner answer Q4: a signature of another file is never valid - its value and appearance
    // go, the empty field stays for a new signature; /SigFlags keeps bit 1 (a signature field) and
    // loses bit 2 (Table 225); /Perms (DocMDP) goes with the signature
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/AcroForm << /Fields [30 0 R] /SigFlags 3 >> /Perms << /DocMDP 31 0 R >>", { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (Signature) /V 31 0 R /Rect [0 0 10 10] /P 3 0 R /AP << /N 32 0 R >> >>");
    a.set(31, "<< /Type /Sig /Filter /Adobe.PPKLite /Name (SIGNERMARKER) /ByteRange [0 10 20 30] /Contents <00> >>");
    a.set(32, MergeTestPdf::stream("% SIGAPPEARANCE", "/Type /XObject /Subtype /Form /BBox [0 0 10 10]"));
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();

    const Merger::Result merged = mergeAll({ a.readPointer(), b });
    QCOMPARE(merged.statistics.signaturesRemoved, 1);
    const PDFDocument result = check(merged);
    const QByteArray bytes = MergeTestPdf::writeDocument(result);
    QVERIFY(!bytes.contains("SIGNERMARKER"));
    QVERIFY(!bytes.contains("SIGAPPEARANCE"));
    QVERIFY(!Read::catalog(result)->hasKey("Perms"));

    const PDFDictionary* form = Read::path(result, { "AcroForm" });
    QCOMPARE(Read::integer(result, form->get("SigFlags")), 1);
    const std::vector<PDFObject> fields = Read::items(result, form->get("Fields"));
    QCOMPARE(fields.size(), size_t(1));
    const PDFDictionary* field = Read::dictionary(result, fields.front());
    QCOMPARE(Read::name(result, field->get("FT")), QByteArray("Sig"));
    QVERIFY(!field->hasKey("V"));
    QVERIFY(!field->hasKey("AP"));
}

void DocumentMergerTest::layersKeepTheirStates_data()
{
    QTest::addColumn<QByteArray>("baseState");
    QTest::newRow("first ON") << QByteArray("/ON");
    QTest::newRow("first OFF") << QByteArray("/OFF");
}

void DocumentMergerTest::layersKeepTheirStates()
{
    // 8.11.2.2, 8.11.4.3 (R10): every group, which the content or an annotation uses (also through
    // /OCGs and /VE of a membership dictionary), is in /OCGs; a hidden group of the second input
    // stays hidden and its visible groups visible, whatever /BaseState the first input has
    QFETCH(QByteArray, baseState);

    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/OCProperties << /OCGs [20 0 R] /D << /BaseState " + baseState + " /ON [20 0 R] /Order [20 0 R] >> >>");
    a.set(20, "<< /Type /OCG /Name (A layer) >>");

    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/OCProperties << /OCGs [30 0 R 31 0 R] /D << /OFF [31 0 R] >> /Configs [<< /Name (Print) /BaseState /OFF >>] >>",
                                               { { 1, "/Annots [40 0 R] /Resources << /Properties << /MC0 32 0 R >> >>" } });
    b.set(30, "<< /Type /OCG /Name (B visible) >>");
    b.set(31, "<< /Type /OCG /Name (B hidden) >>");
    b.set(32, "<< /Type /OCMD /OCGs [33 0 R] >>");
    b.set(33, "<< /Type /OCG /Name (B unlisted) >>");
    b.set(34, "<< /Type /OCMD /VE [/And 35 0 R 30 0 R] >>");
    b.set(35, "<< /Type /OCG /Name (B expression) >>");
    b.set(40, "<< /Type /Annot /Subtype /Square /Rect [0 0 10 10] /OC 34 0 R >>");

    const Merger::Result merged = mergeAll({ a.readPointer(), b.readPointer() });
    QVERIFY(merged.statistics.layerConfigurationsRemoved);
    const PDFDocument result = check(merged);
    const PDFDictionary* properties = Read::path(result, { "OCProperties" });
    QVERIFY(properties);
    QVERIFY(!properties->hasKey("Configs"));
    const PDFDictionary* configuration = Read::dictionary(result, properties->get("D"));
    QVERIFY(configuration);

    const std::vector<PDFObject> on = Read::items(result, configuration->get("ON"));
    const std::vector<PDFObject> off = Read::items(result, configuration->get("OFF"));
    const QByteArray resultBase = Read::name(result, configuration->get("BaseState"));
    QCOMPARE(resultBase, baseState.mid(1));

    std::map<QString, bool> states;
    for (const PDFObject& group : Read::items(result, properties->get("OCGs")))
    {
        const QString name = Read::text(result, Read::dictionary(result, group)->get("Name"));
        const bool isOff = std::find(off.cbegin(), off.cend(), group) != off.cend();
        const bool isOn = std::find(on.cbegin(), on.cend(), group) != on.cend();
        states[name] = isOff ? false : (isOn ? true : resultBase != "OFF");
    }
    QCOMPARE(states.size(), size_t(5));
    QCOMPARE(states[QStringLiteral("A layer")], true);
    QCOMPARE(states[QStringLiteral("B visible")], true);
    QCOMPARE(states[QStringLiteral("B hidden")], false);
    QCOMPARE(states[QStringLiteral("B unlisted")], true);
    QCOMPARE(states[QStringLiteral("B expression")], true);

    // Table 101: the order of each input under its name
    const std::vector<PDFObject> order = Read::items(result, configuration->get("Order"));
    QCOMPARE(order.size(), size_t(2));
    QVERIFY(Read::object(result, Read::items(result, order[0]).front()).isString());
}

void DocumentMergerTest::structureTreeJoined()
{
    // Owner answer Q1, R9, 14.7.2, 14.8.4: one new Document, the top elements of each input under a
    // DocumentFragment of the namespace of PDF 2.0 with the language and the title of the input; the
    // keys of /ParentTree of the second input follow the keys of the first (14.7.5.4) and each key of
    // a page leads to the elements of that page
    auto tagged = [](const QByteArray& prefix, const QByteArray& language)
    {
        MergeTestPdf pdf = MergeTestPdf::createPages(prefix, 1, "/StructTreeRoot 50 0 R /MarkInfo << /Marked true >> /Lang (" + language + ")", { { 1, "/StructParents 0" } });
        pdf.set(50, "<< /Type /StructTreeRoot /K 51 0 R /ParentTree 52 0 R /ParentTreeNextKey 1 >>");
        pdf.set(51, "<< /Type /StructElem /S /Document /P 50 0 R /K 53 0 R >>");
        pdf.set(53, "<< /Type /StructElem /S /P /P 51 0 R /Pg 3 0 R /K [0 54 0 R] >>");
        pdf.set(54, "<< /Type /StructElem /S /Span /P 53 0 R /K [53 0 R] >>");
        pdf.set(52, "<< /Nums [0 [53 0 R]] >>");
        return pdf;
    };

    MergeTestPdf a = tagged("A", "en-US");
    a.set(90, "<< /Title (Alpha) >>");
    a.setTrailerEntries("/Info 90 0 R");
    MergeTestPdf b = tagged("B", "cs-CZ");

    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a.readPointer(), QString()), Merger::Source::createDocument(b.readPointer(), QStringLiteral("Beta.pdf")) };
    std::vector<Merger::Item> items = allPages(sources);
    Merger::Item clone;
    clone.source = 0;
    items.push_back(clone);
    const Merger::Result merged = Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr);
    QVERIFY(merged.statistics.structureTreeMerged);
    const PDFDocument result = check(merged);
    QCOMPARE(result.getInfo()->version, PDFVersion(2, 0));

    const PDFDictionary* root = Read::path(result, { "StructTreeRoot" });
    QVERIFY(root);
    const PDFDictionary* document = Read::dictionary(result, root->get("K"));
    QCOMPARE(Read::name(result, document->get("S")), QByteArray("Document"));
    const PDFDictionary* pdf2Namespace = Read::dictionary(result, document->get("NS"));
    QCOMPARE(Read::object(result, pdf2Namespace->get("NS")).getString(), QByteArray("http://iso.org/pdf2/ssn"));

    const std::vector<PDFObject> fragments = Read::items(result, document->get("K"));
    QCOMPARE(fragments.size(), size_t(2));
    QStringList languages;
    QStringList titles;
    for (const PDFObject& fragment : fragments)
    {
        const PDFDictionary* element = Read::dictionary(result, fragment);
        QCOMPARE(Read::name(result, element->get("S")), QByteArray("DocumentFragment"));
        QCOMPARE(Read::dictionary(result, element->get("NS")), pdf2Namespace);
        QCOMPARE(Read::dictionary(result, element->get("P")), document);
        languages << Read::text(result, element->get("Lang"));
        titles << Read::text(result, element->get("T"));
        const std::vector<PDFObject> kids = Read::items(result, element->get("K"));
        QCOMPARE(kids.size(), size_t(1));
        QCOMPARE(Read::dictionary(result, Read::dictionary(result, kids.front())->get("P")), element);
    }
    QCOMPARE(languages, QStringList({ QStringLiteral("en-US"), QStringLiteral("cs-CZ") }));
    QCOMPARE(titles, QStringList({ QStringLiteral("Alpha"), QStringLiteral("Beta") }));
    QVERIFY(!Read::catalog(result)->hasKey("Lang"));
    QVERIFY(Read::object(result, Read::path(result, { "MarkInfo" })->get("Marked")).getBool());

    // /ParentTree: the key of each page leads to its elements
    QCOMPARE(Read::integer(result, root->get("ParentTreeNextKey")), 2);
    const std::vector<std::pair<PDFInteger, PDFObject>> parentTree = PDFObjectUtils::readNumberTree(&result.getStorage(), root->get("ParentTree"));
    QCOMPARE(parentTree.size(), size_t(2));
    std::set<PDFInteger> keys;
    for (size_t i = 0; i < 2; ++i)
    {
        const PDFDictionary* page = Read::dictionary(result, PDFObject::createReference(pageReference(result, i)));
        const PDFInteger key = Read::integer(result, page->get("StructParents"));
        QVERIFY(keys.insert(key).second);
        auto it = std::find_if(parentTree.cbegin(), parentTree.cend(), [key](const auto& entry) { return entry.first == key; });
        QVERIFY(it != parentTree.cend());
        const PDFDictionary* element = Read::dictionary(result, Read::items(result, it->second).front());
        QCOMPARE(element->get("Pg").getReference(), pageReference(result, i));
    }

    // The clone is outside of the structure
    QVERIFY(!Read::dictionary(result, PDFObject::createReference(pageReference(result, 2)))->hasKey("StructParents"));
}

void DocumentMergerTest::structureHostileRoot()
{
    // Chapter 6: a root of the structure, which is its own kid, and an element shared by two
    // parents end the merge (the root is read before its kids are changed)
    auto tagged = [](const QByteArray& prefix)
    {
        MergeTestPdf pdf = MergeTestPdf::createPages(prefix, 1, "/StructTreeRoot 50 0 R /MarkInfo << /Marked true >>", { { 1, "/StructParents 0" } });
        pdf.set(50, "<< /Type /StructTreeRoot /K [51 0 R 50 0 R 52 0 R] /ParentTree << /Nums [0 [53 0 R]] >> /ParentTreeNextKey 1 >>");
        pdf.set(51, "<< /Type /StructElem /S /Sect /P 50 0 R /K [53 0 R] >>");
        pdf.set(52, "<< /Type /StructElem /S /Sect /P 50 0 R /K [53 0 R] >>");
        pdf.set(53, "<< /Type /StructElem /S /P /P 51 0 R /Pg 3 0 R /K 0 >>");
        return pdf.readPointer();
    };

    const Merger::Result merged = mergeAll({ tagged("A"), tagged("B") });
    QVERIFY2(merged.isSuccess, qPrintable(merged.errorMessage));
    const PDFDocument result = check(merged);
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(2));
    QVERIFY(Read::path(result, { "StructTreeRoot" }));
}

void DocumentMergerTest::structureTreeRemovedUnlessAllTagged()
{
    // Owner answer Q1: an input without tags (or one not taken whole) - the result has no
    // structure, its keys of the parent tree are gone from the pages, the fact says it
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/StructTreeRoot 50 0 R /MarkInfo << /Marked true >> /Lang (en-US)", { { 1, "/StructParents 0" } });
    a.set(50, "<< /Type /StructTreeRoot /K 51 0 R /ParentTree 52 0 R /ParentTreeNextKey 1 >>");
    a.set(51, "<< /Type /StructElem /S /P /P 50 0 R /Pg 3 0 R /K 0 >>");
    a.set(52, "<< /Nums [0 [51 0 R]] >>");
    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/Lang (en-US)");

    const Merger::Result merged = mergeAll({ a.readPointer(), b.readPointer() });
    QVERIFY(merged.statistics.structureTreeRemoved);
    QVERIFY(!merged.statistics.structureTreeMerged);
    const PDFDocument result = check(merged);
    QVERIFY(!Read::catalog(result)->hasKey("StructTreeRoot"));
    QVERIFY(!Read::catalog(result)->hasKey("MarkInfo"));
    QVERIFY(!Read::dictionary(result, PDFObject::createReference(pageReference(result, 0)))->hasKey("StructParents"));

    // 14.9.2: the language of both inputs is the language of the result
    QCOMPARE(Read::text(result, Read::catalog(result)->get("Lang")), QStringLiteral("en-US"));
}

void DocumentMergerTest::structureRoleMapCollision()
{
    // 14.7.4.4: a type, which two inputs map to different standard types, goes into a namespace of
    // the second input with /RoleMapNS; the shared /RoleMap keeps the first mapping
    auto tagged = [](const QByteArray& prefix, const QByteArray& role)
    {
        MergeTestPdf pdf = MergeTestPdf::createPages(prefix, 1, "/StructTreeRoot 50 0 R /MarkInfo << /Marked true >>", { { 1, "/StructParents 0" } });
        pdf.set(50, "<< /Type /StructTreeRoot /K 51 0 R /ParentTree 52 0 R /RoleMap << /Heading /" + role + " >> /ClassMap << /Bold << /O /Layout >> >> >>");
        pdf.set(51, "<< /Type /StructElem /S /Heading /C /Bold /ID (h1) /P 50 0 R /Pg 3 0 R /K 0 >>");
        pdf.set(52, "<< /Nums [0 [51 0 R]] >>");
        pdf.set(55, "<< /Names [(h1) 51 0 R] >>");
        return pdf;
    };
    MergeTestPdf a = tagged("A", "H1");
    MergeTestPdf b = tagged("B", "H2");
    b.set(50, "<< /Type /StructTreeRoot /K 51 0 R /ParentTree 52 0 R /RoleMap << /Heading /H2 >> /ClassMap << /Bold << /O /Layout /FontWeight 700 >> >> /IDTree 55 0 R >>");
    a.set(50, "<< /Type /StructTreeRoot /K 51 0 R /ParentTree 52 0 R /RoleMap << /Heading /H1 >> /ClassMap << /Bold << /O /Layout >> >> /IDTree 55 0 R >>");

    const PDFDocument result = check(mergeAll({ a.readPointer(), b.readPointer() }));
    const PDFDictionary* root = Read::path(result, { "StructTreeRoot" });
    QVERIFY(root);
    QCOMPARE(Read::name(result, Read::dictionary(result, root->get("RoleMap"))->get("Heading")), QByteArray("H1"));
    QVERIFY(Read::dictionary(result, root->get("ClassMap"))->hasKey("Bold_2"));

    const PDFDictionary* fragmentB = Read::dictionary(result, Read::items(result, Read::dictionary(result, root->get("K"))->get("K"))[1]);
    const PDFDictionary* headingB = Read::dictionary(result, Read::items(result, fragmentB->get("K")).front());
    const PDFDictionary* namespaceB = Read::dictionary(result, headingB->get("NS"));
    QVERIFY(namespaceB);
    const std::vector<PDFObject> mapping = Read::items(result, Read::dictionary(result, namespaceB->get("RoleMapNS"))->get("Heading"));
    QCOMPARE(mapping.size(), size_t(2));
    QCOMPARE(Read::name(result, mapping[0]), QByteArray("H2"));
    QCOMPARE(Read::object(result, Read::dictionary(result, mapping[1])->get("NS")).getString(), QByteArray("http://iso.org/pdf/ssn"));
    QCOMPARE(Read::name(result, headingB->get("C")), QByteArray("Bold_2"));
    QCOMPARE(Read::object(result, headingB->get("ID")).getString(), QByteArray("h1_2"));
}

void DocumentMergerTest::pageLabelsKept()
{
    // 12.4.2: every page keeps the label of its input; an input without labels has the decimal
    // numbers of its pages
    MergeTestPdf a = MergeTestPdf::createPages("A", 2, "/PageLabels << /Nums [0 << /S /r >>] >>");
    MergeTestPdf b = MergeTestPdf::createPages("B", 2);
    MergeTestPdf c = MergeTestPdf::createPages("C", 1, "/PageLabels << /Nums [0 << /S /D /P (C-) /St 5 >>] >>");

    const PDFDocument result = check(mergeAll({ a.readPointer(), b.readPointer(), c.readPointer() }));
    QStringList labels;
    for (size_t i = 0; i < result.getCatalog()->getPageCount(); ++i)
    {
        labels << result.getCatalog()->getPageLabel(PDFInteger(i));
    }
    QCOMPARE(labels, QStringList({ QStringLiteral("i"), QStringLiteral("ii"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("C-5") }));

    // Moved pages keep their labels too
    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a.readPointer(), QString()), Merger::Source::createDocument(b.readPointer(), QString()) };
    std::vector<Merger::Item> items(3);
    items[0].source = 1;
    items[0].pageIndex = 1;
    items[1].source = 0;
    items[1].pageIndex = 1;
    items[2].source = 0;
    items[2].pageIndex = 0;
    const PDFDocument moved = check(Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr));
    QCOMPARE(moved.getCatalog()->getPageLabel(0), QStringLiteral("2"));
    QCOMPARE(moved.getCatalog()->getPageLabel(1), QStringLiteral("ii"));
    QCOMPARE(moved.getCatalog()->getPageLabel(2), QStringLiteral("i"));
}

void DocumentMergerTest::outlineDocumentParts()
{
    // Owner answer Q7, 14.12, B16: an outline item and a document part per run of pages of an input;
    // /Start and /End of each part are the first and last page of the run; the version is 2.0
    MergeTestPdf a = MergeTestPdf::createPages("A", 2);
    a.set(90, "<< /Title (Alpha) >>");
    a.setTrailerEntries("/Info 90 0 R");
    a.setVersion("1.4");
    MergeTestPdf b = MergeTestPdf::createPages("B", 1);

    const Merger::Result merged = mergeAll({ a.readPointer(), b.readPointer() }, Merger::OutlineMode::DocumentParts, { QStringLiteral("a.pdf"), QStringLiteral("Beta.pdf") });
    QCOMPARE(merged.statistics.partCount, 2);
    const PDFDocument result = check(merged);
    QCOMPARE(result.getInfo()->version, PDFVersion(2, 0));

    const PDFDictionary* outline = Read::path(result, { "Outlines" });
    QVERIFY(outline);
    checkOutlineLinks(result, Read::catalog(result)->get("Outlines"), 0);
    QCOMPARE(Read::integer(result, outline->get("Count")), 2);
    const PDFDictionary* first = Read::dictionary(result, outline->get("First"));
    const PDFDictionary* second = Read::dictionary(result, first->get("Next"));
    QCOMPARE(Read::text(result, first->get("Title")), QStringLiteral("Alpha"));
    QCOMPARE(Read::text(result, second->get("Title")), QStringLiteral("Beta"));
    QCOMPARE(resolveDestination(result, first->get("Dest")), pageReference(result, 0));
    QCOMPARE(resolveDestination(result, second->get("Dest")), pageReference(result, 2));

    const PDFDictionary* partRoot = Read::path(result, { "DPartRoot" });
    QVERIFY(partRoot);
    const PDFDictionary* rootNode = Read::dictionary(result, partRoot->get("DPartRootNode"));
    const std::vector<PDFObject> parts = Read::items(result, rootNode->get("DParts"));
    QCOMPARE(parts.size(), size_t(2));
    const PDFDictionary* part1 = Read::dictionary(result, Read::items(result, parts[0]).front());
    const PDFDictionary* part2 = Read::dictionary(result, Read::items(result, parts[1]).front());
    QCOMPARE(part1->get("Start").getReference(), pageReference(result, 0));
    QCOMPARE(part1->get("End").getReference(), pageReference(result, 1));
    QCOMPARE(part2->get("Start").getReference(), pageReference(result, 2));
    QCOMPARE(part2->get("End").getReference(), pageReference(result, 2));
}

void DocumentMergerTest::outlineRunsOfMovedPages()
{
    // K14: a run is a contiguous sequence of pages of one input; a page moved among the pages of
    // another input makes its own run, item and part
    const PDFDocumentPointer a = MergeTestPdf::createPages("A", 2).readPointer();
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();
    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a, QStringLiteral("Alpha.pdf")), Merger::Source::createDocument(b, QStringLiteral("Beta.pdf")) };
    std::vector<Merger::Item> items(3);
    items[0].source = 0;
    items[0].pageIndex = 0;
    items[1].source = 1;
    items[1].pageIndex = 0;
    items[2].source = 0;
    items[2].pageIndex = 1;
    const Merger::Result merged = Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr);
    QCOMPARE(merged.statistics.partCount, 3);
    QCOMPARE(merged.statistics.inputCount, 2);
    const PDFDocument result = check(merged);

    QStringList titles;
    for (PDFObject item = Read::path(result, { "Outlines" })->get("First"); item.isReference(); item = Read::dictionary(result, item)->get("Next"))
    {
        titles << Read::text(result, Read::dictionary(result, item)->get("Title"));
    }
    QCOMPARE(titles, QStringList({ QStringLiteral("Alpha, p. 1"), QStringLiteral("Beta"), QStringLiteral("Alpha, p. 2") }));
}

void DocumentMergerTest::outlineJoin()
{
    // B7, B8, 12.3.3, Table 151: the outline of an input under the item of each run, the original
    // items (URI, Named actions, closed items) kept; an item, whose target is outside the run, goes,
    // unless an item below it stays (then it loses only its target); the parts are closed
    MergeTestPdf a = MergeTestPdf::createPages("A", 2, "/Outlines 20 0 R");
    a.set(20, "<< /Type /Outlines /First 21 0 R /Last 24 0 R /Count 3 >>");
    a.set(21, "<< /Title (Chapter 1) /Parent 20 0 R /Next 23 0 R /Dest [3 0 R /Fit] /First 22 0 R /Last 22 0 R /Count -1 /C [1 0 0] /F 1 >>");
    a.set(22, "<< /Title (Section 1.1) /Parent 21 0 R /Dest [4 0 R /XYZ 0 700 0] >>");
    a.set(23, "<< /Title (Web) /Parent 20 0 R /Prev 21 0 R /Next 24 0 R /A << /S /URI /URI (http://example.com/) >> >>");
    a.set(24, "<< /Title (Next page) /Parent 20 0 R /Prev 23 0 R /A << /S /Named /N /NextPage >> >>");
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();

    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a.readPointer(), QStringLiteral("Alpha.pdf")), Merger::Source::createDocument(b, QStringLiteral("Beta.pdf")) };
    std::vector<Merger::Item> items(3);
    items[0].source = 0;
    items[0].pageIndex = 0;
    items[1].source = 1;
    items[1].pageIndex = 0;
    items[2].source = 0;
    items[2].pageIndex = 1;
    Merger::Settings settings;
    settings.outline = Merger::OutlineMode::Join;
    const PDFDocument result = check(Merger::merge(sources, items, settings, nullptr, nullptr));
    checkOutlineLinks(result, Read::catalog(result)->get("Outlines"), 0);

    const PDFDictionary* outline = Read::path(result, { "Outlines" });
    QCOMPARE(Read::integer(result, outline->get("Count")), 3);
    const PDFDictionary* run1 = Read::dictionary(result, outline->get("First"));
    const PDFDictionary* run2 = Read::dictionary(result, run1->get("Next"));
    const PDFDictionary* run3 = Read::dictionary(result, run2->get("Next"));

    // Run 1 (page 1 of A): Chapter 1 without its section, Web and Next page of the first run
    QCOMPARE(Read::integer(result, run1->get("Count")), -3);
    const PDFDictionary* chapter = Read::dictionary(result, run1->get("First"));
    QCOMPARE(Read::text(result, chapter->get("Title")), QStringLiteral("Chapter 1"));
    QCOMPARE(resolveDestination(result, chapter->get("Dest")), pageReference(result, 0));
    QVERIFY(!chapter->hasKey("First"));
    QCOMPARE(Read::integer(result, chapter->get("F")), 1);
    QCOMPARE(Read::items(result, chapter->get("C")).size(), size_t(3));
    const PDFDictionary* web = Read::dictionary(result, chapter->get("Next"));
    QCOMPARE(Read::text(result, web->get("Title")), QStringLiteral("Web"));
    const PDFDictionary* uri = Read::dictionary(result, web->get("A"));
    QCOMPARE(Read::name(result, uri->get("S")), QByteArray("URI"));
    QCOMPARE(Read::object(result, uri->get("URI")).getString(), QByteArray("http://example.com/"));
    const PDFDictionary* named = Read::dictionary(result, Read::dictionary(result, web->get("Next"))->get("A"));
    QCOMPARE(Read::name(result, named->get("N")), QByteArray("NextPage"));

    // Run 2 (B) has no outline of its own
    QVERIFY(!run2->hasKey("First"));

    // Run 3 (page 2 of A): Chapter 1 stays as a heading of its section, closed as it was - the
    // reopened part shows only Chapter 1 (Table 151: the descendants visible after reopening)
    QCOMPARE(Read::integer(result, run3->get("Count")), -1);
    const PDFDictionary* heading = Read::dictionary(result, run3->get("First"));
    QCOMPARE(Read::text(result, heading->get("Title")), QStringLiteral("Chapter 1"));
    QVERIFY(!heading->hasKey("Dest"));
    QVERIFY(!heading->hasKey("A"));
    QCOMPARE(Read::integer(result, heading->get("Count")), -1);
    const PDFDictionary* section = Read::dictionary(result, heading->get("First"));
    QCOMPARE(Read::text(result, section->get("Title")), QStringLiteral("Section 1.1"));
    QCOMPARE(resolveDestination(result, section->get("Dest")), pageReference(result, 2));
    QVERIFY(!heading->get("Next").isReference());
}

void DocumentMergerTest::outlineHostile()
{
    // B10 (SECURITY_TODO 8), chapter 6: an outline, whose /Next chain is a cycle and whose item is
    // shared by two parents, is read by one walk - each item once, the merge ends
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/Outlines 20 0 R");
    a.set(20, "<< /Type /Outlines /First 21 0 R /Last 24 0 R /Count 4 >>");
    a.set(21, "<< /Title (One) /Parent 20 0 R /Next 24 0 R /Dest [3 0 R /Fit] /First 22 0 R /Last 22 0 R /Count 2 >>");
    a.set(22, "<< /Title (Shared) /Parent 21 0 R /Next 23 0 R /Dest [3 0 R /Fit] >>");
    a.set(23, "<< /Title (Loop) /Parent 21 0 R /Next 22 0 R /Dest [3 0 R /Fit] /First 21 0 R >>");
    a.set(24, "<< /Title (Two) /Parent 20 0 R /Dest [3 0 R /Fit] /First 22 0 R /Last 22 0 R /Count 1 >>");
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();

    QElapsedTimer timer;
    timer.start();
    const PDFDocument result = check(mergeAll({ a.readPointer(), b }, Merger::OutlineMode::Join));
    QVERIFY(timer.elapsed() < 10000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
    checkOutlineLinks(result, Read::catalog(result)->get("Outlines"), 0);

    const QByteArray bytes = MergeTestPdf::writeDocument(result);
    QCOMPARE(Read::count(bytes, "(One)"), 1);
    QCOMPARE(Read::count(bytes, "(Two)"), 1);
    QCOMPARE(Read::count(bytes, "(Shared)"), 1);
    QCOMPARE(Read::count(bytes, "(Loop)"), 1);
}

void DocumentMergerTest::noOutline()
{
    // Q7, 7.5.2: no outline, no document parts, the version of the inputs, at least 1.7
    MergeTestPdf a = MergeTestPdf::createPages("A", 1);
    a.setVersion("1.4");
    MergeTestPdf b = MergeTestPdf::createPages("B", 1);
    b.setVersion("1.5");
    const PDFDocument result = check(mergeAll({ a.readPointer(), b.readPointer() }, Merger::OutlineMode::NoOutline));
    QCOMPARE(result.getInfo()->version, PDFVersion(1, 7));
    QVERIFY(!Read::catalog(result)->hasKey("Outlines"));
    QVERIFY(!Read::catalog(result)->hasKey("DPartRoot"));

    MergeTestPdf c = MergeTestPdf::createPages("C", 1);
    c.setVersion("2.0");
    const PDFDocument newer = check(mergeAll({ a.readPointer(), c.readPointer() }, Merger::OutlineMode::NoOutline));
    QCOMPARE(newer.getInfo()->version, PDFVersion(2, 0));
}

void DocumentMergerTest::attachmentsAndArticles()
{
    // 7.11.4, 12.4.3: two attachments "data.csv" are both kept ("data.csv_2"); an article of an input
    // taken whole keeps its beads on its pages; an embedded target follows the renamed file
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/Names << /EmbeddedFiles << /Names [(data.csv) 20 0 R] >> >> /Threads [30 0 R]", { { 1, "/B [31 0 R]" } });
    a.set(20, "<< /Type /Filespec /F (a.csv) /UF (a.csv) >>");
    a.set(30, "<< /Type /Thread /F 31 0 R /I << /Title (Story) >> >>");
    a.set(31, "<< /Type /Bead /T 30 0 R /N 31 0 R /V 31 0 R /P 3 0 R /R [0 0 100 100] >>");
    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/Names << /EmbeddedFiles << /Names [(data.csv) 20 0 R] >> >>", { { 1, "/Annots [21 0 R]" } });
    b.set(20, "<< /Type /Filespec /F (b.csv) /UF (b.csv) >>");
    b.set(21, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A << /S /GoToE /T << /R /C /N (data.csv) >> >> >>");

    const PDFDocument result = check(mergeAll({ a.readPointer(), b.readPointer() }));
    const PDFDictionary* files = Read::path(result, { "Names", "EmbeddedFiles" });
    QVERIFY(files);
    const std::vector<PDFObject> pairs = Read::items(result, files->get("Names"));
    QCOMPARE(pairs.size(), size_t(4));
    QCOMPARE(Read::object(result, pairs[0]).getString(), QByteArray("data.csv"));
    QCOMPARE(Read::text(result, Read::dictionary(result, pairs[1])->get("UF")), QStringLiteral("a.csv"));
    QCOMPARE(Read::object(result, pairs[2]).getString(), QByteArray("data.csv_2"));
    QCOMPARE(Read::text(result, Read::dictionary(result, pairs[3])->get("UF")), QStringLiteral("b.csv"));

    const PDFDictionary* link = Read::dictionary(result, Read::items(result, Read::dictionary(result, PDFObject::createReference(pageReference(result, 1)))->get("Annots")).front());
    const PDFDictionary* target = Read::dictionary(result, Read::dictionary(result, link->get("A"))->get("T"));
    QCOMPARE(Read::object(result, target->get("N")).getString(), QByteArray("data.csv_2"));

    const std::vector<PDFObject> threads = Read::items(result, Read::catalog(result)->get("Threads"));
    QCOMPARE(threads.size(), size_t(1));
    const PDFDictionary* bead = Read::dictionary(result, Read::dictionary(result, threads.front())->get("F"));
    QCOMPARE(bead->get("P").getReference(), pageReference(result, 0));
}

void DocumentMergerTest::documentScriptsRemoved()
{
    // Table 29, 12.6: the scripts of the documents, the action at the opening and the document
    // actions are removed
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/Names << /JavaScript << /Names [(init) << /S /JavaScript /JS (app.alert\\(1\\)) >>] >> >> /OpenAction << /S /JavaScript /JS (OPENSCRIPT) >> /AA << /WC << /S /JavaScript /JS (CLOSESCRIPT) >> >>");
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();
    const Merger::Result merged = mergeAll({ a.readPointer(), b });
    QVERIFY(merged.statistics.scriptsRemoved);
    const PDFDocument result = check(merged);
    const QByteArray bytes = MergeTestPdf::writeDocument(result);
    QVERIFY(!bytes.contains("app.alert"));
    QVERIFY(!bytes.contains("OPENSCRIPT"));
    QVERIFY(!bytes.contains("CLOSESCRIPT"));
    QVERIFY(!Read::catalog(result)->hasKey("OpenAction"));
    QVERIFY(!Read::catalog(result)->hasKey("AA"));
}

void DocumentMergerTest::outputIntents()
{
    // 14.11.5: the output intents of the catalog stay, when every input has the same; else they go
    // and the result says it
    auto withIntent = [](const QByteArray& prefix, const QByteArray& profile)
    {
        MergeTestPdf pdf = MergeTestPdf::createPages(prefix, 1, "/OutputIntents [<< /Type /OutputIntent /S /GTS_PDFX /OutputConditionIdentifier (FOGRA39) /DestOutputProfile 20 0 R >>]");
        pdf.set(20, MergeTestPdf::stream(profile, "/N 4"));
        return pdf.readPointer();
    };

    const Merger::Result same = mergeAll({ withIntent("A", "PROFILE1"), withIntent("B", "PROFILE1") });
    QVERIFY(!same.statistics.outputIntentsRemoved);
    QCOMPARE(Read::items(check(same), Read::catalog(check(same))->get("OutputIntents")).size(), size_t(1));

    const Merger::Result different = mergeAll({ withIntent("A", "PROFILE1"), withIntent("B", "PROFILE2") });
    QVERIFY(different.statistics.outputIntentsRemoved);
    QVERIFY(!Read::catalog(check(different))->hasKey("OutputIntents"));
}

void DocumentMergerTest::metadataAndIdentifier()
{
    // 14.3.2, 14.3.3, 14.4, R7: the result never declares PDF/A, PDF/UA or PDF/X (it is not checked
    // against them); it has a new /ID of two equal strings, a new /Info and, at PDF 2.0, XMP with
    // the same dates
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/Metadata 20 0 R");
    a.set(20, MergeTestPdf::stream("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"><rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\" pdfaid:part=\"1\" pdfaid:conformance=\"B\" pdfuaid:part=\"1\"/></rdf:RDF></x:xmpmeta>",
                                   "/Type /Metadata /Subtype /XML"));
    a.set(90, "<< /Title (Alpha) /GTS_PDFXVersion (PDF/X-4) >>");
    a.setTrailerEntries("/Info 90 0 R /ID [<00112233445566778899AABBCCDDEEFF> <00112233445566778899AABBCCDDEEFF>]");
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 1).readPointer();

    const PDFDocument result = check(mergeAll({ a.readPointer(), b }));
    const QByteArray bytes = MergeTestPdf::writeDocument(result);
    QVERIFY(!bytes.contains("pdfaid"));
    QVERIFY(!bytes.contains("pdfuaid"));
    QVERIFY(!bytes.contains("GTS_PDFXVersion"));

    const std::vector<PDFObject> identifier = Read::items(result, Read::trailer(result)->get("ID"));
    QCOMPARE(identifier.size(), size_t(2));
    QCOMPARE(Read::object(result, identifier[0]).getString().size(), 16);
    QCOMPARE(Read::object(result, identifier[0]), Read::object(result, identifier[1]));
    QVERIFY(Read::object(result, identifier[0]).getString() != QByteArray::fromHex("00112233445566778899AABBCCDDEEFF"));

    const PDFDictionary* info = Read::dictionary(result, Read::trailer(result)->get("Info"));
    QVERIFY(info);
    QVERIFY(!Read::text(result, info->get("Producer")).isEmpty());
    QCOMPARE(Read::object(result, info->get("CreationDate")), Read::object(result, info->get("ModDate")));
    QVERIFY(!info->hasKey("Title"));

    // PDF 2.0 (document parts): the dates are in XMP too
    QCOMPARE(result.getInfo()->version, PDFVersion(2, 0));
    const PDFObject& metadata = Read::object(result, Read::catalog(result)->get("Metadata"));
    QVERIFY(metadata.isStream());
    const QByteArray xmp = *metadata.getStream()->getContent();
    const QDateTime creation = PDFEncoding::convertToDateTime(Read::object(result, info->get("CreationDate")).getString());
    const qsizetype start = xmp.indexOf("<xmp:CreateDate>") + 16;
    const QDateTime xmpCreation = QDateTime::fromString(QString::fromUtf8(xmp.mid(start, xmp.indexOf("</xmp:CreateDate>") - start)), Qt::ISODate);
    QVERIFY(xmpCreation.isValid());
    QCOMPARE(xmpCreation.toSecsSinceEpoch(), creation.toSecsSinceEpoch());
}

void DocumentMergerTest::relativeUris()
{
    // Table 29, 12.6.4.8: inputs with different bases - the relative URIs of each become absolute by
    // its own base, the result has no base; one base of all inputs stays
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/URI << /Base (http://a.example/docs/) >>", { { 1, "/Annots [20 0 R]" } });
    a.set(20, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A << /S /URI /URI (page.html) >> >>");
    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/URI << /Base (http://b.example/) >>", { { 1, "/Annots [20 0 R]" } });
    b.set(20, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A << /S /URI /URI (x.html) >> >>");

    auto uriOf = [](const PDFDocument& document, size_t page)
    {
        const PDFDictionary* link = Read::dictionary(document, Read::items(document, Read::dictionary(document, PDFObject::createReference(pageReference(document, page)))->get("Annots")).front());
        return Read::object(document, Read::dictionary(document, link->get("A"))->get("URI")).getString();
    };

    const PDFDocument different = check(mergeAll({ a.readPointer(), b.readPointer() }));
    QVERIFY(!Read::catalog(different)->hasKey("URI"));
    QCOMPARE(uriOf(different, 0), QByteArray("http://a.example/docs/page.html"));
    QCOMPARE(uriOf(different, 1), QByteArray("http://b.example/x.html"));

    const PDFDocument same = check(mergeAll({ a.readPointer(), a.readPointer() }));
    QVERIFY(Read::catalog(same)->hasKey("URI"));
    QCOMPARE(uriOf(same, 0), QByteArray("page.html"));

    // Join: an item of the outline of an input, whose action is in the item itself, is rewritten
    // when the outline is joined - its relative URI becomes absolute by the base of its input too
    MergeTestPdf c = MergeTestPdf::createPages("C", 1, "/URI << /Base (http://c.example/) >> /Outlines 30 0 R");
    c.set(30, "<< /Type /Outlines /First 31 0 R /Last 31 0 R /Count 1 >>");
    c.set(31, "<< /Title (Web) /Parent 30 0 R /A << /S /URI /URI (help.html) >> >>");
    const PDFDocument joined = check(mergeAll({ a.readPointer(), c.readPointer() }, Merger::OutlineMode::Join));
    const PDFDictionary* partC = Read::dictionary(joined, Read::dictionary(joined, Read::path(joined, { "Outlines" })->get("First"))->get("Next"));
    QVERIFY(partC);
    const PDFDictionary* web = Read::dictionary(joined, partC->get("First"));
    QVERIFY(web);
    QCOMPARE(Read::object(joined, Read::dictionary(joined, web->get("A"))->get("URI")).getString(), QByteArray("http://c.example/help.html"));
}

void DocumentMergerTest::extensionsAndViewerPreferences()
{
    // 7.12: the extensions of all inputs, the highest level of each prefix; Table 29: the
    // preferences of the viewer and the page layout of the first input
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/Extensions << /ADBE << /BaseVersion /1.7 /ExtensionLevel 8 >> >> /ViewerPreferences << /DisplayDocTitle true >> /PageLayout /TwoColumnLeft /PageMode /UseOutlines");
    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/Extensions << /ADBE << /BaseVersion /1.7 /ExtensionLevel 3 >> /GLGR << /BaseVersion /1.7 /ExtensionLevel 1 >> >> /PageLayout /SinglePage");

    const PDFDocument result = check(mergeAll({ a.readPointer(), b.readPointer() }, Merger::OutlineMode::NoOutline));
    const PDFDictionary* extensions = Read::path(result, { "Extensions" });
    QVERIFY(extensions);
    QCOMPARE(Read::integer(result, Read::dictionary(result, extensions->get("ADBE"))->get("ExtensionLevel")), 8);
    QVERIFY(extensions->hasKey("GLGR"));
    QVERIFY(Read::object(result, Read::path(result, { "ViewerPreferences" })->get("DisplayDocTitle")).getBool());
    QCOMPARE(Read::name(result, Read::catalog(result)->get("PageLayout")), QByteArray("TwoColumnLeft"));

    // /UseOutlines without an outline would show an empty panel
    QVERIFY(!Read::catalog(result)->hasKey("PageMode"));
}

void DocumentMergerTest::permissions()
{
    // Owner answer Q2 (variant c), R2, 7.6.4.2 Table 22: bit 11 assembles; the result is not
    // encrypted, so a document, which restricts anything else (bit 10 is deprecated in PDF 2.0),
    // needs its owner password; Diff does not check (its result is no document of the user)
    using Permission = PDFSecurityHandler::Permission;
    const uint32_t all = uint32_t(Permission::PrintLowResolution) | uint32_t(Permission::Modify) | uint32_t(Permission::CopyContent) | uint32_t(Permission::ModifyInteractiveItems) |
                         uint32_t(Permission::ModifyFormFields) | uint32_t(Permission::Accessibility) | uint32_t(Permission::Assemble) | uint32_t(Permission::PrintHighResolution);
    const PDFDocument plain = MergeTestPdf::createPages("A", 1).read();
    const PDFDocumentPointer other = MergeTestPdf::createPages("B", 1).readPointer();

    auto open = [&plain](uint32_t permissions, const QString& password)
    {
        return PDFDocumentPointer(new PDFDocument(MergeTestPdf::readBytes(MergeTestPdf::encrypt(plain, QString(), QStringLiteral("owner"), permissions), password, nullptr, !password.isEmpty())));
    };

    const PDFDocumentPointer noAssemble = open(all & ~uint32_t(Permission::Assemble), QString());
    QCOMPARE(Merger::getPermission(noAssemble.data()), Merger::Permission::AssembleDenied);
    const Merger::Result refused = mergeAll({ noAssemble, other });
    QVERIFY(!refused.isSuccess);
    QVERIFY(!refused.errorMessage.isEmpty());

    const PDFDocumentPointer noPrint = open(all & ~uint32_t(Permission::PrintHighResolution), QString());
    QCOMPARE(Merger::getPermission(noPrint.data()), Merger::Permission::OwnerPasswordRequired);
    QVERIFY(!mergeAll({ noPrint, other }).isSuccess);

    const PDFDocumentPointer noAccessibility = open(all & ~uint32_t(Permission::Accessibility), QString());
    QCOMPARE(Merger::getPermission(noAccessibility.data()), Merger::Permission::Permitted);

    const PDFDocumentPointer owner = open(all & ~uint32_t(Permission::Assemble), QStringLiteral("owner"));
    QCOMPARE(Merger::getPermission(owner.data()), Merger::Permission::Permitted);
    const Merger::Result merged = mergeAll({ owner, other });
    QVERIFY(merged.isSuccess);

    // The result is not encrypted: it is read without a password
    bool isRead = false;
    const PDFDocument result = MergeTestPdf::readBytes(MergeTestPdf::writeDocument(merged.document), QString(), &isRead);
    QVERIFY(isRead);
    QVERIFY(!Read::trailer(result)->hasKey("Encrypt"));
    QCOMPARE(result.getStorage().getSecurityHandler()->getMode(), EncryptionMode::None);
    QVERIFY(MergeTestPdf::writeDocument(result).contains("APAGE1"));

    // Diff
    std::vector<Merger::Source> sources = { Merger::Source::createDocument(noAssemble, QString()), Merger::Source::createDocument(other, QString()) };
    Merger::Settings settings;
    settings.checkPermissions = false;
    QVERIFY(Merger::merge(sources, allPages(sources), settings, nullptr, nullptr).isSuccess);

    // The single document way checks too (one PDF input with an image)
    const PDFDocumentPointer singleNoPrint = open(all & ~uint32_t(Permission::PrintLowResolution), QString());
    std::vector<Merger::Source> single = { Merger::Source::createDocument(singleNoPrint, QString()), Merger::Source::createBlank() };
    std::vector<Merger::Item> singleItems(2);
    singleItems[0].source = 0;
    singleItems[1].source = 1;
    singleItems[1].pageSize = QSizeF(100, 100);
    QVERIFY(!Merger::merge(single, singleItems, Merger::Settings(), nullptr, nullptr).isSuccess);
}

void DocumentMergerTest::singleDocumentKeepsCatalog()
{
    // R3: one PDF input with all its pages (reordered, a blank page added) keeps its catalog - the
    // form, the scripts, the action at the opening, the structure; only the page tree and the
    // labels follow the order; R7: the declaration of PDF/A goes
    MergeTestPdf a = MergeTestPdf::createPages("A", 3, "/AcroForm << /Fields [30 0 R] >> /Names << /JavaScript << /Names [(init) << /S /JavaScript /JS (SCRIPTMARK) >>] >> >> "
                                                       "/OpenAction << /S /JavaScript /JS (OPENMARK) >> /StructTreeRoot 50 0 R /MarkInfo << /Marked true >> /Metadata 20 0 R /PageLabels << /Nums [0 << /S /A >>] >>",
                                               { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    a.set(50, "<< /Type /StructTreeRoot /K 51 0 R >>");
    a.set(51, "<< /Type /StructElem /S /Document /P 50 0 R >>");
    a.set(20, MergeTestPdf::stream("<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                                   "<rdf:Description rdf:about=\"\" xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\"><pdfaid:part>2</pdfaid:part><pdfaid:conformance>B</pdfaid:conformance></rdf:Description>"
                                   "</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>", "/Type /Metadata /Subtype /XML"));

    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a.readPointer(), QString()), Merger::Source::createBlank() };
    std::vector<Merger::Item> items(4);
    items[0].pageIndex = 2;
    items[1].pageIndex = 0;
    items[2].pageIndex = 1;
    items[3].source = 1;
    items[3].pageSize = QSizeF(200, 300);
    const Merger::Result merged = Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr);
    QVERIFY(merged.statistics.isSingleDocument);
    QCOMPARE(merged.statistics.conformanceRemoved, QStringList({ QStringLiteral("PDF/A") }));
    const PDFDocument result = check(merged);

    const QByteArray contents = Read::pageContents(result);
    QCOMPARE(Read::count(contents, "APAGE"), 3);
    QVERIFY(contents.indexOf("APAGE3") < contents.indexOf("APAGE1"));
    QVERIFY(contents.indexOf("APAGE1") < contents.indexOf("APAGE2"));
    QCOMPARE(result.getCatalog()->getPage(3)->getMediaBox(), QRectF(0, 0, 200, 300));

    const QByteArray bytes = MergeTestPdf::writeDocument(result);
    QVERIFY(bytes.contains("SCRIPTMARK"));
    QVERIFY(bytes.contains("OPENMARK"));
    QVERIFY(!bytes.contains("pdfaid"));
    QVERIFY(Read::catalog(result)->hasKey("StructTreeRoot"));
    QCOMPARE(Read::items(result, Read::path(result, { "AcroForm" })->get("Fields")).size(), size_t(1));

    // The labels follow the pages: C, A, B, then the blank page gets its place
    QCOMPARE(result.getCatalog()->getPageLabel(0), QStringLiteral("C"));
    QCOMPARE(result.getCatalog()->getPageLabel(1), QStringLiteral("A"));
    QCOMPARE(result.getCatalog()->getPageLabel(2), QStringLiteral("B"));
}

void DocumentMergerTest::singleDocumentWithImages_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("document parts") << int(Merger::OutlineMode::DocumentParts);
    QTest::newRow("join") << int(Merger::OutlineMode::Join);
    QTest::newRow("no outline") << int(Merger::OutlineMode::NoOutline);
}

void DocumentMergerTest::singleDocumentWithImages()
{
    // Owner answer Q18: one PDF input with images keeps its catalog (R3: the form, the scripts),
    // but the outline mode applies - Document parts: a bookmark and a document part (14.12) for
    // every input, the outline of the PDF below its bookmark; Join as for more inputs (the part
    // closed, Table 151); No outline: none
    QFETCH(int, mode);
    const Merger::OutlineMode outlineMode = Merger::OutlineMode(mode);

    MergeTestPdf a = MergeTestPdf::createPages("A", 2, "/AcroForm << /Fields [30 0 R] >> /Names << /JavaScript << /Names [(init) << /S /JavaScript /JS (SCRIPTMARK) >>] >> /Dests << /Names [(two) [4 0 R /Fit]] >> >> "
                                                       "/Outlines 20 0 R /PageMode /UseOutlines /DPartRoot 60 0 R", { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    a.set(20, "<< /Type /Outlines /First 21 0 R /Last 21 0 R /Count 1 >>");
    a.set(21, "<< /Title (Chapter) /Parent 20 0 R /Dest (two) >>");
    a.set(60, "<< /Type /DPartRoot /DPartRootNode 61 0 R >>");
    a.set(61, "<< /Type /DPart /Parent 60 0 R >>");

    QImage image(30, 30, QImage::Format_RGB32);
    image.fill(Qt::green);
    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a.readPointer(), QStringLiteral("Alpha.pdf")),
                                            Merger::Source::createImage(encodeImage(image, "png"), QByteArray(), 0.0, QStringLiteral("photo.png")) };
    std::vector<Merger::Item> items(3);
    items[1].pageIndex = 1;
    items[2].source = 1;
    Merger::Settings settings;
    settings.outline = outlineMode;
    const Merger::Result merged = Merger::merge(sources, items, settings, nullptr, nullptr);
    QVERIFY(merged.statistics.isSingleDocument);
    const PDFDocument result = check(merged);
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));

    // The catalog of the PDF stays
    QVERIFY(MergeTestPdf::writeDocument(result).contains("SCRIPTMARK"));
    QCOMPARE(Read::items(result, Read::path(result, { "AcroForm" })->get("Fields")).size(), size_t(1));

    const PDFDictionary* outline = Read::path(result, { "Outlines" });
    if (outlineMode == Merger::OutlineMode::NoOutline)
    {
        QVERIFY(!outline);
        QVERIFY(!Read::catalog(result)->hasKey("DPartRoot"));
        QVERIFY(!Read::catalog(result)->hasKey("PageMode"));
        return;
    }

    QVERIFY(outline);
    checkOutlineLinks(result, Read::catalog(result)->get("Outlines"), 0);
    const PDFDictionary* first = Read::dictionary(result, outline->get("First"));
    const PDFDictionary* second = Read::dictionary(result, first->get("Next"));
    QCOMPARE(Read::text(result, first->get("Title")), QStringLiteral("Alpha"));
    QCOMPARE(Read::text(result, second->get("Title")), QStringLiteral("photo.png"));
    QCOMPARE(resolveDestination(result, first->get("Dest")), pageReference(result, 0));
    QCOMPARE(resolveDestination(result, second->get("Dest")), pageReference(result, 2));
    QCOMPARE(Read::integer(result, first->get("Count")), outlineMode == Merger::OutlineMode::Join ? -1 : 1);
    const PDFDictionary* chapter = Read::dictionary(result, first->get("First"));
    QCOMPARE(Read::text(result, chapter->get("Title")), QStringLiteral("Chapter"));
    QCOMPARE(resolveDestination(result, chapter->get("Dest")), pageReference(result, 1));

    // A part per input, the parts of the document are replaced (14.12)
    QCOMPARE(result.getInfo()->version, PDFVersion(2, 0));
    const PDFDictionary* rootNode = Read::path(result, { "DPartRoot", "DPartRootNode" });
    QVERIFY(rootNode);
    const std::vector<PDFObject> parts = Read::items(result, rootNode->get("DParts"));
    QCOMPARE(parts.size(), size_t(2));
    const PDFDictionary* part1 = Read::dictionary(result, Read::items(result, parts[0]).front());
    QCOMPARE(part1->get("Start").getReference(), pageReference(result, 0));
    QCOMPARE(part1->get("End").getReference(), pageReference(result, 1));
}

void DocumentMergerTest::singleDocumentWithOmittedPages()
{
    // R1, B7: one input with omitted pages is the result of Remove Pages - the outline of all levels,
    // the named destinations of the removed page are gone
    MergeTestPdf a = MergeTestPdf::createPages("A", 3, "/Names << /Dests << /Names [(toThree) [5 0 R /Fit]] >> >> /Outlines 40 0 R");
    a.set(40, "<< /Type /Outlines /First 41 0 R /Last 41 0 R /Count 2 >>");
    a.set(41, "<< /Title (Top) /Parent 40 0 R /Dest [3 0 R /Fit] /First 42 0 R /Last 42 0 R /Count 1 >>");
    a.set(42, "<< /Title (Deep) /Parent 41 0 R /Dest [5 0 R /Fit] >>");

    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a.readPointer(), QString()) };
    std::vector<Merger::Item> items(2);
    items[0].pageIndex = 1;
    items[1].pageIndex = 0;
    const PDFDocument result = check(Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(2));
    const QByteArray bytes = MergeTestPdf::writeDocument(result);
    QVERIFY(!bytes.contains("APAGE3"));
    QVERIFY(!bytes.contains("Deep"));
    QVERIFY(!bytes.contains("toThree"));
    QVERIFY(bytes.contains("(Top)"));

    // Owner answer Q1: the tags of an input, which is not taken whole, are removed (as by Remove
    // Pages) - the result says it, as the result of more inputs does
    MergeTestPdf tagged = MergeTestPdf::createPages("T", 2, "/StructTreeRoot 50 0 R /MarkInfo << /Marked true >>", { { 1, "/StructParents 0" } });
    tagged.set(50, "<< /Type /StructTreeRoot /K 51 0 R /ParentTree << /Nums [0 [51 0 R]] >> /ParentTreeNextKey 1 >>");
    tagged.set(51, "<< /Type /StructElem /S /P /P 50 0 R /Pg 3 0 R /K 0 >>");
    std::vector<Merger::Source> taggedSources = { Merger::Source::createDocument(tagged.readPointer(), QString()) };
    std::vector<Merger::Item> firstPage(1);
    const Merger::Result untagged = Merger::merge(taggedSources, firstPage, Merger::Settings(), nullptr, nullptr);
    QVERIFY(untagged.statistics.isSingleDocument);
    QVERIFY(untagged.statistics.structureTreeRemoved);
    QVERIFY(!Read::catalog(check(untagged))->hasKey("StructTreeRoot"));

    // Every page of it: the tags stay, nothing is said
    std::vector<Merger::Item> bothPages(2);
    bothPages[0].pageIndex = 1;
    const Merger::Result kept = Merger::merge(taggedSources, bothPages, Merger::Settings(), nullptr, nullptr);
    QVERIFY(!kept.statistics.structureTreeRemoved);
    QVERIFY(Read::catalog(check(kept))->hasKey("StructTreeRoot"));
}

void DocumentMergerTest::sameDocumentTwice()
{
    // R20: the same document given twice is two inputs (its object numbers by the index of the
    // source): every collision of its names is renamed
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/Names << /Dests << /Names [(intro) [3 0 R /Fit]] >> >> /AcroForm << /Fields [30 0 R] >>", { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    const PDFDocumentPointer document = a.readPointer();

    const Merger::Result merged = mergeAll({ document, document });
    QCOMPARE(merged.statistics.destinationsRenamed, 1);
    QCOMPARE(merged.statistics.fieldsRenamed, 1);
    const PDFDocument result = check(merged);
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(2));
    QVERIFY(pageReference(result, 0) != pageReference(result, 1));
    QCOMPARE(resolveDestination(result, PDFObject::createString("intro")), pageReference(result, 0));
    QCOMPARE(resolveDestination(result, PDFObject::createString("intro_2")), pageReference(result, 1));
}

void DocumentMergerTest::dynamicXfaRefused()
{
    // Table 29 /NeedsRendering: a dynamic XFA form has no PDF pages, which could be merged
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/AcroForm << /Fields [] /XFA (XFA) >> /NeedsRendering true");
    const Merger::Result merged = mergeAll({ a.readPointer(), MergeTestPdf::createPages("B", 1).readPointer() });
    QVERIFY(!merged.isSuccess);
    QVERIFY(!merged.errorMessage.isEmpty());

    // A static XFA form is merged as its AcroForm, the XFA goes
    MergeTestPdf b = MergeTestPdf::createPages("A", 1, "/AcroForm << /Fields [] /XFA (XFAMARKER) >>");
    const Merger::Result staticForm = mergeAll({ b.readPointer(), MergeTestPdf::createPages("B", 1).readPointer() });
    QVERIFY(staticForm.statistics.xfaRemoved);
    QVERIFY(!MergeTestPdf::writeDocument(check(staticForm)).contains("XFAMARKER"));
}

void DocumentMergerTest::hostileFieldTree()
{
    // Chapter 6: a field tree with a cycle of /Kids and a kid shared by two fields is walked once
    MergeTestPdf a = MergeTestPdf::createPages("A", 1, "/AcroForm << /Fields [30 0 R 32 0 R 34 0 R] >>", { { 1, "/Annots [33 0 R]" } });
    a.set(30, "<< /T (a) /Kids [31 0 R] >>");
    a.set(31, "<< /T (b) /Parent 30 0 R /Kids [30 0 R] >>");
    a.set(32, "<< /T (c) /Kids [33 0 R] >>");
    a.set(34, "<< /T (d) /Kids [33 0 R] >>");
    a.set(33, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (e) /Parent 32 0 R /Rect [0 0 10 10] /P 3 0 R >>");
    MergeTestPdf b = MergeTestPdf::createPages("B", 1, "/AcroForm << /Fields [30 0 R] >>", { { 1, "/Annots [30 0 R]" } });
    b.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (a) /Rect [0 0 10 10] /P 3 0 R >>");

    QElapsedTimer timer;
    timer.start();
    const Merger::Result merged = mergeAll({ a.readPointer(), b.readPointer() });
    QVERIFY(timer.elapsed() < 10000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")));
    QVERIFY(merged.isSuccess);
    QCOMPARE(merged.statistics.fieldsRenamed, 1);
}

void DocumentMergerTest::leftOutInput()
{
    // Owner answer Q5: an input, whose copy fails during the run, is left out with its reason, the
    // others are merged; no input left is a failure
    PDFDocument broken = MergeTestPdf::createPages("B", 2).read();
    {
        PDFDocumentBuilder builder(&broken);
        PDFObject deep = PDFObject::createInteger(1);
        for (int i = 0; i < 600; ++i)
        {
            PDFArrayBuilder array;
            array.appendItem(deep);
            deep = PDFObject::createArray(std::move(array));
        }
        PDFDictionaryBuilder catalog;
        catalog.addEntry(PDFInplaceOrMemoryString("Deep"), PDFObject::createReference(builder.addObject(deep)));
        builder.mergeTo(builder.getCatalogReference(), PDFObject::createDictionary(std::move(catalog)));
        broken = builder.build();
    }
    const PDFDocumentPointer good = MergeTestPdf::createPages("A", 1).readPointer();
    const PDFDocumentPointer good2 = MergeTestPdf::createPages("C", 1).readPointer();

    const Merger::Result merged = mergeAll({ good, PDFDocumentPointer(new PDFDocument(broken)), good2 });
    QVERIFY(merged.isSuccess);
    QCOMPARE(merged.leftOut.size(), size_t(1));
    QCOMPARE(merged.leftOut.front().source, size_t(1));
    QCOMPARE(merged.leftOut.front().pageCount, 2);
    QVERIFY(!merged.leftOut.front().reason.isEmpty());
    const PDFDocument result = check(merged);
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(2));
    QVERIFY(!MergeTestPdf::writeDocument(result).contains("BPAGE"));

    const Merger::Result nothing = mergeAll({ PDFDocumentPointer(new PDFDocument(broken)), PDFDocumentPointer(new PDFDocument(broken)) });
    QVERIFY(!nothing.isSuccess);
    QVERIFY(!nothing.errorMessage.isEmpty());

    // An image, which cannot be read, is left out too
    std::vector<Merger::Source> sources = { Merger::Source::createDocument(good, QString()), Merger::Source::createImage(QByteArray("not an image"), QByteArray(), 0.0, QStringLiteral("x.png")) };
    std::vector<Merger::Item> items(2);
    items[1].source = 1;
    const Merger::Result image = Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr);
    QVERIFY(image.isSuccess);
    QCOMPARE(image.leftOut.size(), size_t(1));
    QCOMPARE(check(image).getCatalog()->getPageCount(), size_t(1));
}

void DocumentMergerTest::cancellationAndProgress()
{
    // Chapter 6: a canceled merge makes no document; the progress has a step per page
    const PDFDocumentPointer a = MergeTestPdf::createPages("A", 3).readPointer();
    const PDFDocumentPointer b = MergeTestPdf::createPages("B", 2).readPointer();
    std::vector<Merger::Source> sources = { Merger::Source::createDocument(a, QString()), Merger::Source::createDocument(b, QString()) };

    MergerTestControl control;
    control.cancel();
    const Merger::Result canceled = Merger::merge(sources, allPages(sources), Merger::Settings(), &control, nullptr);
    QVERIFY(canceled.isCanceled);
    QVERIFY(!canceled.isSuccess);

    MergerTestProgress progress;
    const Merger::Result merged = Merger::merge(sources, allPages(sources), Merger::Settings(), nullptr, &progress);
    QVERIFY(merged.isSuccess);
    QCOMPARE(progress.m_stepCount.load(), size_t(5));
    QCOMPARE(progress.m_steps.load(), size_t(5));
    QVERIFY(progress.m_isFinished.load());
}

void DocumentMergerTest::parallelMerges()
{
    // Chapter 6: the merger is reentrant - 8 merges of the same inputs at once give the same result
    MergeTestPdf a = MergeTestPdf::createPages("A", 3, "/Names << /Dests << /Names [(intro) [3 0 R /Fit]] >> >> /AcroForm << /Fields [30 0 R] >>", { { 1, "/Annots [30 0 R]" } });
    a.set(30, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /Rect [0 0 10 10] /P 3 0 R >>");
    const PDFDocumentPointer document = a.readPointer();

    std::vector<Merger::Result> results(8);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < results.size(); ++i)
    {
        threads.emplace_back([&results, &document, i]() { results[i] = mergeAll({ document, document, document }); });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    for (const Merger::Result& result : results)
    {
        QVERIFY(result.isSuccess);
        QCOMPARE(result.statistics.pageCount, 9);
        QCOMPARE(result.statistics.fieldsRenamed, 2);
        QCOMPARE(result.statistics.destinationsRenamed, 2);
    }
}

void DocumentMergerTest::blankPages()
{
    // 5.4 of the plan, Table 31: a document of blank pages of one size - /MediaBox and /Resources
    // (both required and inheritable) on the root of the page tree, the pages without /Contents;
    // 7.5.2: PDF 1.7; 14.4: /ID
    std::vector<Merger::Source> sources = { Merger::Source::createBlank() };
    std::vector<Merger::Item> items(3);
    for (Merger::Item& item : items)
    {
        item.pageSize = QSizeF(210.0 * 72.0 / 25.4, 297.0 * 72.0 / 25.4);
    }
    Merger::Settings settings;
    settings.outline = Merger::OutlineMode::NoOutline;
    const PDFDocument result = check(Merger::merge(sources, items, settings, nullptr, nullptr));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));
    QCOMPARE(result.getInfo()->version, PDFVersion(1, 7));

    const PDFDictionary* root = Read::path(result, { "Pages" });
    const std::vector<PDFObject> mediaBox = Read::items(result, root->get("MediaBox"));
    QCOMPARE(mediaBox.size(), size_t(4));
    QVERIFY(std::abs(Read::object(result, mediaBox[2]).getReal() - 595.276) < 0.001);
    QVERIFY(std::abs(Read::object(result, mediaBox[3]).getReal() - 841.890) < 0.001);
    QVERIFY(Read::dictionary(result, root->get("Resources")));
    for (size_t i = 0; i < 3; ++i)
    {
        const PDFDictionary* page = Read::dictionary(result, PDFObject::createReference(pageReference(result, i)));
        QVERIFY(!page->hasKey("MediaBox"));
        QVERIFY(!page->hasKey("Resources"));
        QVERIFY(!page->hasKey("Contents"));
        QVERIFY(std::abs(result.getCatalog()->getPage(i)->getMediaBox().width() - 595.276) < 0.001);
    }
    QCOMPARE(Read::items(result, Read::trailer(result)->get("ID")).size(), size_t(2));

    // The limits of a side (PDF Reference 1.7, Table C.1): 3 and 14 400 points pass, 2.9 and 14 401 do not
    for (const QSizeF& size : { QSizeF(3.0, 3.0), QSizeF(14400.0, 14400.0) })
    {
        items.front().pageSize = size;
        QVERIFY(Merger::merge(sources, { items.front() }, settings, nullptr, nullptr).isSuccess);
    }
    for (const QSizeF& size : { QSizeF(2.9, 100.0), QSizeF(100.0, 14401.0) })
    {
        items.front().pageSize = size;
        QVERIFY(!Merger::merge(sources, { items.front() }, settings, nullptr, nullptr).isSuccess);
    }
}

void DocumentMergerTest::imageJpegUnchanged()
{
    // 5.3 of the plan, 7.4.8: a JPEG is written with its bytes unchanged (DCTDecode); Q6: the page
    // has the size by the resolution of the image (120 x 80 pixels at 300 dpi = 28.8 x 19.2 points)
    QImage image(120, 80, QImage::Format_RGB32);
    image.fill(QColor(200, 30, 40));
    image.setDotsPerMeterX(11811);
    image.setDotsPerMeterY(11811);
    const QByteArray jpeg = encodeImage(image, "jpeg");

    QImage gray(40, 20, QImage::Format_Grayscale8);
    gray.fill(128);
    gray.setDotsPerMeterX(11811);
    gray.setDotsPerMeterY(11811);
    const QByteArray grayJpeg = encodeImage(gray, "jpeg");

    std::vector<Merger::Source> sources = { Merger::Source::createImage(jpeg, QByteArray(), 0.0, QStringLiteral("photo.jpg")), Merger::Source::createImage(grayJpeg, QByteArray("jpeg"), 0.0, QStringLiteral("gray.jpg")) };
    const PDFDocument result = check(Merger::merge(sources, allPages(sources), Merger::Settings(), nullptr, nullptr));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(2));

    const PDFDictionary* xObject = getPageImage(result, 0);
    QVERIFY(xObject);
    const PDFObjectReference imageReference = Read::dictionary(result, Read::dictionary(result, Read::dictionary(result, PDFObject::createReference(pageReference(result, 0)))->get("Resources"))->get("XObject"))->getValue(0).getReference();
    const PDFObject& stream = result.getObjectByReference(imageReference);
    QVERIFY(stream.isStream());
    QCOMPARE(*stream.getStream()->getContent(), jpeg);
    QCOMPARE(Read::name(result, xObject->get("Filter")), QByteArray("DCTDecode"));
    QCOMPARE(Read::name(result, xObject->get("ColorSpace")), QByteArray("DeviceRGB"));
    QCOMPARE(Read::integer(result, xObject->get("Width")), 120);
    QCOMPARE(Read::integer(result, xObject->get("BitsPerComponent")), 8);
    const QRectF mediaBox = result.getCatalog()->getPage(0)->getMediaBox();
    QVERIFY(std::abs(mediaBox.width() - 28.8) < 0.01);
    QVERIFY(std::abs(mediaBox.height() - 19.2) < 0.01);

    QCOMPARE(Read::name(result, getPageImage(result, 1)->get("ColorSpace")), QByteArray("DeviceGray"));
}

void DocumentMergerTest::imageJpegOrientation()
{
    // 5.3: the orientation of EXIF (6 = rotate 90 degrees clockwise) - the bytes stay, the content
    // matrix turns the image: the stored 120 x 80 image is shown 80 x 120, the unit square maps by
    // [0 -H W 0 0 H] (8.3.3 and 8.9.4: the image space has its origin at the bottom left)
    QImage image(120, 80, QImage::Format_RGB32);
    image.fill(Qt::blue);
    image.setDotsPerMeterX(11811);
    image.setDotsPerMeterY(11811);
    QByteArray jpeg = encodeImage(image, "jpeg");

    // APP1 Exif after SOI (CIPA DC-008, 4.6.4: the TIFF header and one IFD with Orientation = 6)
    const QByteArray exif = QByteArray::fromHex("457869660000" "49492a0008000000" "0100" "120103000100000006000000" "00000000");
    QByteArray segment = QByteArray::fromHex("ffe1");
    segment.append(char(0));
    segment.append(char(exif.size() + 2));
    segment.append(exif);
    jpeg.insert(2, segment);

    std::optional<PDFImagePageFactory::Frame> frame = PDFImagePageFactory::readFrame(jpeg, QByteArray(), 0, 0.0, nullptr);
    QVERIFY(frame);
    QCOMPARE(frame->image.size(), QSize(80, 120));
    QCOMPARE(frame->jpegData, jpeg);
    QCOMPARE(frame->transformation, QImageIOHandler::TransformationRotate90);

    std::vector<Merger::Source> sources = { Merger::Source::createImage(jpeg, QByteArray(), 0.0, QString()) };
    const PDFDocument result = check(Merger::merge(sources, allPages(sources), Merger::Settings(), nullptr, nullptr));
    const QRectF mediaBox = result.getCatalog()->getPage(0)->getMediaBox();
    QVERIFY(std::abs(mediaBox.width() - 19.2) < 0.01);
    QVERIFY(std::abs(mediaBox.height() - 28.8) < 0.01);
    const PDFDictionary* page = Read::dictionary(result, PDFObject::createReference(pageReference(result, 0)));
    const QByteArray content = *Read::object(result, page->get("Contents")).getStream()->getContent();
    const QList<QByteArray> tokens = content.simplified().split(' ');
    const qsizetype cm = tokens.indexOf("cm");
    QVERIFY2(cm >= 6, content.constData());
    const std::vector<double> expected = { 0.0, -28.8, 19.2, 0.0, 0.0, 28.8 };
    for (size_t i = 0; i < expected.size(); ++i)
    {
        QVERIFY2(std::abs(tokens[cm - 6 + qsizetype(i)].toDouble() - expected[i]) < 0.001, content.constData());
    }
}

void DocumentMergerTest::imageJpegCmyk()
{
    // 5.3: a CMYK JPEG with the Adobe marker (APP14) has inverted samples - /Decode [1 0 1 0 1 0 1 0]
    QImage image(16, 16, QImage::Format_CMYK8888);
    image.fill(QColor::fromCmyk(10, 20, 30, 40));
    const QByteArray jpeg = encodeImage(image, "jpeg");
    QVERIFY(!jpeg.isEmpty());
    QVERIFY2(jpeg.contains("Adobe"), "Qt writes a CMYK JPEG with the marker APP14 of Adobe");

    std::vector<Merger::Source> sources = { Merger::Source::createImage(jpeg, QByteArray(), 0.0, QString()) };
    const PDFDocument result = check(Merger::merge(sources, allPages(sources), Merger::Settings(), nullptr, nullptr));
    const PDFDictionary* xObject = getPageImage(result, 0);
    QCOMPARE(Read::name(result, xObject->get("ColorSpace")), QByteArray("DeviceCMYK"));
    const std::vector<PDFObject> decode = Read::items(result, xObject->get("Decode"));
    QCOMPARE(decode.size(), size_t(8));
    for (size_t i = 0; i < decode.size(); ++i)
    {
        QCOMPARE(Read::integer(result, decode[i]), i % 2 == 0 ? 1 : 0);
    }
}

void DocumentMergerTest::imageAlphaAndProfile()
{
    // 11.6.5.3: the alpha channel is a soft mask (a DeviceGray image of the same size); an opaque
    // image has none; 8.6.5.5: an ICC profile of RGB samples is ICCBased with /N 3
    QImage image(10, 10, QImage::Format_ARGB32);
    image.fill(QColor(0, 255, 0, 128));
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    QImage opaque(10, 10, QImage::Format_ARGB32);
    opaque.fill(QColor(0, 0, 255, 255));

    std::vector<Merger::Source> sources = { Merger::Source::createImage(encodeImage(image, "png"), QByteArray(), 0.0, QString()), Merger::Source::createImage(encodeImage(opaque, "png"), QByteArray(), 0.0, QString()) };
    const PDFDocument result = check(Merger::merge(sources, allPages(sources), Merger::Settings(), nullptr, nullptr));

    const PDFDictionary* transparent = getPageImage(result, 0);
    QCOMPARE(Read::name(result, transparent->get("Filter")), QByteArray("FlateDecode"));
    const PDFDictionary* mask = Read::dictionary(result, transparent->get("SMask"));
    QVERIFY(mask);
    QCOMPARE(Read::name(result, mask->get("ColorSpace")), QByteArray("DeviceGray"));
    QCOMPARE(Read::integer(result, mask->get("Width")), 10);
    QCOMPARE(Read::integer(result, mask->get("Height")), 10);

    const std::vector<PDFObject> colorSpace = Read::items(result, transparent->get("ColorSpace"));
    QCOMPARE(colorSpace.size(), size_t(2));
    QCOMPARE(Read::name(result, colorSpace[0]), QByteArray("ICCBased"));
    QCOMPARE(Read::integer(result, Read::dictionary(result, colorSpace[1])->get("N")), 3);

    QVERIFY(!getPageImage(result, 1)->hasKey("SMask"));
}

void DocumentMergerTest::imageResolution()
{
    // Owner answer Q6, R15: the size by the resolution of the file (pHYs of PNG), a resolution, which
    // the file does not give or which is outside 72 to 2400 dpi, is 300 dpi; a valid resolution of
    // the caller wins
    auto pageWidth = [](int dotsPerMeter, double dotsPerInch)
    {
        QImage image(300, 150, QImage::Format_RGB32);
        image.fill(Qt::white);
        image.setDotsPerMeterX(dotsPerMeter);
        image.setDotsPerMeterY(dotsPerMeter);
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QBuffer::WriteOnly);
        image.save(&buffer, "png");
        if (dotsPerMeter == 0)
        {
            // A file without the resolution: the chunk pHYs (PNG 11.3.5.3: 9 bytes of data) removed
            const qsizetype chunk = data.indexOf("pHYs");
            if (chunk >= 4)
            {
                data.remove(chunk - 4, 4 + 4 + 9 + 4);
            }
        }
        std::optional<PDFImagePageFactory::Frame> frame = PDFImagePageFactory::readFrame(data, QByteArray(), 0, dotsPerInch, nullptr);
        return frame ? PDFImagePageFactory::getPageSize(*frame).width() : -1.0;
    };

    QVERIFY(std::abs(pageWidth(5906, 0.0) - 144.0) < 0.1);      // 150 dpi: 300 / 150 x 72
    QVERIFY(std::abs(pageWidth(0, 0.0) - 72.0) < 0.1);          // no pHYs: 300 dpi
    QVERIFY(std::abs(pageWidth(1181, 0.0) - 72.0) < 0.1);       // 30 dpi: out of the range
    QVERIFY(std::abs(pageWidth(118110, 0.0) - 72.0) < 0.1);     // 3000 dpi: out of the range
    QVERIFY(std::abs(pageWidth(5906, 600.0) - 36.0) < 0.1);     // the caller says 600 dpi
    QVERIFY(std::abs(pageWidth(5906, std::nan("")) - 144.0) < 0.1);

    QCOMPARE(PDFImagePageFactory::getResolution(0.0), 300.0);
    QCOMPARE(PDFImagePageFactory::getResolution(-10.0), 300.0);
    QCOMPARE(PDFImagePageFactory::getResolution(std::nan("")), 300.0);
    QCOMPARE(PDFImagePageFactory::getResolution(71.9), 300.0);
    QCOMPARE(PDFImagePageFactory::getResolution(72.0), 72.0);
    QCOMPARE(PDFImagePageFactory::getResolution(2400.0), 2400.0);
    QCOMPARE(PDFImagePageFactory::getResolution(2400.1), 300.0);

    // An image of the clipboard has the resolution of an image without it
    QImage clipboard(600, 300, QImage::Format_RGB32);
    clipboard.fill(Qt::red);
    std::optional<PDFImagePageFactory::Frame> frame = PDFImagePageFactory::createFrame(clipboard, 0.0, nullptr);
    QVERIFY(frame);
    QCOMPARE(PDFImagePageFactory::getPageSize(*frame), QSizeF(144.0, 72.0));
}

void DocumentMergerTest::imageLimits()
{
    // R15: a page side out of 3 to 14 400 points is scaled into the limits keeping the aspect
    // ratio; the decoder has its allocation limit (SECURITY_TODO 2), a decoded image the same
    // limit of its pixels
    QCOMPARE(PDFImagePageFactory::getPageSize(QSize(20000, 10000), 72.0, 72.0), QSizeF(14400.0, 7200.0));
    QCOMPARE(PDFImagePageFactory::getPageSize(QSize(5, 5), 2400.0, 2400.0), QSizeF(3.0, 3.0));
    QCOMPARE(PDFImagePageFactory::getPageSize(QSize(100, 1), 72.0, 72.0), QSizeF(300.0, 3.0));

    QString error;
    QVERIFY(!PDFImagePageFactory::createFrame(QImage(8193, 8192, QImage::Format_Mono), 0.0, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(PDFImagePageFactory::createFrame(QImage(8192, 8192, QImage::Format_Mono), 0.0, &error));

    // A PNG, whose header claims 30 000 x 30 000 pixels: refused before it is decoded
    QImage small(1, 1, QImage::Format_RGB32);
    small.fill(Qt::white);
    QByteArray png = encodeImage(small, "png");
    const qsizetype header = png.indexOf("IHDR");
    QVERIFY(header > 0);
    const quint32 side = 30000;
    for (int i = 0; i < 4; ++i)
    {
        png[header + 4 + i] = char((side >> (24 - 8 * i)) & 0xFF);
        png[header + 8 + i] = char((side >> (24 - 8 * i)) & 0xFF);
    }

    // The CRC of the chunk (PNG 5.5: CRC-32 of the type and the data, 13 bytes of IHDR)
    quint32 crc = 0xFFFFFFFFu;
    for (qsizetype i = header; i < header + 4 + 13; ++i)
    {
        crc ^= uchar(png[i]);
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    crc ^= 0xFFFFFFFFu;
    for (int i = 0; i < 4; ++i)
    {
        png[header + 17 + i] = char((crc >> (24 - 8 * i)) & 0xFF);
    }
    const PDFImagePageFactory::ImageInfo info = PDFImagePageFactory::readInfo(png, QByteArray());
    QCOMPARE(info.frameCount, 0);
    QVERIFY(!info.error.isEmpty());
}

void DocumentMergerTest::imageTiffFrames()
{
    // Owner answer Q6: every frame of a multi-page TIFF is a page, at most 10 000 (R15)
    if (!QImageReader::supportedImageFormats().contains("tiff"))
    {
        QSKIP("The image formats of Qt have no TIFF plugin in this build.");
    }

    const QByteArray tiff = createTiff(3);
    QCOMPARE(PDFImagePageFactory::readInfo(tiff, QByteArray()).frameCount, 3);

    std::vector<Merger::Source> sources = { Merger::Source::createImage(tiff, QByteArray(), 0.0, QStringLiteral("scan.tif")) };
    std::vector<Merger::Item> items(3);
    for (int i = 0; i < 3; ++i)
    {
        items[size_t(i)].pageIndex = i;
    }
    const PDFDocument result = check(Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));

    const PDFImagePageFactory::ImageInfo tooMany = PDFImagePageFactory::readInfo(createTiff(10001), QByteArray());
    QCOMPARE(tooMany.frameCount, 0);
    QVERIFY(!tooMany.error.isEmpty());
    QCOMPARE(PDFImagePageFactory::readInfo(createTiff(10000), QByteArray()).frameCount, 10000);
}

void DocumentMergerTest::imageFrameCompacted()
{
    // Chapter 6 (memory): the merge decodes every image before it makes the pages, so a decoded
    // frame waits compacted - its pixels freed, its samples coded (the size they have in the
    // result) - and the page of a compacted frame is the page of the frame: the same objects
    QImage transparent(40, 30, QImage::Format_ARGB32);
    transparent.fill(QColor(0, 128, 255, 100));
    transparent.setColorSpace(QColorSpace(QColorSpace::SRgb));
    QImage photo(48, 32, QImage::Format_RGB32);
    photo.fill(QColor(200, 30, 40));

    for (const QByteArray& data : { encodeImage(transparent, "png"), encodeImage(photo, "jpeg") })
    {
        std::optional<PDFImagePageFactory::Frame> original = PDFImagePageFactory::readFrame(data, QByteArray(), 0, 0.0, nullptr);
        QVERIFY(original);
        QVERIFY(!original->image.isNull());
        const bool isJpeg = !original->jpegData.isEmpty();

        PDFImagePageFactory::Frame compacted = *original;
        PDFImagePageFactory::compact(&compacted);
        QVERIFY(compacted.image.isNull());
        QCOMPARE(compacted.size, original->image.size());
        QCOMPARE(PDFImagePageFactory::getPageSize(compacted), PDFImagePageFactory::getPageSize(*original));
        QCOMPARE(compacted.codedImage.isNull(), isJpeg);
        QCOMPARE(compacted.codedMask.isNull(), isJpeg);
        QCOMPARE(compacted.jpegData, original->jpegData);

        PDFDocumentBuilder first;
        first.createDocument();
        const size_t start = first.getStorage()->getObjects().size();
        const PDFObjectReference firstPage = PDFImagePageFactory::createPage(&first, *original, QSizeF(), QRectF());
        PDFDocumentBuilder second;
        second.createDocument();
        QCOMPARE(second.getStorage()->getObjects().size(), start);
        const PDFObjectReference secondPage = PDFImagePageFactory::createPage(&second, compacted, QSizeF(), QRectF());
        QCOMPARE(secondPage, firstPage);

        const PDFObjectStorage::PDFObjects& firstObjects = first.getStorage()->getObjects();
        const PDFObjectStorage::PDFObjects& secondObjects = second.getStorage()->getObjects();
        QCOMPARE(secondObjects.size(), firstObjects.size());
        for (size_t i = start; i < firstObjects.size(); ++i)
        {
            QCOMPARE(PDFDocumentWriter::getSerializedObject(secondObjects[i].object), PDFDocumentWriter::getSerializedObject(firstObjects[i].object));
        }
    }

    // Compacting twice changes nothing; a frame without pixels and without coded data makes no page
    std::optional<PDFImagePageFactory::Frame> frame = PDFImagePageFactory::readFrame(encodeImage(photo, "png"), QByteArray(), 0, 0.0, nullptr);
    QVERIFY(frame);
    PDFImagePageFactory::compact(&frame.value());
    const PDFObject coded = frame->codedImage;
    PDFImagePageFactory::compact(&frame.value());
    QCOMPARE(frame->codedImage, coded);
    PDFDocumentBuilder builder;
    builder.createDocument();
    QVERIFY(PDFImagePageFactory::createPage(&builder, *frame, QSizeF(), QRectF()).isValid());
    QVERIFY_THROWS_EXCEPTION(PDFException, PDFImagePageFactory::createPage(&builder, PDFImagePageFactory::Frame(), QSizeF(), QRectF()));
}

void DocumentMergerTest::imageTiffFramesInOnePass()
{
    // R15, chapter 6: a TIFF of thousands of pages (up to 10 000 are allowed) is read in one pass -
    // each frame is found from the one before it; a new reader for every frame, which counts the
    // directories and seeks from the first one, made 2 000 frames of 1 pixel take a minute
    if (!QImageReader::supportedImageFormats().contains("tiff"))
    {
        QSKIP("The image formats of Qt have no TIFF plugin in this build.");
    }

    const int count = 3000;
    std::vector<Merger::Source> sources = { Merger::Source::createImage(createTiff(count), QByteArray(), 0.0, QStringLiteral("scan.tif")) };
    std::vector<Merger::Item> items(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        items[size_t(i)].pageIndex = i;
    }

    QElapsedTimer timer;
    timer.start();
    const Merger::Result merged = Merger::merge(sources, items, Merger::Settings(), nullptr, nullptr);
    QVERIFY2(timer.elapsed() < 20000 * qMax(1, qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR")), qPrintable(QString::number(timer.elapsed())));
    QVERIFY2(merged.isSuccess, qPrintable(merged.errorMessage));
    QCOMPARE(merged.statistics.pageCount, count);

    // Each page shows its own frame: the gray of the pixel of the frame n is n modulo 256
    for (const int page : { 1, 300, 1777, count - 1 })
    {
        const PDFDictionary* resources = Read::dictionary(merged.document, Read::dictionary(merged.document, PDFObject::createReference(pageReference(merged.document, size_t(page))))->get("Resources"));
        const PDFObject& image = merged.document.getObject(Read::dictionary(merged.document, resources->get("XObject"))->getValue(0));
        QVERIFY(image.isStream());
        const QByteArray samples = merged.document.getDecodedStream(image.getStream());
        QVERIFY(!samples.isEmpty());
        for (const char sample : samples)
        {
            QCOMPARE(int(uchar(sample)), page % 256);
        }
    }
}

QTEST_GUILESS_MAIN(DocumentMergerTest)

#include "tst_documentmergertest.moc"
