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

// The page geometry of the core and the tools Crop, Resize, Set Page Boxes (package B of step 3a
// of phase 2, plan 5.1-5.5, 8): every key, /Rotate, /UserUnit, the destinations, the Editor Page
// Geometry. The expected values come from ISO 32000-2 (14.11.2, Table 31, 12.3.2.2, 12.5, 12.9,
// 12.4.3, 8.4.2, 8.7.2, 14.8.5.4.3) and from the numbers of the plan and the frames.

#include "pdfcatalog.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_geometry.h"
#include "pdfpagegeometry.h"
#include "pdfpagesizepresets.h"
#include "pdfparser.h"

#include <QtTest>

#include <map>

using namespace pdf;

/// A test document written from the texts of its objects
class GeometryTestPdf
{
public:
    void set(int number, const QByteArray& text) { m_objects[number] = text; }

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

    /// One page 3 (content 4) below the page tree 2 with \p treeEntries, the page with \p pageEntries
    static GeometryTestPdf createPage(const QByteArray& pageEntries, const QByteArray& treeEntries = QByteArray(), const QByteArray& content = "0 0 m 10 10 l S", const QByteArray& catalogEntries = QByteArray())
    {
        GeometryTestPdf pdf;
        pdf.set(1, "<< /Type /Catalog /Pages 2 0 R " + catalogEntries + " >>");
        pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 " + treeEntries + " >>");
        pdf.set(3, "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << >> " + pageEntries + " >>");
        pdf.set(4, stream(content));
        return pdf;
    }

private:
    std::map<int, QByteArray> m_objects;
};

/// Helpers of the test
class GeometryTestHelper
{
public:
    GeometryTestHelper() = delete;

    static constexpr double TOLERANCE = 0.01;

    static bool isNear(double a, double b, double tolerance = TOLERANCE) { return std::abs(a - b) <= tolerance; }

    static bool isNear(const QRectF& a, const QRectF& b, double tolerance = TOLERANCE)
    {
        const QRectF x = a.normalized();
        const QRectF y = b.normalized();
        return isNear(x.left(), y.left(), tolerance) && isNear(x.top(), y.top(), tolerance) && isNear(x.right(), y.right(), tolerance) && isNear(x.bottom(), y.bottom(), tolerance);
    }

    static double mm(double millimeters) { return millimeters * 72.0 / 25.4; }

    static const PDFDictionary* dictionary(const PDFDocument& document, int number)
    {
        return document.getDictionaryFromObject(document.getObjectByReference(PDFObjectReference(number, 0)));
    }

    static QRectF rect(const PDFDocument& document, const PDFObject& object)
    {
        PDFDocumentDataLoaderDecorator loader(&document);
        return loader.readRectangle(object, QRectF());
    }

    /// The numbers of an array rounded to 6 decimals (the reals written by the engine)
    static std::vector<double> numbers(const PDFDocument& document, const PDFObject& object)
    {
        PDFDocumentDataLoaderDecorator loader(&document);
        std::vector<double> result = loader.readNumberArray(object, { });
        for (double& value : result)
        {
            value = std::round(value * 1e6) / 1e6;
        }
        return result;
    }

    /// 391:26688: the media box 226 x 313 mm with the trim box 210 x 297 mm in its middle
    static QByteArray flyerBoxes()
    {
        auto number = [](double millimeters) { return QByteArray::number(mm(millimeters), 'f', 4); };
        return "/MediaBox [0 0 " + number(226) + " " + number(313) + "] /TrimBox [" + number(8) + " " + number(8) + " " + number(218) + " " + number(305) + "]";
    }

    static PDFDocumentToolContext context(const PDFDocument& document)
    {
        return PDFDocumentToolContext::create(PDFDocumentToolInputDocument::create(PDFDocumentPointer(new PDFDocument(document)), QByteArray(), QStringLiteral("Flyer.pdf"), true));
    }

    static QVariant allPages()
    {
        PDFDocumentToolPageSelection selection;
        return selection.toVariant();
    }

    /// The whole content of the page as it is drawn (the streams one after another)
    static QByteArray content(const PDFDocument& document, int pageIndex)
    {
        const PDFPage* page = document.getCatalog()->getPage(size_t(pageIndex));
        QByteArray result;
        const PDFObject& contents = document.getObject(page->getContents());
        std::vector<PDFObject> streams;
        if (contents.isStream())
        {
            streams.push_back(contents);
        }
        else if (contents.isArray())
        {
            for (const PDFObject& item : *contents.getArray())
            {
                streams.push_back(document.getObject(item));
            }
        }
        for (const PDFObject& stream : streams)
        {
            if (stream.isStream())
            {
                result += document.getDecodedStream(stream.getStream()) + "\n";
            }
        }
        return result;
    }

    /// Resize of the only page to a custom sheet of \p sheet points, which halves an 600 x 800 page
    static PDFPageGeometry::ResizeSettings halfSettings(bool scaleAnnotations = true)
    {
        PDFPageGeometry::ResizeSettings settings;
        settings.pageSizeMM = QSizeF(PDFPageSizePresets::getMillimeters(300.0), PDFPageSizePresets::getMillimeters(400.0));
        settings.isCustomSize = true;
        settings.scaleAnnotations = scaleAnnotations;
        return settings;
    }

    static PDFDocument resize(const PDFDocument& document, const PDFPageGeometry::ResizeSettings& settings, PDFPageGeometryStatistics* statistics = nullptr)
    {
        PDFPageGeometry::ResizePlacement placement = PDFPageGeometry::computeResize(document.getCatalog()->getPage(0), settings);
        if (!placement.isValid)
        {
            qWarning() << placement.errorMessage;
            return PDFDocument();
        }
        placement.change.pageIndex = 0;
        return PDFPageGeometry::applyChanges(&document, { placement.change }, nullptr, statistics);
    }
};

class DocumentPageGeometryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // PDFPage (B40, B41, R6)
    void effectiveBoxesAreReducedToMediaBox();
    void userUnitGivesMillimeters();
    void hostileUserUnitIsOne();
    void oldGettersKeepTheirMeaning();

    // The shown coordinates (5.2)
    void shownMarginsFollowRotation();

    // Crop (5.3)
    void cropEqualMarginsOfA4();
    void cropUserUnitTwo();
    void cropRotatedPageLeftMarginIsBottom();
    void cropCustomBoxOtherSizeKeepsMargins();
    void cropCustomBoxOutsideNamesLargestWidth();
    void cropTooLargeMarginIsRefused();
    void cropRemoveWritesMediaBoxOverInheritedCrop();
    void cropKeepsObjectsAndRemovesThumbnail();
    void cropToolFactsAndNothingToDo();

    // Resize (5.4)
    void resizeLetterToA4Scale();
    void resizeContentIsWrappedAgainstExcessQ();
    void resizeAnnotationKeys();
    void resizeAnnotationsOnlyMovedWithoutScaling();
    void resizeDestinationsFollowContent();
    void resizeSharedDestinationOnce();
    void resizeViewportMeasures();
    void resizeBeadsPatternsAndLayoutBoxes();
    void resizePageBoxesFollowContent();
    void resizeKeepSizeRewritesNothing();
    void resizeKeepSizeCroppedPageShowsNewSheet();
    void resizeRotatedPageAuto();
    void resizeMarginsWithoutRoomAreRefused();
    void resizeToolFactsAndPermissionBit();

    // Boxes (5.5)
    void boxesBleedFromTrim();
    void boxesOutsideMediaIsWrittenWithWarning();
    void boxesTwoBoxesAndDefaultTrim();
    void boxesWriteToErrors();

    // The dialog Page Geometry of the Editor (B42 - B46)
    void editorGeometryRotatedMargins();
    void editorGeometryScaleMovesOtherBoxesAndDestinations();
    void editorGeometryRangeOutsideIsError();
    void pageMasterGeometryRangeLimitedToEachOutput();

    // Hostile input
    void hostileBoxesAndParentCycle();
};

