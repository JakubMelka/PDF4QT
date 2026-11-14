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

// The one collection of the images of step 4 of phase 2 (PDFDocumentImageList, plan 4.1 with
// R4-3, R4-4, R4-5, R4-8, R4-11; the row "Sběr obrázků" of plan 8): a shared image is one item
// with its pages, the key of an inline image (also in a form drawn on two pages), a cycle of forms
// and a form shared by two paths, an image only in /Resources or only in /AP is no item, a mask is
// no item, the flag of a hidden layer, the color by the declared space (ISO 32000-2, 8.6, 8.9.5)
// and Settings::onlyColorImages of the optimizer by the same rule. The step 4 packages extend it.

#include "pdfbitonaldocumentcreator.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentimagelist.h"
#include "pdfimageoptimizer.h"
#include "pdfparser.h"

#include <QtTest>

#include <vector>

/// A document built from objects written as text
class ImageDocument
{
public:
    /// An object of its text (references "12 0 R" are references)
    static pdf::PDFObject parse(const QByteArray& text)
    {
        pdf::PDFParser parser(text, nullptr, pdf::PDFParser::None);
        return parser.getObject();
    }

    /// A stream of the dictionary and the data; \p reference replaces an object added before
    pdf::PDFObjectReference addStream(const QByteArray& dictionaryText, QByteArray data, pdf::PDFObjectReference reference = pdf::PDFObjectReference())
    {
        const pdf::PDFObject dictionaryObject = parse(dictionaryText);
        pdf::PDFDictionaryBuilder dictionary(*dictionaryObject.getDictionary());
        dictionary.setEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(data.size()));
        pdf::PDFObject stream = pdf::PDFObject::createStream(pdf::PDFStream(std::move(dictionary), std::move(data)));
        if (reference.isValid())
        {
            builder.setObject(reference, std::move(stream));
            return reference;
        }
        return builder.addObject(std::move(stream));
    }

    /// An image XObject of 2 x 2 pixels of the declaration (no filter)
    pdf::PDFObjectReference addImage(const QByteArray& colorSpace = "/DeviceRGB", int bitsPerComponent = 8, const QByteArray& extra = QByteArray())
    {
        return addStream("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace " + colorSpace + " /BitsPerComponent " + QByteArray::number(bitsPerComponent) + " " + extra + " >>", QByteArray(12, 'x'));
    }

    /// A page with the resources and the content
    pdf::PDFObjectReference addPage(const QByteArray& resources, const QByteArray& content, const QByteArray& extra = QByteArray())
    {
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 200, 200));
        const pdf::PDFObjectReference contents = addStream("<< >>", content);
        builder.mergeTo(page, parse("<< /Resources " + resources + " /Contents " + ref(contents) + " " + extra + " >>"));
        return page;
    }

    static QByteArray ref(pdf::PDFObjectReference reference)
    {
        return QByteArray::number(reference.objectNumber) + " " + QByteArray::number(reference.generation) + " R";
    }

    static QString key(pdf::PDFObjectReference reference)
    {
        return pdf::PDFDocumentImageList::Key::createReference(reference).toString();
    }

    pdf::PDFDocument build() { return builder.build(); }

    pdf::PDFDocumentBuilder builder;
};

class DocumentImageListTest : public QObject
{
    Q_OBJECT

private slots:
    void keyText();
    void sharedImageIsOneItem();
    void inlineImageKeys();
    void formCycleAndSharedForm();
    void resourcesAndAppearancesAreNoItems();
    void maskIsNoItem();
    void hiddenLayer();
    void colorByDeclaredSpace_data();
    void colorByDeclaredSpace();
    void optimizerSkipsImagesWithoutColor();
    void colorSpaceOfResourceName();
    void imageSharedWithAppearanceIsKept();

private:
    static pdf::PDFDocumentImageList collect(const pdf::PDFDocument& document, const pdf::PDFDocumentImageList::Options& options = pdf::PDFDocumentImageList::Options::createForChange())
    {
        return pdf::PDFDocumentImageList::create(&document, options, nullptr, nullptr);
    }

