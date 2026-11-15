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

// The tool Convert to Black & White (step 4 of phase 2, package P3; plan 8 row Black & White, R4-21:
// the creator stays in UnitTestsBitonalDocument): the parameters of plan 7, the syntax of "items",
// the modes of the items on the pages, the lossless codings, the images drawn by the content (B61),
// the cancellation of the images (B58), the blank pages, the annotations, the limits of the pixels
// (B65), the version and the hash of the source (R4-9), the facts of the result (307:6564, 307:6388).

#include "pdfbitonaldocumentcreator.h"
#include "pdfconstants.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_bitonal.h"
#include "pdfoperationcontrol.h"
#include "pdfpage.h"

#include <QtTest>
#include <QJsonArray>

class DocumentToolBitonalTest : public QObject
{
    Q_OBJECT

private slots:
    void parameters();
    void itemsSyntax();
    void itemsSyntax_data();
    void pageModes();
    void losslessCodings();
    void imagesDrawnByContent();
    void imagesCanceled();
    void blankPages();
    void annotationsStay();
    void pixelLimit();
    void sourceHashKept();
    void itemsValidation();
    void notConvertedFact();
    void renderingIssuesAreConverted();
    void tooLargeImageIsNotDecoded();
    void permission();

private:
    /// A page of the test document: its size, its content and its resources
    struct Page
    {
        QSizeF size = QSizeF(200, 100);
        QByteArray content;
        std::vector<std::pair<QByteArray, pdf::PDFObjectReference>> images;   ///< /XObject entries
        bool hasAnnotation = false;
    };

    /// An image XObject of 8 x 2 pixels DeviceRGB, left half \p left, right half \p right
    static pdf::PDFObjectReference addImage(pdf::PDFDocumentBuilder& builder, QColor left, QColor right, bool isBroken = false);

    /// Builds the document of the pages
    static pdf::PDFDocumentPointer createDocument(pdf::PDFDocumentBuilder& builder, const std::vector<Page>& pages, QByteArray sourceHash = QByteArray());

    /// Runs the tool over the document
    static pdf::PDFDocumentToolResult run(const pdf::PDFDocumentPointer& document, const QVariantMap& parameters, const pdf::PDFOperationControl* control = nullptr);

    /// The page image of a converted page (its only XObject), nullptr = none
    static const pdf::PDFStream* getPageImage(const pdf::PDFDocument* document, size_t pageIndex, pdf::PDFObjectReference* reference = nullptr);

    /// The decoded page image of a converted page
    static QImage decodePageImage(const pdf::PDFDocument* document, size_t pageIndex);

    /// The value of a fact, empty = none
    static QString getFact(const pdf::PDFDocumentToolResult& result, const QString& id);

    /// Number of the black pixels of an image (gray below 128)
    static int countBlack(const QImage& image);
};

/// Operation control, which reports the operation as canceled from the beginning
class DocumentToolBitonalCanceled : public pdf::PDFOperationControl
{
public:
    virtual bool isOperationCanceled() const override { return true; }
};

pdf::PDFObjectReference DocumentToolBitonalTest::addImage(pdf::PDFDocumentBuilder& builder, QColor left, QColor right, bool isBroken)
{
    QByteArray samples;
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const QColor color = x < 4 ? left : right;
            samples.append(char(color.red()));
            samples.append(char(color.green()));
            samples.append(char(color.blue()));
        }
    }

    pdf::PDFDictionaryBuilder dictionary;
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(8));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(2));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(8));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName("DeviceRGB"));
    if (isBroken)
    {
        // A valid dictionary, data, which are no flate stream: the image cannot be decoded
        samples = "this is not a compressed image stream at all";
        dictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_FILTER), pdf::PDFObject::createName("FlateDecode"));
    }
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(samples.size()));
    return builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(samples))));
}