void DocumentPageGeometryTest::effectiveBoxesAreReducedToMediaBox()
{
    // 14.11.2: the crop, bleed, trim and art boxes are reduced to their intersection with the media
    // box; bleed, trim and art default to the crop box (Table 31)
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /CropBox [-10 -10 700 900] /TrimBox [-5 0 300 400]").read();
    const PDFPage* page = document.getCatalog()->getPage(0);
    QVERIFY(GeometryTestHelper::isNear(page->getEffectiveCropBox(), QRectF(0, 0, 600, 800)));
    QVERIFY(GeometryTestHelper::isNear(page->getEffectiveTrimBox(), QRectF(0, 0, 300, 400)));
    QVERIFY(GeometryTestHelper::isNear(page->getEffectiveBleedBox(), QRectF(0, 0, 600, 800)));
    QVERIFY(GeometryTestHelper::isNear(PDFPageGeometry::getEffectiveBox(page, PDFPageGeometry::Box::ArtBox), QRectF(0, 0, 600, 800)));

    // A box without an intersection with the media box is the media box
    QVERIFY(GeometryTestHelper::isNear(PDFPage::getEffectiveBox(QRectF(1000, 1000, 10, 10), QRectF(0, 0, 600, 800)), QRectF(0, 0, 600, 800)));
}

void DocumentPageGeometryTest::userUnitGivesMillimeters()
{
    // Table 31 /UserUnit: a unit of 2/72 inch - a media box 297.64 x 420.94 is A4
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 297.64 420.94] /UserUnit 2").read();
    const PDFPage* page = document.getCatalog()->getPage(0);
    const QRectF rect = page->getPhysicalRectMM(page->getMediaBox());
    QVERIFY(GeometryTestHelper::isNear(rect.width(), 210.0, 0.05));
    QVERIFY(GeometryTestHelper::isNear(rect.height(), 297.0, 0.05));
    QVERIFY(GeometryTestHelper::isNear(PDFPageGeometry::getShownSizeMM(page).width(), 210.0, 0.05));
    QVERIFY(GeometryTestHelper::isNear(page->getMillimetersToUnits(), 72.0 / 25.4 / 2.0, 1e-9));
}

void DocumentPageGeometryTest::hostileUserUnitIsOne()
{
    // A unit, which is not a reasonable positive number, is the default 1.0 (plan 6)
    for (const QByteArray& value : { QByteArray("-1"), QByteArray("0"), QByteArray("100000000000"), QByteArray("0.01"), QByteArray("(text)") })
    {
        const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /UserUnit " + value).read();
        QCOMPARE(document.getCatalog()->getPage(0)->getEffectiveUserUnit(), 1.0);
    }
}

void DocumentPageGeometryTest::oldGettersKeepTheirMeaning()
{
    // R6: getCropBox and getRectMM stay as the renderer, the merger and the old applications read them
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /CropBox [-10 -10 700 900] /UserUnit 2").read();
    const PDFPage* page = document.getCatalog()->getPage(0);
    QVERIFY(GeometryTestHelper::isNear(page->getCropBox(), QRectF(-10, -10, 710, 910)));
    QVERIFY(GeometryTestHelper::isNear(page->getRectMM(QRectF(0, 0, 72, 72)).width(), 25.4));
}

void DocumentPageGeometryTest::shownMarginsFollowRotation()
{
    // 5.2: a page turned clockwise by 90° shows its unrotated bottom edge at the left, 180° its
    // right edge, 270° its top edge
    using Box = QMarginsF;
    const Box shownLeft(10, 0, 0, 0);
    QCOMPARE(PDFPageGeometry::getUnrotatedMargins(shownLeft, PageRotation::None), Box(10, 0, 0, 0));
    QCOMPARE(PDFPageGeometry::getUnrotatedMargins(shownLeft, PageRotation::Rotate90), Box(0, 0, 0, 10));
    QCOMPARE(PDFPageGeometry::getUnrotatedMargins(shownLeft, PageRotation::Rotate180), Box(0, 0, 10, 0));
    QCOMPARE(PDFPageGeometry::getUnrotatedMargins(shownLeft, PageRotation::Rotate270), Box(0, 10, 0, 0));
}

void DocumentPageGeometryTest::cropEqualMarginsOfA4()
{
    // Plan 5.3: A4 and 10 mm give the crop box [28.35 28.35 566.93 813.54]
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 595.2756 841.8898]").read();
    PDFPageGeometry::CropSettings settings;
    settings.marginMM = 10.0;
    const std::optional<QRectF> crop = PDFPageGeometry::computeCropBox(document.getCatalog()->getPage(0), nullptr, settings);
    QVERIFY(crop.has_value());
    QVERIFY(GeometryTestHelper::isNear(crop->left(), 28.35));
    QVERIFY(GeometryTestHelper::isNear(crop->top(), 28.35));
    QVERIFY(GeometryTestHelper::isNear(crop->right(), 566.93));
    QVERIFY(GeometryTestHelper::isNear(crop->bottom(), 813.54));
}

void DocumentPageGeometryTest::cropUserUnitTwo()
{
    // Plan 5.1: with /UserUnit 2 a margin of 10 mm is 14.17 units
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 297.64 420.94] /UserUnit 2").read();
    PDFPageGeometry::CropSettings settings;
    settings.marginMM = 10.0;
    const std::optional<QRectF> crop = PDFPageGeometry::computeCropBox(document.getCatalog()->getPage(0), nullptr, settings);
    QVERIFY(crop.has_value());
    QVERIFY(GeometryTestHelper::isNear(crop->left(), 14.17));
    QVERIFY(GeometryTestHelper::isNear(crop->top(), 14.17));
}

void DocumentPageGeometryTest::cropRotatedPageLeftMarginIsBottom()
{
    // 5.2: /Rotate 90 and a custom box at the shown left 20 mm: the bottom edge of the unrotated
    // crop box moves (the page 600 x 800 is shown 800 x 600)
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /Rotate 90").read();
    const PDFPage* page = document.getCatalog()->getPage(0);
    const QSizeF shown = PDFPageGeometry::getShownSizeMM(page);
    PDFPageGeometry::CropSettings settings;
    settings.mode = PDFPageGeometry::CropMode::CustomBox;
    settings.boxMM = QRectF(20.0, 0.0, shown.width() - 20.0, shown.height());
    const std::optional<QRectF> crop = PDFPageGeometry::computeCropBox(page, nullptr, settings);
    QVERIFY(crop.has_value());
    QVERIFY(GeometryTestHelper::isNear(*crop, QRectF(QPointF(0, GeometryTestHelper::mm(20)), QPointF(600, 800))));
}

void DocumentPageGeometryTest::cropCustomBoxOtherSizeKeepsMargins()
{
    // 5.3: a page of another size gets the margins of the box on the reference page (A4 reference,
    // A3 page: left 20, top 25, right 20, bottom 32 mm)
    GeometryTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595.2756 841.8898] /Contents 4 0 R >>");
    pdf.set(4, GeometryTestPdf::stream("0 0 m S"));
    pdf.set(5, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 841.8898 1190.5512] /Contents 4 0 R >>");
    const PDFDocument document = pdf.read();
    const PDFPage* a4 = document.getCatalog()->getPage(0);
    const PDFPage* a3 = document.getCatalog()->getPage(1);

    PDFPageGeometry::CropSettings settings;
    settings.mode = PDFPageGeometry::CropMode::CustomBox;
    settings.boxMM = QRectF(20, 25, 170, 240);
    const std::optional<QRectF> crop = PDFPageGeometry::computeCropBox(a3, a4, settings);
    QVERIFY(crop.has_value());
    QVERIFY(GeometryTestHelper::isNear(*crop, QRectF(QPointF(GeometryTestHelper::mm(20), GeometryTestHelper::mm(32)), QPointF(GeometryTestHelper::mm(277), GeometryTestHelper::mm(395)))));
}