    static std::vector<pdf::PDFInteger> pages(std::initializer_list<pdf::PDFInteger> list) { return std::vector<pdf::PDFInteger>(list); }
};

void DocumentImageListTest::keyText()
{
    // R4-8: "148 0" and "inline:<obj> <gen>:<n>", the same text in and out
    using Key = pdf::PDFDocumentImageList::Key;
    QCOMPARE(Key::createReference(pdf::PDFObjectReference(148, 0)).toString(), QStringLiteral("148 0"));
    QCOMPARE(Key::createInline(pdf::PDFObjectReference(12, 0), 3).toString(), QStringLiteral("inline:12 0:3"));
    QCOMPARE(Key::fromString(u"148 0"), std::optional<Key>(Key::createReference(pdf::PDFObjectReference(148, 0))));
    QCOMPARE(Key::fromString(u" inline:12 0:3 "), std::optional<Key>(Key::createInline(pdf::PDFObjectReference(12, 0), 3)));
    for (const QString& invalid : { QStringLiteral("148"), QStringLiteral("0 0"), QStringLiteral("-1 0"), QStringLiteral("148 0 R"), QStringLiteral("inline:12 0"),
                                    QStringLiteral("inline:12 0:-1"), QStringLiteral("99999999999 0"), QString() })
    {
        QVERIFY2(!Key::fromString(invalid), qPrintable(invalid));
    }

    // The stream operators (the meta type of a signal with std::vector<Key>) write the id text and
    // read it back; a text, which is no key, reads an invalid key
    QByteArray data;
    {
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << Key::createReference(pdf::PDFObjectReference(148, 0)) << Key::createInline(pdf::PDFObjectReference(12, 0), 3) << QStringLiteral("148 0 R");
    }
    {
        QDataStream stream(&data, QIODevice::ReadOnly);
        QString text;
        stream >> text;
        QCOMPARE(text, QStringLiteral("148 0"));
    }
    QDataStream stream(&data, QIODevice::ReadOnly);
    Key reference;
    Key inlineKey;
    Key invalid = Key::createReference(pdf::PDFObjectReference(1, 0));
    stream >> reference >> inlineKey >> invalid;
    QCOMPARE(reference, Key::createReference(pdf::PDFObjectReference(148, 0)));
    QCOMPARE(inlineKey, Key::createInline(pdf::PDFObjectReference(12, 0), 3));
    QVERIFY(!invalid.isValid());
    QCOMPARE(stream.status(), QDataStream::Ok);
}

void DocumentImageListTest::sharedImageIsOneItem()
{
    // 7.3.10: one image object on two pages is one item with both pages; the resolution is the
    // smallest of its drawings (2 pixels over 100 pt = 1.44 dpi, over 50 pt = 2.88 dpi)
    ImageDocument builder;
    const pdf::PDFObjectReference image = builder.addImage();
    const QByteArray resources = "<< /XObject << /Im1 " + ImageDocument::ref(image) + " >> >>";
    builder.addPage(resources, "q 50 0 0 50 0 0 cm /Im1 Do Q");
    builder.addPage(resources, "q 100 0 0 100 0 0 cm /Im1 Do Q");
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFDocumentImageList list = collect(document);
    QVERIFY(list.isComplete());
    QCOMPARE(list.getItems().size(), size_t(1));
    const pdf::PDFDocumentImageList::Item& item = list.getItems().front();
    QCOMPARE(item.key.toString(), ImageDocument::key(image));
    QCOMPARE(item.kind, pdf::PDFDocumentImageList::Kind::XObject);
    QCOMPARE(item.pageIndices, pages({ 0, 1 }));
    QCOMPARE(item.pixelSize, QSize(2, 2));
    QCOMPARE(item.colorSpaceName, QStringLiteral("DeviceRGB"));
    QCOMPARE(item.streamBytes, qint64(12));
    QVERIFY(qAbs(item.minimalDpi.x() - 2.0 / (100.0 / 72.0)) < 1e-6);
}

