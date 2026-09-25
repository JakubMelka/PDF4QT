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

#include "pdfscanpreparation.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentreader.h"
#include "pdfstreamfilters.h"
#include "pdfconstants.h"
#include "pdfcatalog.h"
#include "pdfpage.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfoptionalcontent.h"
#include "pdfmeshqualitysettings.h"

#include <QtTest>
#include <QBuffer>
#include <QPainter>

#include <cmath>

using namespace pdf;

namespace
{

/// Rendering environment of the tests
class RenderingContext
{
public:
    explicit RenderingContext(const PDFDocument* document) :
        m_optionalContentActivity(document, OCUsage::Export, nullptr),
        m_fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT)
    {
        PDFModifiedDocument modifiedDocument(const_cast<PDFDocument*>(document), &m_optionalContentActivity);
        m_fontCache.setDocument(modifiedDocument);
        m_fontCache.setCacheShrinkEnabled(nullptr, false);
    }

    ~RenderingContext()
    {
        m_fontCache.setCacheShrinkEnabled(nullptr, true);
    }

    PDFOCRPagePreparer createPreparer(const PDFDocument* document)
    {
        return PDFOCRPagePreparer(document, &m_fontCache, &m_cms, &m_optionalContentActivity, m_meshQualitySettings, RendererEngine::QPainter);
    }

    PDFOptionalContentActivity m_optionalContentActivity;
    PDFCMSGeneric m_cms;
    PDFFontCache m_fontCache;
    PDFMeshQualitySettings m_meshQualitySettings;
};

/// Page of the test documents
struct TestPage
{
    QSizeF size = QSizeF(400, 300);
    QByteArray content;
    PageRotation rotation = PageRotation::None;
    QRectF cropBox;
    double userUnit = 1.0;
    bool withImage = false;
    bool structParents = false;
    std::vector<QRectF> links;
};

PDFObjectReference addStream(PDFDocumentBuilder& builder, QByteArray data, PDFDictionary dictionary = PDFDictionary())
{
    dictionary.addEntry(PDFInplaceOrMemoryString(PDF_STREAM_DICT_LENGTH), PDFObject::createInteger(data.size()));
    return builder.addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(dictionary), std::move(data))));
}

PDFDocument createDocument(const std::vector<TestPage>& pages, const QByteArray& pageLabels = QByteArray())
{
    PDFDocumentBuilder builder;

    // Gray image shared by the pages, which draw it
    QByteArray imageData(64 * 64, '\x80');
    PDFDictionary imageDictionary;
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(64));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(64));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceGray"));
    imageDictionary.addEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
    const PDFObjectReference imageReference = addStream(builder, imageData, imageDictionary);

    for (const TestPage& spec : pages)
    {
        const PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0, 0), spec.size));
        const PDFObjectReference contentReference = addStream(builder, spec.content);

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Contents");
        factory << contentReference;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Resources");
        factory.beginDictionary();
        if (spec.withImage)
        {
            factory.beginDictionaryItem("XObject");
            factory.beginDictionary();
            factory.beginDictionaryItem("Im1");
            factory << imageReference;
            factory.endDictionaryItem();
            factory.endDictionary();
            factory.endDictionaryItem();
        }
        factory.endDictionary();
        factory.endDictionaryItem();
        if (spec.structParents)
        {
            factory.beginDictionaryItem("StructParents");
            factory << PDFInteger(0);
            factory.endDictionaryItem();
        }
        factory.endDictionary();
        builder.mergeTo(pageReference, factory.takeObject());

        if (!spec.links.empty())
        {
            std::vector<PDFObjectReference> annotations;
            for (const QRectF& rect : spec.links)
            {
                PDFObjectFactory annotation;
                annotation.beginDictionary();
                annotation.beginDictionaryItem("Type");
                annotation << WrapName("Annot");
                annotation.endDictionaryItem();
                annotation.beginDictionaryItem("Subtype");
                annotation << WrapName("Link");
                annotation.endDictionaryItem();
                annotation.beginDictionaryItem("Rect");
                annotation << rect;
                annotation.endDictionaryItem();
                annotation.beginDictionaryItem("P");
                annotation << pageReference;
                annotation.endDictionaryItem();
                annotation.endDictionary();
                annotations.push_back(builder.addObject(annotation.takeObject()));
            }

            PDFObjectFactory annotationsFactory;
            annotationsFactory.beginDictionary();
            annotationsFactory.beginDictionaryItem("Annots");
            annotationsFactory << annotations;
            annotationsFactory.endDictionaryItem();
            annotationsFactory.endDictionary();
            builder.mergeTo(pageReference, annotationsFactory.takeObject());
        }

        if (spec.rotation != PageRotation::None)
        {
            builder.setPageRotation(pageReference, spec.rotation);
        }
        if (spec.cropBox.isValid())
        {
            builder.setPageCropBox(pageReference, spec.cropBox);
        }
        if (!qFuzzyCompare(spec.userUnit, 1.0))
        {
            builder.setPageUserUnit(pageReference, spec.userUnit);
        }
    }

    if (!pageLabels.isEmpty())
    {
        // Page labels are given as "index style; index style", for example "0 D; 2 r"
        PDFDictionary catalog = *builder.getDictionaryFromObject(builder.getObjectByReference(builder.getCatalogReference()));
        PDFObjectFactory labelsFactory;
        labelsFactory.beginDictionary();
        labelsFactory.beginDictionaryItem("Nums");
        labelsFactory.beginArray();
        for (const QByteArray& part : pageLabels.split(';'))
        {
            const QList<QByteArray> items = part.trimmed().split(' ');
            labelsFactory << PDFInteger(items[0].toLongLong());
            labelsFactory.beginDictionary();
            labelsFactory.beginDictionaryItem("S");
            labelsFactory << WrapName(items[1]);
            labelsFactory.endDictionaryItem();
            labelsFactory.endDictionary();
        }
        labelsFactory.endArray();
        labelsFactory.endDictionaryItem();
        labelsFactory.endDictionary();
        catalog.setEntry(PDFInplaceOrMemoryString("PageLabels"), labelsFactory.takeObject());
        builder.setObject(builder.getCatalogReference(), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(catalog))));
    }

    return builder.build();
}