void DocumentPageGeometryTest::cropCustomBoxOutsideNamesLargestWidth()
{
    // 398:28158: Left 20 + width 250 on the 210 mm wide page
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 595.2756 841.8898]").read();
    const QString error = PDFDocumentToolCropPages::checkCustomBox(document.getCatalog()->getPage(0), QRectF(20, 25, 250, 240));
    QCOMPARE(error, QStringLiteral("Left 20 mm + width 250 mm ends 60 mm outside the 210 mm wide page. The width can be 190 mm at most."));
    QVERIFY(PDFDocumentToolCropPages::checkCustomBox(document.getCatalog()->getPage(0), QRectF(20, 25, 190, 240)).isEmpty());

    // The tool refuses it
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), GeometryTestHelper::allPages());
    parameters.insert(QStringLiteral("mode"), QStringLiteral("custom-box"));
    parameters.insert(QStringLiteral("left"), 20.0);
    parameters.insert(QStringLiteral("top"), 25.0);
    parameters.insert(QStringLiteral("width"), 250.0);
    parameters.insert(QStringLiteral("height"), 240.0);
    PDFDocumentToolCropPages tool;
    const PDFDocumentToolResult result = tool.run(GeometryTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("190 mm at most")));
}

void DocumentPageGeometryTest::cropTooLargeMarginIsRefused()
{
    // 5.3: less than 3 points of the page would stay
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 100 100]").read();
    PDFPageGeometry::CropSettings settings;
    settings.marginMM = PDFPageSizePresets::getMillimeters(48.6);
    QVERIFY(!PDFPageGeometry::computeCropBox(document.getCatalog()->getPage(0), nullptr, settings).has_value());
    settings.marginMM = PDFPageSizePresets::getMillimeters(48.4);
    QVERIFY(PDFPageGeometry::computeCropBox(document.getCatalog()->getPage(0), nullptr, settings).has_value());
}

void DocumentPageGeometryTest::cropRemoveWritesMediaBoxOverInheritedCrop()
{
    // 5.3: Remove Crop writes the crop box = the media box into the page, an inherited crop box of
    // the page tree (Table 31) must not stay; the other page keeps the inherited one
    GeometryTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 /MediaBox [0 0 600 800] /CropBox [50 50 550 750] >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>");
    pdf.set(4, GeometryTestPdf::stream("0 0 m S"));
    pdf.set(5, "<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>");
    const PDFDocument document = pdf.read();

    QVariantMap parameters;
    PDFDocumentToolPageSelection first;
    first.pages = QStringLiteral("1");
    parameters.insert(QStringLiteral("pages"), first.toVariant());
    parameters.insert(QStringLiteral("mode"), QStringLiteral("remove"));
    PDFDocumentToolCropPages tool;
    const PDFDocumentToolResult result = tool.run(GeometryTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    const PDFDocument& output = *result.documents.front().document;
    QVERIFY(GeometryTestHelper::isNear(output.getCatalog()->getPage(0)->getCropBox(), QRectF(0, 0, 600, 800)));
    QVERIFY(GeometryTestHelper::dictionary(output, 3)->hasKey("CropBox"));
    QVERIFY(GeometryTestHelper::isNear(output.getCatalog()->getPage(1)->getCropBox(), QRectF(50, 50, 500, 700)));
}

void DocumentPageGeometryTest::cropKeepsObjectsAndRemovesThumbnail()
{
    // 12.3.4: the thumbnail would not match the page; the numbers of the objects stay (an
    // incremental update)
    GeometryTestPdf pdf = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /Thumb 5 0 R");
    pdf.set(5, GeometryTestPdf::stream("xxx", "/Width 1 /Height 1 /BitsPerComponent 8 /ColorSpace /DeviceGray"));
    const PDFDocument document = pdf.read();
    std::vector<PDFPageGeometryChange> changes(1);
    changes[0].pageIndex = 0;
    changes[0].cropBox = QRectF(10, 10, 500, 700);
    PDFPageGeometryStatistics statistics;
    const PDFDocument output = PDFPageGeometry::applyChanges(&document, changes, nullptr, &statistics);
    QCOMPARE(output.getCatalog()->getPage(0)->getPageReference(), PDFObjectReference(3, 0));
    QVERIFY(!GeometryTestHelper::dictionary(output, 3)->hasKey("Thumb"));
    QCOMPARE(statistics.thumbnailsRemoved, 1);
    QCOMPARE(output.getSourceDataHash(), document.getSourceDataHash());
    QVERIFY(GeometryTestHelper::isNear(output.getCatalog()->getPage(0)->getCropBox(), QRectF(10, 10, 500, 700)));
}

void DocumentPageGeometryTest::cropToolFactsAndNothingToDo()
{
    // 389:24633 "190 × 277 mm (was 210 × 297 mm)", "Kept in the file"
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 595.2756 841.8898]").read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), GeometryTestHelper::allPages());
    parameters.insert(QStringLiteral("margin"), 10.0);
    PDFDocumentToolCropPages tool;
    const PDFDocumentToolResult result = tool.run(GeometryTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    std::map<QString, QString> facts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    QCOMPARE(facts[QStringLiteral("pageSize")], QStringLiteral("190 × 277 mm (was 210 × 297 mm)"));
    QCOMPARE(facts[QStringLiteral("hiddenContent")], QStringLiteral("Kept in the file"));
    QVERIFY(facts.count(QStringLiteral("pageBoxes")));
    QCOMPARE(result.data.value(QStringLiteral("certified")).toBool(), false);

    // Remove Crop of a page without a crop: nothing to do
    parameters.insert(QStringLiteral("mode"), QStringLiteral("remove"));
    parameters.remove(QStringLiteral("margin"));
    QCOMPARE(tool.run(GeometryTestHelper::context(document), parameters, nullptr, nullptr).status, PDFDocumentToolResultStatus::NothingToDo);

    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("incremental-save"));

    // A certified document (12.8.2.2, Table 257: no level of /P allows a change of the boxes of
    // a page): the certification is removed from the changed document (/Perms) - it is saved only
    // as a copy written whole ("lost-copy-only", Q12, R10); the source stays certified
    GeometryTestPdf certified = GeometryTestPdf::createPage("/MediaBox [0 0 595.2756 841.8898]", QByteArray(), "0 0 m S", "/Perms << /DocMDP 9 0 R >>");
    certified.set(9, "<< /Type /Sig /Reference [<< /TransformMethod /DocMDP /TransformParams << /P 2 >> >>] >>");
    parameters.insert(QStringLiteral("mode"), QStringLiteral("equal-margins"));
    const PDFDocument certifiedDocument = certified.read();
    const PDFDocumentToolResult certifiedResult = tool.run(GeometryTestHelper::context(certifiedDocument), parameters, nullptr, nullptr);
    QCOMPARE(certifiedResult.status, PDFDocumentToolResultStatus::Success);
    QCOMPARE(certifiedResult.data.value(QStringLiteral("certified")).toBool(), true);
    QCOMPARE(certifiedResult.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));
    QCOMPARE(certifiedResult.data.value(QStringLiteral("certificationRemoved")).toBool(), true);
    QCOMPARE(certifiedResult.documents.size(), size_t(1));
    const PDFDocumentPointer copy = certifiedResult.documents.front().document;
    QVERIFY(copy);
    const PDFDictionary* trailer = copy->getStorage().getDictionaryFromObject(copy->getStorage().getTrailerDictionary());
    const PDFDictionary* catalog = trailer ? copy->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    QVERIFY(catalog);
    QVERIFY(!catalog->hasKey("Perms"));
    QVERIFY(PDFDocumentToolGeometry::isCertified(&certifiedDocument));
}

