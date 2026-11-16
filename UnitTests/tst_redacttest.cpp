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

// PDFRedact, the rebuild of the pages (step 6 of phase 2, plan 5.4, 9 with B138 ... B154, R6-5 ... R6-7,
// R6-17, R6-18; package P3; B157 - the class had no test): for every kind of content under an area
// (text, image, inline image, soft mask, mask, mesh, shading, open stroke, Type 3, a form XObject
// shared by two pages and in a cycle) nothing stays in the area (pixels and bytes) and nothing
// changes outside of it.

#include "pdfredact.h"
#include "pdfaction.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttextindex.h"
#include "pdfencoding.h"
#include "pdfoutline.h"
#include "pdffont.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfparser.h"
#include "pdfpathboolean.h"
#include "pdfprogress.h"

#include "mergetestpdf.h"

#include <QtTest>

#include <atomic>

using namespace pdf;

/// The objects painted by a page in its default user space
struct RedactTestPainted
{
    std::vector<QRectF> filledBoxes;    ///< Filled paths
    std::vector<QPainterPath> filledPaths;  ///< Filled paths in the page space
    std::vector<QRectF> strokedBoxes;   ///< Stroked paths (the path, not its outline)
    std::vector<QRectF> imageBoxes;     ///< Images
    std::vector<QImage> images;         ///< The images as the reader decodes them (with their masks as the alpha channel), ARGB32
    int meshCount = 0;
    int characterCount = 0;
};

/// Collects RedactTestPainted (the matrix of the page to the device is the identity)
class RedactTestCollector : public PDFPageContentProcessor, public RedactTestPainted
{
public:
    using PDFPageContentProcessor::PDFPageContentProcessor;

protected:
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule) override
    {
        Q_UNUSED(text);
        Q_UNUSED(fillRule);
        const QPainterPath pagePath = getCurrentWorldMatrix().map(path);
        const QRectF box = pagePath.boundingRect();
        if (fill)
        {
            filledBoxes.push_back(box);
            filledPaths.push_back(pagePath);
        }
        if (stroke)
        {
            strokedBoxes.push_back(box);
        }
    }

    virtual void performImagePainting(const QImage& image) override
    {
        imageBoxes.push_back(getCurrentWorldMatrix().mapRect(QRectF(0, 0, 1, 1)));
        images.push_back(image.convertToFormat(QImage::Format_ARGB32));
    }

    virtual void performMeshPainting(const PDFMesh& mesh) override
    {
        Q_UNUSED(mesh);
        ++meshCount;
    }

    virtual void performOutputCharacter(const PDFTextCharacterInfo& info) override
    {
        Q_UNUSED(info);
        ++characterCount;
    }
};

/// A progress, which counts its steps from any thread
class RedactTestProgress : public PDFProgressSink
{
public:
    virtual void start(size_t stepCount, ProgressStartupInfo startupInfo) override { Q_UNUSED(startupInfo); count = stepCount; }
    virtual void step() override { ++steps; }
    virtual void finish() override { isFinished = true; }

    size_t count = 0;
    std::atomic<size_t> steps = 0;
    bool isFinished = false;
};

/// A canceled run
class RedactTestCanceled : public PDFOperationControl
{
public:
    virtual bool isOperationCanceled() const override { return true; }
};

class RedactTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void overlappingQuadrilateralsLeaveNoStrip();
    void contentStreamImagesAreLossless();
    void uniteSubpathsIgnoresOrientation();
    void imagePixelsAreFilled_data();
    void imagePixelsAreFilled();
    void imageMaskPixelsAreTransparent();
    void imageInsideAreaIsDropped();
    void shadingMeshIsRedacted_data();
    void shadingMeshIsRedacted();
    void openStrokeStaysOpen();
    void strokeCrossingAreaIsCut();
    void type3TextBecomesCurves();
    void type3SelectionHasBoxes();
    void sharedAndCyclicFormXObjects();
    void contentOutsideCropBoxIsRemoved();
    void pageBoxesRotationAndUserUnitAreKept();
    void pageWithoutMediaBox();
    void fillIsLastContentStream();
    void fillCoversAreaOnPage_data();
    void fillCoversAreaOnPage();
    void areaFarAwayKeepsAreasOfPage_data();
    void areaFarAwayKeepsAreasOfPage();
    void shadingPatternKeepsItsPath();
    void pageLargerThanUserSpaceLimit();
    void wholePageIsBlank();
    void documentInfoOfDirectValuesOnly();
    void outlineKeepsOnlyPagesOfTheCopy();
    void markLimits();
    void pagesInParallelAndProgress();
    void canceledRun();

    // Step 6 of phase 2, fix wave P2S6FIXCORE
    void outlinePageNumberBecomesReference();
    void bitonalImageKeepsItsBits_data();
    void bitonalImageKeepsItsBits();

private:
    /// Renders the page: \p scale pixels per point, the media box, white paper, no antialiasing
    static QImage render(const PDFDocument& document, size_t pageIndex, double scale);

    /// The pixel of the point of the page (default user space) of an image of render
    static QRgb pixelAt(const QImage& image, const PDFDocument& document, size_t pageIndex, double scale, QPointF point);

    /// A document of one page 200 x 200 painted blue by its content with the Redact annotation of \p quadPoints
    static PDFDocument createBluePage(const QByteArray& quadPoints);

    /// A document of one page of the content and the resources; the further objects (from 10) by \p objects
    static PDFDocument createPage(const QByteArray& content, const QByteArray& resources, const std::map<int, QByteArray>& objects = { },
                                  const QByteArray& pageEntries = "/MediaBox [0 0 200 200]", const QByteArray& trailerEntries = QByteArray());

    /// The redaction of the old interface (the marks, nothing of the document kept)
    static PDFDocument redact(const PDFDocument& document, QColor fillColor);

    /// The redaction of the areas
    static PDFRedact::Result redactAreas(const PDFDocument& document, const std::vector<PDFRedact::Area>& areas, std::optional<QColor> fillColor,
                                         PDFRedact::Options options = PDFRedact::None, const PDFOperationControl* control = nullptr, PDFProgressSink* progress = nullptr);

    /// An area of a rectangle
    static PDFRedact::Area createArea(PDFInteger pageIndex, const QRectF& rectangle);

    /// The objects painted by the page
    static RedactTestPainted collect(const PDFDocument& document, size_t pageIndex);

    /// The document without the last content stream of each page with more of them (the fill, R6-5)
    static PDFDocument withoutFill(const PDFDocument& document);

    /// The image XObjects of the resources of the page (the copy has no other images)
    static std::vector<const PDFStream*> getImages(const PDFDocument& document, size_t pageIndex);

    /// The samples of an 8-bit image with \p components components
    static QRgb sampleAt(const PDFDocument& document, const PDFStream* image, int x, int y);

    /// The only image of the page as the reader decodes it (its soft mask or mask is its alpha
    /// channel, ISO 32000-2, 11.6.5.3, 8.9.6.3), ARGB32; a null image for another count of images
    static QImage getDecodedImage(const PDFDocument& document, size_t pageIndex);

    /// The commands of a content stream (operators only)
    static QByteArrayList getCommands(const QByteArray& content);

    /// Samples of an image of one color, 8 bits per component, DeviceRGB
    static QByteArray createImageData(int width, int height, QColor color);

    /// The Type 3 font of the glyphs A and B (ISO 32000-2, 9.6.4: each a filled square of its em,
    /// 1000 units of the glyph space wide, /ToUnicode for the text, 9.10.3) as the objects 10 ... 13
    static std::map<int, QByteArray> getType3Font();
};

std::map<int, QByteArray> RedactTest::getType3Font()
{
    const QByteArray glyph = mergetest::MergeTestPdf::stream("1000 0 0 0 1000 1000 d1 0 0 1000 1000 re f");
    const QByteArray toUnicode = mergetest::MergeTestPdf::stream("/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def "
                                                                 "/CMapName /Adobe-Identity-UCS def /CMapType 2 def 1 begincodespacerange <00> <FF> endcodespacerange "
                                                                 "2 beginbfchar <41> <0041> <42> <0042> endbfchar endcmap CMapName currentdict /CMap defineresource pop end end");
    return { { 10, "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] /CharProcs << /A 11 0 R /B 12 0 R >> "
                   "/Encoding << /Type /Encoding /Differences [65 /A /B] >> /FirstChar 65 /LastChar 66 /Widths [1000 1000] /Resources << >> /ToUnicode 13 0 R >>" },
             { 11, glyph },
             { 12, glyph },
             { 13, toUnicode } };
}