/// Content: horizontal bars around the center, rotated clockwise (visually) by the angle
QByteArray createSkewedBars(QPointF center, double angle, double width = 260.0)
{
    const QTransform matrix = QTransform::fromTranslate(-center.x(), -center.y()) * QTransform().rotate(-angle) * QTransform::fromTranslate(center.x(), center.y());
    QByteArray content = QStringLiteral("q %1 %2 %3 %4 %5 %6 cm 0 g\n").arg(matrix.m11()).arg(matrix.m12()).arg(matrix.m21()).arg(matrix.m22()).arg(matrix.dx()).arg(matrix.dy()).toLatin1();
    for (int i = 0; i < 9; ++i)
    {
        const double y = center.y() - 90 + i * 22;
        content += QStringLiteral("%1 %2 %3 6 re\n").arg(center.x() - width * 0.5).arg(y).arg(width).toLatin1();
    }
    content += "f Q\n";
    return content;
}

QImage render(const PDFDocument& document, PDFInteger pageIndex, double dpi = 100.0)
{
    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);
    return preparer.rasterize(pageIndex, dpi, { }, PDFOCRPagePreparer::DefaultMaximumPixels, nullptr).image.convertToFormat(QImage::Format_Grayscale8);
}

double measureSkew(const PDFDocument& document, PDFInteger pageIndex)
{
    double confidence = 0.0;
    return PDFOCRPagePreparer::estimateSkewAngle(render(document, pageIndex), &confidence, nullptr, PDFScanPreparation::MaximumDeskewAngle, true);
}

QPointF getDarkCentroid(const QImage& image)
{
    double sumX = 0.0;
    double sumY = 0.0;
    qint64 count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x)
        {
            if (row[x] < 128)
            {
                sumX += x;
                sumY += y;
                ++count;
            }
        }
    }
    return count > 0 ? QPointF(sumX / count, sumY / count) : QPointF();
}

QByteArray write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    PDFDocumentWriter(nullptr).write(&buffer, &document);
    return buffer.data();
}

PDFDocument read(const QByteArray& data)
{
    PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(data);
}

/// Writes an own OCR layer on the page
PDFDocument writeLayer(const PDFDocument& document, PDFInteger pageIndex)
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

QRectF getCropBox(const PDFDocument& document, PDFInteger pageIndex)
{
    return document.getCatalog()->getPage(pageIndex)->getCropBox();
}

} // namespace

class ScanPreparationTest : public QObject
{
    Q_OBJECT

private slots:
    void deskewSignAndCenter();
    void deskewUnbalancedContent();
    void splitSharesContent();
    void splitReadingOrderAndLabels();
    void splitWithDeskewAndCrop();
    void taggedPageIsNotSplit();
    void ocrLayerDecision();
    void imageAnalysis();
    void pageAnalysis();
    void planValidation();
};