pdf::PDFDocumentPointer DocumentToolBitonalTest::createDocument(pdf::PDFDocumentBuilder& builder, const std::vector<Page>& pages, QByteArray sourceHash)
{
    for (const Page& page : pages)
    {
        const pdf::PDFObjectReference pageReference = builder.appendPage(QRectF(QPointF(0, 0), page.size));

        pdf::PDFDictionaryBuilder pageUpdate;
        if (!page.images.empty())
        {
            pdf::PDFDictionaryBuilder xobjects;
            for (const auto& [name, reference] : page.images)
            {
                xobjects.addEntry(pdf::PDFInplaceOrMemoryString(name), pdf::PDFObject::createReference(reference));
            }
            pdf::PDFDictionaryBuilder resources;
            resources.addEntry(pdf::PDFInplaceOrMemoryString("XObject"), pdf::PDFObject::createDictionary(std::move(xobjects)));
            pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Resources"), pdf::PDFObject::createDictionary(std::move(resources)));
        }

        QByteArray content = page.content;
        pdf::PDFDictionaryBuilder contentDictionary;
        contentDictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(content.size()));
        const pdf::PDFObjectReference contentReference = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(contentDictionary), std::move(content))));
        pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(contentReference));

        if (page.hasAnnotation)
        {
            pdf::PDFDictionaryBuilder annotation;
            annotation.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Annot"));
            annotation.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Text"));
            pdf::PDFArrayBuilder rect;
            for (const int value : { 10, 10, 30, 30 })
            {
                rect.appendItem(pdf::PDFObject::createInteger(value));
            }
            annotation.addEntry(pdf::PDFInplaceOrMemoryString("Rect"), pdf::PDFObject::createArray(std::move(rect)));
            const pdf::PDFObjectReference annotationReference = builder.addObject(pdf::PDFObject::createDictionary(std::move(annotation)));

            pdf::PDFArrayBuilder annotations;
            annotations.appendItem(pdf::PDFObject::createReference(annotationReference));
            pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Annots"), pdf::PDFObject::createArray(std::move(annotations)));
        }

        builder.mergeTo(pageReference, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
    }

    pdf::PDFDocument document = builder.build();
    if (!sourceHash.isEmpty())
    {
        document = pdf::PDFDocument(pdf::PDFObjectStorage(document.getStorage()), document.getInfo()->version, sourceHash);
    }
    return pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document)));
}

pdf::PDFDocumentToolResult DocumentToolBitonalTest::run(const pdf::PDFDocumentPointer& document, const QVariantMap& parameters, const pdf::PDFOperationControl* control)
{
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(document, QByteArray(), QStringLiteral("Scanned_Archive.pdf"), true));
    pdf::PDFDocumentToolBitonal tool;
    return tool.run(context, parameters, control, nullptr);
}

const pdf::PDFStream* DocumentToolBitonalTest::getPageImage(const pdf::PDFDocument* document, size_t pageIndex, pdf::PDFObjectReference* reference)
{
    const pdf::PDFPage* page = document->getCatalog()->getPage(pageIndex);
    const pdf::PDFDictionary* resources = document->getDictionaryFromObject(page->getResources());
    const pdf::PDFDictionary* xobjects = resources ? document->getDictionaryFromObject(resources->get("XObject")) : nullptr;
    if (!xobjects || xobjects->getCount() != 1 || !xobjects->getValue(0).isReference())
    {
        return nullptr;
    }

    if (reference)
    {
        *reference = xobjects->getValue(0).getReference();
    }
    const pdf::PDFObject& object = document->getObject(xobjects->getValue(0));
    return object.isStream() ? object.getStream() : nullptr;
}

QImage DocumentToolBitonalTest::decodePageImage(const pdf::PDFDocument* document, size_t pageIndex)
{
    pdf::PDFObjectReference reference;
    if (!getPageImage(document, pageIndex, &reference))
    {
        return QImage();
    }
    return pdf::PDFBitonalDocumentCreator(document, nullptr, nullptr).getDecodedImage(reference, nullptr);
}

QString DocumentToolBitonalTest::getFact(const pdf::PDFDocumentToolResult& result, const QString& id)
{
    for (const pdf::PDFDocumentToolFact& fact : result.facts)
    {
        if (fact.id == id)
        {
            return fact.value;
        }
    }
    return QString();
}

int DocumentToolBitonalTest::countBlack(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            count += qGray(image.pixel(x, y)) < 128 ? 1 : 0;
        }
    }
    return count;
}