QImage RedactTest::render(const PDFDocument& document, size_t pageIndex, double scale)
{
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    if (!page)
    {
        return QImage();
    }

    PDFCMSGeneric cms;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    PDFRenderer renderer(&document, &fontCache, &iccProfileCache, &cms, &activity, PDFRenderer::None, PDFMeshQualitySettings());

    PDFPrecompiledPage compiledPage;
    renderer.compile(&compiledPage, pageIndex);

    const QRectF box = page->getMediaBox();
    QImage image(qCeil(box.width() * scale), qCeil(box.height() * scale), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    // ISO 32000-2, 8.3.2.3: the default user space has y up; the image has y down
    const QTransform matrix(scale, 0.0, 0.0, -scale, -box.left() * scale, box.bottom() * scale);
    QPainter painter(&image);
    compiledPage.draw(&painter, box, matrix, PDFRenderer::None, 1.0);
    painter.end();
    return image;
}

QRgb RedactTest::pixelAt(const QImage& image, const PDFDocument& document, size_t pageIndex, double scale, QPointF point)
{
    const QRectF box = document.getCatalog()->getPage(pageIndex)->getMediaBox();
    const QPoint pixel(qFloor((point.x() - box.left()) * scale), qFloor((box.bottom() - point.y()) * scale));
    return image.pixel(pixel);
}

PDFDocument RedactTest::createBluePage(const QByteArray& quadPoints)
{
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 4 0 R /Annots [5 0 R] >>");
    pdf.set(4, mergetest::MergeTestPdf::stream("0 0 1 rg 0 0 200 200 re f"));
    pdf.set(5, "<< /Type /Annot /Subtype /Redact /Rect [0 0 200 200] /QuadPoints [" + quadPoints + "] >>");
    return pdf.read();
}

PDFDocument RedactTest::createPage(const QByteArray& content, const QByteArray& resources, const std::map<int, QByteArray>& objects, const QByteArray& pageEntries, const QByteArray& trailerEntries)
{
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R " + pageEntries + " /Resources " + resources + " /Contents 4 0 R >>");
    pdf.set(4, mergetest::MergeTestPdf::stream(content));
    for (const auto& [number, text] : objects)
    {
        pdf.set(number, text);
    }
    pdf.setTrailerEntries(trailerEntries);
    return pdf.read();
}

PDFDocument RedactTest::redact(const PDFDocument& document, QColor fillColor)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::Export, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    const PDFMeshQualitySettings meshQualitySettings;
    PDFRedact redactor(&document, &fontCache, &iccProfileCache, &cms, &activity, &meshQualitySettings, fillColor);
    return mergetest::MergeTestPdf::roundTrip(redactor.perform(PDFRedact::None));
}

PDFRedact::Result RedactTest::redactAreas(const PDFDocument& document, const std::vector<PDFRedact::Area>& areas, std::optional<QColor> fillColor, PDFRedact::Options options,
                                          const PDFOperationControl* control, PDFProgressSink* progress)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::Export, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    const PDFMeshQualitySettings meshQualitySettings;
    PDFRedact redactor(&document, &fontCache, &iccProfileCache, &cms, &activity, &meshQualitySettings, QColor());

    PDFRedact::Settings settings;
    settings.areas = areas;
    settings.fillColor = fillColor;
    settings.options = options;
    PDFRedact::Result result = redactor.perform(settings, control, progress);
    if (!result.isStopped)
    {
        // What a reader of the written file sees
        result.document = mergetest::MergeTestPdf::roundTrip(result.document);
    }
    return result;
}

PDFRedact::Area RedactTest::createArea(PDFInteger pageIndex, const QRectF& rectangle)
{
    QPainterPath path;
    path.addRect(rectangle);
    return { pageIndex, path };
}

RedactTestPainted RedactTest::collect(const PDFDocument& document, size_t pageIndex)
{
    PDFCMSGeneric cms;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    iccProfileCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(&document), &activity));
    RedactTestCollector collector(document.getCatalog()->getPage(pageIndex), &document, &fontCache, &iccProfileCache, &cms, &activity, QTransform(), PDFMeshQualitySettings());
    collector.processContents();
    return static_cast<const RedactTestPainted&>(collector);
}

PDFDocument RedactTest::withoutFill(const PDFDocument& document)
{
    PDFDocumentBuilder builder(&document);
    for (size_t pageIndex = 0; pageIndex < document.getCatalog()->getPageCount(); ++pageIndex)
    {
        const PDFPage* page = document.getCatalog()->getPage(pageIndex);
        const PDFObject& contents = document.getObject(page->getContents());
        if (!contents.isArray() || contents.getArray()->getCount() < 2)
        {
            continue;
        }

        PDFArrayBuilder items;
        for (size_t i = 0; i + 1 < contents.getArray()->getCount(); ++i)
        {
            items.appendItem(contents.getArray()->getItem(i));
        }
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Contents");
        factory << PDFObject::createArray(std::move(items));
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(page->getPageReference(), factory.takeObject());
    }
    return builder.build();
}

std::vector<const PDFStream*> RedactTest::getImages(const PDFDocument& document, size_t pageIndex)
{
    std::vector<const PDFStream*> images;
    const PDFPage* page = document.getCatalog()->getPage(pageIndex);
    const PDFDictionary* resources = document.getDictionaryFromObject(page->getResources());
    const PDFDictionary* xobjects = resources ? document.getDictionaryFromObject(resources->get("XObject")) : nullptr;
    for (size_t i = 0; xobjects && i < xobjects->getCount(); ++i)
    {
        const PDFObject& object = document.getObject(xobjects->getValue(i));
        const PDFObject& subtype = object.isStream() ? document.getObject(object.getStream()->getDictionary()->get("Subtype")) : PDFObject();
        if (subtype.isName() && subtype.getString() == "Image")
        {
            images.push_back(object.getStream());
        }
    }
    return images;
}

QRgb RedactTest::sampleAt(const PDFDocument& document, const PDFStream* image, int x, int y)
{
    const PDFDictionary* dictionary = image->getDictionary();
    const int width = int(document.getObject(dictionary->get("Width")).getInteger());
    const bool isGray = document.getObject(dictionary->get("ColorSpace")).getString() == "DeviceGray";
    const QByteArray samples = document.getDecodedStream(image);
    if (isGray)
    {
        const uchar gray = uchar(samples.at(qsizetype(y) * width + x));
        return qRgb(gray, gray, gray);
    }
    const qsizetype offset = (qsizetype(y) * width + x) * 3;
    return qRgb(uchar(samples.at(offset)), uchar(samples.at(offset + 1)), uchar(samples.at(offset + 2)));
}

QImage RedactTest::getDecodedImage(const PDFDocument& document, size_t pageIndex)
{
    const RedactTestPainted painted = collect(document, pageIndex);
    return painted.images.size() == 1 ? painted.images.front() : QImage();
}

QByteArrayList RedactTest::getCommands(const QByteArray& content)
{
    QByteArrayList commands;
    PDFLexicalAnalyzer analyzer(content.constData(), content.constData() + content.size());
    while (!analyzer.isAtEnd())
    {
        const PDFLexicalAnalyzer::Token token = analyzer.fetch();
        if (token.type == PDFLexicalAnalyzer::TokenType::EndOfFile)
        {
            break;
        }
        if (token.type == PDFLexicalAnalyzer::TokenType::Command)
        {
            commands << token.data.toByteArray();
        }
    }
    return commands;
}

QByteArray RedactTest::createImageData(int width, int height, QColor color)
{
    QByteArray data;
    data.reserve(width * height * 3);
    for (int i = 0; i < width * height; ++i)
    {
        data.append(char(color.red()));
        data.append(char(color.green()));
        data.append(char(color.blue()));
    }
    return data;
}

void RedactTest::overlappingQuadrilateralsLeaveNoStrip()
{
    // R6-7, ISO 32000-2 Table 195 /QuadPoints: two lines of a selection of text, whose quadrilaterals
    // overlap in y 50 ... 60 (a selection enlarges the lines by 40 %). The area of the mark is the union
    // of its quadrilaterals - the even-odd rule made the overlap a hole, which kept the content.
    // The order of the points of a quadrilateral is x1 y1 x2 y2 (upper edge) x3 y3 x4 y4 (lower edge).
    const PDFDocument document = createBluePage("20 60 120 60 20 20 120 20  20 90 120 90 20 50 120 50");
    const PDFDocument redacted = redact(document, QColor());
    QCOMPARE(redacted.getCatalog()->getPageCount(), size_t(1));

    const double scale = 2.0;
    const QImage image = render(redacted, 0, scale);
    QVERIFY(!image.isNull());

    // No fill: the area is paper, the overlap included; the content outside of the area stays
    QCOMPARE(QColor(pixelAt(image, redacted, 0, scale, QPointF(70, 30))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(image, redacted, 0, scale, QPointF(70, 55))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(image, redacted, 0, scale, QPointF(70, 80))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(image, redacted, 0, scale, QPointF(150, 150))), QColor(Qt::blue));
    QCOMPARE(QColor(pixelAt(image, redacted, 0, scale, QPointF(70, 95))), QColor(Qt::blue));

    // The quadrilateral of the other order (x1 y1 x2 y2 x4 y4 x3 y3, B146): the same area, no bow tie
    const PDFDocument otherOrder = createBluePage("20 60 120 60 120 20 20 20");
    const PDFDocument otherRedacted = redact(otherOrder, QColor());
    const QImage otherImage = render(otherRedacted, 0, scale);
    for (const QPointF point : { QPointF(30, 25), QPointF(30, 55), QPointF(110, 25), QPointF(110, 55), QPointF(70, 40) })
    {
        QCOMPARE(QColor(pixelAt(otherImage, otherRedacted, 0, scale, point)), QColor(Qt::white));
    }
}