void ScanPreparationTest::deskewSignAndCenter()
{
    // The skew is measured on the visible page and corrected on pages with every
    // rotation, with a crop box with an offset and with a user unit
    struct Case
    {
        PageRotation rotation;
        QRectF cropBox;
        double userUnit;
        double angle;
    };
    const std::vector<Case> cases =
    {
        { PageRotation::None, QRectF(), 1.0, 3.0 },
        { PageRotation::Rotate90, QRectF(), 1.0, 2.5 },
        { PageRotation::Rotate180, QRectF(), 1.0, -4.0 },
        { PageRotation::Rotate270, QRectF(), 1.0, 3.5 },
        { PageRotation::None, QRectF(40, 30, 340, 250), 1.0, -2.0 },
        { PageRotation::Rotate90, QRectF(40, 30, 340, 250), 2.0, 6.0 },
    };

    for (const Case& testCase : cases)
    {
        TestPage page;
        page.size = QSizeF(420, 320);
        page.rotation = testCase.rotation;
        page.cropBox = testCase.cropBox;
        page.userUnit = testCase.userUnit;
        const QRectF cropBox = testCase.cropBox.isValid() ? testCase.cropBox : QRectF(QPointF(0, 0), page.size);
        page.content = createSkewedBars(cropBox.center(), testCase.angle);
        const PDFDocument document = createDocument({ page });

        const double measured = measureSkew(document, 0);
        QVERIFY2(std::abs(measured - testCase.angle) < 0.3, qPrintable(QStringLiteral("rotation %1: measured %2, expected %3").arg(int(testCase.rotation)).arg(measured).arg(testCase.angle)));

        const QPointF centroidBefore = getDarkCentroid(render(document, 0));

        PDFScanPreparation::Plan plan = PDFScanPreparation::createIdentityPlan(&document);
        plan.pages.front().deskewAngle = measured;
        QVERIFY(PDFScanPreparation::isSourceChanged(&document, plan, 0));
        const PDFScanPreparation::Result result = PDFScanPreparation::apply(&document, plan, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
        QVERIFY(result.document);
        QCOMPARE(result.deskewedPages, 1);
        QCOMPARE(result.pageCount, 1);

        // Straight after the deskew, rotated around the center of the crop box
        const double remaining = measureSkew(*result.document, 0);
        QVERIFY2(std::abs(remaining) < 0.3, qPrintable(QStringLiteral("rotation %1: remaining skew %2").arg(int(testCase.rotation)).arg(remaining)));
        const QPointF centroidAfter = getDarkCentroid(render(*result.document, 0));
        QVERIFY2(QLineF(centroidBefore, centroidAfter).length() < 3.0, qPrintable(QStringLiteral("rotation %1: centroid moved by %2 px").arg(int(testCase.rotation)).arg(QLineF(centroidBefore, centroidAfter).length())));

        // The content is valid and the original content stream is kept (shared, not re-encoded)
        QString error;
        QVERIFY2(PDFOCRTextLayerWriter::validatePageContent(result.document.data(), 0, &error), qPrintable(error));
        const std::vector<PDFObjectReference> contents = PDFOCRTextLayerWriter::getPageContentReferences(result.document.data(), 0);
        QCOMPARE(contents.size(), size_t(3));
        QCOMPARE(contents[1], PDFOCRTextLayerWriter::getPageContentReferences(&document, 0).front());

        // Written and read again
        const PDFDocument reopened = read(write(*result.document));
        QCOMPARE(reopened.getCatalog()->getPageCount(), size_t(1));
        QVERIFY(std::abs(measureSkew(reopened, 0)) < 0.3);
    }
}

void ScanPreparationTest::deskewUnbalancedContent()
{
    // Scanners write "cm" without "q"/"Q" and leave the graphic state open: the deskew
    // must enclose such a content correctly. A stray "Q" pops the extra "q" of the
    // wrapper (the rotation stays), a stray "ET" is an error and the page is not straightened.
    TestPage open;
    open.size = QSizeF(420, 320);
    open.content = "1 0 0 1 0 0 cm q q\n" + createSkewedBars(QPointF(210, 160), 3.0) + "BT\n";
    TestPage strayQ;
    strayQ.size = QSizeF(420, 320);
    strayQ.content = "Q\n" + createSkewedBars(QPointF(210, 160), 3.0);
    TestPage strayET;
    strayET.size = QSizeF(420, 320);
    strayET.content = createSkewedBars(QPointF(210, 160), 3.0) + "ET\n";
    const PDFDocument document = createDocument({ open, strayQ, strayET });

    // Analysis reports the stray closing operator of the text object
    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);
    QVERIFY(!PDFScanPreparation::analyzePage(preparer, &document, 0, nullptr).hasUnbalancedContent);
    QVERIFY(!PDFScanPreparation::analyzePage(preparer, &document, 1, nullptr).hasUnbalancedContent);
    QVERIFY(PDFScanPreparation::analyzePage(preparer, &document, 2, nullptr).hasUnbalancedContent);

    PDFScanPreparation::Plan plan = PDFScanPreparation::createIdentityPlan(&document);
    for (PDFScanPreparation::OutputPage& page : plan.pages)
    {
        page.deskewAngle = 3.0;
    }
    const PDFScanPreparation::Result result = PDFScanPreparation::apply(&document, plan, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    QCOMPARE(result.deskewedPages, 2);
    QVERIFY2(result.warnings.join(QChar('\n')).contains(QStringLiteral("Page 3")), qPrintable(result.warnings.join(QChar('\n'))));

    // The open graphic states and the text object are closed before the final "Q",
    // the stray "Q" does not end the rotation
    for (PDFInteger page = 0; page < 2; ++page)
    {
        QString error;
        QVERIFY2(PDFOCRTextLayerWriter::validatePageContent(result.document.data(), page, &error), qPrintable(error));
        const double remaining = measureSkew(*result.document, page);
        QVERIFY2(std::abs(remaining) < 0.3, qPrintable(QStringLiteral("page %1: %2").arg(page + 1).arg(remaining)));
    }
    QCOMPARE(PDFOCRTextLayerWriter::getPageContentReferences(result.document.data(), 2).size(), size_t(1));
}

void ScanPreparationTest::splitSharesContent()
{
    // A spread (two pages side by side) with an image and two links
    TestPage first;
    TestPage spread;
    spread.size = QSizeF(600, 400);
    spread.withImage = true;
    spread.content = "q 600 0 0 400 0 0 cm /Im1 Do Q\n";
    spread.links = { QRectF(50, 50, 100, 20), QRectF(400, 300, 100, 20) };
    TestPage last;
    const PDFDocument document = createDocument({ first, spread, last });

    const std::array<QRectF, 2> halves = PDFScanPreparation::computeSplit(QSizeF(600, 400), PDFScanPreparation::SplitOrientation::SideBySide, 300.0, 10.0);
    QCOMPARE(halves[0], QRectF(0, 0, 295, 400));
    QCOMPARE(halves[1], QRectF(305, 0, 295, 400));
    const std::array<QRectF, 2> rows = PDFScanPreparation::computeSplit(QSizeF(400, 600), PDFScanPreparation::SplitOrientation::OneAboveAnother, 300.0, 0.0);
    QCOMPARE(rows[0], QRectF(0, 0, 400, 300));
    QCOMPARE(rows[1], QRectF(0, 300, 400, 300));

    PDFScanPreparation::Plan plan = PDFScanPreparation::createIdentityPlan(&document);
    plan.pages[1].visibleRect = halves[0];
    PDFScanPreparation::OutputPage right;
    right.sourcePageIndex = 1;
    right.visibleRect = halves[1];
    plan.pages.insert(plan.pages.begin() + 2, right);

    const PDFScanPreparation::Result result = PDFScanPreparation::apply(&document, plan, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    QCOMPARE(result.pageCount, 4);
    QCOMPARE(result.splitPages, 1);
    const PDFDocument& split = *result.document;
    QCOMPARE(split.getCatalog()->getPageCount(), size_t(4));

    // The halves have their crop boxes (the page is not rotated: visible = page space, y flipped)
    QCOMPARE(getCropBox(split, 1), QRectF(0, 0, 295, 400));
    QCOMPARE(getCropBox(split, 2), QRectF(305, 0, 295, 400));
    QCOMPARE(split.getCatalog()->getPage(2)->getMediaBox(), QRectF(0, 0, 600, 400));

    // The halves share the content stream and the image (the image is in the file once)
    QCOMPARE(PDFOCRTextLayerWriter::getPageContentReferences(&split, 1), PDFOCRTextLayerWriter::getPageContentReferences(&split, 2));
    int imageCount = 0;
    for (const PDFObjectStorage::Entry& entry : split.getStorage().getObjects())
    {
        if (entry.object.isStream())
        {
            const PDFObject& subtype = split.getObject(entry.object.getStream()->getDictionary()->get("Subtype"));
            imageCount += (subtype.isName() && subtype.getString() == "Image") ? 1 : 0;
        }
    }
    QCOMPARE(imageCount, 1);

    // Each link belongs to the half containing it
    const PDFObjectReference leftPage = split.getCatalog()->getPage(1)->getPageReference();
    const PDFObjectReference rightPage = split.getCatalog()->getPage(2)->getPageReference();
    QCOMPARE(split.getCatalog()->getPage(1)->getAnnotations().size(), size_t(1));
    QCOMPARE(split.getCatalog()->getPage(2)->getAnnotations().size(), size_t(1));
    const PDFDictionary* rightLink = split.getDictionaryFromObject(split.getObjectByReference(split.getCatalog()->getPage(2)->getAnnotations().front()));
    QCOMPARE(rightLink->get("P").getReference(), rightPage);
    const PDFDictionary* leftLink = split.getDictionaryFromObject(split.getObjectByReference(split.getCatalog()->getPage(1)->getAnnotations().front()));
    QCOMPARE(leftLink->get("P").getReference(), leftPage);

    // Written and read again, the rendering of the halves shows the halves of the spread
    const PDFDocument reopened = read(write(split));
    QCOMPARE(reopened.getCatalog()->getPageCount(), size_t(4));
    QCOMPARE(render(reopened, 2, 72.0).size(), QSize(295, 400));
    QVERIFY(result.getSummary().contains(QStringLiteral("4 page(s)")));
}

void ScanPreparationTest::splitReadingOrderAndLabels()
{
    // Book read from the right to the left: the right half is the first page. The labels
    // behind the spread are shifted, a rotated spread is split along its visible axis.
    TestPage first;
    TestPage spread;
    spread.size = QSizeF(400, 600);
    spread.rotation = PageRotation::Rotate90;
    spread.content = "0 g 10 10 50 50 re f\n";
    TestPage last;
    const PDFDocument document = createDocument({ first, spread, last }, "0 D; 2 r");

    const QSizeF visibleSize = PDFScanPreparation::getVisibleSize(document.getCatalog()->getPage(1));
    QCOMPARE(visibleSize, QSizeF(600, 400));
    const std::array<QRectF, 2> halves = PDFScanPreparation::computeSplit(visibleSize, PDFScanPreparation::SplitOrientation::SideBySide, 300.0, 0.0);

    PDFScanPreparation::Plan plan = PDFScanPreparation::createIdentityPlan(&document);
    plan.pages[1].visibleRect = halves[1];
    PDFScanPreparation::OutputPage left;
    left.sourcePageIndex = 1;
    left.visibleRect = halves[0];
    plan.pages.insert(plan.pages.begin() + 2, left);

    const PDFScanPreparation::Result result = PDFScanPreparation::apply(&document, plan, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    const PDFDocument& split = *result.document;

    // Both halves are 300 x 400 visible points, each shows a different part of the page
    const QRectF firstCrop = getCropBox(split, 1);
    const QRectF secondCrop = getCropBox(split, 2);
    QCOMPARE(split.getCatalog()->getPage(1)->getRotatedCropBox().size(), QSizeF(300, 400));
    QCOMPARE(split.getCatalog()->getPage(2)->getRotatedCropBox().size(), QSizeF(300, 400));
    QVERIFY(!firstCrop.intersects(secondCrop.adjusted(1, 1, -1, -1)));
    QCOMPARE(firstCrop.united(secondCrop), QRectF(0, 0, 400, 600));

    // The page label of the last page moved from the index 2 to the index 3
    const PDFDictionary* catalog = split.getDictionaryFromObject(split.getTrailerDictionary()->get("Root"));
    const PDFDictionary* labels = split.getDictionaryFromObject(catalog->get("PageLabels"));
    QVERIFY(labels);
    const PDFObject& nums = split.getObject(labels->get("Nums"));
    QVERIFY(nums.isArray());
    QCOMPARE(nums.getArray()->getCount(), size_t(4));
    QCOMPARE(split.getObject(nums.getArray()->getItem(0)).getInteger(), PDFInteger(0));
    QCOMPARE(split.getObject(nums.getArray()->getItem(2)).getInteger(), PDFInteger(3));
}

void ScanPreparationTest::taggedPageIsNotSplit()
{
    TestPage tagged;
    tagged.size = QSizeF(600, 400);
    tagged.structParents = true;
    tagged.content = "0 g 10 10 50 50 re f\n";
    const PDFDocument document = createDocument({ tagged });

    PDFScanPreparation::Plan plan = PDFScanPreparation::createIdentityPlan(&document);
    const std::array<QRectF, 2> halves = PDFScanPreparation::computeSplit(QSizeF(600, 400), PDFScanPreparation::SplitOrientation::SideBySide, 300.0, 0.0);
    plan.pages.front().visibleRect = halves[0];
    PDFScanPreparation::OutputPage second;
    second.sourcePageIndex = 0;
    second.visibleRect = halves[1];
    plan.pages.push_back(second);

    const PDFScanPreparation::Result result = PDFScanPreparation::apply(&document, plan, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    QVERIFY(result.warnings.join(QChar('\n')).contains(QStringLiteral("structure tree")));
    QVERIFY(!result.document);
    QCOMPARE(result.pageCount, 1);

    // A crop of a tagged page is allowed (the page stays one page)
    PDFScanPreparation::Plan cropPlan = PDFScanPreparation::createIdentityPlan(&document);
    cropPlan.pages.front().visibleRect = QRectF(10, 10, 500, 300);
    const PDFScanPreparation::Result cropped = PDFScanPreparation::apply(&document, cropPlan, nullptr);
    QVERIFY2(cropped.isSuccess(), qPrintable(cropped.errorMessage));
    QCOMPARE(cropped.croppedPages, 1);
    QCOMPARE(getCropBox(*cropped.document, 0), QRectF(10, 90, 500, 300));
}

void ScanPreparationTest::ocrLayerDecision()
{
    TestPage page;
    page.content = "0 g 10 10 50 50 re f\n";
    const PDFDocument document = writeLayer(createDocument({ page, page }), 0);
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(&document, 0).isPresent);

    PDFScanPreparation::Plan plan = PDFScanPreparation::createIdentityPlan(&document);
    plan.pages[0].visibleRect = QRectF(20, 20, 300, 200);
    plan.pages[1].visibleRect = QRectF(20, 20, 300, 200);
    QCOMPARE(PDFScanPreparation::getChangedPagesWithOCRLayer(&document, plan), std::vector<PDFInteger>{ 0 });

    // Default: the page with the layer is not changed, the other one is
    const PDFScanPreparation::Result skipped = PDFScanPreparation::apply(&document, plan, nullptr);
    QVERIFY2(skipped.isSuccess(), qPrintable(skipped.errorMessage));
    QCOMPARE(skipped.skippedPages, std::vector<PDFInteger>{ 0 });
    QCOMPARE(skipped.croppedPages, 1);
    QCOMPARE(getCropBox(*skipped.document, 0), QRectF(0, 0, 400, 300));
    QVERIFY(PDFOCRTextLayerWriter::readLayerInfo(skipped.document.data(), 0).fingerprintMatches);

    // Removal of the layer: the page is changed and has no layer
    plan.layerActions[0] = PDFScanPreparation::LayerAction::RemoveLayer;
    const PDFScanPreparation::Result removed = PDFScanPreparation::apply(&document, plan, nullptr);
    QVERIFY2(removed.isSuccess(), qPrintable(removed.errorMessage));
    QCOMPARE(removed.removedLayers, std::vector<PDFInteger>{ 0 });
    QCOMPARE(removed.croppedPages, 2);
    QVERIFY(!PDFOCRTextLayerWriter::readLayerInfo(removed.document.data(), 0).isPresent);
    QString error;
    QVERIFY2(PDFOCRTextLayerWriter::validatePageContent(removed.document.data(), 0, &error), qPrintable(error));
}

void ScanPreparationTest::imageAnalysis()
{
    // Page of a scan at 100 DPI: a dark edge of the scanner on the left and a block of text
    QImage scan(850, 1100, QImage::Format_Grayscale8);
    scan.fill(232);
    {
        QPainter painter(&scan);
        painter.fillRect(QRect(0, 0, 30, 1100), QColor(20, 20, 20));
        for (int i = 0; i < 20; ++i)
        {
            painter.fillRect(QRect(200, 300 + i * 25, 450, 8), QColor(30, 30, 30));
        }
    }

    PDFScanPreparation::PageAnalysis analysis;
    PDFScanPreparation::analyzeImage(scan, 100.0, analysis, nullptr);
    QCOMPARE(analysis.visibleSize, QSizeF(612, 792));
    QVERIFY(std::abs(analysis.skewAngle) < 0.2);

    // The edge of the scanner is not a content
    const QRectF expected(200 * 0.72, 300 * 0.72, 450 * 0.72, (19 * 25 + 8) * 0.72);
    QVERIFY2(std::abs(analysis.contentRect.left() - expected.left()) < 4.0, qPrintable(QString::number(analysis.contentRect.left())));
    QVERIFY2(std::abs(analysis.contentRect.top() - expected.top()) < 4.0, qPrintable(QString::number(analysis.contentRect.top())));
    QVERIFY2(std::abs(analysis.contentRect.right() - expected.right()) < 4.0, qPrintable(QString::number(analysis.contentRect.right())));
    QVERIFY2(std::abs(analysis.contentRect.bottom() - expected.bottom()) < 4.0, qPrintable(QString::number(analysis.contentRect.bottom())));

    // Automatic crop with a margin, limited to a region and enlarged by the deskew
    const QRectF crop = PDFScanPreparation::computeContentCrop(analysis, QRectF(), 0.0, 10.0);
    QVERIFY(std::abs(crop.left() - (analysis.contentRect.left() - 10.0)) < 0.01);
    QVERIFY(std::abs(crop.bottom() - (analysis.contentRect.bottom() + 10.0)) < 0.01);
    const QRectF regionCrop = PDFScanPreparation::computeContentCrop(analysis, QRectF(0, 0, 612, 300), 0.0, 0.0);
    QVERIFY(regionCrop.bottom() <= 300.0);
    QVERIFY(regionCrop.top() >= analysis.contentRect.top() - 3.0);
    const QRectF rotatedCrop = PDFScanPreparation::computeContentCrop(analysis, QRectF(), 5.0, 10.0);
    QVERIFY(rotatedCrop.width() > crop.width());
    QCOMPARE(PDFScanPreparation::computeContentCrop(analysis, QRectF(0, 0, 100, 100), 0.0, 5.0), QRectF(0, 0, 100, 100));

    // Spread with a white gap between the pages
    QImage spread(1700, 1100, QImage::Format_Grayscale8);
    spread.fill(235);
    {
        QPainter painter(&spread);
        for (int i = 0; i < 25; ++i)
        {
            painter.fillRect(QRect(100, 200 + i * 28, 650, 9), QColor(25, 25, 25));
            painter.fillRect(QRect(950, 200 + i * 28, 650, 9), QColor(25, 25, 25));
        }
    }
    PDFScanPreparation::PageAnalysis spreadAnalysis;
    PDFScanPreparation::analyzeImage(spread, 100.0, spreadAnalysis, nullptr);
    QVERIFY(spreadAnalysis.gutterSideBySide.has_value());
    QVERIFY2(std::abs(spreadAnalysis.gutterSideBySide->position - 850 * 0.72) < 8.0, qPrintable(QString::number(spreadAnalysis.gutterSideBySide->position)));
    QVERIFY(spreadAnalysis.gutterSideBySide->confidence > 50.0);
    QVERIFY(!spreadAnalysis.gutterSideBySide->isShadow);

    // Spread with the shadow of the spine
    QImage shadow = spread;
    for (int y = 0; y < shadow.height(); ++y)
    {
        uchar* row = shadow.scanLine(y);
        for (int x = 780; x < 920; ++x)
        {
            const double distance = std::abs(x - 850) / 70.0;
            row[x] = uchar(qMin<int>(row[x], int(90 + 145 * distance)));
        }
    }
    PDFScanPreparation::PageAnalysis shadowAnalysis;
    PDFScanPreparation::analyzeImage(shadow, 100.0, shadowAnalysis, nullptr);
    QVERIFY(shadowAnalysis.gutterSideBySide.has_value());
    QVERIFY(shadowAnalysis.gutterSideBySide->isShadow);
    QVERIFY2(std::abs(shadowAnalysis.gutterSideBySide->position - 850 * 0.72) < 8.0, qPrintable(QString::number(shadowAnalysis.gutterSideBySide->position)));

    // Skew of a page of a text
    QImage skewed(850, 1100, QImage::Format_Grayscale8);
    skewed.fill(235);
    {
        QPainter painter(&skewed);
        painter.translate(425, 550);
        painter.rotate(4.0);
        painter.translate(-425, -550);
        for (int i = 0; i < 20; ++i)
        {
            painter.fillRect(QRect(150, 300 + i * 25, 550, 8), QColor(30, 30, 30));
        }
    }
    PDFScanPreparation::PageAnalysis skewAnalysis;
    PDFScanPreparation::analyzeImage(skewed, 100.0, skewAnalysis, nullptr);
    QVERIFY2(std::abs(skewAnalysis.skewAngle - 4.0) < 0.2, qPrintable(QString::number(skewAnalysis.skewAngle)));
    QVERIFY(skewAnalysis.skewConfidence > 30.0);
}

void ScanPreparationTest::pageAnalysis()
{
    TestPage scan;
    scan.size = QSizeF(420, 320);
    scan.withImage = true;
    scan.content = "q 420 0 0 320 0 0 cm /Im1 Do Q\n" + createSkewedBars(QPointF(210, 160), 2.0);
    TestPage vector;
    vector.content = "0 g 10 10 50 50 re f\n";
    const PDFDocument document = writeLayer(createDocument({ scan, vector }), 1);

    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);
    const PDFScanPreparation::PageAnalysis scanAnalysis = PDFScanPreparation::analyzePage(preparer, &document, 0, nullptr);
    QVERIFY(scanAnalysis.isValid());
    QVERIFY(scanAnalysis.isScan);
    QVERIFY(!scanAnalysis.hasOwnOCRLayer);
    QCOMPARE(scanAnalysis.visibleSize, QSizeF(420, 320));
    QVERIFY2(std::abs(scanAnalysis.skewAngle - 2.0) < 0.3, qPrintable(QString::number(scanAnalysis.skewAngle)));
    QVERIFY(!scanAnalysis.contentRect.isEmpty());
    QVERIFY(!scanAnalysis.inkMask.isNull());

    const PDFScanPreparation::PageAnalysis vectorAnalysis = PDFScanPreparation::analyzePage(preparer, &document, 1, nullptr);
    QVERIFY(!vectorAnalysis.isScan);
    QVERIFY(vectorAnalysis.hasOwnOCRLayer);
}

void ScanPreparationTest::planValidation()
{
    TestPage page;
    page.content = "0 g 10 10 50 50 re f\n";
    const PDFDocument document = createDocument({ page, page });

    // Identity plan does not change anything
    const PDFScanPreparation::Result identity = PDFScanPreparation::apply(&document, PDFScanPreparation::createIdentityPlan(&document), nullptr);
    QVERIFY(identity.isSuccess());
    QVERIFY(!identity.document);
    QVERIFY(!PDFScanPreparation::isSourceChanged(&document, PDFScanPreparation::createIdentityPlan(&document), 0));

    // A crop equal to the whole page is no change
    PDFScanPreparation::Plan whole = PDFScanPreparation::createIdentityPlan(&document);
    whole.pages[0].visibleRect = QRectF(0, 0, 400, 300);
    QVERIFY(!PDFScanPreparation::isSourceChanged(&document, whole, 0));

    // Missing page, order of the pages, page out of the document, too large angle
    PDFScanPreparation::Plan missing = PDFScanPreparation::createIdentityPlan(&document);
    missing.pages.pop_back();
    QVERIFY(!PDFScanPreparation::apply(&document, missing, nullptr).isSuccess());
    PDFScanPreparation::Plan reordered = PDFScanPreparation::createIdentityPlan(&document);
    std::swap(reordered.pages[0], reordered.pages[1]);
    QVERIFY(!PDFScanPreparation::apply(&document, reordered, nullptr).isSuccess());
    PDFScanPreparation::Plan outside = PDFScanPreparation::createIdentityPlan(&document);
    outside.pages.push_back(PDFScanPreparation::OutputPage{ 5, QRectF(), 0.0, QRectF() });
    QVERIFY(!PDFScanPreparation::apply(&document, outside, nullptr).isSuccess());
    PDFScanPreparation::Plan steep = PDFScanPreparation::createIdentityPlan(&document);
    steep.pages[0].deskewAngle = 15.0;
    QVERIFY(!PDFScanPreparation::apply(&document, steep, nullptr).isSuccess());

    // A crop outside of the page is refused
    PDFScanPreparation::Plan outsideCrop = PDFScanPreparation::createIdentityPlan(&document);
    outsideCrop.pages[0].visibleRect = QRectF(1000, 1000, 50, 50);
    QVERIFY(!PDFScanPreparation::apply(&document, outsideCrop, nullptr).isSuccess());

    // The deskew matrix rotates around the center of the crop box
    const QTransform matrix = PDFScanPreparation::getDeskewMatrix(QRectF(100, 50, 200, 100), 7.0);
    QVERIFY(QLineF(matrix.map(QPointF(200, 100)), QPointF(200, 100)).length() < 1e-9);
}

void ScanPreparationTest::splitWithDeskewAndCrop()
{
    // Each half of a spread has its own skew; the halves are straightened around their
    // own centers and cropped automatically to their content
    TestPage spread;
    spread.size = QSizeF(840, 320);
    spread.content = createSkewedBars(QPointF(210, 160), 3.0, 240.0) + createSkewedBars(QPointF(630, 160), -2.0, 240.0);
    const PDFDocument document = createDocument({ spread });

    RenderingContext context(&document);
    PDFOCRPagePreparer preparer = context.createPreparer(&document);
    const PDFScanPreparation::PageAnalysis analysis = PDFScanPreparation::analyzePage(preparer, &document, 0, nullptr);
    QVERIFY(analysis.gutterSideBySide.has_value());
    QVERIFY2(std::abs(analysis.gutterSideBySide->position - 420.0) < 30.0, qPrintable(QString::number(analysis.gutterSideBySide->position)));

    const std::array<QRectF, 2> halves = PDFScanPreparation::computeSplit(analysis.visibleSize, PDFScanPreparation::SplitOrientation::SideBySide, 420.0, 0.0);
    const std::array<double, 2> angles = { 3.0, -2.0 };

    PDFScanPreparation::Plan plan;
    for (size_t i = 0; i < 2; ++i)
    {
        PDFScanPreparation::OutputPage output;
        output.sourcePageIndex = 0;
        output.regionRect = halves[i];
        output.deskewAngle = angles[i];
        output.visibleRect = PDFScanPreparation::computeContentCrop(analysis, halves[i], angles[i], 12.0);
        QVERIFY(halves[i].contains(output.visibleRect));
        QVERIFY(output.visibleRect.width() < halves[i].width());
        plan.pages.push_back(output);
    }

    const PDFScanPreparation::Result result = PDFScanPreparation::apply(&document, plan, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.errorMessage));
    QCOMPARE(result.pageCount, 2);
    QCOMPARE(result.deskewedPages, 2);
    QCOMPARE(result.croppedPages, 2);

    // The halves are straight and their whole content is inside the crop (no ink touches the edge)
    for (PDFInteger page = 0; page < 2; ++page)
    {
        const double remaining = measureSkew(*result.document, page);
        QVERIFY2(std::abs(remaining) < 0.3, qPrintable(QStringLiteral("half %1: %2").arg(page + 1).arg(remaining)));

        const QImage image = render(*result.document, page);
        int edgeInk = 0;
        for (int x = 0; x < image.width(); ++x)
        {
            edgeInk += qGray(image.pixel(x, 0)) < 128 ? 1 : 0;
            edgeInk += qGray(image.pixel(x, image.height() - 1)) < 128 ? 1 : 0;
        }
        for (int y = 0; y < image.height(); ++y)
        {
            edgeInk += qGray(image.pixel(0, y)) < 128 ? 1 : 0;
            edgeInk += qGray(image.pixel(image.width() - 1, y)) < 128 ? 1 : 0;
        }
        QVERIFY2(edgeInk == 0, qPrintable(QStringLiteral("half %1: %2 ink pixels at the edge").arg(page + 1).arg(edgeInk)));
    }
}

QTEST_MAIN(ScanPreparationTest)

#include "tst_scanpreparationtest.moc"