void DocumentToolBitonalTest::parameters()
{
    // Plan 7, 306:5353: source (Pages by default, 304:5102), the resolution up to 600 dpi with 0 =
    // estimated, the threshold 0 to 255, the five compressions, the modes of the items, the blank pages
    pdf::PDFDocumentToolBitonal tool;
    QCOMPARE(tool.getId(), QStringLiteral("optimize.bitonal"));
    QVERIFY(tool.getInfo());

    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    QString error;
    QVERIFY2(pdf::PDFDocumentToolParameters::isValid(definitions, &error), qPrintable(error));
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("source"))->getDefaultValue().toString(), QStringLiteral("pages"));
    const pdf::PDFDocumentToolParameterDefinition* dpi = pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("dpi"));
    QVERIFY(dpi);
    QCOMPARE(dpi->getIntegerMaximum(), qint64(600));
    QCOMPARE(dpi->getDefaultValue().toLongLong(), qint64(0));
    QCOMPARE(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("threshold"))->getIntegerMaximum(), qint64(255));

    QStringList compressions;
    for (const pdf::PDFDocumentToolParameterOption& option : pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("compression"))->getOptions())
    {
        compressions << option.id;
    }
    QCOMPARE(compressions, QStringList({ "auto", "flate", "runlength", "ccitt-g4", "jbig2" }));
    QVERIFY(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("items")));
    QVERIFY(pdf::PDFDocumentToolParameters::find(definitions, QStringLiteral("replaceBlankPages")));
}

void DocumentToolBitonalTest::itemsSyntax_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("isValid");

    // The syntax of the header of the tool: "<page>=<mode>; <image id>=<mode>"
    QTest::newRow("example of the header") << QStringLiteral("1=fill-white; 148 0=convert-inverted") << true;
    QTest::newRow("empty") << QString() << true;
    QTest::newRow("spaces and an empty entry") << QStringLiteral(" 3 = unchanged ;; ") << true;
    QTest::newRow("no mode") << QStringLiteral("1") << false;
    QTest::newRow("unknown mode") << QStringLiteral("1=gray") << false;
    QTest::newRow("page zero") << QStringLiteral("0=convert") << false;
    QTest::newRow("negative page") << QStringLiteral("-2=convert") << false;
    QTest::newRow("no target") << QStringLiteral("=convert") << false;
    QTest::newRow("inline image is kept (R4-8)") << QStringLiteral("inline:12 0:3=fill-black") << false;
}

void DocumentToolBitonalTest::itemsSyntax()
{
    QFETCH(QString, text);
    QFETCH(bool, isValid);

    QString error;
    const std::optional<std::vector<pdf::PDFDocumentToolBitonal::ItemEntry>> entries = pdf::PDFDocumentToolBitonal::parseItems(text, &error);
    QCOMPARE(entries.has_value(), isValid);
    QCOMPARE(error.isEmpty(), isValid);

    if (QTest::currentDataTag() == QByteArray("example of the header"))
    {
        QCOMPARE(entries->size(), size_t(2));
        QCOMPARE((*entries)[0].pageIndex, pdf::PDFInteger(0));
        QVERIFY((*entries)[0].mode == pdf::PDFBitonalDocumentCreator::ItemMode::FillWhite);
        QVERIFY(!(*entries)[1].isPage());
        QCOMPARE((*entries)[1].image.reference, pdf::PDFObjectReference(148, 0));
        QVERIFY((*entries)[1].mode == pdf::PDFBitonalDocumentCreator::ItemMode::AlgorithmInverted);

        // The text written by the panel is read back to the same entries
        QCOMPARE(pdf::PDFDocumentToolBitonal::createItemsText(*entries), QStringLiteral("1=fill-white; 148 0=convert-inverted"));
    }
}