void DocumentImageListTest::inlineImageKeys()
{
    // R4-8: an inline image is the content stream (the page, or the form) and the index of its BI;
    // a form drawn on two pages gives its inline image one key with both pages. The first page has
    // its own inline image before the form: the form counts its BI in its own content.
    ImageDocument builder;
    const pdf::PDFObjectReference form = builder.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] >>", "q 10 0 0 10 0 0 cm BI /W 2 /H 2 /CS /G /BPC 8 ID abcd EI Q");
    const QByteArray resources = "<< /XObject << /F1 " + ImageDocument::ref(form) + " >> >>";
    const pdf::PDFObjectReference first = builder.addPage(resources, "q 20 0 0 20 0 0 cm BI /W 2 /H 2 /CS /RGB /BPC 8 ID abcdefghijkl EI Q /F1 Do");
    builder.addPage(resources, "/F1 Do");
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFDocumentImageList list = collect(document);
    QCOMPARE(list.getItems().size(), size_t(2));

    const pdf::PDFDocumentImageList::Item& pageInline = list.getItems()[0];
    QCOMPARE(pageInline.key.toString(), QStringLiteral("inline:%1 0:0").arg(first.objectNumber));
    QCOMPARE(pageInline.kind, pdf::PDFDocumentImageList::Kind::Inline);
    QVERIFY(pageInline.isKeptByChangingTools());
    QCOMPARE(pageInline.pageIndices, pages({ 0 }));
    QCOMPARE(pageInline.color, pdf::PDFDocumentImageList::ImageColor::Color);

    const pdf::PDFDocumentImageList::Item& formInline = list.getItems()[1];
    QCOMPARE(formInline.key.toString(), QStringLiteral("inline:%1 0:0").arg(form.objectNumber));
    QCOMPARE(formInline.pageIndices, pages({ 0, 1 }));
    QCOMPARE(formInline.color, pdf::PDFDocumentImageList::ImageColor::Gray);
    QCOMPARE(list.indexOf(formInline.key), 1);
}

void DocumentImageListTest::formCycleAndSharedForm()
{
    // A form, which draws itself (a cycle), and a form drawn by the page directly and through
    // another form (a node shared by two paths): the walk ends, the image is one item
    ImageDocument builder;
    const pdf::PDFObjectReference image = builder.addImage();
    const pdf::PDFObjectReference shared = builder.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] /Resources << /XObject << /Im1 " + ImageDocument::ref(image) + " >> >> >>", "/Im1 Do");
    const pdf::PDFObjectReference cycle = builder.addStream("<< >>", QByteArray());
    builder.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 100 100] /Resources << /XObject << /Self " + ImageDocument::ref(cycle) + " /Shared " + ImageDocument::ref(shared) + " >> >> >>",
                      "/Shared Do /Self Do", cycle);
    builder.addPage("<< /XObject << /C " + ImageDocument::ref(cycle) + " /S " + ImageDocument::ref(shared) + " >> >>", "/S Do /C Do");
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFDocumentImageList list = collect(document);
    QVERIFY(list.isComplete());
    QCOMPARE(list.getItems().size(), size_t(1));
    QCOMPARE(list.getItems().front().key.toString(), ImageDocument::key(image));
    QCOMPARE(list.getItems().front().pageIndices, pages({ 0 }));
}

void DocumentImageListTest::resourcesAndAppearancesAreNoItems()
{
    // B61: an image only in /Resources is not drawn; R4-4: an image only in the appearance of an
    // annotation is changed by no tool, it is no item
    ImageDocument builder;
    const pdf::PDFObjectReference drawn = builder.addImage();
    const pdf::PDFObjectReference unused = builder.addImage();
    const pdf::PDFObjectReference stampImage = builder.addImage();
    const pdf::PDFObjectReference appearance = builder.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 50 50] /Resources << /XObject << /Im3 " + ImageDocument::ref(stampImage) + " >> >> >>", "q 50 0 0 50 0 0 cm /Im3 Do Q");
    const pdf::PDFObjectReference annotation = builder.builder.addObject(ImageDocument::parse("<< /Type /Annot /Subtype /Stamp /Rect [0 0 50 50] /AP << /N " + ImageDocument::ref(appearance) + " >> >>"));
    builder.addPage("<< /XObject << /Im1 " + ImageDocument::ref(drawn) + " /Im2 " + ImageDocument::ref(unused) + " >> >>", "/Im1 Do", "/Annots [" + ImageDocument::ref(annotation) + "]");
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFDocumentImageList list = collect(document);
    QCOMPARE(list.getItems().size(), size_t(1));
    QCOMPARE(list.getItems().front().key.toString(), ImageDocument::key(drawn));
}