void RedactTest::contentStreamImagesAreLossless()
{
    // B154: QPdfWriter compressed an image by JPEG (DCTDecode, lossy) when it was smaller; the rebuilt
    // page must keep the pixels exactly (the check of the redaction compares them, R6-5). ISO 32000-2,
    // 7.4.4 (FlateDecode) is lossless, 7.4.8 (DCTDecode) is not. A photo-like image: a gradient with
    // a small noise, which JPEG compresses well.
    QImage image(64, 48, QImage::Format_RGB32);
    quint32 seed = 12345;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            seed = seed * 1103515245u + 12345u;
            const int noise = int((seed >> 16) % 7) - 3;
            image.setPixel(x, y, qRgb(qBound(0, x * 4 + noise, 255), qBound(0, y * 5 - noise, 255), qBound(0, 128 + noise * 2, 255)));
        }
    }

    PDFContentStreamBuilder builder(QSizeF(100, 100), PDFContentStreamBuilder::CoordinateSystem::PDF);
    QPainter* painter = builder.begin();
    painter->drawImage(QRectF(10, 10, 64, 48), image);
    PDFContentStreamBuilder::ContentStream contentStream = builder.end(painter);

    const PDFDocument& document = contentStream.document;
    const PDFDictionary* resources = document.getDictionaryFromObject(contentStream.resources);
    QVERIFY(resources);
    const PDFDictionary* xobjects = document.getDictionaryFromObject(resources->get("XObject"));
    QVERIFY(xobjects);

    const PDFStream* imageStream = nullptr;
    for (size_t i = 0; i < xobjects->getCount(); ++i)
    {
        const PDFObject& object = document.getObject(xobjects->getValue(i));
        if (!object.isStream())
        {
            continue;
        }
        const PDFDictionary* dictionary = object.getStream()->getDictionary();
        const PDFObject& subtype = document.getObject(dictionary->get("Subtype"));
        if (subtype.isName() && subtype.getString() == "Image" && document.getObject(dictionary->get("Width")).getInteger() == image.width())
        {
            imageStream = object.getStream();
        }
    }
    QVERIFY(imageStream);

    // No lossy filter
    const PDFObject& filter = document.getObject(imageStream->getDictionary()->get("Filter"));
    QByteArrayList filters;
    if (filter.isName())
    {
        filters << filter.getString();
    }
    else if (filter.isArray())
    {
        for (size_t i = 0; i < filter.getArray()->getCount(); ++i)
        {
            filters << document.getObject(filter.getArray()->getItem(i)).getString();
        }
    }
    QVERIFY2(!filters.contains("DCTDecode") && !filters.contains("JPXDecode"), filters.join(' ').constData());

    // The samples (8 bits, DeviceRGB) are exactly the pixels
    QCOMPARE(document.getObject(imageStream->getDictionary()->get("BitsPerComponent")).getInteger(), PDFInteger(8));
    QCOMPARE(document.getObject(imageStream->getDictionary()->get("ColorSpace")).getString(), QByteArray("DeviceRGB"));
    const QByteArray samples = document.getDecodedStream(imageStream);
    QCOMPARE(samples.size(), qsizetype(image.width() * image.height() * 3));
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const qsizetype offset = (qsizetype(y) * image.width() + x) * 3;
            const QRgb expected = image.pixel(x, y);
            QCOMPARE(QColor(qRgb(uchar(samples[offset]), uchar(samples[offset + 1]), uchar(samples[offset + 2]))), QColor(expected));
        }
    }
}

void RedactTest::uniteSubpathsIgnoresOrientation()
{
    // R6-7: two rectangles, one clockwise and one counterclockwise, overlapping in x 50 ... 100: the
    // non-zero rule of the plain path makes the overlap a hole (winding 1 - 1 = 0), the union covers it
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.addPolygon(QPolygonF({ QPointF(0, 0), QPointF(100, 0), QPointF(100, 50), QPointF(0, 50) }));
    path.closeSubpath();
    path.addPolygon(QPolygonF({ QPointF(50, 0), QPointF(50, 50), QPointF(150, 50), QPointF(150, 0) }));
    path.closeSubpath();
    QVERIFY(!path.contains(QPointF(75, 25)));

    const QPainterPath united = PDFPathBoolean::uniteSubpaths(path);
    QCOMPARE(united.fillRule(), Qt::WindingFill);
    for (const QPointF point : { QPointF(25, 25), QPointF(75, 25), QPointF(125, 25) })
    {
        QVERIFY2(united.contains(point), qPrintable(QString("%1 %2").arg(point.x()).arg(point.y())));
    }
    QVERIFY(!united.contains(QPointF(175, 25)));
    QVERIFY(!united.contains(QPointF(75, 75)));
    const QRectF box = united.boundingRect();
    QVERIFY(qAbs(box.left()) < 1e-6 && qAbs(box.top()) < 1e-6 && qAbs(box.right() - 150) < 1e-6 && qAbs(box.bottom() - 50) < 1e-6);

    // A degenerate subpath (a line) covers nothing
    QPainterPath line;
    line.moveTo(0, 0);
    line.lineTo(10, 10);
    QVERIFY(PDFPathBoolean::uniteSubpaths(line).isEmpty());
}

void RedactTest::imagePixelsAreFilled_data()
{
    QTest::addColumn<bool>("isInline");
    QTest::addColumn<bool>("hasFill");

    QTest::newRow("xobject-fill") << false << true;
    QTest::newRow("xobject-no-fill") << false << false;
    QTest::newRow("inline-fill") << true << true;
    QTest::newRow("inline-no-fill") << true << false;
}

void RedactTest::imagePixelsAreFilled()
{
    QFETCH(bool, isInline);
    QFETCH(bool, hasFill);

    // B138: an image of 100 x 100 red pixels on the whole page 200 x 200 (2 points a pixel), the area
    // (50, 50) - (150, 150) is the pixels 25 ... 74 of the rows 25 ... 74 (the image has its first row
    // at the top, ISO 32000-2, 8.9.5). Every one of them gets the fill (black), or the paper (white)
    // without a fill (Table 195: no /IC, the area shows what is under it - nothing is left there) -
    // none keeps its red; the old redaction drew only the outline of the area into the image.
    const QByteArray data = createImageData(100, 100, Qt::red);
    PDFDocument document;
    if (isInline)
    {
        document = createPage("q 200 0 0 200 0 0 cm BI /W 100 /H 100 /CS /RGB /BPC 8 ID " + data + "\nEI Q", "<< >>");
    }
    else
    {
        document = createPage("q 200 0 0 200 0 0 cm /Im1 Do Q", "<< /XObject << /Im1 10 0 R >> >>",
                              { { 10, mergetest::MergeTestPdf::stream(data, "/Type /XObject /Subtype /Image /Width 100 /Height 100 /ColorSpace /DeviceRGB /BitsPerComponent 8") } });
    }

    const std::optional<QColor> fillColor = hasFill ? std::optional<QColor>(QColor(Qt::black)) : std::nullopt;
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(50, 50, 100, 100)) }, fillColor);
    QVERIFY(result.pageProblems.empty());

    const std::vector<const PDFStream*> images = getImages(result.document, 0);
    QCOMPARE(images.size(), size_t(1));
    const PDFStream* image = images.front();
    QCOMPARE(result.document.getObject(image->getDictionary()->get("Width")).getInteger(), PDFInteger(100));
    QCOMPARE(result.document.getObject(image->getDictionary()->get("Height")).getInteger(), PDFInteger(100));
    const QImage decoded = getDecodedImage(result.document, 0);
    QCOMPARE(decoded.size(), QSize(100, 100));

    const QColor expected = hasFill ? QColor(Qt::black) : QColor(Qt::white);
    for (int y = 25; y < 75; ++y)
    {
        for (int x = 25; x < 75; ++x)
        {
            QCOMPARE(QColor(sampleAt(result.document, image, x, y)), expected);
            QCOMPARE(QColor(decoded.pixel(x, y)), expected);
        }
    }

    // Outside of the area (and of the strip of one pixel, by which the area is grown) the pixels stay
    for (const QPoint pixel : { QPoint(0, 0), QPoint(10, 50), QPoint(90, 50), QPoint(50, 10), QPoint(50, 90), QPoint(99, 99) })
    {
        QCOMPARE(QColor(sampleAt(result.document, image, pixel.x(), pixel.y())), QColor(Qt::red));
        QCOMPARE(qAlpha(decoded.pixel(pixel)), 255);
    }
}