void DocumentToolBitonalTest::pageModes()
{
    // 306:5615: the five modes, one per page; a page is the black left half and the white right half
    pdf::PDFDocumentBuilder builder;
    std::vector<Page> pages(5);
    for (Page& page : pages)
    {
        page.content = "0 0 0 rg 0 0 100 100 re f";
    }
    const pdf::PDFDocumentPointer document = createDocument(builder, pages);
    const pdf::PDFObject unchangedContents = document->getCatalog()->getPage(2)->getContents();

    QVariantMap parameters;
    parameters.insert(QStringLiteral("dpi"), 72);
    parameters.insert(QStringLiteral("compression"), QStringLiteral("flate"));
    parameters.insert(QStringLiteral("items"), QStringLiteral("1=fill-white; 2=fill-black; 3=unchanged; 4=convert-inverted"));
    const pdf::PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.size(), size_t(1));
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("Scanned_Archive_bw.pdf"));
    const pdf::PDFDocument* output = result.documents.front().document.data();

    // Fill white / fill black: the whole page is one color
    const QImage white = decodePageImage(output, 0);
    const QImage black = decodePageImage(output, 1);
    QVERIFY(!white.isNull() && !black.isNull());
    QCOMPARE(countBlack(white), 0);
    QCOMPARE(countBlack(black), black.width() * black.height());

    // Unchanged: the page keeps its own content stream
    QVERIFY(document->getObject(output->getCatalog()->getPage(2)->getContents()) == document->getObject(unchangedContents));
    QVERIFY(!getPageImage(output, 2) || getPageImage(output, 2)->getDictionary()->get("BitsPerComponent") != pdf::PDFObject::createInteger(1));

    // Converted: 200 x 100 pt at 72 dpi = 200 x 100 px, the left half black; inverted the other way
    const QImage inverted = decodePageImage(output, 3);
    const QImage converted = decodePageImage(output, 4);
    QCOMPARE(converted.size(), QSize(200, 100));
    QCOMPARE(inverted.size(), QSize(200, 100));
    QVERIFY(qGray(converted.pixel(50, 50)) < 128);
    QVERIFY(qGray(converted.pixel(150, 50)) >= 128);
    QVERIFY(qGray(inverted.pixel(50, 50)) >= 128);
    QVERIFY(qGray(inverted.pixel(150, 50)) < 128);

    // A converted page image is 1 bit DeviceGray (8.9.5, Table 87)
    const pdf::PDFDictionary* imageDictionary = getPageImage(output, 4)->getDictionary();
    QVERIFY(imageDictionary->get("BitsPerComponent") == pdf::PDFObject::createInteger(1));
    QCOMPARE(imageDictionary->get("ColorSpace").getString(), QByteArray("DeviceGray"));

    // 307:6564: the counts by the modes (the inverted one is converted too)
    QCOMPARE(getFact(result, QStringLiteral("items")), QStringLiteral("2 converted · 1 filled white · 1 filled black · 1 unchanged"));
    QCOMPARE(result.data.value(QStringLiteral("converted")).toInt(), 2);
    QCOMPARE(result.data.value(QStringLiteral("inverted")).toInt(), 1);
    QVERIFY(getFact(result, QStringLiteral("size")).endsWith(QStringLiteral(" · Flate")));
}

void DocumentToolBitonalTest::losslessCodings()
{
    // 7.4.6 CCITTFaxDecode, 7.4.7 JBIG2Decode: lossless - the decoded pixels equal those of Flate
    pdf::PDFDocumentBuilder builder;
    Page page;
    page.content = "0 0 0 rg 0 0 100 100 re f 20 20 m 180 80 l 3 w S";
    const pdf::PDFDocumentPointer document = createDocument(builder, { page });

    QImage reference;
    for (const QString& compression : { QStringLiteral("flate"), QStringLiteral("ccitt-g4"), QStringLiteral("jbig2"), QStringLiteral("runlength") })
    {
        QVariantMap parameters;
        parameters.insert(QStringLiteral("dpi"), 72);
        parameters.insert(QStringLiteral("compression"), compression);
        const pdf::PDFDocumentToolResult result = run(document, parameters);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));

        const pdf::PDFDocument* output = result.documents.front().document.data();
        const QByteArray filter = getPageImage(output, 0)->getDictionary()->get("Filter").getString();
        const QByteArray expectedFilter = compression == QLatin1String("flate") ? "FlateDecode" :
                                          compression == QLatin1String("ccitt-g4") ? "CCITTFaxDecode" :
                                          compression == QLatin1String("jbig2") ? "JBIG2Decode" : "RunLengthDecode";
        QCOMPARE(filter, expectedFilter);

        const QImage image = decodePageImage(output, 0).convertToFormat(QImage::Format_Grayscale8);
        QVERIFY(!image.isNull());
        if (reference.isNull())
        {
            reference = image;
            QVERIFY(countBlack(reference) > 0);
        }
        QCOMPARE(image, reference);
    }
}