void DocumentImageListTest::maskIsNoItem()
{
    // 8.9.6: a soft mask of another image is no item; an image drawn and also used as a mask of
    // another one is an item, which says so (B62)
    ImageDocument builder;
    const pdf::PDFObjectReference mask = builder.addImage("/DeviceGray");
    const pdf::PDFObjectReference image = builder.addImage("/DeviceRGB", 8, "/SMask " + ImageDocument::ref(mask));
    const pdf::PDFObjectReference sharedMask = builder.addImage("/DeviceGray");
    const pdf::PDFObjectReference second = builder.addImage("/DeviceRGB", 8, "/SMask " + ImageDocument::ref(sharedMask));
    builder.addPage("<< /XObject << /Im1 " + ImageDocument::ref(image) + " /Im2 " + ImageDocument::ref(second) + " /M2 " + ImageDocument::ref(sharedMask) + " >> >>", "/Im1 Do /Im2 Do /M2 Do");
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFDocumentImageList list = collect(document);
    QCOMPARE(list.getItems().size(), size_t(3));
    const pdf::PDFDocumentImageList::Item* first = list.findItem(pdf::PDFDocumentImageList::Key::createReference(image));
    QVERIFY(first);
    QCOMPARE(first->softMaskReference, mask);
    QCOMPARE(first->maskBytes, qint64(12));
    QVERIFY(!list.findItem(pdf::PDFDocumentImageList::Key::createReference(mask)));

    const pdf::PDFDocumentImageList::Item* drawnMask = list.findItem(pdf::PDFDocumentImageList::Key::createReference(sharedMask));
    QVERIFY(drawnMask);
    QVERIFY(drawnMask->isUsedAsMask);
    QVERIFY(!first->isUsedAsMask);
}

void DocumentImageListTest::hiddenLayer()
{
    // R4-4: the changing tools see the images of every layer (no flag); R4-11: an export flags an
    // image drawn only in a layer hidden by the configuration /D or by the states of the tab
    ImageDocument builder;
    const pdf::PDFObjectReference layer = builder.builder.addObject(ImageDocument::parse("<< /Type /OCG /Name (Hidden) >>"));
    builder.builder.mergeTo(builder.builder.getCatalogReference(), ImageDocument::parse("<< /OCProperties << /OCGs [" + ImageDocument::ref(layer) + "] /D << /OFF [" + ImageDocument::ref(layer) + "] >> >> >>"));
    const pdf::PDFObjectReference visible = builder.addImage();
    const pdf::PDFObjectReference hidden = builder.addImage();
    builder.addPage("<< /XObject << /Im1 " + ImageDocument::ref(visible) + " /Im2 " + ImageDocument::ref(hidden) + " >> /Properties << /oc1 " + ImageDocument::ref(layer) + " >> >>",
                    "/Im1 Do /OC /oc1 BDC /Im2 Do EMC");
    const pdf::PDFDocument document = builder.build();

    using Options = pdf::PDFDocumentImageList::Options;
    const pdf::PDFDocumentImageList change = collect(document, Options::createForChange());
    QCOMPARE(change.getItems().size(), size_t(2));
    QVERIFY(!change.getItems()[1].isInHiddenLayer);

    const pdf::PDFDocumentImageList exported = collect(document, Options::createForExport());
    QCOMPARE(exported.getItems().size(), size_t(2));
    QCOMPARE(exported.getItems()[0].key.toString(), ImageDocument::key(visible));
    QVERIFY(!exported.getItems()[0].isInHiddenLayer);
    QCOMPARE(exported.getItems()[1].key.toString(), ImageDocument::key(hidden));
    QVERIFY(exported.getItems()[1].isInHiddenLayer);

    // The tab shows the layer: nothing is hidden
    const pdf::PDFDocumentImageList shown = collect(document, Options::createForExport({ { layer, pdf::OCState::ON } }));
    QCOMPARE(shown.getItems().size(), size_t(2));
    QVERIFY(!shown.getItems()[1].isInHiddenLayer);
}