void DocumentPageGeometryTest::resizeLetterToA4Scale()
{
    // Plan 5.4: Letter to A4 with 15 mm margins: min(180 / 215.9, 267 / 279.4) = 0.834
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 612 792]").read();
    PDFPageGeometry::ResizeSettings settings;
    settings.pageSizeMM = QSizeF(210, 297);
    settings.marginsMM = QMarginsF(15, 15, 15, 15);
    const PDFPageGeometry::ResizePlacement placement = PDFPageGeometry::computeResize(document.getCatalog()->getPage(0), settings);
    QVERIFY(placement.isValid);
    const double scale = qMin(180.0 / 215.9, 267.0 / 279.4);
    QVERIFY(GeometryTestHelper::isNear(placement.scaleX, scale, 1e-4));
    QVERIFY(GeometryTestHelper::isNear(placement.scaleY, scale, 1e-4));
    QVERIFY(std::abs(scale - 0.834) < 0.001);
    QVERIFY(GeometryTestHelper::isNear(*placement.change.mediaBox, QRectF(0, 0, 595.28, 841.89)));

    // Middle center: the content 180 mm wide from the left margin, the vertical rest halved
    const QTransform& matrix = *placement.change.contentTransform;
    const double bottom = 15.0 + (267.0 - 279.4 * scale) * 0.5;
    QVERIFY(GeometryTestHelper::isNear(matrix.m11(), scale, 1e-4));
    QVERIFY(GeometryTestHelper::isNear(matrix.dx(), GeometryTestHelper::mm(15.0)));
    QVERIFY(GeometryTestHelper::isNear(matrix.dy(), GeometryTestHelper::mm(bottom)));
}

void DocumentPageGeometryTest::resizeContentIsWrappedAgainstExcessQ()
{
    // R7, 8.4.2: hostile content "Q Q" - the matrix of the resize must stay in effect for the
    // whole content, and the content ends balanced
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800]", QByteArray(), "Q Q 0 0 m 10 10 l S BT ET q").read();
    const PDFDocument output = GeometryTestHelper::resize(document, GeometryTestHelper::halfSettings());
    const QByteArray content = GeometryTestHelper::content(output, 0);

    PDFLexicalAnalyzer analyzer(content.constBegin(), content.constEnd());
    int depth = 0;
    int matrixDepth = -1;
    std::vector<int> depthsAfterMatrix;
    while (true)
    {
        const PDFLexicalAnalyzer::Token token = analyzer.fetch();
        if (token.type == PDFLexicalAnalyzer::TokenType::EndOfFile)
        {
            break;
        }
        if (token.type != PDFLexicalAnalyzer::TokenType::Command)
        {
            continue;
        }
        const QByteArray command = token.data.toByteArray();
        if (command == "q")
        {
            ++depth;
        }
        else if (command == "Q")
        {
            --depth;
            if (matrixDepth >= 0)
            {
                depthsAfterMatrix.push_back(depth);
            }
        }
        else if (command == "cm" && matrixDepth < 0)
        {
            matrixDepth = depth;
        }
    }

    // Only the very last "Q" (of the closing stream) may end the state of the matrix
    QCOMPARE(matrixDepth, 1);
    QVERIFY(!depthsAfterMatrix.empty());
    QCOMPARE(depthsAfterMatrix.back(), 0);
    depthsAfterMatrix.pop_back();
    QVERIFY(std::all_of(depthsAfterMatrix.cbegin(), depthsAfterMatrix.cend(), [matrixDepth](int value) { return value >= matrixDepth; }));
    QCOMPARE(depth, 0);
    QVERIFY(content.startsWith("q\n0.5 0 0 0.5 0 0 cm\n"));
}

void DocumentPageGeometryTest::resizeAnnotationKeys()
{
    // 5.4, R7: every key of the geometry of the annotations scaled by 0.5 (Tables 173 - 191)
    GeometryTestPdf pdf = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /Annots [10 0 R 11 0 R 12 0 R 13 0 R 14 0 R 15 0 R]");
    pdf.set(10, "<< /Type /Annot /Subtype /Line /Rect [100 100 300 300] /L [100 100 300 300] /LL 10 /LLE 5 /LLO 2 /Measure << /Type /Measure /Subtype /RL /X [<< /U (m) /C 2 >>] >> >>");
    pdf.set(11, "<< /Type /Annot /Subtype /Highlight /Rect [0 0 100 100] /QuadPoints [0 100 100 100 0 0 100 0] >>");
    pdf.set(12, "<< /Type /Annot /Subtype /Ink /Rect [0 0 100 100] /InkList [[0 0 100 100]] /Path [[0 0] [100 100 200 200 300 300]] >>");
    pdf.set(13, "<< /Type /Annot /Subtype /Polygon /Rect [0 0 100 100] /Vertices [0 0 100 0 100 100] >>");
    pdf.set(14, "<< /Type /Annot /Subtype /FreeText /Rect [0 0 200 200] /CL [0 0 50 50 100 100] /RD [2 4 6 8] /DA (/Helv 12 Tf) /BS << /W 3 >> >>");
    pdf.set(15, "<< /Type /Annot /Subtype /Popup /Rect [100 100 200 200] >>");
    const PDFDocument output = GeometryTestHelper::resize(pdf.read(), GeometryTestHelper::halfSettings());

    using H = GeometryTestHelper;
    const PDFDictionary* line = H::dictionary(output, 10);
    QVERIFY(H::isNear(H::rect(output, line->get("Rect")), QRectF(50, 50, 100, 100)));
    QCOMPARE(H::numbers(output, line->get("L")), std::vector<double>({ 50, 50, 150, 150 }));
    QCOMPARE(output.getObject(line->get("LL")).getReal(), 5.0);
    QCOMPARE(output.getObject(line->get("LLE")).getReal(), 2.5);
    QCOMPARE(output.getObject(line->get("LLO")).getReal(), 1.0);
    const PDFDictionary* measure = output.getDictionaryFromObject(line->get("Measure"));
    const PDFDictionary* format = output.getDictionaryFromObject(output.getObject(measure->get("X")).getArray()->getItem(0));
    QCOMPARE(output.getObject(format->get("C")).getReal(), 4.0);

    QCOMPARE(H::numbers(output, H::dictionary(output, 11)->get("QuadPoints")), std::vector<double>({ 0, 50, 50, 50, 0, 0, 50, 0 }));
    const PDFArray* ink = output.getObject(H::dictionary(output, 12)->get("InkList")).getArray();
    QCOMPARE(H::numbers(output, ink->getItem(0)), std::vector<double>({ 0, 0, 50, 50 }));
    const PDFArray* path = output.getObject(H::dictionary(output, 12)->get("Path")).getArray();
    QCOMPARE(H::numbers(output, path->getItem(1)), std::vector<double>({ 50, 50, 100, 100, 150, 150 }));
    QCOMPARE(H::numbers(output, H::dictionary(output, 13)->get("Vertices")), std::vector<double>({ 0, 0, 50, 0, 50, 50 }));
    const PDFDictionary* freeText = H::dictionary(output, 14);
    QCOMPARE(H::numbers(output, freeText->get("CL")), std::vector<double>({ 0, 0, 25, 25, 50, 50 }));
    QCOMPARE(H::numbers(output, freeText->get("RD")), std::vector<double>({ 1, 2, 3, 4 }));

    // /DA and /BS /W stay (R7: the appearance is mapped by 12.5.5)
    QCOMPARE(output.getObject(freeText->get("DA")).getString(), QByteArray("/Helv 12 Tf"));
    QCOMPARE(output.getObject(output.getDictionaryFromObject(freeText->get("BS"))->get("W")).getInteger(), 3);

    // A pop-up only moves with the place under its center (75, 75): its size stays
    QVERIFY(H::isNear(H::rect(output, H::dictionary(output, 15)->get("Rect")), QRectF(25, 25, 100, 100)));
}

void DocumentPageGeometryTest::resizeAnnotationsOnlyMovedWithoutScaling()
{
    // 5.4: Scale comments and form fields off - the annotations keep their size and move with the
    // content under them; NoZoom (Table 167) the same also with the scaling on
    GeometryTestPdf pdf = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /Annots [10 0 R]");
    pdf.set(10, "<< /Type /Annot /Subtype /Square /Rect [100 100 200 200] >>");
    const PDFDocument output = GeometryTestHelper::resize(pdf.read(), GeometryTestHelper::halfSettings(false));
    QVERIFY(GeometryTestHelper::isNear(GeometryTestHelper::rect(output, GeometryTestHelper::dictionary(output, 10)->get("Rect")), QRectF(25, 25, 100, 100)));

    GeometryTestPdf noZoom = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /Annots [10 0 R]");
    noZoom.set(10, "<< /Type /Annot /Subtype /Text /F 8 /Rect [100 100 200 200] >>");
    const PDFDocument noZoomOutput = GeometryTestHelper::resize(noZoom.read(), GeometryTestHelper::halfSettings(true));
    QVERIFY(GeometryTestHelper::isNear(GeometryTestHelper::rect(noZoomOutput, GeometryTestHelper::dictionary(noZoomOutput, 10)->get("Rect")), QRectF(25, 25, 100, 100)));
}