void DocumentToolBitonalTest::imagesDrawnByContent()
{
    // B61: the source Images converts the images, which the content draws; an image only listed in
    // /Resources is never drawn and stays byte by byte; an inline image is kept (R4-8)
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference drawn = addImage(builder, Qt::darkRed, QColor(240, 240, 200));
    const pdf::PDFObjectReference unused = addImage(builder, Qt::darkBlue, Qt::yellow);

    Page page;
    page.images = { { "Im1", drawn }, { "Im2", unused } };
    page.content = "q 80 0 0 20 10 10 cm /Im1 Do Q q 8 0 0 2 100 50 cm BI /W 2 /H 1 /BPC 8 /CS /RGB ID \x10\x20\x30\x40\x50\x60 EI Q";
    const pdf::PDFDocumentPointer document = createDocument(builder, { page });
    const pdf::PDFObject unusedBefore = document->getObjectByReference(unused);
    const pdf::PDFObject contentBefore = document->getObject(document->getCatalog()->getPage(0)->getContents());

    QVariantMap parameters;
    parameters.insert(QStringLiteral("source"), QStringLiteral("images"));
    parameters.insert(QStringLiteral("compression"), QStringLiteral("flate"));
    const pdf::PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument* output = result.documents.front().document.data();

    // The drawn image: 1 bit DeviceGray, the same size (the dark half black, the light one white)
    const pdf::PDFDictionary* drawnDictionary = output->getDictionaryFromObject(output->getObjectByReference(drawn));
    QVERIFY(drawnDictionary);
    QVERIFY(drawnDictionary->get("BitsPerComponent") == pdf::PDFObject::createInteger(1));
    QVERIFY(drawnDictionary->get("Width") == pdf::PDFObject::createInteger(8));
    const QImage drawnImage = pdf::PDFBitonalDocumentCreator(output, nullptr, nullptr).getDecodedImage(drawn, nullptr);
    QVERIFY(qGray(drawnImage.pixel(1, 0)) < 128);
    QVERIFY(qGray(drawnImage.pixel(6, 0)) >= 128);

    // The unused one and the content with the inline image did not change
    QVERIFY(output->getObjectByReference(unused) == unusedBefore);
    QVERIFY(output->getObject(output->getCatalog()->getPage(0)->getContents()) == contentBefore);
    QCOMPARE(getFact(result, QStringLiteral("items")), QStringLiteral("1 converted"));
}

void DocumentToolBitonalTest::imagesCanceled()
{
    // B58: the conversion of the images stops at the cancellation (the old one never asked)
    pdf::PDFDocumentBuilder builder;
    Page page;
    page.images = { { "Im1", addImage(builder, Qt::black, Qt::white) } };
    page.content = "q 80 0 0 20 10 10 cm /Im1 Do Q";
    const pdf::PDFDocumentPointer document = createDocument(builder, { page });

    QVariantMap parameters;
    parameters.insert(QStringLiteral("source"), QStringLiteral("images"));
    DocumentToolBitonalCanceled control;
    const pdf::PDFDocumentToolResult result = run(document, parameters, &control);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Stopped);
    QVERIFY(result.documents.empty());

    // The creator alone: a canceled conversion creates no document
    pdf::PDFBitonalDocumentCreator creator(document.data(), nullptr, nullptr);
    pdf::PDFBitonalDocumentCreator::Settings settings;
    settings.conversionSource = pdf::PDFBitonalDocumentCreator::ConversionSource::Images;
    pdf::PDFBitonalDocumentCreator::ItemInfo item;
    item.imageReference = creator.getConvertibleImages().front();
    settings.items = { item };
    QVERIFY(!creator.createBitonalDocument(settings, &control));
    QVERIFY(creator.createBitonalDocument(settings, nullptr));
}