void DocumentImageListTest::colorByDeclaredSpace_data()
{
    QTest::addColumn<QByteArray>("colorSpace");
    QTest::addColumn<int>("bitsPerComponent");
    QTest::addColumn<QByteArray>("extra");
    QTest::addColumn<int>("expected");

    using Color = pdf::PDFDocumentImageList::ImageColor;
    QTest::newRow("DeviceGray") << QByteArray("/DeviceGray") << 8 << QByteArray() << int(Color::Gray);
    QTest::newRow("CalGray") << QByteArray("[/CalGray << /WhitePoint [0.9505 1 1.089] >>]") << 8 << QByteArray() << int(Color::Gray);
    QTest::newRow("Indexed over DeviceGray") << QByteArray("[/Indexed /DeviceGray 1 <00FF>]") << 8 << QByteArray() << int(Color::Gray);
    QTest::newRow("Indexed over DeviceRGB") << QByteArray("[/Indexed /DeviceRGB 1 <000000FFFFFF>]") << 8 << QByteArray() << int(Color::Color);
    QTest::newRow("DeviceRGB") << QByteArray("/DeviceRGB") << 8 << QByteArray() << int(Color::Color);
    QTest::newRow("DeviceCMYK") << QByteArray("/DeviceCMYK") << 8 << QByteArray() << int(Color::Color);
    QTest::newRow("Lab") << QByteArray("[/Lab << /WhitePoint [0.9505 1 1.089] >>]") << 8 << QByteArray() << int(Color::Color);
    QTest::newRow("Separation") << QByteArray("[/Separation /Black /DeviceGray << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>]") << 8 << QByteArray() << int(Color::Color);
    QTest::newRow("ImageMask") << QByteArray("/DeviceGray") << 1 << QByteArray("/ImageMask true") << int(Color::Bitonal);

    // Owner 14. 11.: 1 bit is bitonal only in a gray space of one component or in a palette, whose
    // two used entries (0 and 1, 8.6.6.3) are gray; 1 bit of DeviceRGB has 8 colors (8.9.5)
    QTest::newRow("1 bit DeviceGray") << QByteArray("/DeviceGray") << 1 << QByteArray() << int(Color::Bitonal);
    QTest::newRow("1 bit CalGray") << QByteArray("[/CalGray << /WhitePoint [0.9505 1 1.089] >>]") << 1 << QByteArray() << int(Color::Bitonal);
    QTest::newRow("1 bit DeviceRGB") << QByteArray("/DeviceRGB") << 1 << QByteArray() << int(Color::Color);
    QTest::newRow("1 bit Separation") << QByteArray("[/Separation /Black /DeviceGray << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>]") << 1 << QByteArray() << int(Color::Color);
    QTest::newRow("1 bit Indexed over DeviceGray") << QByteArray("[/Indexed /DeviceGray 1 <00FF>]") << 1 << QByteArray() << int(Color::Bitonal);
    QTest::newRow("1 bit Indexed black and white RGB") << QByteArray("[/Indexed /DeviceRGB 1 <000000FFFFFF>]") << 1 << QByteArray() << int(Color::Bitonal);
    QTest::newRow("1 bit Indexed red and blue RGB") << QByteArray("[/Indexed /DeviceRGB 1 <FF00000000FF>]") << 1 << QByteArray() << int(Color::Color);
    QTest::newRow("1 bit Indexed gray, then red") << QByteArray("[/Indexed /DeviceRGB 1 <808080FF0000>]") << 1 << QByteArray() << int(Color::Color);
    QTest::newRow("1 bit Indexed RGB, entries 2+ unused") << QByteArray("[/Indexed /DeviceRGB 2 <000000FFFFFFFF0000>]") << 1 << QByteArray() << int(Color::Bitonal);
    QTest::newRow("1 bit Indexed one entry") << QByteArray("[/Indexed /DeviceRGB 0 <404040>]") << 1 << QByteArray() << int(Color::Bitonal);
    QTest::newRow("1 bit Indexed CMYK black") << QByteArray("[/Indexed /DeviceCMYK 1 <00000000000000FF>]") << 1 << QByteArray() << int(Color::Bitonal);
    QTest::newRow("1 bit Indexed CMYK cyan") << QByteArray("[/Indexed /DeviceCMYK 1 <00000000FF000000>]") << 1 << QByteArray() << int(Color::Color);
    QTest::newRow("1 bit Indexed short palette") << QByteArray("[/Indexed /DeviceRGB 1 <000000>]") << 1 << QByteArray() << int(Color::Color);
    QTest::newRow("1 bit Indexed over Lab") << QByteArray("[/Indexed [/Lab << /WhitePoint [0.9505 1 1.089] >>] 1 <000000FF8080>]") << 1 << QByteArray() << int(Color::Color);
}