void RedactTest::imageMaskPixelsAreTransparent()
{
    // B138 of a stencil mask (ISO 32000-2, 8.9.6.2): 8 x 8 samples over the page, 25 points a sample;
    // the samples 0 are painted by the green fill color, the first row (the top, y 175 ... 200) has
    // the samples 1 (not painted). The area (50, 50) - (150, 150) is the samples 2 ... 5, grown by one
    // sample: 1 ... 6. The redacted page itself drawn over a red background: the samples of the area
    // are the paper (white, opaque - no transparency is added), the rest stays as it was.
    const QByteArray samples = QByteArray::fromHex("FF00000000000000");
    const PDFDocument document = createPage("q 0 1 0 rg 200 0 0 200 0 0 cm BI /W 8 /H 8 /IM true /BPC 1 ID " + samples + "\nEI Q", "<< >>");
    PDFCMSGeneric cms;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::View, nullptr);
    PDFRenderer renderer(&document, &fontCache, &iccProfileCache, &cms, &activity, PDFRenderer::None, PDFMeshQualitySettings());
    PDFPrecompiledPage compiledPage;
    renderer.compile(&compiledPage, 0);

    const QTransform matrix(1.0, 0.0, 0.0, -1.0, 0.0, 200.0);
    QPainterPath area;
    area.addRect(QRectF(50, 50, 100, 100));
    compiledPage.redact(area, QRectF(), matrix, QColor());

    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::red);
    {
        QPainter painter(&image);
        compiledPage.draw(&painter, QRectF(), matrix, PDFRenderer::None, 1.0);
    }
    for (const QPoint pixel : { QPoint(60, 60), QPoint(100, 100), QPoint(140, 140), QPoint(52, 148) })
    {
        QCOMPARE(QColor(image.pixel(pixel)), QColor(Qt::white));
    }
    // The device has y down: the first row of the samples (not painted) is at the device y 0 ... 25
    for (const QPoint pixel : { QPoint(10, 190), QPoint(190, 190), QPoint(10, 100), QPoint(190, 100) })
    {
        QCOMPARE(QColor(image.pixel(pixel)), QColor(Qt::green));
    }
    for (const QPoint pixel : { QPoint(10, 10), QPoint(100, 10) })
    {
        QCOMPARE(QColor(image.pixel(pixel)), QColor(Qt::red));
    }

    // The written page: QPdfWriter writes the alpha of 0 and 255 as an explicit mask, whose
    // /BitsPerComponent the copy writes (Table 87), so the reader decodes the image with its mask
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(50, 50, 100, 100)) }, std::nullopt);
    const std::vector<const PDFStream*> images = getImages(result.document, 0);
    QCOMPARE(images.size(), size_t(1));
    const PDFObject& mask = result.document.getObject(images.front()->getDictionary()->get("Mask"));
    QVERIFY(mask.isStream());
    QCOMPARE(result.document.getObject(mask.getStream()->getDictionary()->get("BitsPerComponent")).getInteger(), PDFInteger(1));

    const QImage decoded = getDecodedImage(result.document, 0);
    QCOMPARE(decoded.size(), QSize(8, 8));
    QCOMPARE(qAlpha(decoded.pixel(0, 0)), 0);
    QCOMPARE(QColor(decoded.pixel(0, 7)), QColor(Qt::green));
    QCOMPARE(QColor(decoded.pixel(4, 4)), QColor(Qt::white));

    // The rendered page: the area is paper, the painted samples outside of it green
    const QImage rendered = render(result.document, 0, 1.0);
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(100, 100))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(10, 10))), QColor(Qt::green));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(10, 190))), QColor(Qt::white));
}

void RedactTest::imageInsideAreaIsDropped()
{
    // An image wholly inside of the area is not written at all (none of its pixels stays in the file)
    const QByteArray data = createImageData(10, 10, Qt::red);
    const PDFDocument document = createPage("0 0 1 rg 0 0 20 20 re f q 50 0 0 50 75 75 cm /Im1 Do Q", "<< /XObject << /Im1 10 0 R >> >>",
                                            { { 10, mergetest::MergeTestPdf::stream(data, "/Type /XObject /Subtype /Image /Width 10 /Height 10 /ColorSpace /DeviceRGB /BitsPerComponent 8") } });
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(70, 70, 60, 60)) }, QColor(Qt::black));
    QVERIFY(getImages(result.document, 0).empty());

    // The rest of the page stays
    const QImage rendered = render(withoutFill(result.document), 0, 1.0);
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(10, 10))), QColor(Qt::blue));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(100, 100))), QColor(Qt::white));
}

void RedactTest::shadingMeshIsRedacted_data()
{
    QTest::addColumn<QByteArray>("content");

    // B140: a shading by the operator sh; R6-18: a fill by a shading pattern (also a mesh)
    QTest::newRow("sh") << QByteArray("/Sh1 sh");
    QTest::newRow("pattern") << QByteArray("/Pattern cs /P1 scn 0 0 200 200 re f");
}

void RedactTest::shadingMeshIsRedacted()
{
    QFETCH(QByteArray, content);

    // An axial shading red -> blue over the page (ISO 32000-2, 8.7.4.5.3) is a mesh of triangles. The
    // area (50, 50) - (150, 150): no triangle inside of it is written (its color would stay in the file),
    // the triangles across its boundary are clipped; outside of it the shading stays.
    const QByteArray shading = "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 200 0] /Extend [true true] "
                               "/Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >> >>";
    const PDFDocument document = createPage(content, "<< /Shading << /Sh1 10 0 R >> /Pattern << /P1 << /PatternType 2 /Shading 10 0 R >> >> >>", { { 10, shading } });
    const QRectF area(50, 50, 100, 100);
    const PDFRedact::Result result = redactAreas(document, { createArea(0, area) }, std::nullopt);
    QVERIFY(result.pageProblems.empty());

    const QImage rendered = render(result.document, 0, 1.0);
    for (const QPointF point : { QPointF(60, 60), QPointF(100, 100), QPointF(140, 140) })
    {
        QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, point)), QColor(Qt::white));
    }
    for (const QPointF point : { QPointF(10, 100), QPointF(190, 100), QPointF(100, 10), QPointF(100, 190) })
    {
        QVERIFY(QColor(pixelAt(rendered, result.document, 0, 1.0, point)) != QColor(Qt::white));
    }

    // The bytes: no filled triangle of the copy lies inside of the area
    const RedactTestPainted painted = collect(result.document, 0);
    QVERIFY(!painted.filledBoxes.empty());
    const QRectF inside = area.adjusted(0.01, 0.01, -0.01, -0.01);
    for (const QRectF& box : painted.filledBoxes)
    {
        QVERIFY2(!inside.contains(box), qPrintable(QString("%1 %2 %3 %4").arg(box.left()).arg(box.top()).arg(box.width()).arg(box.height())));
    }
}

void RedactTest::openStrokeStaysOpen()
{
    // B141: an open stroke (ISO 32000-2, 8.5.3.2: a stroke never closes the path) - the inverted V
    // (20, 20) - (100, 180) - (180, 20), 4 points wide. The area lies below its apex, between its legs:
    // the old subtraction closed the path, which then got the segment (180, 20) - (20, 20) along the
    // bottom (and an open line of two points vanished)
    const PDFDocument document = createPage("1 0 0 RG 4 w 20 20 m 100 180 l 180 20 l S", "<< >>");
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(90, 30, 20, 30)) }, std::nullopt);

    const QImage rendered = render(result.document, 0, 1.0);
    // The legs stay (x 60 on the left leg is at y 100, x 140 on the right one too)
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(60, 100))), QColor(Qt::red));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(140, 100))), QColor(Qt::red));
    // No closing segment along the bottom
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(60, 20))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(140, 20))), QColor(Qt::white));
}

void RedactTest::strokeCrossingAreaIsCut()
{
    // B141: a horizontal line 6 points wide at y 100 across the page; the area x 80 ... 120 cuts it -
    // the outline of the stroke loses the area, the line on both sides stays with its width
    const PDFDocument document = createPage("1 0 0 RG 6 w 0 100 m 200 100 l S", "<< >>");
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(80, 0, 40, 200)) }, std::nullopt);

    const QImage rendered = render(result.document, 0, 2.0);
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 2.0, QPointF(40, 100))), QColor(Qt::red));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 2.0, QPointF(40, 102.5))), QColor(Qt::red));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 2.0, QPointF(160, 100))), QColor(Qt::red));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 2.0, QPointF(100, 100))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 2.0, QPointF(81, 100))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 2.0, QPointF(40, 105))), QColor(Qt::white));

    // The copy fills outlines, it strokes nothing
    const RedactTestPainted painted = collect(result.document, 0);
    QVERIFY(painted.strokedBoxes.empty());
    QVERIFY(!painted.filledPaths.empty());
    for (const QPainterPath& path : painted.filledPaths)
    {
        QVERIFY(!path.intersects(QRectF(80.01, 0, 39.98, 200)));
    }
}