void DocumentPageGeometryTest::resizeDestinationsFollowContent()
{
    // 12.3.2.2: explicit destinations to the page in /Dests, in the name tree, in the outline, in
    // a link and in a structure destination (/SD of PDF 2.0) point to the same content; a
    // destination of another page stays
    GeometryTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R /Dests << /a [3 0 R /XYZ 72 720 0] >> /Names << /Dests 20 0 R >> /Outlines 21 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 /MediaBox [0 0 600 800] >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Annots [10 0 R] >>");
    pdf.set(4, GeometryTestPdf::stream("0 0 m S"));
    pdf.set(5, "<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>");
    pdf.set(10, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /A << /S /GoTo /D [5 0 R /XYZ 72 720 0] /Next << /S /GoTo /D [3 0 R /FitR 100 200 300 400] >> >> >>");
    pdf.set(20, "<< /Names [(b) [3 0 R /FitH 600]] >>");
    pdf.set(21, "<< /Type /Outlines /First 22 0 R /Last 22 0 R /Count 1 >>");
    pdf.set(22, "<< /Title (x) /Parent 21 0 R /Dest [3 0 R /FitV 400] /SE 23 0 R >>");
    pdf.set(23, "<< /Type /StructElem /S /P /Pg 3 0 R /SD [23 0 R /XYZ 200 100 null] >>");
    const PDFDocument document = pdf.read();

    PDFPageGeometry::ResizePlacement placement = PDFPageGeometry::computeResize(document.getCatalog()->getPage(0), GeometryTestHelper::halfSettings());
    placement.change.pageIndex = 0;
    const PDFDocument output = PDFPageGeometry::applyChanges(&document, { placement.change }, nullptr, nullptr);

    using H = GeometryTestHelper;
    const PDFDictionary* dests = output.getDictionaryFromObject(H::dictionary(output, 1)->get("Dests"));
    const PDFArray* a = output.getObject(dests->get("a")).getArray();
    QCOMPARE(output.getObject(a->getItem(2)).getReal(), 36.0);
    QCOMPARE(output.getObject(a->getItem(3)).getReal(), 360.0);

    const PDFArray* names = output.getObject(H::dictionary(output, 20)->get("Names")).getArray();
    QCOMPARE(output.getObject(output.getObject(names->getItem(1)).getArray()->getItem(2)).getReal(), 300.0);
    QCOMPARE(output.getObject(output.getObject(H::dictionary(output, 22)->get("Dest")).getArray()->getItem(2)).getReal(), 200.0);

    const PDFDictionary* action = output.getDictionaryFromObject(H::dictionary(output, 10)->get("A"));
    const PDFArray* other = output.getObject(action->get("D")).getArray();
    QCOMPARE(output.getObject(other->getItem(2)).getInteger(), 72);
    const PDFDictionary* next = output.getDictionaryFromObject(action->get("Next"));
    const PDFArray* fitR = output.getObject(next->get("D")).getArray();
    QCOMPARE(output.getObject(fitR->getItem(2)).getReal(), 50.0);
    QCOMPARE(output.getObject(fitR->getItem(5)).getReal(), 200.0);

    const PDFArray* structure = output.getObject(H::dictionary(output, 23)->get("SD")).getArray();
    QCOMPARE(output.getObject(structure->getItem(2)).getReal(), 100.0);
    QCOMPARE(output.getObject(structure->getItem(3)).getReal(), 50.0);
    QVERIFY(output.getObject(structure->getItem(4)).isNull());
}

void DocumentPageGeometryTest::resizeSharedDestinationOnce()
{
    // One destination object referenced from two links (a node shared by two paths) moves once
    GeometryTestPdf pdf = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /Annots [10 0 R 11 0 R]");
    pdf.set(10, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest 12 0 R >>");
    pdf.set(11, "<< /Type /Annot /Subtype /Link /Rect [0 0 10 10] /Dest 12 0 R >>");
    pdf.set(12, "[3 0 R /XYZ 100 200 0]");
    const PDFDocument output = GeometryTestHelper::resize(pdf.read(), GeometryTestHelper::halfSettings());
    const PDFArray* destination = output.getObjectByReference(PDFObjectReference(12, 0)).getArray();
    QCOMPARE(output.getObject(destination->getItem(2)).getReal(), 50.0);
    QCOMPARE(output.getObject(destination->getItem(3)).getReal(), 100.0);
}

void DocumentPageGeometryTest::resizeViewportMeasures()
{
    // 12.9, R7: the box of the viewport follows the content; a rectilinear measure divides /C of
    // the first number format of /X and /D by the scale, of /A by its square, the further formats
    // stay; a distance measured after the change is the same (100 units * 0.5 ft = 50 units * 1 ft)
    GeometryTestPdf pdf = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /VP [<< /Type /Viewport /BBox [0 0 600 800] /Measure 30 0 R >> 31 0 R]");
    pdf.set(30, "<< /Type /Measure /Subtype /RL /R (1 in = 1 ft) /X [<< /U (ft) /C 0.5 >> << /U (in) /C 12 >>] /D [<< /U (ft) /C 0.5 >>] /A [<< /U (sq ft) /C 0.25 >>] /O [100 100] >>");
    pdf.set(31, "<< /Type /Viewport /BBox [100 100 200 200] /Measure << /Type /Measure /Subtype /GEO /GPTS [0 0 1 1] /LPTS [0 0 1 1] >> >>");
    const PDFDocument output = GeometryTestHelper::resize(pdf.read(), GeometryTestHelper::halfSettings());

    using H = GeometryTestHelper;
    const PDFArray* viewports = output.getObject(H::dictionary(output, 3)->get("VP")).getArray();
    const PDFDictionary* first = output.getDictionaryFromObject(viewports->getItem(0));
    QVERIFY(H::isNear(H::rect(output, first->get("BBox")), QRectF(0, 0, 300, 400)));
    const PDFDictionary* measure = H::dictionary(output, 30);
    const PDFArray* x = output.getObject(measure->get("X")).getArray();
    QCOMPARE(output.getObject(output.getDictionaryFromObject(x->getItem(0))->get("C")).getReal(), 1.0);
    QCOMPARE(output.getObject(output.getDictionaryFromObject(x->getItem(1))->get("C")).getInteger(), 12);
    QCOMPARE(output.getObject(output.getDictionaryFromObject(output.getObject(measure->get("D")).getArray()->getItem(0))->get("C")).getReal(), 1.0);
    QCOMPARE(output.getObject(output.getDictionaryFromObject(output.getObject(measure->get("A")).getArray()->getItem(0))->get("C")).getReal(), 1.0);
    QCOMPARE(H::numbers(output, measure->get("O")), std::vector<double>({ 50, 50 }));

    // A geospatial measure: only the box of its viewport
    const PDFDictionary* geo = H::dictionary(output, 31);
    QVERIFY(H::isNear(H::rect(output, geo->get("BBox")), QRectF(50, 50, 50, 50)));
    QCOMPARE(H::numbers(output, output.getDictionaryFromObject(geo->get("Measure"))->get("LPTS")), std::vector<double>({ 0, 0, 1, 1 }));
}

void DocumentPageGeometryTest::resizeBeadsPatternsAndLayoutBoxes()
{
    // 12.4.3 beads, 8.7.2 the matrix of a pattern maps to the default space of the page, 14.8.5.4.3
    // the box of a layout attribute (a direct and a referenced one)
    GeometryTestPdf pdf = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /B [40 0 R]");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 800] /Contents 4 0 R /B [40 0 R] /Resources << /Pattern << /P1 41 0 R >> >> >>");
    pdf.set(40, "<< /T 42 0 R /R [100 100 200 200] >>");
    pdf.set(41, "<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 1 0] /Function << /FunctionType 2 /Domain [0 1] /N 1 >> >> /Matrix [1 0 0 1 10 20] >>");
    pdf.set(42, "<< /F 40 0 R >>");
    pdf.set(43, "<< /Type /StructElem /S /Figure /Pg 3 0 R /A [<< /O /Layout /BBox [100 100 300 300] >> 44 0 R] >>");
    pdf.set(44, "<< /O /Layout /BBox [0 0 100 100] >>");
    const PDFDocument output = GeometryTestHelper::resize(pdf.read(), GeometryTestHelper::halfSettings());

    using H = GeometryTestHelper;
    QVERIFY(H::isNear(H::rect(output, H::dictionary(output, 40)->get("R")), QRectF(50, 50, 50, 50)));

    // The page has its own resources with a new pattern; the old pattern is not changed (other pages may use it)
    const PDFDictionary* resources = output.getDictionaryFromObject(H::dictionary(output, 3)->get("Resources"));
    const PDFDictionary* patterns = output.getDictionaryFromObject(resources->get("Pattern"));
    const PDFDictionary* pattern = output.getDictionaryFromObject(patterns->get("P1"));
    QCOMPARE(H::numbers(output, pattern->get("Matrix")), std::vector<double>({ 0.5, 0, 0, 0.5, 5, 10 }));
    QCOMPARE(H::numbers(output, H::dictionary(output, 41)->get("Matrix")), std::vector<double>({ 1, 0, 0, 1, 10, 20 }));

    const PDFArray* attributes = output.getObject(H::dictionary(output, 43)->get("A")).getArray();
    QVERIFY(H::isNear(H::rect(output, output.getDictionaryFromObject(attributes->getItem(0))->get("BBox")), QRectF(50, 50, 100, 100)));
    QVERIFY(H::isNear(H::rect(output, H::dictionary(output, 44)->get("BBox")), QRectF(0, 0, 50, 50)));
}

void DocumentPageGeometryTest::resizePageBoxesFollowContent()
{
    // 5.4: a trim box, which the page has (also one equal to its crop box), follows the content;
    // a bleed box, which the page does not have, takes its default - the new crop box (Table 31).
    // The sheet 400 x 500 gives the scale 0.625 and the content 375 x 500 in its middle.
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /TrimBox [0 0 600 800]").read();
    PDFPageGeometry::ResizeSettings settings;
    settings.pageSizeMM = QSizeF(PDFPageSizePresets::getMillimeters(400.0), PDFPageSizePresets::getMillimeters(500.0));
    settings.isCustomSize = true;
    PDFPageGeometry::ResizePlacement placement = PDFPageGeometry::computeResize(document.getCatalog()->getPage(0), settings, &document.getStorage());
    QVERIFY(placement.isValid);
    placement.change.pageIndex = 0;
    const PDFDocument output = PDFPageGeometry::applyChanges(&document, { placement.change }, nullptr, nullptr);
    const PDFPage* page = output.getCatalog()->getPage(0);
    QVERIFY(GeometryTestHelper::isNear(page->getTrimBox(), QRectF(12.5, 0, 375, 500)));
    QVERIFY(GeometryTestHelper::isNear(page->getBleedBox(), QRectF(0, 0, 400, 500)));
    QVERIFY(!GeometryTestHelper::dictionary(output, 3)->hasKey("BleedBox"));
}

void DocumentPageGeometryTest::resizeKeepSizeRewritesNothing()
{
    // 391:24594: Keep size - the media box moves around the content, the content streams and the
    // annotations stay as they are
    GeometryTestPdf pdf = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /Annots [10 0 R]");
    pdf.set(10, "<< /Type /Annot /Subtype /Square /Rect [100 100 200 200] >>");
    const PDFDocument document = pdf.read();
    PDFPageGeometry::ResizeSettings settings = GeometryTestHelper::halfSettings();
    settings.scaleContent = false;
    settings.anchor = Qt::AlignTop | Qt::AlignLeft;
    PDFPageGeometryStatistics statistics;
    const PDFDocument output = GeometryTestHelper::resize(document, settings, &statistics);

    QVERIFY(GeometryTestHelper::isNear(output.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 400, 300, 400)));
    QVERIFY(GeometryTestHelper::isNear(output.getCatalog()->getPage(0)->getCropBox(), QRectF(0, 400, 300, 400)));
    QCOMPARE(GeometryTestHelper::dictionary(output, 3)->get("Contents"), PDFObject::createReference(PDFObjectReference(4, 0)));
    QCOMPARE(GeometryTestHelper::content(output, 0), GeometryTestHelper::content(document, 0));
    QVERIFY(GeometryTestHelper::isNear(GeometryTestHelper::rect(output, GeometryTestHelper::dictionary(output, 10)->get("Rect")), QRectF(100, 100, 100, 100)));
    QCOMPARE(statistics.annotationsTransformed, 0);
    QVERIFY(!PDFPageGeometry::isAnnotationChanged(&document, { PDFPageGeometry::computeResize(document.getCatalog()->getPage(0), settings).change }));
}