void DocumentImageListTest::colorByDeclaredSpace()
{
    // R4-5: the color of an image is the color of its declaration (8.6, 8.9.5), never its pixels
    QFETCH(QByteArray, colorSpace);
    QFETCH(int, bitsPerComponent);
    QFETCH(QByteArray, extra);
    QFETCH(int, expected);

    ImageDocument builder;
    const pdf::PDFObjectReference image = builder.addImage(colorSpace, bitsPerComponent, extra);
    builder.addPage("<< /XObject << /Im1 " + ImageDocument::ref(image) + " >> >>", "/Im1 Do");
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFDocumentImageList list = collect(document);
    QCOMPARE(list.getItems().size(), size_t(1));
    QCOMPARE(int(list.getItems().front().color), expected);

    const pdf::PDFDictionary* dictionary = document.getObjectByReference(image).getStream()->getDictionary();
    QCOMPARE(pdf::PDFDocumentImageList::isColorImage(&document, dictionary), expected == int(pdf::PDFDocumentImageList::ImageColor::Color));
}

void DocumentImageListTest::colorSpaceOfResourceName()
{
    // An image XObject, which names a color space of the resources of its page (files do it, Table 87
    // names a family): the content processor resolves the name in the resources, so does the list -
    // the image is gray, also for Grayscale (R4-5), and the walk of all layers decodes it (P1 of step 4)
    ImageDocument builder;
    const pdf::PDFObjectReference image = builder.addImage("/CS0", 8);
    builder.addPage("<< /XObject << /Im1 " + ImageDocument::ref(image) + " >> /ColorSpace << /CS0 /DeviceGray >> >>", "q 50 0 0 50 0 0 cm /Im1 Do Q");
    const pdf::PDFDocument document = builder.build();

    const pdf::PDFDocumentImageList list = collect(document);
    QCOMPARE(list.getItems().size(), size_t(1));
    QCOMPARE(list.getItems().front().colorSpaceName, QStringLiteral("DeviceGray"));
    QCOMPARE(list.getItems().front().color, pdf::PDFDocumentImageList::ImageColor::Gray);

    // RS4CORE: Grayscale (onlyColorImages) reads the same resolved space as the list - the gray
    // image is skipped before decoding, its stream stays byte by byte
    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = false;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Grayscale;
    settings.keepOriginalIfLarger = false;
    settings.onlyColorImages = true;
    std::vector<pdf::PDFImageOptimizer::ImageResult> results;
    const pdf::PDFDocument optimized = pdf::PDFImageOptimizer().optimize(&document, settings, pdf::PDFImageOptimizer::ImageOverrides(), nullptr, nullptr, &results);
    QCOMPARE(results.size(), size_t(1));
    QCOMPARE(results.front().reference, image);
    QCOMPARE(results.front().skipReason, pdf::PDFImageOptimizer::SkipReason::NotColorImage);
    QCOMPARE(*optimized.getObjectByReference(image).getStream()->getContent(), *document.getObjectByReference(image).getStream()->getContent());
}