void RedactTest::type3TextBecomesCurves()
{
    // A Type 3 font (ISO 32000-2, 9.6.4): the glyphs A and B are squares of their whole em; "ABAB" in
    // 20 points from (20, 100) are the squares x 20 ... 100 by 20, y 100 ... 120. The area takes the
    // second glyph (x 40 ... 60): the copy has no text operator, the glyph B is gone, A stays.
    const PDFDocument document = createPage("BT /F1 20 Tf 20 100 Td (ABAB) Tj ET", "<< /Font << /F1 10 0 R >> >>", getType3Font());

    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(40, 100, 20, 20)) }, std::nullopt);
    const PDFPage* page = result.document.getCatalog()->getPage(0);
    const PDFObject& contents = result.document.getObject(page->getContents());
    const PDFObject& content = contents.isArray() ? result.document.getObject(contents.getArray()->getItem(0)) : contents;
    QVERIFY(content.isStream());
    const QByteArrayList commands = getCommands(result.document.getDecodedStream(content.getStream()));
    for (const char* command : { "BT", "Tj", "TJ", "'", "\"", "Tf" })
    {
        QVERIFY2(!commands.contains(command), command);
    }

    const QImage rendered = render(result.document, 0, 2.0);
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 2.0, QPointF(30, 110))), QColor(Qt::black));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 2.0, QPointF(50, 110))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 2.0, QPointF(70, 110))), QColor(Qt::black));
}

void RedactTest::type3SelectionHasBoxes()
{
    // B147: the glyphs of a Type 3 font had no box, so a search (and Redact Text) found no area. A
    // glyph is estimated by its advance (20 points here): the match "BA" covers the glyphs x 40 ... 80.
    // B151: a selection is wider than its glyphs by 0.5 point on both sides.
    const PDFDocument document = createPage("BT /F1 20 Tf 20 100 Td (ABAB) Tj ET", "<< /Font << /F1 10 0 R >> >>", getType3Font());

    const std::shared_ptr<const PDFDocumentTextIndex> index = PDFDocumentTextIndex::build(&document, nullptr, nullptr, nullptr);
    QVERIFY(index);
    QCOMPARE(index->getPageText(0).trimmed(), QStringLiteral("ABAB"));
    const PDFDocumentTextSearchResult found = PDFDocumentTextSearch::find(*index, QStringLiteral("BA"), PDFDocumentTextSearchOptions(), 0, nullptr);
    QCOMPARE(found.results.size(), size_t(1));
    const QRectF box = found.results.front().boundingRect;
    QVERIFY2(box.contains(QRectF(40, 100, 40, 20)), qPrintable(QString("%1 %2 %3 %4").arg(box.left()).arg(box.top()).arg(box.width()).arg(box.height())));
    QVERIFY(box.left() <= 39.5 + 1e-6 && box.right() >= 80.5 - 1e-6);
    QVERIFY(box.left() > 20.0 && box.right() < 100.0);
}

void RedactTest::sharedAndCyclicFormXObjects()
{
    // A form XObject drawn by two pages: the area of page 1 removes its content there only (the
    // form is flattened into each page, the shared object is never changed). A form, which draws
    // itself (a cycle), ends by the nesting limit of the content processor: the redaction ends, the
    // area is clean.
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 /MediaBox [0 0 200 200] >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /Resources << /XObject << /X1 10 0 R /X2 11 0 R >> >> /Contents 4 0 R >>");
    pdf.set(4, mergetest::MergeTestPdf::stream("/X1 Do /X2 Do"));
    pdf.set(5, "<< /Type /Page /Parent 2 0 R /Resources << /XObject << /X1 10 0 R >> >> /Contents 6 0 R >>");
    pdf.set(6, mergetest::MergeTestPdf::stream("/X1 Do"));
    pdf.set(10, mergetest::MergeTestPdf::stream("0 0 1 rg 0 0 200 100 re f", "/Type /XObject /Subtype /Form /BBox [0 0 200 200]"));
    pdf.set(11, mergetest::MergeTestPdf::stream("1 0 0 rg 0 100 200 100 re f /X2 Do", "/Type /XObject /Subtype /Form /BBox [0 0 200 200] /Resources << /XObject << /X2 11 0 R >> >>"));
    const PDFDocument document = pdf.read();

    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(50, 25, 100, 150)) }, std::nullopt);
    QCOMPARE(result.document.getCatalog()->getPageCount(), size_t(2));

    const QImage first = render(result.document, 0, 1.0);
    QCOMPARE(QColor(pixelAt(first, result.document, 0, 1.0, QPointF(100, 50))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(first, result.document, 0, 1.0, QPointF(100, 150))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(first, result.document, 0, 1.0, QPointF(20, 50))), QColor(Qt::blue));
    QCOMPARE(QColor(pixelAt(first, result.document, 0, 1.0, QPointF(20, 150))), QColor(Qt::red));

    const QImage second = render(result.document, 1, 1.0);
    QCOMPARE(QColor(pixelAt(second, result.document, 1, 1.0, QPointF(100, 50))), QColor(Qt::blue));
    QCOMPARE(QColor(pixelAt(second, result.document, 1, 1.0, QPointF(100, 150))), QColor(Qt::white));
}

void RedactTest::contentOutsideCropBoxIsRemoved()
{
    // Owner answer Q6.11, B150: the media box of the copy is the crop box (ISO 32000-2, Table 31: the
    // crop box is what is shown); the content outside of it is removed - the paths are cut, the pixels
    // of an image outside become the paper. The image (60 x 60 over the media box 300 x 300: 5 points
    // a pixel) has the pixels 0 ... 9 left of the crop box x 50.
    const QByteArray data = createImageData(60, 60, Qt::green);
    const PDFDocument document = createPage("0 0 1 rg 0 0 300 300 re f q 300 0 0 300 0 0 cm /Im1 Do Q", "<< /XObject << /Im1 10 0 R >> >>",
                                            { { 10, mergetest::MergeTestPdf::stream(data, "/Type /XObject /Subtype /Image /Width 60 /Height 60 /ColorSpace /DeviceRGB /BitsPerComponent 8") } },
                                            "/MediaBox [0 0 300 300] /CropBox [50 50 250 250]");
    const PDFRedact::Result result = redactAreas(document, { }, QColor(Qt::black));

    const PDFPage* page = result.document.getCatalog()->getPage(0);
    QCOMPARE(page->getMediaBox(), QRectF(50, 50, 200, 200));
    QCOMPARE(page->getCropBox(), QRectF(50, 50, 200, 200));

    const RedactTestPainted painted = collect(result.document, 0);
    QVERIFY(!painted.filledBoxes.empty());
    const QRectF cropBox(50, 50, 200, 200);
    for (const QRectF& box : painted.filledBoxes)
    {
        QVERIFY(cropBox.adjusted(-0.01, -0.01, 0.01, 0.01).contains(box));
    }

    const std::vector<const PDFStream*> images = getImages(result.document, 0);
    QCOMPARE(images.size(), size_t(1));
    const QImage decoded = getDecodedImage(result.document, 0);
    QCOMPARE(decoded.size(), QSize(60, 60));
    for (const QPoint pixel : { QPoint(2, 30), QPoint(30, 2), QPoint(57, 57), QPoint(9, 9) })
    {
        QCOMPARE(QColor(sampleAt(result.document, images.front(), pixel.x(), pixel.y())), QColor(Qt::white));
    }
    QCOMPARE(QColor(sampleAt(result.document, images.front(), 30, 30)), QColor(Qt::green));

    // The pixels at the crop box (their centers inside) stay
    QCOMPARE(QColor(sampleAt(result.document, images.front(), 10, 30)), QColor(Qt::green));
    QCOMPARE(QColor(sampleAt(result.document, images.front(), 49, 30)), QColor(Qt::green));
}

void RedactTest::pageBoxesRotationAndUserUnitAreKept()
{
    // B148: /UserUnit (Table 31) is kept, so is /Rotate; the boxes within the new media box
    const PDFDocument document = createPage("0 0 1 rg 0 0 200 200 re f", "<< >>", { },
                                            "/MediaBox [0 0 200 200] /Rotate 90 /UserUnit 2 /TrimBox [10 10 190 190] /BleedBox [-10 -10 210 210]");
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(0, 0, 10, 10)) }, QColor(Qt::black));
    const PDFPage* page = result.document.getCatalog()->getPage(0);
    QCOMPARE(page->getPageRotation(), PageRotation::Rotate90);
    QCOMPARE(page->getUserUnit(), 2.0);
    QCOMPARE(page->getTrimBox(), QRectF(10, 10, 180, 180));
    QCOMPARE(page->getBleedBox(), QRectF(0, 0, 200, 200));
}