void DocumentPageGeometryTest::resizeKeepSizeCroppedPageShowsNewSheet()
{
    // Owner 2026-11-09 (RS3CORE M2): Keep size of a cropped page - the page shows the new sheet
    // (14.11.2: the crop box is the region shown, so it is the new media box); the area hidden by
    // the old crop box stays hidden by a clip around the content (8.5.4 "re W n" inside "q ... Q"),
    // the content stream of the page stays the same object with the same bytes. The A5 crop box
    // 419.5 x 595.3 on a Letter sheet, the A4 sheet 595.28 x 841.89 centered around it.
    using H = GeometryTestHelper;
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 612 792] /CropBox [50 50 469.5 645.3]").read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), H::allPages());
    parameters.insert(QStringLiteral("pageSize"), QStringLiteral("a4"));
    parameters.insert(QStringLiteral("content"), QStringLiteral("keep-size"));
    PDFDocumentToolResizePages tool;
    const PDFDocumentToolResult result = tool.run(H::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    std::map<QString, QString> facts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    QCOMPARE(facts[QStringLiteral("pageSize")], QStringLiteral("A4 · 210 × 297 mm (was A5)"));
    QCOMPARE(facts[QStringLiteral("content")], QStringLiteral("Not scaled"));

    QVERIFY(!result.documents.empty());
    const PDFDocument& output = *result.documents.front().document;
    const PDFPage* page = output.getCatalog()->getPage(0);
    const QRectF center(50.0 + (419.5 - H::mm(210)) * 0.5, 50.0 + (595.3 - H::mm(297)) * 0.5, H::mm(210), H::mm(297));
    QVERIFY(H::isNear(page->getMediaBox(), center));
    QVERIFY(H::isNear(page->getCropBox(), center));

    // [prefix, the old stream 4 0 R, suffix]; the prefix has no matrix (nothing moves), only the clip
    const PDFObject& contentsObject = output.getObject(H::dictionary(output, 3)->get("Contents"));
    QVERIFY(contentsObject.isArray());
    const PDFArray* contents = contentsObject.getArray();
    QCOMPARE(contents->getCount(), size_t(3));
    QCOMPARE(contents->getItem(1), PDFObject::createReference(PDFObjectReference(4, 0)));
    QCOMPARE(output.getDecodedStream(output.getObject(contents->getItem(1)).getStream()), QByteArray("0 0 m 10 10 l S"));
    const QList<QByteArray> prefix = output.getDecodedStream(output.getObject(contents->getItem(0)).getStream()).simplified().split(' ');
    QCOMPARE(prefix.size(), 8);
    QCOMPARE(prefix.front(), QByteArray("q"));
    const std::vector<double> clip = { prefix[1].toDouble(), prefix[2].toDouble(), prefix[3].toDouble(), prefix[4].toDouble() };
    QVERIFY(H::isNear(QRectF(clip[0], clip[1], clip[2], clip[3]), QRectF(50, 50, 419.5, 595.3)));
    QCOMPARE(prefix.mid(5), QList<QByteArray>({ "re", "W", "n" }));
    QCOMPARE(output.getDecodedStream(output.getObject(contents->getItem(2)).getStream()).simplified(), QByteArray("Q"));
}