void DocumentToolBitonalTest::blankPages()
{
    // The detection and the white fill in one run (MCP): an empty page is blank, a page with a
    // single printed line (a rule of 100 mm) is not, a page of a text is not
    pdf::PDFDocumentBuilder builder;
    std::vector<Page> pages(3, Page());
    for (Page& page : pages)
    {
        page.size = QSizeF(595, 842);
    }
    pages[1].content = "0 0 0 RG 1 w 100 400 m 383 400 l S";
    pages[2].content = "0 0 0 rg 0 0 297 842 re f";
    const pdf::PDFDocumentPointer document = createDocument(builder, pages);

    QVariantMap parameters;
    parameters.insert(QStringLiteral("dpi"), 50);
    parameters.insert(QStringLiteral("replaceBlankPages"), true);
    const pdf::PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));

    const QJsonArray blankPages = result.data.value(QStringLiteral("blankPages")).toArray();
    QCOMPARE(blankPages.size(), 1);
    QCOMPARE(blankPages.at(0).toInt(), 1);
    QCOMPARE(getFact(result, QStringLiteral("items")), QStringLiteral("2 converted · 1 filled white"));

    // The source Images knows no blank pages
    parameters.insert(QStringLiteral("source"), QStringLiteral("images"));
    const pdf::PDFDocumentToolResult refused = run(document, parameters);
    QCOMPARE(refused.status, pdf::PDFDocumentToolResultStatus::Failed);
}

void DocumentToolBitonalTest::annotationsStay()
{
    // 4.6: a converted page is one image over its media box, its annotations stay (they are not
    // painted into the image - the features of the rasterization have no annotations)
    pdf::PDFDocumentBuilder builder;
    Page page;
    page.content = "0 0 0 rg 0 0 100 100 re f";
    page.hasAnnotation = true;
    const pdf::PDFDocumentPointer document = createDocument(builder, { page });

    QVariantMap parameters;
    parameters.insert(QStringLiteral("dpi"), 72);
    const pdf::PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    const pdf::PDFDocument* output = result.documents.front().document.data();
    QCOMPARE(output->getCatalog()->getPage(0)->getAnnotations().size(), size_t(1));
    QVERIFY(getPageImage(output, 0));
}

void DocumentToolBitonalTest::pixelLimit()
{
    // B65, plan 4.4: a page of 5000 x 5000 pt at 600 dpi would be 41,667 px - the resolution is an
    // error; at 100 dpi (6,944 px) it is not
    pdf::PDFDocumentBuilder builder;
    Page page;
    page.size = QSizeF(5000, 5000);
    const pdf::PDFDocumentPointer document = createDocument(builder, { page });

    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(document, QByteArray(), QString(), true));
    pdf::PDFDocumentToolBitonal tool;

    QVariantMap parameters;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(tool.getParameters(), QVariantMap(), &parameters).empty());
    parameters.insert(QStringLiteral("dpi"), 600);
    std::vector<pdf::PDFDocumentToolParameterError> errors = tool.validate(context, parameters);
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors.front().parameter, QStringLiteral("dpi"));
    QVERIFY(errors.front().message.contains(QStringLiteral("lower the resolution")));

    parameters.insert(QStringLiteral("dpi"), 100);
    QVERIFY(tool.validate(context, parameters).empty());

    // A page left unchanged is never rendered: no error
    parameters.insert(QStringLiteral("dpi"), 600);
    parameters.insert(QStringLiteral("items"), QStringLiteral("1=unchanged"));
    QVERIFY(tool.validate(context, parameters).empty());

    // The creator refuses the size itself
    QVERIFY(!pdf::PDFBitonalDocumentCreator::getPageImageSize(document->getCatalog()->getPage(0), 600).isValid());
    QCOMPARE(pdf::PDFBitonalDocumentCreator::getPageImageSize(document->getCatalog()->getPage(0), 100), QSize(6944, 6944));
}

void DocumentToolBitonalTest::sourceHashKept()
{
    // R4-9: the result keeps the version and the hash of the source data of the input (the own
    // OCR layers of PDF4QT recognize their document by it), in both sources
    for (const QString& source : { QStringLiteral("pages"), QStringLiteral("images") })
    {
        pdf::PDFDocumentBuilder builder;
        Page page;
        page.images = { { "Im1", addImage(builder, Qt::black, Qt::white) } };
        page.content = "q 80 0 0 20 10 10 cm /Im1 Do Q";
        const pdf::PDFDocumentPointer document = createDocument(builder, { page }, QByteArray("0123456789abcdef"));

        QVariantMap parameters;
        parameters.insert(QStringLiteral("source"), source);
        if (source == QLatin1String("pages"))
        {
            parameters.insert(QStringLiteral("dpi"), 72);
        }
        const pdf::PDFDocumentToolResult result = run(document, parameters);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));
        const pdf::PDFDocument* output = result.documents.front().document.data();
        QCOMPARE(output->getSourceDataHash(), QByteArray("0123456789abcdef"));
        QCOMPARE(output->getInfo()->version, document->getInfo()->version);
    }
}