void RedactTest::pageWithoutMediaBox()
{
    // R6-17: a page without a valid media box crashed the rebuild (a null painter); it is a problem of
    // the page, its copy is an empty page of US Letter (the default of the readers)
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 0 0] /Resources << >> /Contents 4 0 R >>");
    pdf.set(4, mergetest::MergeTestPdf::stream("0 0 1 rg 0 0 200 200 re f"));
    pdf.set(5, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 4 0 R >>");
    const PDFDocument document = pdf.read();

    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(0, 0, 50, 50)), createArea(1, QRectF(0, 0, 50, 50)) }, QColor(Qt::black));
    QCOMPARE(result.document.getCatalog()->getPageCount(), size_t(2));
    QCOMPARE(result.pageProblems.size(), size_t(1));
    QCOMPARE(result.pageProblems.front().pageIndex, PDFInteger(0));
    QCOMPARE(result.document.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 612, 792));

    const RedactTestPainted painted = collect(result.document, 0);
    QVERIFY(painted.filledBoxes.empty());
}

void RedactTest::fillIsLastContentStream()
{
    // R6-5: the fill of the areas is the last content stream of the page and nothing else - the color
    // (DeviceRGB) and the filled area; No fill writes none
    const PDFDocument document = createPage("0 0 1 rg 0 0 200 200 re f", "<< >>");
    const PDFRedact::Result filled = redactAreas(document, { createArea(0, QRectF(20, 30, 40, 50)) }, QColor(255, 0, 0));
    const PDFPage* page = filled.document.getCatalog()->getPage(0);
    const PDFObject& contents = filled.document.getObject(page->getContents());
    QVERIFY(contents.isArray());
    QCOMPARE(contents.getArray()->getCount(), size_t(2));
    const PDFObject& fill = filled.document.getObject(contents.getArray()->getItem(1));
    QVERIFY(fill.isStream());
    const QByteArray fillContent = filled.document.getDecodedStream(fill.getStream());
    QVERIFY(fillContent.contains("1 0 0 rg"));
    const QByteArrayList commands = getCommands(fillContent);
    for (const QByteArray& command : commands)
    {
        QVERIFY2(command == "q" || command == "Q" || command == "rg" || command == "m" || command == "l" || command == "h" || command == "f", command.constData());
    }

    // The fill covers the area exactly
    const PDFDocument onlyFill = createPage(fillContent, "<< >>");
    const RedactTestPainted painted = collect(onlyFill, 0);
    QCOMPARE(painted.filledBoxes.size(), size_t(1));
    const QRectF box = painted.filledBoxes.front();
    QVERIFY(qAbs(box.left() - 20) < 0.001 && qAbs(box.top() - 30) < 0.001 && qAbs(box.width() - 40) < 0.001 && qAbs(box.height() - 50) < 0.001);

    // Without the fill the area is paper
    const QImage rendered = render(withoutFill(filled.document), 0, 1.0);
    QCOMPARE(QColor(pixelAt(rendered, filled.document, 0, 1.0, QPointF(40, 55))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(rendered, filled.document, 0, 1.0, QPointF(100, 150))), QColor(Qt::blue));

    const PDFRedact::Result noFill = redactAreas(document, { createArea(0, QRectF(20, 30, 40, 50)) }, std::nullopt);
    const PDFObject& noFillContents = noFill.document.getObject(noFill.document.getCatalog()->getPage(0)->getContents());
    QVERIFY(!noFillContents.isArray() || noFillContents.getArray()->getCount() == 1);
}

void RedactTest::fillCoversAreaOnPage_data()
{
    QTest::addColumn<QByteArray>("pageEntries");
    QTest::addColumn<QRectF>("area");

    // A page, which is not symmetric in y, and a crop box away from the origin (the media box of the copy)
    QTest::newRow("page") << QByteArray("/MediaBox [0 0 200 300]") << QRectF(20, 220, 60, 40);
    QTest::newRow("crop-box") << QByteArray("/MediaBox [0 0 200 400] /CropBox [0 100 200 400]") << QRectF(20, 320, 60, 40);
}

void RedactTest::fillCoversAreaOnPage()
{
    // R6-5, ISO 32000-2, 7.8.2 and 8.4.2: the streams of /Contents are one content, the graphics state
    // of one goes on into the next. The fill is in the default user space of the page, so it covers the
    // area itself - the whole page, as a reader shows it, has the fill over the area and the content
    // elsewhere (the matrix of the rebuilt content, a flip of y, mirrored the fill to y' = h - y)
    QFETCH(QByteArray, pageEntries);
    QFETCH(QRectF, area);

    const PDFDocument document = createPage("0 0 1 rg 0 0 200 400 re f", "<< >>", { }, pageEntries);
    const PDFRedact::Result result = redactAreas(document, { createArea(0, area) }, QColor(Qt::red));
    QVERIFY(result.pageProblems.empty());

    const QRectF box = result.document.getCatalog()->getPage(0)->getMediaBox();
    const QImage rendered = render(result.document, 0, 1.0);
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, area.center())), QColor(Qt::red));

    // The place, where the mirrored fill was, and the rest of the page: the content
    const QPointF mirrored(area.center().x(), box.top() + box.bottom() - area.center().y());
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, mirrored)), QColor(Qt::blue));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(150, box.center().y()))), QColor(Qt::blue));
}

void RedactTest::areaFarAwayKeepsAreasOfPage_data()
{
    QTest::addColumn<QRectF>("otherArea");
    QTest::addColumn<bool>("isWholePage");

    // The areas of a page are clipped by the page before they are united: a coordinate of 1e12 (an
    // area of MCP, /QuadPoints of a mark) made the grid of the union of 24 000 points - every area of
    // the page vanished, nothing was redacted and the check had no area to check
    QTest::newRow("far-away") << QRectF(1e12, 1e12, 1.0, 1.0) << false;
    QTest::newRow("huge") << QRectF(-1e12, -1e12, 2e12, 2e12) << true;
}

void RedactTest::areaFarAwayKeepsAreasOfPage()
{
    QFETCH(QRectF, otherArea);
    QFETCH(bool, isWholePage);

    const PDFDocument document = createPage("0 0 1 rg 0 0 200 200 re f", "<< >>");
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(50, 50, 100, 100)), createArea(0, otherArea) }, std::nullopt);

    // The area of the page is the requested area within the page (Table 195: no fill, the paper)
    QCOMPARE(result.areas.size(), size_t(1));
    const QRectF expectedArea = isWholePage ? QRectF(0, 0, 200, 200) : QRectF(50, 50, 100, 100);
    const QRectF areaBox = result.areas.front().path.boundingRect();
    QVERIFY2(qAbs(areaBox.left() - expectedArea.left()) < 0.01 && qAbs(areaBox.top() - expectedArea.top()) < 0.01 &&
             qAbs(areaBox.width() - expectedArea.width()) < 0.01 && qAbs(areaBox.height() - expectedArea.height()) < 0.01,
             qPrintable(QString("%1 %2 %3 %4").arg(areaBox.left()).arg(areaBox.top()).arg(areaBox.width()).arg(areaBox.height())));

    const QImage rendered = render(result.document, 0, 1.0);
    for (const QPointF point : { QPointF(60, 60), QPointF(100, 100), QPointF(140, 140) })
    {
        QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, point)), QColor(Qt::white));
    }
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(10, 10))), isWholePage ? QColor(Qt::white) : QColor(Qt::blue));
}

void RedactTest::shadingPatternKeepsItsPath()
{
    // ISO 32000-2, 8.7.3.3: a shading pattern paints only the path it fills. An axial shading with
    // /Extend fills the square (20, 20) - (80, 80); its mesh covers the page and is clipped by the
    // square. The area (50, 50) - (150, 150) cuts the square - the rest of the page stays the paper,
    // the mesh never covers it (the clip of the mesh was lost after the area was cut out of it)
    const QByteArray shading = "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 200 0] /Extend [true true] "
                               "/Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >> >>";
    const PDFDocument document = createPage("/Pattern cs /P1 scn 20 20 60 60 re f", "<< /Pattern << /P1 << /PatternType 2 /Shading 10 0 R >> >> >>", { { 10, shading } });
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(50, 50, 100, 100)) }, std::nullopt);
    QVERIFY(result.pageProblems.empty());

    const QImage rendered = render(result.document, 0, 1.0);
    QVERIFY(QColor(pixelAt(rendered, result.document, 0, 1.0, QPointF(30, 30))) != QColor(Qt::white));
    for (const QPointF point : { QPointF(100, 100), QPointF(170, 30), QPointF(30, 170), QPointF(170, 170), QPointF(10, 10), QPointF(90, 30) })
    {
        QCOMPARE(QColor(pixelAt(rendered, result.document, 0, 1.0, point)), QColor(Qt::white));
    }
}

void RedactTest::pageLargerThanUserSpaceLimit()
{
    // ISO 32000-2, Table 31 /UserUnit, Annex C: a page 20 000 points wide (a banner) - QPdfWriter writes
    // the rebuilt content of such a page in its own larger unit; the copy keeps the points of the page,
    // so its content stays where it was (the red rectangle at x 19 000 ... 19 500), and the area is
    // removed where it is
    const PDFDocument document = createPage("0 0 1 rg 0 0 20000 300 re f 1 0 0 rg 19000 100 500 100 re f", "<< >>", { }, "/MediaBox [0 0 20000 300]");
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(100, 100, 500, 100)) }, std::nullopt);
    QVERIFY(result.pageProblems.empty());
    QCOMPARE(result.document.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 20000, 300));

    const double scale = 0.05;
    const QImage rendered = render(result.document, 0, scale);
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, scale, QPointF(19250, 150))), QColor(Qt::red));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, scale, QPointF(18000, 150))), QColor(Qt::blue));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, scale, QPointF(350, 150))), QColor(Qt::white));
    QCOMPARE(QColor(pixelAt(rendered, result.document, 0, scale, QPointF(10000, 250))), QColor(Qt::blue));
}