void DocumentPageGeometryTest::resizeRotatedPageAuto()
{
    // 5.2: an A4 page with /Rotate 90 shown landscape gets the landscape A4 sheet with Auto; the
    // unrotated media box is portrait again, /Rotate stays
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 612 792] /Rotate 90").read();
    PDFPageGeometry::ResizeSettings settings;
    settings.pageSizeMM = QSizeF(210, 297);
    PDFPageGeometry::ResizePlacement placement = PDFPageGeometry::computeResize(document.getCatalog()->getPage(0), settings);
    QVERIFY(placement.isValid);
    QVERIFY(placement.sheetSize.width() > placement.sheetSize.height());
    QVERIFY(GeometryTestHelper::isNear(*placement.change.mediaBox, QRectF(0, 0, 595.28, 841.89)));

    // The content keeps its rotation: the matrix has no rotation; the corner of the content at
    // the shown top left stays at the shown top left of the sheet (Top left anchor, no margins)
    settings.anchor = Qt::AlignTop | Qt::AlignLeft;
    placement = PDFPageGeometry::computeResize(document.getCatalog()->getPage(0), settings);
    const QTransform& matrix = *placement.change.contentTransform;
    QVERIFY(qFuzzyIsNull(matrix.m12()) && qFuzzyIsNull(matrix.m21()));
    const QPointF topLeft = matrix.map(QPointF(0, 0));
    QVERIFY(GeometryTestHelper::isNear(topLeft.x(), 0.0) && GeometryTestHelper::isNear(topLeft.y(), 0.0));
}

void DocumentPageGeometryTest::resizeMarginsWithoutRoomAreRefused()
{
    // 391:25272: 120 + 120 mm on the A4 page of 210 mm
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 612 792]").read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), GeometryTestHelper::allPages());
    parameters.insert(QStringLiteral("pageSize"), QStringLiteral("a4"));
    parameters.insert(QStringLiteral("marginLeft"), 120.0);
    parameters.insert(QStringLiteral("marginRight"), 120.0);
    PDFDocumentToolResizePages tool;
    const PDFDocumentToolResult result = tool.run(GeometryTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("The left and right margins together (240 mm) are wider than the A4 page (210 mm).")));
}

void DocumentPageGeometryTest::resizeToolFactsAndPermissionBit()
{
    // 391:25948 "A4 · 210 × 297 mm (was Letter)", "Scaled to 83 %"; R4: bit 6 is needed, when an
    // annotation moves
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 612 792]").read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), GeometryTestHelper::allPages());
    parameters.insert(QStringLiteral("pageSize"), QStringLiteral("a4"));
    for (const char* key : { "marginLeft", "marginTop", "marginRight", "marginBottom" })
    {
        parameters.insert(QString::fromLatin1(key), 15.0);
    }
    PDFDocumentToolResizePages tool;
    const PDFDocumentToolResult result = tool.run(GeometryTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    std::map<QString, QString> facts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    QCOMPARE(facts[QStringLiteral("pageSize")], QStringLiteral("A4 · 210 × 297 mm (was Letter)"));
    QCOMPARE(facts[QStringLiteral("content")], QStringLiteral("Scaled to 83 %"));

    GeometryTestPdf annotated = GeometryTestPdf::createPage("/MediaBox [0 0 612 792] /Annots [10 0 R]");
    annotated.set(10, "<< /Type /Annot /Subtype /Square /Rect [100 100 200 200] >>");
    const PDFDocument annotatedDocument = annotated.read();
    QString error;
    const std::optional<std::vector<PDFPageGeometryChange>> changes = PDFDocumentToolResizePages::createChanges(&annotatedDocument, parameters, &error);
    QVERIFY(changes.has_value());
    QVERIFY(PDFPageGeometry::isAnnotationChanged(&annotatedDocument, *changes));
    QVERIFY(!PDFPageGeometry::isAnnotationChanged(&document, *changes));
}

void DocumentPageGeometryTest::boxesBleedFromTrim()
{
    // 391:26688: media 226 x 313, trim 210 x 297 + 3 mm = bleed 216 x 303 mm
    using H = GeometryTestHelper;
    const PDFDocument document = GeometryTestPdf::createPage(H::flyerBoxes()).read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), H::allPages());
    PDFDocumentToolSetPageBoxes tool;
    const PDFDocumentToolResult result = tool.run(H::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    const PDFPage* page = result.documents.front().document->getCatalog()->getPage(0);
    QVERIFY(H::isNear(page->getBleedBox(), QRectF(H::mm(5), H::mm(5), H::mm(216), H::mm(303))));
    std::map<QString, QString> facts;
    for (const PDFDocumentToolFact& fact : result.facts)
    {
        facts[fact.id] = fact.value;
    }
    QCOMPARE(facts[QStringLiteral("bleedBox")], QStringLiteral("216 × 303 mm (trim box + 3 mm)"));
    QCOMPARE(facts[QStringLiteral("content")], QStringLiteral("Not changed"));
    QVERIFY(!facts.count(QStringLiteral("outsideMedia")));
}

void DocumentPageGeometryTest::boxesOutsideMediaIsWrittenWithWarning()
{
    // 391:27638: + 10 mm gives 230 x 317 mm over the media box of 226 x 313: written as it is,
    // the warning says the largest margin inside of the media box (8 mm)
    using H = GeometryTestHelper;
    const PDFDocument document = GeometryTestPdf::createPage(H::flyerBoxes()).read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), H::allPages());
    for (const char* key : { "marginLeft", "marginTop", "marginRight", "marginBottom" })
    {
        parameters.insert(QString::fromLatin1(key), 10.0);
    }
    PDFDocumentToolSetPageBoxes tool;
    const PDFDocumentToolResult result = tool.run(H::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    const PDFPage* page = result.documents.front().document->getCatalog()->getPage(0);
    QVERIFY(H::isNear(page->getBleedBox(), QRectF(H::mm(-2), H::mm(-2), H::mm(230), H::mm(317))));
    QCOMPARE(result.data.value(QStringLiteral("outsideMediaPages")).toArray().size(), 1);
    QVERIFY(H::isNear(PDFDocumentToolSetPageBoxes::getLargestMarginInsideMedia(document.getCatalog()->getPage(0), parameters), 8.0, 0.051));

    // With the media box in Write to the sheet grows, no warning
    parameters.insert(QStringLiteral("writeTo"), QStringLiteral("media,bleed"));
    const PDFDocumentToolResult withMedia = tool.run(H::context(document), parameters, nullptr, nullptr);
    QCOMPARE(withMedia.data.value(QStringLiteral("outsideMediaPages")).toArray().size(), 0);
    QVERIFY(H::isNear(withMedia.documents.front().document->getCatalog()->getPage(0)->getMediaBox(), QRectF(H::mm(-2), H::mm(-2), H::mm(230), H::mm(317))));
}

void DocumentPageGeometryTest::boxesTwoBoxesAndDefaultTrim()
{
    // 5.5: two boxes at once get the same rectangle each (B43); a page without the trim box is
    // based on its default, the crop box (Table 31)
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /CropBox [100 100 500 700]").read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), GeometryTestHelper::allPages());
    parameters.insert(QStringLiteral("writeTo"), QStringLiteral("trim, art"));
    for (const char* key : { "marginLeft", "marginTop", "marginRight", "marginBottom" })
    {
        parameters.insert(QString::fromLatin1(key), -PDFPageSizePresets::getMillimeters(10.0));
    }
    PDFDocumentToolSetPageBoxes tool;
    const PDFDocumentToolResult result = tool.run(GeometryTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Success);
    const PDFPage* page = result.documents.front().document->getCatalog()->getPage(0);
    QVERIFY(GeometryTestHelper::isNear(page->getTrimBox(), QRectF(110, 110, 380, 580)));
    QVERIFY(GeometryTestHelper::isNear(page->getArtBox(), QRectF(110, 110, 380, 580)));
    QVERIFY(GeometryTestHelper::isNear(page->getBleedBox(), QRectF(100, 100, 400, 600)));
    QVERIFY(GeometryTestHelper::isNear(page->getCropBox(), QRectF(100, 100, 400, 600)));
}