void DocumentImageListTest::optimizerSkipsImagesWithoutColor()
{
    // R4-5: Grayscale (onlyColorImages) skips a gray image before decoding it - its stream stays
    // as it is - and converts the color image to DeviceGray
    ImageDocument builder;
    const pdf::PDFObjectReference color = builder.addStream("<< /Type /XObject /Subtype /Image /Width 8 /Height 8 /ColorSpace /DeviceRGB /BitsPerComponent 8 >>", QByteArray(8 * 8 * 3, char(0x50)) + QByteArray());
    const pdf::PDFObjectReference gray = builder.addStream("<< /Type /XObject /Subtype /Image /Width 8 /Height 8 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", QByteArray(8 * 8, char(0x30)));
    builder.addPage("<< /XObject << /Im1 " + ImageDocument::ref(color) + " /Im2 " + ImageDocument::ref(gray) + " >> >>", "q 100 0 0 100 0 0 cm /Im1 Do /Im2 Do Q");
    const pdf::PDFDocument document = builder.build();

    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = false;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Grayscale;
    settings.keepOriginalIfLarger = false;
    settings.onlyColorImages = true;

    std::vector<pdf::PDFImageOptimizer::ImageResult> results;
    pdf::PDFImageOptimizer optimizer;
    const pdf::PDFDocument optimized = optimizer.optimize(&document, settings, pdf::PDFImageOptimizer::ImageOverrides(), nullptr, nullptr, &results);

    QCOMPARE(results.size(), size_t(2));
    const auto findResult = [&results](pdf::PDFObjectReference reference) -> const pdf::PDFImageOptimizer::ImageResult*
    {
        for (const pdf::PDFImageOptimizer::ImageResult& result : results)
        {
            if (result.reference == reference)
            {
                return &result;
            }
        }
        return nullptr;
    };

    const pdf::PDFImageOptimizer::ImageResult* grayResult = findResult(gray);
    QVERIFY(grayResult);
    QCOMPARE(grayResult->skipReason, pdf::PDFImageOptimizer::SkipReason::NotColorImage);
    QVERIFY(grayResult->keptOriginal);
    QCOMPARE(*optimized.getObjectByReference(gray).getStream()->getContent(), *document.getObjectByReference(gray).getStream()->getContent());

    const pdf::PDFImageOptimizer::ImageResult* colorResult = findResult(color);
    QVERIFY(colorResult);
    QCOMPARE(colorResult->skipReason, pdf::PDFImageOptimizer::SkipReason::None);
    QVERIFY(!colorResult->keptOriginal);
    const pdf::PDFDictionary* converted = optimized.getObjectByReference(color).getStream()->getDictionary();
    QCOMPARE(optimized.getObject(converted->get("ColorSpace")).getString(), QByteArray("DeviceGray"));
}