void RedactTest::wholePageIsBlank()
{
    // R6-30: a whole page is a blank page of the same size (its content is not compiled at all), with
    // the fill over its box
    const PDFDocument document = createPage("0 0 1 rg 0 0 200 200 re f", "<< >>");
    PDFCMSGeneric cms;
    PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    PDFICCProfileCache iccProfileCache;
    PDFOptionalContentActivity activity(&document, OCUsage::Export, nullptr);
    const PDFMeshQualitySettings meshQualitySettings;
    PDFRedact redactor(&document, &fontCache, &iccProfileCache, &cms, &activity, &meshQualitySettings, QColor());
    PDFRedact::Settings settings;
    settings.wholePages = { 0 };
    settings.fillColor = QColor(Qt::black);
    const PDFRedact::Result result = redactor.perform(settings, nullptr, nullptr);
    const PDFDocument copy = mergetest::MergeTestPdf::roundTrip(result.document);

    QCOMPARE(copy.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 200, 200));
    QCOMPARE(result.areas.size(), size_t(1));
    QCOMPARE(result.areas.front().path.boundingRect(), QRectF(0, 0, 200, 200));

    const RedactTestPainted painted = collect(withoutFill(copy), 0);
    QVERIFY(painted.filledBoxes.empty());
    QVERIFY(painted.imageBoxes.empty());

    const QImage rendered = render(copy, 0, 1.0);
    QCOMPARE(QColor(pixelAt(rendered, copy, 0, 1.0, QPointF(100, 100))), QColor(Qt::black));
}

void RedactTest::documentInfoOfDirectValuesOnly()
{
    // R6-6: /Info of the copy has only the direct values of the standard keys (Table 46): a key of
    // the Info, which refers to another object (an image under an area, a page), carries nothing over
    const QByteArray data = createImageData(10, 10, Qt::red);
    const QByteArray info = "<< /Title (Secret report) /Author 11 0 R /Subject 12 0 R /Custom 10 0 R /Producer (Producer X) /Trapped /False >>";
    std::map<int, QByteArray> objects = {
        { 10, mergetest::MergeTestPdf::stream(data, "/Type /XObject /Subtype /Image /Width 10 /Height 10 /ColorSpace /DeviceRGB /BitsPerComponent 8") },
        { 11, "(Jane Novak)" },
        { 12, "3 0 R" },
        { 13, info }
    };
    const PDFDocument document = createPage("q 200 0 0 200 0 0 cm /Im1 Do Q", "<< /XObject << /Im1 10 0 R >> >>", objects, "/MediaBox [0 0 200 200]", "/Info 13 0 R");

    const PDFRedact::Result kept = redactAreas(document, { createArea(0, QRectF(0, 0, 200, 200)) }, QColor(Qt::black), PDFRedact::Options(PDFRedact::CopyTitle | PDFRedact::CopyMetadata));
    const PDFDictionary* keptInfo = kept.document.getDictionaryFromObject(kept.document.getTrailerDictionary()->get("Info"));
    QVERIFY(keptInfo);
    QCOMPARE(PDFEncoding::convertTextString(keptInfo->get("Title").getString()), QStringLiteral("Secret report"));
    QCOMPARE(PDFEncoding::convertTextString(keptInfo->get("Author").getString()), QStringLiteral("Jane Novak"));
    QCOMPARE(keptInfo->get("Trapped").getString(), QByteArray("False"));
    QVERIFY(!keptInfo->hasKey("Subject"));
    QVERIFY(!keptInfo->hasKey("Custom"));
    for (size_t i = 0; i < keptInfo->getCount(); ++i)
    {
        QVERIFY(!keptInfo->getValue(i).isReference());
    }

    // The image of the Info is not in the copy (the page image is wholly in the area, so the copy
    // has no image at all)
    for (const PDFObjectStorage::Entry& entry : kept.document.getStorage().getObjects())
    {
        if (entry.object.isStream())
        {
            const PDFObject& subtype = kept.document.getObject(entry.object.getStream()->getDictionary()->get("Subtype"));
            QVERIFY(!subtype.isName() || subtype.getString() != "Image");
        }
    }

    // The metadata without the title (R6-6: keepTitle false removes the title also with the metadata)
    const PDFRedact::Result withoutTitle = redactAreas(document, { }, QColor(Qt::black), PDFRedact::CopyMetadata);
    const PDFDictionary* withoutTitleInfo = withoutTitle.document.getDictionaryFromObject(withoutTitle.document.getTrailerDictionary()->get("Info"));
    QVERIFY(withoutTitleInfo);
    QVERIFY(!withoutTitleInfo->hasKey("Title"));
    QVERIFY(withoutTitleInfo->hasKey("Author"));
}

void RedactTest::outlineKeepsOnlyPagesOfTheCopy()
{
    // R6-30: an item of the outline to a page of the document leads to its page of the copy; an item to
    // an object, which is no page of the copy, to a document part (GoToDp) or of another action keeps
    // its title without the action; a named destination is resolved
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R /Outlines 20 0 R /Names << /Dests << /Names [(target) [3 0 R /Fit]] >> >> >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [0 0 200 200] >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /Resources << >> /Contents 4 0 R >>");
    pdf.set(4, mergetest::MergeTestPdf::stream("0 0 1 rg 0 0 200 200 re f"));
    pdf.set(20, "<< /Type /Outlines /First 21 0 R /Last 25 0 R /Count 5 >>");
    pdf.set(21, "<< /Title (Page) /Parent 20 0 R /Next 22 0 R /Dest [3 0 R /Fit] >>");
    pdf.set(22, "<< /Title (Unknown) /Parent 20 0 R /Prev 21 0 R /Next 23 0 R /Dest [4 0 R /Fit] >>");
    pdf.set(23, "<< /Title (Part) /Parent 20 0 R /Prev 22 0 R /Next 24 0 R /A << /S /GoToDp /Dp 30 0 R >> >>");
    pdf.set(24, "<< /Title (Script) /Parent 20 0 R /Prev 23 0 R /Next 25 0 R /A << /S /JavaScript /JS (app.alert(1)) >> >>");
    pdf.set(25, "<< /Title (Named) /Parent 20 0 R /Prev 24 0 R /Dest (target) >>");
    pdf.set(30, "<< /Type /DPart >>");
    const PDFDocument document = pdf.read();

    const PDFRedact::Result result = redactAreas(document, { }, QColor(Qt::black), PDFRedact::CopyOutline);
    const QSharedPointer<PDFOutlineItem> outline = result.document.getCatalog()->getOutlineRootPtr();
    QVERIFY(outline);
    QCOMPARE(outline->getChildCount(), size_t(5));

    const PDFObjectReference newPage = result.document.getCatalog()->getPage(0)->getPageReference();
    auto destinationPage = [](const PDFOutlineItem* item) -> PDFObjectReference
    {
        const PDFActionGoTo* action = dynamic_cast<const PDFActionGoTo*>(item->getAction());
        return action ? action->getDestination().getPageReference() : PDFObjectReference();
    };
    QCOMPARE(outline->getChild(0)->getTitle(), QStringLiteral("Page"));
    QCOMPARE(destinationPage(outline->getChild(0)), newPage);
    QCOMPARE(outline->getChild(1)->getTitle(), QStringLiteral("Unknown"));
    QVERIFY(!outline->getChild(1)->getAction());
    QVERIFY(!outline->getChild(2)->getAction());
    QVERIFY(!outline->getChild(3)->getAction());
    QCOMPARE(destinationPage(outline->getChild(4)), newPage);

    // The catalog of the copy has no names (the named destinations are resolved, not carried over)
    const PDFDictionary* catalog = result.document.getDictionaryFromObject(result.document.getTrailerDictionary()->get("Root"));
    QVERIFY(!catalog->hasKey("Names"));
}

void RedactTest::markLimits()
{
    // Plan 7.2: at most MAXIMUM_QUADRILATERAL_COUNT quadrilaterals of one mark; the marks are all read
    QByteArray quadPoints;
    for (int i = 0; i <= PDFRedact::MAXIMUM_QUADRILATERAL_COUNT; ++i)
    {
        quadPoints += "0 10 10 10 0 0 10 0 ";
    }
    const PDFDocument document = createBluePage(quadPoints);
    QString error;
    int markCount = 0;
    const std::vector<PDFRedact::Area> areas = PDFRedact::getMarkAreas(&document, &error, &markCount);
    QVERIFY(!error.isEmpty());
    QCOMPARE(markCount, 1);
    QCOMPARE(areas.size(), size_t(1));

    const PDFDocument fine = createBluePage("0 10 10 10 0 0 10 0");
    QVERIFY(!PDFRedact::getMarkAreas(&fine, &error, &markCount).empty());
    QVERIFY(error.isEmpty());
}