void DocumentPageGeometryTest::boxesWriteToErrors()
{
    // 391:28085 no box; an unknown name
    const PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800]").read();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pages"), GeometryTestHelper::allPages());
    parameters.insert(QStringLiteral("writeTo"), QString());
    PDFDocumentToolSetPageBoxes tool;
    PDFDocumentToolResult result = tool.run(GeometryTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("No box is selected")));

    parameters.insert(QStringLiteral("writeTo"), QStringLiteral("bleed,sheet"));
    result = tool.run(GeometryTestHelper::context(document), parameters, nullptr, nullptr);
    QCOMPARE(result.status, PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("sheet")));
}

void DocumentPageGeometryTest::editorGeometryRotatedMargins()
{
    // B42: the dialog Page Geometry works as the page is shown - a left margin of 10 mm of a page
    // with /Rotate 90 makes the sheet larger at its unrotated bottom edge
    PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /Rotate 90").read();
    PDFPageGeometrySettings settings;
    settings.referenceBox = PDFPageGeometrySettings::ReferenceBox::MediaBox;
    settings.marginsMM = QMarginsF(10, 0, 0, 0);
    settings.anchor = PDFPageGeometrySettings::Anchor::MiddleCenter;
    QVERIFY(bool(PDFPageGeometry::apply(&document, settings)));
    QVERIFY(GeometryTestHelper::isNear(document.getCatalog()->getPage(0)->getMediaBox(), QRectF(QPointF(0, -GeometryTestHelper::mm(10)), QPointF(600, 800))));
}

void DocumentPageGeometryTest::editorGeometryScaleMovesOtherBoxesAndDestinations()
{
    // B43, B44: with scaled content the boxes, which are not selected, follow the content, and so
    // do the destinations; the selected boxes are the new sheet
    GeometryTestPdf pdf = GeometryTestPdf::createPage("/MediaBox [0 0 600 800] /TrimBox [100 100 500 700]", QByteArray(), "0 0 m S", "/Dests << /a [3 0 R /XYZ 100 200 0] >>");
    PDFDocument document = pdf.read();
    PDFPageGeometrySettings settings;
    settings.referenceBox = PDFPageGeometrySettings::ReferenceBox::MediaBox;
    settings.useTargetPageSize = true;
    settings.targetPageSizeMM = QSizeF(PDFPageSizePresets::getMillimeters(300), PDFPageSizePresets::getMillimeters(400));
    settings.scaleContent = true;
    QVERIFY(bool(PDFPageGeometry::apply(&document, settings)));

    const PDFPage* page = document.getCatalog()->getPage(0);
    QVERIFY(GeometryTestHelper::isNear(page->getMediaBox(), QRectF(0, 0, 300, 400)));
    QVERIFY(GeometryTestHelper::isNear(page->getCropBox(), QRectF(0, 0, 300, 400)));
    QVERIFY(GeometryTestHelper::isNear(page->getTrimBox(), QRectF(50, 50, 200, 300)));
    const PDFArray* destination = document.getObject(document.getDictionaryFromObject(GeometryTestHelper::dictionary(document, 1)->get("Dests"))->get("a")).getArray();
    QCOMPARE(document.getObject(destination->getItem(2)).getReal(), 50.0);
    QCOMPARE(document.getObject(destination->getItem(3)).getReal(), 100.0);
}

void DocumentPageGeometryTest::editorGeometryRangeOutsideIsError()
{
    // B46: a range outside of the document is an error, not "no page"
    PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800]").read();
    PDFPageGeometrySettings settings;
    settings.pageRange = QStringLiteral("60");
    const PDFOperationResult result = PDFPageGeometry::apply(&document, settings);
    QVERIFY(!bool(result));
    QVERIFY(result.getErrorMessage().contains(QStringLiteral("60")));

    settings.pageRange = QStringLiteral("1-1000000000");
    QVERIFY(!bool(PDFPageGeometry::apply(&document, settings)));
}

void DocumentPageGeometryTest::pageMasterGeometryRangeLimitedToEachOutput()
{
    // B27 (review RS3CORE): PageMaster applies its Page Geometry to each output of an export; an
    // output shorter than the range is no error (B46 would refuse the whole export) - its pages
    // of the range change, the pages beyond it are left out, an output without a page of the
    // range stays as it is (never "every page")
    PDFPageGeometrySettings settings;
    settings.marginsMM = QMarginsF(10.0, 10.0, 10.0, 10.0);
    settings.applyCropBox = false;
    settings.isRangeLimitedToDocument = true;

    // The range covers the page: the sheet is the page with 10 mm around it (the content stays)
    PDFDocument document = GeometryTestPdf::createPage("/MediaBox [0 0 600 800]").read();
    settings.pageRange = QStringLiteral("1-5");
    PDFOperationResult result = PDFPageGeometry::apply(&document, settings);
    QVERIFY2(bool(result), qPrintable(result.getErrorMessage()));
    const double margin = GeometryTestHelper::mm(10.0);
    QVERIFY(GeometryTestHelper::isNear(document.getCatalog()->getPage(0)->getMediaBox(), QRectF(-margin, -margin, 600.0 + 2.0 * margin, 800.0 + 2.0 * margin)));

    // The range is beyond the output: nothing changes, no error
    PDFDocument beyond = GeometryTestPdf::createPage("/MediaBox [0 0 600 800]").read();
    settings.pageRange = QStringLiteral("3-5");
    result = PDFPageGeometry::apply(&beyond, settings);
    QVERIFY2(bool(result), qPrintable(result.getErrorMessage()));
    QCOMPARE(beyond.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 600, 800));

    // The dialog of the Editor keeps the error of B46
    settings.isRangeLimitedToDocument = false;
    settings.pageRange = QStringLiteral("1-5");
    QVERIFY(!bool(PDFPageGeometry::apply(&beyond, settings)));
}

void DocumentPageGeometryTest::hostileBoxesAndParentCycle()
{
    // Plan 6: a box of no area or huge is no valid box; a cycle of /Parent does not hang the walk
    // to an inherited crop box
    QVERIFY(!PDFPageGeometry::isValidBox(QRectF(0, 0, 0, 0)));
    QVERIFY(!PDFPageGeometry::isValidBox(QRectF(0, 0, 1e30, 1e30)));
    QVERIFY(!PDFPageGeometry::isValidBox(QRectF(0, 0, 2, 800)));
    QVERIFY(PDFPageGeometry::isValidBox(QRectF(612, 792, -612, -792)));

    GeometryTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 600 800] /Parent 5 0 R >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /Contents 4 0 R >>");
    pdf.set(4, GeometryTestPdf::stream("0 0 m S"));
    pdf.set(5, "<< /Type /Pages /Parent 2 0 R >>");
    const PDFDocument document = pdf.read();
    QVERIFY(!PDFPageGeometry::isBoxDefined(document.getCatalog()->getPage(0), &document.getStorage(), PDFPageGeometry::Box::CropBox));

    // The sheet of 1e30 is refused by Resize
    PDFPageGeometry::ResizeSettings settings;
    settings.pageSizeMM = QSizeF(1e30, 1e30);
    settings.isCustomSize = true;
    QVERIFY(!PDFPageGeometry::computeResize(document.getCatalog()->getPage(0), settings).isValid);
}

QTEST_GUILESS_MAIN(DocumentPageGeometryTest)

#include "tst_documentpagegeometrytest.moc"