void DocumentToolBitonalTest::itemsValidation()
{
    // The targets of "items" are checked against the source and the document (an entry, which
    // would be ignored, is an error for MCP)
    pdf::PDFDocumentBuilder builder;
    Page page;
    page.images = { { "Im1", addImage(builder, Qt::black, Qt::white) } };
    page.content = "q 80 0 0 20 10 10 cm /Im1 Do Q";
    const pdf::PDFDocumentPointer document = createDocument(builder, { page, page });

    QVariantMap parameters;
    parameters.insert(QStringLiteral("items"), QStringLiteral("3=fill-white"));
    pdf::PDFDocumentToolResult result = run(document, parameters);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("(1–2)")));

    parameters.insert(QStringLiteral("source"), QStringLiteral("images"));
    parameters.insert(QStringLiteral("items"), QStringLiteral("1=fill-white"));
    result = run(document, parameters);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);

    parameters.insert(QStringLiteral("items"), QStringLiteral("999 0=fill-white"));
    result = run(document, parameters);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(result.message.contains(QStringLiteral("999 0")));

    // Every item left unchanged: nothing to do
    parameters.insert(QStringLiteral("source"), QStringLiteral("pages"));
    parameters.insert(QStringLiteral("items"), QStringLiteral("1=unchanged; 2=unchanged"));
    result = run(document, parameters);
    QCOMPARE(result.status, pdf::PDFDocumentToolResultStatus::NothingToDo);

    // MCP: a value of the pages with the source images is an error, not ignored
    pdf::PDFDocumentToolBitonal tool;
    QVariantMap given;
    given.insert(QStringLiteral("source"), QStringLiteral("images"));
    given.insert(QStringLiteral("dpi"), 300);
    QCOMPARE(tool.checkGivenParameters(given).size(), size_t(1));
}

void DocumentToolBitonalTest::notConvertedFact()
{
    // 307:6388 "Not converted: pages 17, 43, 44 (rendering errors)": a partial result is Success
    // with the items, which failed (R4-20); here an image, which cannot be decoded
    pdf::PDFDocumentBuilder builder;
    Page page;
    page.images = { { "Im1", addImage(builder, Qt::black, Qt::white) }, { "Im2", addImage(builder, Qt::black, Qt::white, true) } };
    page.content = "q 80 0 0 20 10 10 cm /Im1 Do Q q 80 0 0 20 100 10 cm /Im2 Do Q";
    const pdf::PDFDocumentPointer document = createDocument(builder, { page });

    QVariantMap parameters;
    parameters.insert(QStringLiteral("source"), QStringLiteral("images"));
    const pdf::PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(getFact(result, QStringLiteral("notConverted")), QStringLiteral("image 2 (cannot be read)"));
    QCOMPARE(getFact(result, QStringLiteral("items")), QStringLiteral("1 converted · 1 not converted"));

    const QJsonArray notConverted = result.data.value(QStringLiteral("notConverted")).toArray();
    QCOMPARE(notConverted.size(), 1);
    QCOMPARE(notConverted.at(0).toObject().value(QStringLiteral("imageNumber")).toInt(), 2);
}

void DocumentToolBitonalTest::renderingIssuesAreConverted()
{
    // Owner 14. 11. 2026: a rendering error never blocks the conversion - the page is converted and
    // the result asks the user to check it ("Rendering issues", as PDF to Images 339:6505); it is not
    // "not converted", so no partial result question follows (307:6388 is for real failures only)
    pdf::PDFDocumentBuilder builder;
    Page damaged;
    damaged.content = "0 0 0 rg 10 10 50 50 re f /Missing Do";
    Page clean;
    clean.content = "0 0 0 rg 10 10 50 50 re f";
    const pdf::PDFDocumentPointer document = createDocument(builder, { clean, damaged });

    QVariantMap parameters;
    parameters.insert(QStringLiteral("source"), QStringLiteral("pages"));
    parameters.insert(QStringLiteral("dpi"), 72);
    const pdf::PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(getFact(result, QStringLiteral("items")), QStringLiteral("2 converted"));
    QVERIFY(getFact(result, QStringLiteral("notConverted")).isEmpty());
    QVERIFY(result.data.value(QStringLiteral("notConverted")).toArray().isEmpty());
    QCOMPARE(getFact(result, QStringLiteral("renderingIssues")), QStringLiteral("page 2 – check this page"));
    QCOMPARE(result.data.value(QStringLiteral("renderingIssuePages")).toArray(), QJsonArray({ 2 }));
    QVERIFY(!result.renderingIssues.empty());
    QCOMPARE(result.renderingIssues.front().pageIndex, pdf::PDFInteger(1));

    // The damaged page has its black and white image with the drawn rectangle
    const QImage page = decodePageImage(result.documents.front().document.data(), 1);
    QVERIFY(!page.isNull());
    QVERIFY(countBlack(page) > 0);
}