void RedactTest::pagesInParallelAndProgress()
{
    // Plan 7.2: the pages are rebuilt in parallel, each with its own writer of the content stream
    // (QPdfWriter, QPainter); the progress is stepped from the threads. 48 pages of an image each, the
    // area of each page in another place: every page has exactly its own area filled.
    constexpr int PAGE_COUNT = 48;
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    QByteArray kids;
    for (int i = 0; i < PAGE_COUNT; ++i)
    {
        kids += QByteArray::number(100 + i) + " 0 R ";
        pdf.set(100 + i, "<< /Type /Page /Parent 2 0 R /Resources << /XObject << /Im1 3 0 R >> >> /Contents 4 0 R >>");
    }
    pdf.set(2, "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(PAGE_COUNT) + " /MediaBox [0 0 200 200] >>");
    pdf.set(3, mergetest::MergeTestPdf::stream(createImageData(100, 100, Qt::red), "/Type /XObject /Subtype /Image /Width 100 /Height 100 /ColorSpace /DeviceRGB /BitsPerComponent 8"));
    pdf.set(4, mergetest::MergeTestPdf::stream("q 200 0 0 200 0 0 cm /Im1 Do Q"));
    const PDFDocument document = pdf.read();

    std::vector<PDFRedact::Area> areas;
    for (int i = 0; i < PAGE_COUNT; ++i)
    {
        areas.push_back(createArea(i, QRectF(2.0 * (i % 8) * 10.0, 2.0 * (i / 8) * 10.0, 20, 20)));
    }

    RedactTestProgress progress;
    const PDFRedact::Result result = redactAreas(document, areas, QColor(Qt::black), PDFRedact::None, nullptr, &progress);
    QCOMPARE(progress.count, size_t(PAGE_COUNT));
    QCOMPARE(progress.steps.load(), size_t(PAGE_COUNT));
    QVERIFY(progress.isFinished);
    QCOMPARE(result.document.getCatalog()->getPageCount(), size_t(PAGE_COUNT));

    for (int i = 0; i < PAGE_COUNT; ++i)
    {
        const std::vector<const PDFStream*> images = getImages(result.document, size_t(i));
        QCOMPARE(images.size(), size_t(1));

        // The pixels of the area (20 points = 10 pixels; the image rows from the top)
        const int x = (i % 8) * 10 + 5;
        const int y = 99 - ((i / 8) * 10 + 5);
        QCOMPARE(QColor(sampleAt(result.document, images.front(), x, y)), QColor(Qt::black));

        // The area of the next page is not on this page
        const int nextX = ((i + 1) % 8) * 10 + 5;
        const int nextY = 99 - (((i + 1) / 8) * 10 + 5);
        QCOMPARE(QColor(sampleAt(result.document, images.front(), nextX, qBound(0, nextY, 99))), QColor(Qt::red));
    }
}

void RedactTest::canceledRun()
{
    const PDFDocument document = createPage("0 0 1 rg 0 0 200 200 re f", "<< >>");
    const RedactTestCanceled canceled;
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(0, 0, 10, 10)) }, QColor(Qt::black), PDFRedact::None, &canceled);
    QVERIFY(result.isStopped);
}

void RedactTest::outlinePageNumberBecomesReference()
{
    // RS6SEC Low of step 6, ISO 32000-2, 12.3.2.2: the page of a destination in the document is an
    // indirect reference to its page object (an integer page number designates a page of a remote
    // document only). A kept item of a damaged file by a page number (from 0) leads to the page object
    // of the copy; a number of no page drops the action
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R /Outlines 20 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 /MediaBox [0 0 200 200] >>");
    pdf.set(3, "<< /Type /Page /Parent 2 0 R /Resources << >> /Contents 4 0 R >>");
    pdf.set(4, mergetest::MergeTestPdf::stream("0 0 1 rg 0 0 200 200 re f"));
    pdf.set(5, "<< /Type /Page /Parent 2 0 R /Resources << >> /Contents 4 0 R >>");
    pdf.set(20, "<< /Type /Outlines /First 21 0 R /Last 22 0 R /Count 2 >>");
    pdf.set(21, "<< /Title (Second) /Parent 20 0 R /Next 22 0 R /Dest [1 /Fit] >>");
    pdf.set(22, "<< /Title (Nowhere) /Parent 20 0 R /Prev 21 0 R /Dest [7 /Fit] >>");
    const PDFDocument document = pdf.read();

    const PDFRedact::Result result = redactAreas(document, { }, QColor(Qt::black), PDFRedact::CopyOutline);
    QCOMPARE(result.document.getCatalog()->getPageCount(), size_t(2));
    const PDFObjectReference secondPage = result.document.getCatalog()->getPage(1)->getPageReference();

    // The written item (the copy read back): its destination array starts with the reference of the page
    const PDFDictionary* catalog = result.document.getDictionaryFromObject(result.document.getTrailerDictionary()->get("Root"));
    const PDFDictionary* outlines = result.document.getDictionaryFromObject(catalog->get("Outlines"));
    QVERIFY(outlines);
    const PDFDictionary* first = result.document.getDictionaryFromObject(outlines->get("First"));
    QVERIFY(first);
    PDFObject destination = result.document.getObject(first->get("Dest"));
    if (destination.isNull())
    {
        const PDFDictionary* action = result.document.getDictionaryFromObject(first->get("A"));
        QVERIFY(action);
        destination = result.document.getObject(action->get("D"));
    }
    QVERIFY(destination.isArray() && destination.getArray()->getCount() >= 1);
    QVERIFY(destination.getArray()->getItem(0).isReference());
    QCOMPARE(destination.getArray()->getItem(0).getReference(), secondPage);

    const QSharedPointer<PDFOutlineItem> outline = result.document.getCatalog()->getOutlineRootPtr();
    QCOMPARE(outline->getChildCount(), size_t(2));
    QCOMPARE(outline->getChild(1)->getTitle(), QStringLiteral("Nowhere"));
    QVERIFY(!outline->getChild(1)->getAction());
}

void RedactTest::bitonalImageKeepsItsBits_data()
{
    QTest::addColumn<QColor>("fillColor");

    // RS6SEC Low of step 6: a black and white image is compiled as an image of 1 bit (Format_Mono,
    // PDFImage::canBeConvertedToMonochromatic). A fill of a color of its table (black) is written into
    // its bits; another color is a new entry of a table of 8 bits - never 32 bits per pixel. QPdfWriter
    // writes every image by 8 bits a component, so the copy shows the pixels, not the memory
    QTest::newRow("black-of-the-table") << QColor(Qt::black);
    QTest::newRow("red-new-entry") << QColor(Qt::red);
}

void RedactTest::bitonalImageKeepsItsBits()
{
    // A scan of 1 bit (ISO 32000-2, 8.9.5: /DeviceGray /BitsPerComponent 1, 1 = white) of 104 x 104
    // white samples over the page 208 x 208 (2 points a sample): the area (52, 52) - (156, 156) is the
    // samples 26 ... 77 (grown by one, B138). The pixels of the area get the fill, the rest stays white
    QFETCH(QColor, fillColor);

    const QByteArray samples = QByteArray::fromHex(QByteArray(104 / 8 * 104 * 2, 'F'));
    const PDFDocument document = createPage("q 208 0 0 208 0 0 cm /Im1 Do Q", "<< /XObject << /Im1 10 0 R >> >>",
                                            { { 10, mergetest::MergeTestPdf::stream(samples, "/Type /XObject /Subtype /Image /Width 104 /Height 104 /ColorSpace /DeviceGray /BitsPerComponent 1") } },
                                            "/MediaBox [0 0 208 208]");
    const PDFRedact::Result result = redactAreas(document, { createArea(0, QRectF(52, 52, 104, 104)) }, fillColor);
    QVERIFY(result.pageProblems.empty());

    QCOMPARE(getImages(result.document, 0).size(), size_t(1));
    const QImage decoded = getDecodedImage(result.document, 0);
    QCOMPARE(decoded.size(), QSize(104, 104));
    for (const QPoint pixel : { QPoint(30, 30), QPoint(52, 52), QPoint(74, 74), QPoint(30, 74) })
    {
        QCOMPARE(QColor(decoded.pixel(pixel)).rgb(), fillColor.rgb());
    }
    for (const QPoint pixel : { QPoint(0, 0), QPoint(10, 52), QPoint(95, 52), QPoint(52, 10), QPoint(52, 95), QPoint(103, 103) })
    {
        QCOMPARE(QColor(decoded.pixel(pixel)).rgb(), QColor(Qt::white).rgb());
    }
}

QTEST_GUILESS_MAIN(RedactTest)

#include "tst_redacttest.moc"