void DocumentImageListTest::imageSharedWithAppearanceIsKept()
{
    // R4-4: the appearances of the annotations (12.5.5) are never changed. The page draws the
    // images "shared", "gstate" and "page"; "shared" is drawn by the appearance too (its form F1),
    // "gstate" is the group of the soft mask of a graphics state of F2 (11.6.5.2, Table 142),
    // "only" is drawn by F2 alone. F1 and F2 name each other (a cycle) and F2 is the state /Off
    // of the second annotation too (a node shared by two paths).
    ImageDocument builder;
    const pdf::PDFObjectReference shared = builder.addImage();
    const pdf::PDFObjectReference gstate = builder.addImage();
    const pdf::PDFObjectReference page = builder.addImage();
    const pdf::PDFObjectReference only = builder.addImage();
    const pdf::PDFObjectReference sharedMask = builder.addStream("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceGray /BitsPerComponent 8 >>", QByteArray(4, '\xFF'));
    builder.addStream("<< /Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8 /SMask " + ImageDocument::ref(sharedMask) + " >>", QByteArray(12, 'x'), shared);

    const pdf::PDFObjectReference form1 = builder.addStream("<< >>", QByteArray());
    const pdf::PDFObjectReference group = builder.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 1 1] /Resources << /XObject << /G1 " + ImageDocument::ref(gstate) + " >> >> >>", "/G1 Do");
    const pdf::PDFObjectReference form2 = builder.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 50 50] /Resources << /XObject << /F1 " + ImageDocument::ref(form1) + " /Only " + ImageDocument::ref(only) +
                                                            " >> /ExtGState << /GS1 << /SMask << /S /Luminosity /G " + ImageDocument::ref(group) + " >> >> >> >> >>",
                                                            "/GS1 gs /Only Do /F1 Do");
    builder.addStream("<< /Type /XObject /Subtype /Form /BBox [0 0 50 50] /Resources << /XObject << /Sh " + ImageDocument::ref(shared) + " /F2 " + ImageDocument::ref(form2) + " >> >> >>", "/Sh Do /F2 Do", form1);

    const pdf::PDFObjectReference first = builder.builder.addObject(ImageDocument::parse("<< /Type /Annot /Subtype /Stamp /Rect [0 0 50 50] /AP << /N " + ImageDocument::ref(form1) + " >> >>"));
    const pdf::PDFObjectReference second = builder.builder.addObject(ImageDocument::parse("<< /Type /Annot /Subtype /Widget /Rect [0 0 50 50] /AS /Off /AP << /N << /On " + ImageDocument::ref(form1) + " /Off " + ImageDocument::ref(form2) + " >> /D << /Off " +
                                                                                           ImageDocument::ref(form2) + " >> >> >>"));
    builder.addPage("<< /XObject << /Im1 " + ImageDocument::ref(shared) + " /Im2 " + ImageDocument::ref(gstate) + " /Im3 " + ImageDocument::ref(page) + " >> >>",
                    "q 100 0 0 100 0 0 cm /Im1 Do /Im2 Do /Im3 Do Q", "/Annots [" + ImageDocument::ref(first) + " " + ImageDocument::ref(second) + "]");
    const pdf::PDFDocument document = builder.build();

    QCOMPARE(pdf::PDFDocumentImageList::collectAppearanceImages(&document), std::set<pdf::PDFObjectReference>({ shared, sharedMask, gstate, only }));

    const pdf::PDFDocumentImageList list = collect(document);
    QCOMPARE(list.getItems().size(), size_t(3));
    for (const pdf::PDFDocumentImageList::Item& item : list.getItems())
    {
        const bool isInAppearance = item.key.reference != page;
        QCOMPARE(item.isInAppearance, isInAppearance);
        QCOMPARE(item.isKeptByChangingTools(), isInAppearance);
        QCOMPARE(pdf::PDFBitonalDocumentCreator::isConvertibleImage(item), !isInAppearance);
    }

    // Grayscale keeps the images of the appearance byte by byte and converts the image of the page
    pdf::PDFImageOptimizer::Settings settings = pdf::PDFImageOptimizer::Settings::createDefault();
    settings.enabled = true;
    settings.autoMode = false;
    settings.colorMode = pdf::PDFImageOptimizer::ColorMode::Grayscale;
    settings.keepOriginalIfLarger = false;
    settings.onlyColorImages = true;
    std::vector<pdf::PDFImageOptimizer::ImageResult> results;
    const pdf::PDFDocument optimized = pdf::PDFImageOptimizer().optimize(&document, settings, pdf::PDFImageOptimizer::ImageOverrides(), nullptr, nullptr, &results);
    QCOMPARE(results.size(), size_t(3));
    for (const pdf::PDFImageOptimizer::ImageResult& result : results)
    {
        if (result.reference == page)
        {
            QCOMPARE(result.skipReason, pdf::PDFImageOptimizer::SkipReason::None);
            QVERIFY(!result.keptOriginal);
            QCOMPARE(optimized.getObject(optimized.getObjectByReference(page).getStream()->getDictionary()->get("ColorSpace")).getString(), QByteArray("DeviceGray"));
        }
        else
        {
            QCOMPARE(result.skipReason, pdf::PDFImageOptimizer::SkipReason::InAppearance);
            QVERIFY(result.keptOriginal);
            QCOMPARE(*optimized.getObjectByReference(result.reference).getStream()->getContent(), *document.getObjectByReference(result.reference).getStream()->getContent());
            QCOMPARE(optimized.getObjectByReference(result.reference).getStream()->getDictionary()->get("ColorSpace"), document.getObjectByReference(result.reference).getStream()->getDictionary()->get("ColorSpace"));
        }
    }
}

QTEST_MAIN(DocumentImageListTest)

#include "tst_documentimagelisttest.moc"