void DocumentToolBitonalTest::tooLargeImageIsNotDecoded()
{
    // Plan 6 of step 4 (hostile input): an image, which declares more decoded pixels than 2 GiB of
    // 32 bit pixels (23 200 x 23 200 > 2^29 pixels) with a stream of a few bytes, is never decoded:
    // it stays as it is with the reason "too large"; the other image is converted
    pdf::PDFDocumentBuilder builder;
    pdf::PDFDictionaryBuilder dictionary;
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("XObject"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Subtype"), pdf::PDFObject::createName("Image"));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Width"), pdf::PDFObject::createInteger(23200));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("Height"), pdf::PDFObject::createInteger(23200));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("BitsPerComponent"), pdf::PDFObject::createInteger(8));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString("ColorSpace"), pdf::PDFObject::createName("DeviceGray"));
    QByteArray samples(16, char(0));
    dictionary.addEntry(pdf::PDFInplaceOrMemoryString(pdf::PDF_STREAM_DICT_LENGTH), pdf::PDFObject::createInteger(samples.size()));
    const pdf::PDFObjectReference huge = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(samples))));

    Page page;
    page.images = { { "Im1", addImage(builder, Qt::black, Qt::white) }, { "Im2", huge } };
    page.content = "q 80 0 0 20 10 10 cm /Im1 Do Q q 80 0 0 20 100 10 cm /Im2 Do Q";
    const pdf::PDFDocumentPointer document = createDocument(builder, { page });

    const pdf::PDFBitonalDocumentCreator creator(document.data(), nullptr, nullptr);
    QVERIFY(creator.isImageTooLarge(huge));
    QVERIFY(creator.getDecodedImage(huge, nullptr).isNull());

    QVariantMap parameters;
    parameters.insert(QStringLiteral("source"), QStringLiteral("images"));
    const pdf::PDFDocumentToolResult result = run(document, parameters);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(getFact(result, QStringLiteral("notConverted")), QStringLiteral("image 2 (too large)"));
    QCOMPARE(getFact(result, QStringLiteral("items")), QStringLiteral("1 converted · 1 not converted"));

    // The image keeps its stream byte by byte (the image of /Im2 of the page)
    QCOMPARE(result.documents.size(), size_t(1));
    const pdf::PDFDocument* converted = result.documents.front().document.data();
    const pdf::PDFDictionary* resources = converted->getDictionaryFromObject(converted->getCatalog()->getPage(0)->getResources());
    const pdf::PDFDictionary* xobjects = resources ? converted->getDictionaryFromObject(resources->get("XObject")) : nullptr;
    QVERIFY(xobjects);
    const pdf::PDFObject& kept = converted->getObject(xobjects->get("Im2"));
    QVERIFY(kept.isStream());
    QCOMPARE(*kept.getStream()->getContent(), QByteArray(16, char(0)));
    QCOMPARE(pdf::PDFDocumentDataLoaderDecorator(converted).readIntegerFromDictionary(kept.getStream()->getDictionary(), "Width", 0), pdf::PDFInteger(23200));
}

void DocumentToolBitonalTest::permission()
{
    // B79, ISO 32000-2 Table 22: the tool changes the content - bit 4 (Modify)
    pdf::PDFDocumentToolBitonal tool;
    const std::vector<pdf::PDFSecurityHandler::Permission> permissions = tool.getInfo()->getRequiredPermissions();
    QVERIFY(std::find(permissions.cbegin(), permissions.cend(), pdf::PDFSecurityHandler::Permission::Modify) != permissions.cend());
}

QTEST_MAIN(DocumentToolBitonalTest)

#include "tst_documenttoolbitonaltest.moc"
