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

// Sign by Hand of the core and the drawing of the pages (step 6 of phase 2, plan 4.2, 5.5, 9; package
// P2): the skeleton tests the fixed interface (plan 10.3) - the id, the parameters, the value of the
// drawing. Package P2 adds the runs and the JSON of the elements.

#include "pdfdocumenttool_signbyhand.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfpagedrawing.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfdocmdppermissions.h"
#include "pdfcatalog.h"

#include "mergetestpdf.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class DocumentToolSignByHandTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void interface();
    void drawingValue();

    // Package P2 (plan 4.2, 5.5, 7.2, 9)
    void boundingBox();
    void validity();
    void jsonRoundTrip();
    void jsonStrict_data();
    void jsonStrict();
    void svgSafety();
    void appendedAfterContent_data();
    void appendedAfterContent();
    void pageWithoutMediaBox();
    void formXObject();
    void nestedPageTree();
    void givenParameters();
    void runOfTool();
    void certifiedDocument();
    void elementsLimitFitsPointCount();

private:
    using Drawing = pdf::PDFPageDrawing;

    static Drawing::Element createRectangle(pdf::PDFInteger pageIndex, QRectF rectangle, double penWidth);
    static QByteArray write(const pdf::PDFDocument& document);
    static pdf::PDFDocument read(const QByteArray& data);

    /// The decoded streams of /Contents of the page in their order
    static QList<QByteArray> getContentStreams(const pdf::PDFDocument& document, size_t pageIndex);
};

pdf::PDFPageDrawing::Element DocumentToolSignByHandTest::createRectangle(pdf::PDFInteger pageIndex, QRectF rectangle, double penWidth)
{
    Drawing::Element element;
    element.kind = Drawing::Kind::Rectangle;
    element.pageIndex = pageIndex;
    element.rectangle = rectangle;
    element.style.penWidth = penWidth;
    return element;
}

QByteArray DocumentToolSignByHandTest::write(const pdf::PDFDocument& document)
{
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !pdf::PDFDocumentWriter(nullptr).write(&buffer, &document))
    {
        return QByteArray();
    }
    return data;
}

pdf::PDFDocument DocumentToolSignByHandTest::read(const QByteArray& data)
{
    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    return reader.readFromBuffer(data);
}

QList<QByteArray> DocumentToolSignByHandTest::getContentStreams(const pdf::PDFDocument& document, size_t pageIndex)
{
    QList<QByteArray> streams;
    const pdf::PDFObject contents = document.getObject(document.getCatalog()->getPage(pageIndex)->getContents());
    if (contents.isStream())
    {
        streams << document.getDecodedStream(contents.getStream());
    }
    else if (contents.isArray())
    {
        for (const pdf::PDFObject& item : *contents.getArray())
        {
            const pdf::PDFObject stream = document.getObject(item);
            streams << (stream.isStream() ? document.getDecodedStream(stream.getStream()) : QByteArray());
        }
    }
    return streams;
}

void DocumentToolSignByHandTest::interface()
{
    const pdf::PDFDocumentToolSignByHand tool;
    QCOMPARE(tool.getId(), QStringLiteral("protect.sign-by-hand"));
    QVERIFY(tool.getInfo());
    QVERIFY(pdf::PDFDocumentToolParameters::isValid(tool.getParameters()));

    // 4.2: a change of the pages of the open document (Table 257: no certification allows it)
    QVERIFY(tool.isPageChange(QVariantMap()));
    QCOMPARE(tool.getResultKind(QVariantMap()), pdf::PDFDocumentToolResultKind::ChangeDocument);

    // Q6.7, R6-20: MCP gives the elements (no images), the panels the drawing
    const QJsonObject properties = pdf::PDFDocumentToolParameters::toJsonSchema(tool.getParameters()).value(QStringLiteral("properties")).toObject();
    QVERIFY(properties.contains(QStringLiteral("elements")));
    QVERIFY(!properties.contains(QStringLiteral("drawing")));
}

void DocumentToolSignByHandTest::drawingValue()
{
    // A value of implicitly shared elements, equal by the identity of its data (RB7 of step 3b)
    using Drawing = pdf::PDFPageDrawing;
    Drawing::Element text;
    text.kind = Drawing::Kind::Text;
    text.pageIndex = 2;
    text.text = QStringLiteral("Jane Novak");
    Drawing::Element dot;
    dot.kind = Drawing::Kind::Dot;
    dot.pageIndex = 0;

    const Drawing drawing({ text, dot, text });
    const Drawing copy = drawing;
    QVERIFY(copy == drawing);
    QVERIFY(!(Drawing({ text, dot, text }) == drawing));
    QVERIFY(!drawing.isEmpty());
    QVERIFY(Drawing().isEmpty());
    QCOMPARE(drawing.getElements().size(), size_t(3));
    QCOMPARE(drawing.getPageIndices(), std::vector<pdf::PDFInteger>({ 0, 2 }));
    QCOMPARE(drawing.getFirstPageIndex(), pdf::PDFInteger(0));
    QCOMPARE(Drawing().getFirstPageIndex(), pdf::PDFInteger(-1));

    // The parameter Value: the type is registered, the variant keeps the identity
    const QVariant variant = QVariant::fromValue(drawing);
    QVERIFY(variant.canConvert<Drawing>());
    QVERIFY(variant.value<Drawing>() == drawing);
}

void DocumentToolSignByHandTest::boundingBox()
{
    // The /BBox of an appearance holds everything painted: a stroke reaches half of its width out of
    // the box or the line (ISO 32000-2, 8.5.3.1), a text box turns about its center (8.3.4)
    Drawing::Element line;
    line.kind = Drawing::Kind::Line;
    line.points = { QPointF(0, 0), QPointF(100, 0) };
    line.style.penWidth = 2.0;

    Drawing::Element text;
    text.kind = Drawing::Kind::Text;
    text.rectangle = QRectF(0, 0, 100, 20);
    text.text = QStringLiteral("Jane Novak");
    text.style.angle = 90.0;

    Drawing::Element dot;
    dot.kind = Drawing::Kind::Dot;
    dot.pageIndex = 1;
    dot.rectangle = QRectF(5, 5, 10, 10);
    dot.style.penWidth = 3.0;

    const Drawing rectangle({ createRectangle(0, QRectF(10, 20, 100, 50), 4.0) });
    QCOMPARE(rectangle.getBoundingBox(0), QRectF(8, 18, 104, 54));
    QCOMPARE(Drawing({ line }).getBoundingBox(0), QRectF(-1, -1, 102, 2));

    const QRectF turned = Drawing({ text }).getBoundingBox(0);
    QVERIFY(qAbs(turned.left() - 40.0) < 1e-9 && qAbs(turned.top() + 40.0) < 1e-9);
    QVERIFY(qAbs(turned.width() - 20.0) < 1e-9 && qAbs(turned.height() - 100.0) < 1e-9);

    // A dot is filled, not stroked; another page has its own box; a page without elements none
    const Drawing both({ createRectangle(0, QRectF(10, 20, 100, 50), 4.0), dot });
    QCOMPARE(both.getBoundingBox(1), QRectF(5, 5, 10, 10));
    QCOMPARE(both.getBoundingBox(0), QRectF(8, 18, 104, 54));
    QVERIFY(both.getBoundingBox(2).isEmpty());
}

void DocumentToolSignByHandTest::validity()
{
    // Plan 7.2: the limits of a drawing, its elements on the pages of the document, finite numbers
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    builder.appendPage(QRectF(0, 0, 300, 400));
    const pdf::PDFDocument document = builder.build();

    QString error;
    QVERIFY(Drawing({ createRectangle(1, QRectF(10, 10, 20, 20), 1.0) }).isValid(&document, &error));
    QVERIFY(error.isEmpty());

    QVERIFY(!Drawing({ createRectangle(2, QRectF(10, 10, 20, 20), 1.0) }).isValid(&document, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(Drawing({ createRectangle(2, QRectF(10, 10, 20, 20), 1.0) }).isValid(nullptr, &error));

    QVERIFY(!Drawing({ createRectangle(0, QRectF(qQNaN(), 10, 20, 20), 1.0) }).isValid(&document, &error));
    QVERIFY(!Drawing({ createRectangle(0, QRectF(10, 10, 0, 20), 1.0) }).isValid(&document, &error));
    QVERIFY(!Drawing({ createRectangle(0, QRectF(10, 10, 20, 20), 101.0) }).isValid(&document, &error));
    QVERIFY(Drawing({ createRectangle(0, QRectF(10, 10, 20, 20), 100.0) }).isValid(&document, &error));

    Drawing::Element text;
    text.kind = Drawing::Kind::Text;
    text.rectangle = QRectF(0, 0, 100, 20);
    text.text = QString(Drawing::MAXIMUM_TEXT_LENGTH, QLatin1Char('a'));
    QVERIFY(Drawing({ text }).isValid(&document, &error));
    text.text += QLatin1Char('a');
    QVERIFY(!Drawing({ text }).isValid(&document, &error));
    text.text = QStringLiteral("  ");
    QVERIFY(!Drawing({ text }).isValid(&document, &error));

    Drawing::Element line;
    line.kind = Drawing::Kind::Line;
    line.points = { QPointF(0, 0), QPointF(1, 1), QPointF(2, 2) };
    QVERIFY(!Drawing({ line }).isValid(&document, &error));
    line.kind = Drawing::Kind::FreehandCurve;
    QVERIFY(Drawing({ line }).isValid(&document, &error));

    // Image data only for an image
    Drawing::Element rectangle = createRectangle(0, QRectF(10, 10, 20, 20), 1.0);
    rectangle.svgData = "<svg/>";
    QVERIFY(!Drawing({ rectangle }).isValid(&document, &error));

    // The count of the elements and of the points
    std::vector<Drawing::Element> elements(Drawing::MAXIMUM_ELEMENT_COUNT, createRectangle(0, QRectF(10, 10, 20, 20), 1.0));
    QVERIFY(Drawing(elements).isValid(&document, &error));
    elements.push_back(createRectangle(0, QRectF(10, 10, 20, 20), 1.0));
    QVERIFY(!Drawing(elements).isValid(&document, &error));

    Drawing::Element curve;
    curve.kind = Drawing::Kind::FreehandCurve;
    curve.points.assign(Drawing::MAXIMUM_POINT_COUNT / 2, QPointF(1, 1));
    QVERIFY(Drawing({ curve, curve }).isValid(&document, &error));
    QVERIFY(!Drawing({ curve, curve, curve }).isValid(&document, &error));
}

void DocumentToolSignByHandTest::jsonRoundTrip()
{
    // The JSON of MCP (Q6.7): every kind but an image, the page from 1, the box [x, y, width, height]
    Drawing::Element text;
    text.kind = Drawing::Kind::Text;
    text.pageIndex = 1;
    text.rectangle = QRectF(10.5, 20, 100, 30);
    text.text = QStringLiteral("Jane Novák");
    text.style.penColor = QColor(0x10, 0x20, 0x30);
    text.style.fontSize = 14.0;
    text.style.alignment = Drawing::Alignment::Center;
    text.style.angle = 15.0;

    Drawing::Element curve;
    curve.kind = Drawing::Kind::FreehandCurve;
    curve.points = { QPointF(1, 2), QPointF(3, 4.25), QPointF(5, 6) };
    curve.style.penWidth = 2.5;

    Drawing::Element rounded = createRectangle(0, QRectF(0, 0, 50, 40), 1.0);
    rounded.kind = Drawing::Kind::RoundedRectangle;
    rounded.cornerRadius = 5.0;
    rounded.style.fillColor = QColor(0xff, 0xee, 0xdd);

    Drawing::Element image = createRectangle(0, QRectF(0, 0, 50, 40), 1.0);
    image.kind = Drawing::Kind::SvgImage;
    image.svgData = "<svg xmlns=\"http://www.w3.org/2000/svg\"/>";

    std::vector<Drawing::Element> elements = { text, curve, rounded };
    for (const Drawing::Kind kind : { Drawing::Kind::Line, Drawing::Kind::Rectangle, Drawing::Kind::Dot, Drawing::Kind::AcceptMark, Drawing::Kind::RejectMark })
    {
        Drawing::Element element = kind == Drawing::Kind::Line ? curve : createRectangle(0, QRectF(1, 2, 3, 4), 1.0);
        element.kind = kind;
        if (kind == Drawing::Kind::Line)
        {
            element.points.resize(2);
        }
        elements.push_back(element);
    }

    std::vector<Drawing::Element> withImage = elements;
    withImage.push_back(image);
    const QJsonArray json = Drawing(withImage).toJson();
    QCOMPARE(json.size(), qsizetype(elements.size()));
    QCOMPARE(json.at(0).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("text"));
    QCOMPARE(json.at(0).toObject().value(QStringLiteral("page")).toInt(), 2);
    QCOMPARE(json.at(0).toObject().value(QStringLiteral("style")).toObject().value(QStringLiteral("penColor")).toString(), QStringLiteral("#102030"));
    QVERIFY(!QJsonDocument(json).toJson().contains("svg"));

    QString error;
    const std::optional<Drawing> read = Drawing::fromJson(json, &error);
    QVERIFY2(read, qPrintable(error));
    QCOMPARE(read->getElements().size(), elements.size());
    for (size_t i = 0; i < elements.size(); ++i)
    {
        QVERIFY2(read->getElements()[i] == elements[i], qPrintable(QString::number(i)));
    }
}

void DocumentToolSignByHandTest::jsonStrict_data()
{
    QTest::addColumn<QByteArray>("json");

    // The JSON of an assistant is untrusted (plan 7.2): an unknown key, a wrong type, a value out of
    // its range and an image (Q6.7, R6-20) are refused, nothing is guessed
    const QByteArray dot = R"("kind": "dot", "page": 1, "rect": [0, 0, 10, 10])";
    QTest::newRow("unknown-key") << QByteArray("[{" + dot + R"(, "foo": 1}])");
    QTest::newRow("svg-image") << QByteArray(R"([{"kind": "svg-image", "page": 1, "rect": [0, 0, 10, 10]}])");
    QTest::newRow("unknown-kind") << QByteArray(R"([{"kind": "image", "page": 1, "rect": [0, 0, 10, 10]}])");
    QTest::newRow("page-zero") << QByteArray(R"([{"kind": "dot", "page": 0, "rect": [0, 0, 10, 10]}])");
    QTest::newRow("page-fraction") << QByteArray(R"([{"kind": "dot", "page": 1.5, "rect": [0, 0, 10, 10]}])");
    QTest::newRow("no-page") << QByteArray(R"([{"kind": "dot", "rect": [0, 0, 10, 10]}])");
    QTest::newRow("negative-size") << QByteArray(R"([{"kind": "dot", "page": 1, "rect": [0, 0, -1, 10]}])");
    QTest::newRow("huge-coordinate") << QByteArray(R"([{"kind": "dot", "page": 1, "rect": [1e300, 0, 1, 10]}])");
    QTest::newRow("points-of-rectangle") << QByteArray(R"([{"kind": "rectangle", "page": 1, "points": [[0, 0], [1, 1]]}])");
    QTest::newRow("line-three-points") << QByteArray(R"([{"kind": "line", "page": 1, "points": [[0, 0], [1, 1], [2, 2]]}])");
    QTest::newRow("color-name") << QByteArray("[{" + dot + R"(, "style": {"penColor": "red"}}])");
    QTest::newRow("pen-too-wide") << QByteArray("[{" + dot + R"(, "style": {"penWidth": 101}}])");
    QTest::newRow("unknown-style") << QByteArray("[{" + dot + R"(, "style": {"font": "Arial"}}])");
    QTest::newRow("text-not-string") << QByteArray(R"([{"kind": "text", "page": 1, "rect": [0, 0, 10, 10], "text": 5}])");
    QTest::newRow("text-of-dot") << QByteArray("[{" + dot + R"(, "text": "a"}])");
    QTest::newRow("not-object") << QByteArray("[1]");

    QByteArray many = "[";
    for (int i = 0; i <= Drawing::MAXIMUM_ELEMENT_COUNT; ++i)
    {
        many += (i ? ",{" : "{") + dot + "}";
    }
    QTest::newRow("too-many-elements") << QByteArray(many + "]");
}

void DocumentToolSignByHandTest::jsonStrict()
{
    QFETCH(QByteArray, json);
    const QJsonDocument document = QJsonDocument::fromJson(json);
    QVERIFY(document.isArray());

    QString error;
    QVERIFY(!Drawing::fromJson(document.array(), &error));
    QVERIFY(!error.isEmpty());
}

void DocumentToolSignByHandTest::svgSafety()
{
    // Plan 7.2: an SVG image of at most 4 MB, no reference outside of itself (xlink:href only data:)
    Drawing::Element image = createRectangle(0, QRectF(0, 0, 50, 40), 0.0);
    image.kind = Drawing::Kind::SvgImage;
    QString error;

    image.svgData = R"(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"><rect width="10" height="10"/><image xlink:href="data:image/png;base64,AAAA"/><use href="#a"/></svg>)";
    QVERIFY2(Drawing({ image }).isValid(nullptr, &error), qPrintable(error));

    image.svgData = R"(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"><image xlink:href="file:///C:/secret.png"/></svg>)";
    QVERIFY(!Drawing({ image }).isValid(nullptr, &error));

    image.svgData = R"(<svg xmlns="http://www.w3.org/2000/svg"><image href="https://example.com/a.png"/></svg>)";
    QVERIFY(!Drawing({ image }).isValid(nullptr, &error));

    image.svgData = R"(<?xml version="1.0"?><!DOCTYPE svg [<!ENTITY a "aaaa">]><svg xmlns="http://www.w3.org/2000/svg">&a;</svg>)";
    QVERIFY(!Drawing({ image }).isValid(nullptr, &error));

    image.svgData = R"(<svg xmlns="http://www.w3.org/2000/svg"><rect></svg>)";
    QVERIFY(!Drawing({ image }).isValid(nullptr, &error));

    image.svgData = QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"><!--") + QByteArray(Drawing::MAXIMUM_SVG_SIZE, 'a') + QByteArray("--></svg>");
    QVERIFY(!Drawing({ image }).isValid(nullptr, &error));
}

void DocumentToolSignByHandTest::appendedAfterContent_data()
{
    QTest::addColumn<QPointF>("origin");
    QTest::newRow("origin-zero") << QPointF(0, 0);
    QTest::newRow("origin-moved") << QPointF(100, 200);
}

void DocumentToolSignByHandTest::appendedAfterContent()
{
    // 5.5: the drawing goes into the content of the page after its content; the old content keeps
    // its graphics state to itself (it may leave a cm, which would move the drawing) and the drawing
    // is in q … Q moved to the origin of the media box (the page of QPdfWriter starts at zero)
    QFETCH(QPointF, origin);

    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(origin, QSizeF(300, 400)));
    const QByteArray oldContent = "2 0 0 2 0 0 cm\n";
    pdf::PDFDictionaryBuilder streamDictionary;
    streamDictionary.addEntry(pdf::PDFInplaceOrMemoryString("Length"), pdf::PDFObject::createInteger(oldContent.size()));
    const pdf::PDFObjectReference stream = builder.addObject(pdf::PDFObject::createStream(pdf::PDFStream(std::move(streamDictionary), QByteArray(oldContent))));
    pdf::PDFDictionaryBuilder pageUpdate;
    pageUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Contents"), pdf::PDFObject::createReference(stream));
    builder.mergeTo(page, pdf::PDFObject::createDictionary(std::move(pageUpdate)));
    const pdf::PDFDocument document = read(write(builder.build()));

    pdf::PDFDocumentBuilder drawingBuilder(&document);
    const Drawing drawing({ createRectangle(0, QRectF(origin + QPointF(10, 10), QSizeF(50, 50)), 2.0) });
    QString error;
    QVERIFY2(drawing.appendToPages(&drawingBuilder, &error), qPrintable(error));
    const pdf::PDFDocument result = read(write(drawingBuilder.build()));

    const QList<QByteArray> streams = getContentStreams(result, 0);
    QCOMPARE(streams.size(), 5);
    QCOMPARE(streams[0], QByteArray("q\n"));
    QCOMPARE(streams[1], oldContent);
    QCOMPARE(streams[2], QByteArray("Q\nq\n1 0 0 1 ") + QByteArray::number(origin.x()) + " " + QByteArray::number(origin.y()) + " cm\n");
    QVERIFY(!streams[3].isEmpty());
    QCOMPARE(streams[4], QByteArray("Q\n"));

    // The brackets are balanced over the whole content (8.4.2)
    const QByteArray content = streams.join(' ');
    QCOMPARE(content.count("q\n") - content.count("Q\n"), streams[3].count("q\n") - streams[3].count("Q\n"));

    // No annotation, no field: not a digital ID signature (5.5)
    QVERIFY(result.getCatalog()->getPage(0)->getAnnotations().empty());
    QVERIFY(result.getCatalog()->getFormObject().isNull());
}

void DocumentToolSignByHandTest::pageWithoutMediaBox()
{
    // R6-17 and Table 31 (/MediaBox is inheritable): an inherited box is used, a page without any
    // box is an error, nothing is drawn
    pdf::PDFDocumentBuilder builder;
    const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 300, 400));
    const pdf::PDFDictionary* pageDictionary = builder.getDictionaryFromObject(builder.getObjectByReference(page));
    const pdf::PDFObjectReference parent = pageDictionary->get("Parent").getReference();
    pdf::PDFDictionaryBuilder withoutBox(*pageDictionary);
    withoutBox.removeEntry("MediaBox");
    builder.setObject(page, pdf::PDFObject::createDictionary(std::move(withoutBox)));
    const pdf::PDFDocument noBox = builder.build();

    pdf::PDFDocumentBuilder failing(&noBox);
    QString error;
    QVERIFY(!Drawing({ createRectangle(0, QRectF(10, 10, 50, 50), 1.0) }).appendToPages(&failing, &error));
    QVERIFY(error.contains(QLatin1String("1")));

    pdf::PDFDocumentBuilder inherited(&noBox);
    pdf::PDFDictionaryBuilder parentUpdate(*inherited.getDictionaryFromObject(inherited.getObjectByReference(parent)));
    parentUpdate.setEntry(pdf::PDFInplaceOrMemoryString("MediaBox"), pdf::PDFObject::createArray(pdf::PDFArrayBuilder(std::vector<pdf::PDFObject>{
        pdf::PDFObject::createInteger(0), pdf::PDFObject::createInteger(0), pdf::PDFObject::createInteger(200), pdf::PDFObject::createInteger(100) })));
    inherited.setObject(parent, pdf::PDFObject::createDictionary(std::move(parentUpdate)));
    QVERIFY2(Drawing({ createRectangle(0, QRectF(10, 10, 50, 50), 1.0) }).appendToPages(&inherited, &error), qPrintable(error));

    const pdf::PDFDocument result = inherited.build();
    QCOMPARE(result.getCatalog()->getPage(0)->getMediaBox(), QRectF(0, 0, 200, 100));
    QCOMPARE(getContentStreams(result, 0).size(), 4);
}

void DocumentToolSignByHandTest::formXObject()
{
    // The appearance of a visible signature (4.1 step 3): a form XObject (Table 93), /BBox = the
    // bounding box of the elements in the space of the page of QPdfWriter (the media box moved to zero)
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    builder.appendPage(QRectF(100, 200, 300, 400));
    const Drawing drawing({ createRectangle(1, QRectF(150, 250, 60, 40), 2.0) });

    const pdf::PDFObjectReference form = drawing.createFormXObject(&builder, 1, true);
    QVERIFY(form.isValid());
    const pdf::PDFObject object = builder.getObjectByReference(form);
    QVERIFY(object.isStream());
    const pdf::PDFDictionary* dictionary = object.getStream()->getDictionary();
    QCOMPARE(dictionary->get("Type").getString(), QByteArray("XObject"));
    QCOMPARE(dictionary->get("Subtype").getString(), QByteArray("Form"));
    pdf::PDFDocumentDataLoaderDecorator loader(builder.getStorage());
    QCOMPARE(loader.readRectangle(dictionary->get("BBox"), QRectF()), QRectF(49, 49, 62, 42));
    QVERIFY(builder.getDictionaryFromObject(builder.getObject(dictionary->get("Resources"))));

    // No form of a page without elements, of a page, which does not exist
    QVERIFY(!drawing.createFormXObject(&builder, 0, false).isValid());
    QVERIFY(!drawing.createFormXObject(&builder, 5, false).isValid());
}

void DocumentToolSignByHandTest::nestedPageTree()
{
    // ISO 32000-2, 7.7.3.2, 7.7.3.3: the pages are the leaves of the page tree, an intermediate node
    // /Pages is no page. The tree [[page 1, page 2], page 3] (the shape of large documents): the
    // drawing of page 2 goes into the content of the page 2 (object 5), and the form of the page 3
    // has the media box of the page 3 - never the /Kids of the root, whose item 0 is a node
    mergetest::MergeTestPdf pdf;
    pdf.set(1, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(2, "<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 3 >>");
    pdf.set(3, "<< /Type /Pages /Parent 2 0 R /Kids [4 0 R 5 0 R] /Count 2 /MediaBox [0 0 300 400] >>");
    pdf.set(4, "<< /Type /Page /Parent 3 0 R /Resources << >> /Contents 7 0 R >>");
    pdf.set(5, "<< /Type /Page /Parent 3 0 R /Resources << >> /Contents 7 0 R >>");
    pdf.set(6, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 500 600] /Resources << >> /Contents 7 0 R >>");
    pdf.set(7, mergetest::MergeTestPdf::stream("0 0 1 rg 0 0 10 10 re f"));
    const pdf::PDFDocument document = pdf.read();
    QCOMPARE(document.getCatalog()->getPageCount(), size_t(3));

    pdf::PDFDocumentBuilder builder(&document);
    QString error;
    QVERIFY2(Drawing({ createRectangle(1, QRectF(10, 10, 50, 50), 1.0) }).appendToPages(&builder, &error), qPrintable(error));
    const pdf::PDFDocument result = read(write(builder.build()));
    QCOMPARE(result.getCatalog()->getPageCount(), size_t(3));

    // The page 2 has the drawing ([q, old, Q q cm, drawing, Q]), the pages 1 and 3 keep their content
    QCOMPARE(result.getCatalog()->getPage(1)->getPageReference(), pdf::PDFObjectReference(5, 0));
    QCOMPARE(getContentStreams(result, 1).size(), 5);
    QCOMPARE(getContentStreams(result, 0).size(), 1);
    QCOMPARE(getContentStreams(result, 2).size(), 1);

    // The node /Pages got no content (a key of a page only, Table 31)
    const pdf::PDFDictionary* node = result.getDictionaryFromObject(result.getObjectByReference(pdf::PDFObjectReference(3, 0)));
    QVERIFY(node);
    QVERIFY(!node->hasKey("Contents"));

    // The form of the page 3: the space of its own media box 500 x 600 (the page of QPdfWriter of
    // its size; the /BBox is the box of the element with the half of its pen)
    pdf::PDFDocumentBuilder formBuilder(&document);
    const pdf::PDFObjectReference form = Drawing({ createRectangle(2, QRectF(400, 500, 60, 40), 2.0) }).createFormXObject(&formBuilder, 2, false);
    QVERIFY(form.isValid());
    const pdf::PDFObject formObject = formBuilder.getObjectByReference(form);
    QVERIFY(formObject.isStream());
    pdf::PDFDocumentDataLoaderDecorator loader(formBuilder.getStorage());
    QCOMPARE(loader.readRectangle(formObject.getStream()->getDictionary()->get("BBox"), QRectF()), QRectF(399, 499, 62, 42));
}

void DocumentToolSignByHandTest::givenParameters()
{
    // An assistant gives the elements (Q6.7); none or both are an error; the JSON is read strictly
    const pdf::PDFDocumentToolSignByHand tool;
    const QString elements = QStringLiteral(R"([{"kind": "dot", "page": 1, "rect": [0, 0, 10, 10]}])");
    QCOMPARE(tool.checkGivenParameters(QVariantMap()).size(), size_t(1));
    QVERIFY(tool.checkGivenParameters({ { QStringLiteral("elements"), elements } }).empty());
    QCOMPARE(tool.checkGivenParameters({ { QStringLiteral("elements"), QStringLiteral("no json") } }).size(), size_t(1));
    QCOMPARE(tool.checkGivenParameters({ { QStringLiteral("elements"), QStringLiteral(R"([{"kind": "svg-image", "page": 1, "rect": [0, 0, 10, 10]}])") } }).size(), size_t(1));
    QCOMPARE(tool.checkGivenParameters({ { QStringLiteral("elements"), elements }, { QStringLiteral("drawing"), QVariant::fromValue(Drawing()) } }).size(), size_t(1));
}

void DocumentToolSignByHandTest::runOfTool()
{
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    builder.appendPage(QRectF(0, 0, 300, 400));
    const QByteArray data = write(builder.build());
    const pdf::PDFDocumentPointer document(new pdf::PDFDocument(read(data)));
    const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(document, data, QStringLiteral("C:/folder/contract.pdf"), false));
    const pdf::PDFDocumentToolSignByHand tool;

    // 435:8908: nothing drawn - the run is refused
    pdf::PDFDocumentToolResult empty = tool.run(context, QVariantMap(), nullptr, nullptr);
    QCOMPARE(empty.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(empty.message.contains(QLatin1String("Draw your signature")));

    const QString elements = QStringLiteral(R"([{"kind": "text", "page": 1, "rect": [20, 20, 120, 30], "text": "Jane Novak"},)"
                                            R"( {"kind": "accept-mark", "page": 2, "rect": [50, 50, 20, 20], "style": {"penWidth": 2}}])");
    const pdf::PDFDocumentToolResult result = tool.run(context, { { QStringLiteral("elements"), elements } }, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QCOMPARE(result.documents.size(), size_t(1));
    QCOMPARE(result.documents.front().kind, pdf::PDFDocumentToolOutputDocument::Kind::Changed);
    QCOMPARE(result.documents.front().suggestedFileName, QStringLiteral("contract_signed_by_hand.pdf"));
    QCOMPARE(result.data.value(QStringLiteral("elementCount")).toInt(), 2);
    QCOMPARE(result.data.value(QStringLiteral("pages")).toArray(), QJsonArray({ 1, 2 }));
    QCOMPARE(result.data.value(QStringLiteral("certified")).toBool(), false);

    // 435:10812: "Elements  4 on pages 3 and 308", "Kind  Drawing – not a digital ID signature"
    const auto findFact = [](const pdf::PDFDocumentToolResult& toolResult, const char* id)
    {
        const auto it = std::find_if(toolResult.facts.cbegin(), toolResult.facts.cend(), [id](const pdf::PDFDocumentToolFact& fact) { return fact.id == QLatin1String(id); });
        return it != toolResult.facts.cend() ? &*it : nullptr;
    };
    const pdf::PDFDocumentToolFact* drawn = findFact(result, "drawn");
    QVERIFY(drawn);
    QCOMPARE(drawn->title, QStringLiteral("Elements"));
    QCOMPARE(drawn->value, QStringLiteral("2 on pages 1 and 2"));
    const pdf::PDFDocumentToolFact* kind = findFact(result, "kind");
    QVERIFY(kind);
    QCOMPARE(kind->title, QStringLiteral("Kind"));
    QCOMPARE(kind->value, QStringLiteral("Drawing – not a digital ID signature"));

    // One page, and more pages than two (the one text of the page lists: "1–3")
    const pdf::PDFDocumentToolResult onePage = tool.run(context, { { QStringLiteral("elements"), QStringLiteral(R"([{"kind": "dot", "page": 2, "rect": [0, 0, 10, 10]}])") } }, nullptr, nullptr);
    QVERIFY2(onePage.isSuccess(), qPrintable(onePage.message));
    QCOMPARE(findFact(onePage, "drawn")->value, QStringLiteral("1 on page 2"));

    pdf::PDFDocumentBuilder threePagesBuilder;
    for (int i = 0; i < 3; ++i)
    {
        threePagesBuilder.appendPage(QRectF(0, 0, 300, 400));
    }
    const QByteArray threePagesData = write(threePagesBuilder.build());
    const pdf::PDFDocumentPointer threePagesDocument(new pdf::PDFDocument(read(threePagesData)));
    const QString threeElements = QStringLiteral(R"([{"kind": "dot", "page": 1, "rect": [0, 0, 10, 10]}, {"kind": "dot", "page": 2, "rect": [0, 0, 10, 10]},)"
                                                 R"( {"kind": "dot", "page": 3, "rect": [0, 0, 10, 10]}])");
    const pdf::PDFDocumentToolResult threePages = tool.run(pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(threePagesDocument, threePagesData, QString(), false)),
                                                           { { QStringLiteral("elements"), threeElements } }, nullptr, nullptr);
    QVERIFY2(threePages.isSuccess(), qPrintable(threePages.message));
    QCOMPARE(findFact(threePages, "drawn")->value, QStringLiteral("3 on pages 1–3"));

    const pdf::PDFDocument& changed = *result.documents.front().document;
    QCOMPARE(getContentStreams(changed, 0).size(), 4);
    QCOMPARE(getContentStreams(changed, 1).size(), 4);
    QVERIFY(changed.getCatalog()->getFormObject().isNull());

    // A document without pages: nothing to draw on (plan 7.2)
    pdf::PDFDocumentBuilder noPages;
    const QByteArray noPagesData = write(noPages.build());
    const pdf::PDFDocumentPointer noPagesDocument(new pdf::PDFDocument(read(noPagesData)));
    const pdf::PDFDocumentToolResult noPagesResult = tool.run(pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(noPagesDocument, noPagesData, QString(), false)),
                                                              { { QStringLiteral("elements"), elements } }, nullptr, nullptr);
    QCOMPARE(noPagesResult.status, pdf::PDFDocumentToolResultStatus::Failed);
    QVERIFY(noPagesResult.message.contains(QLatin1String("no pages")));
}

void DocumentToolSignByHandTest::certifiedDocument()
{
    // 4.2, ISO 32000-2 Table 257: no level of a certification allows a change of the content of a
    // page - the certification is removed and the result is only a copy (owner 13. 11.)
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 300, 400));
    pdf::PDFDictionaryBuilder parameters;
    parameters.addEntry(pdf::PDFInplaceOrMemoryString("P"), pdf::PDFObject::createInteger(2));
    pdf::PDFDictionaryBuilder reference;
    reference.addEntry(pdf::PDFInplaceOrMemoryString("TransformMethod"), pdf::PDFObject::createName("DocMDP"));
    reference.addEntry(pdf::PDFInplaceOrMemoryString("TransformParams"), pdf::PDFObject::createDictionary(std::move(parameters)));
    pdf::PDFDictionaryBuilder signature;
    signature.addEntry(pdf::PDFInplaceOrMemoryString("Type"), pdf::PDFObject::createName("Sig"));
    signature.addEntry(pdf::PDFInplaceOrMemoryString("Reference"), pdf::PDFObject::createArray(pdf::PDFArrayBuilder(std::vector<pdf::PDFObject>{ pdf::PDFObject::createDictionary(std::move(reference)) })));
    const pdf::PDFObjectReference certification = builder.addObject(pdf::PDFObject::createDictionary(std::move(signature)));
    pdf::PDFDictionaryBuilder permissions;
    permissions.addEntry(pdf::PDFInplaceOrMemoryString("DocMDP"), pdf::PDFObject::createReference(certification));
    pdf::PDFDictionaryBuilder catalogUpdate;
    catalogUpdate.addEntry(pdf::PDFInplaceOrMemoryString("Perms"), pdf::PDFObject::createDictionary(std::move(permissions)));
    builder.mergeTo(builder.getCatalogReference(), pdf::PDFObject::createDictionary(std::move(catalogUpdate)));
    const QByteArray data = write(builder.build());
    const pdf::PDFDocumentPointer document(new pdf::PDFDocument(read(data)));
    QCOMPARE(pdf::PDFDocMDPPermissions::getLevel(document.data()), pdf::PDFDocMDPPermissions::Level::FormFilling);

    const pdf::PDFDocumentToolSignByHand tool;
    const pdf::PDFDocumentToolResult result = tool.run(pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(document, data, QString(), false)),
                                                       { { QStringLiteral("elements"), QStringLiteral(R"([{"kind": "dot", "page": 1, "rect": [0, 0, 10, 10]}])") } }, nullptr, nullptr);
    QVERIFY2(result.isSuccess(), qPrintable(result.message));
    QVERIFY(result.data.value(QStringLiteral("certificationRemoved")).toBool());
    QCOMPARE(result.data.value(QStringLiteral("signatureImpact")).toString(), QStringLiteral("lost-copy-only"));
    QCOMPARE(pdf::PDFDocMDPPermissions::getLevel(result.documents.front().document.data()), pdf::PDFDocMDPPermissions::Level::NotCertified);
}

void DocumentToolSignByHandTest::elementsLimitFitsPointCount()
{
    // ASan D1 of step 6: the length of the JSON of elements fits the counts of plan 7.2 (all the points of
    // the curves of a drawing written by toJson with full precision), MCP stays bounded (the schema)
    const pdf::PDFDocumentToolSignByHand tool;
    const pdf::PDFDocumentToolParameterDefinitions definitions = tool.getParameters();
    const auto it = std::find_if(definitions.cbegin(), definitions.cend(), [](const pdf::PDFDocumentToolParameterDefinition& definition) { return definition.getName() == QLatin1String("elements"); });
    QVERIFY(it != definitions.cend());
    QVERIFY(it->isStructuredData());
    QCOMPARE(it->getMaximumLength(), pdf::PDFDocumentToolSignByHand::MAXIMUM_ELEMENTS_LENGTH);
    QCOMPARE(pdf::PDFDocumentToolParameters::toJsonSchema(definitions).value(QStringLiteral("properties")).toObject().value(QStringLiteral("elements")).toObject().value(QStringLiteral("maxLength")).toInteger(),
             qint64(pdf::PDFDocumentToolParameterDefinition::MAXIMUM_DATA_TEXT_LENGTH));

    // Two curves of all the points a drawing may have, each coordinate of 17 significant digits
    Drawing::Element curve;
    curve.kind = Drawing::Kind::FreehandCurve;
    curve.points.assign(Drawing::MAXIMUM_POINT_COUNT / 2, QPointF(123.45678901234567, 234.56789012345678));
    const Drawing drawing({ curve, curve });
    QString error;
    QVERIFY2(drawing.isValid(nullptr, &error), qPrintable(error));
    const QString elements = QString::fromUtf8(QJsonDocument(drawing.toJson()).toJson(QJsonDocument::Compact));
    QVERIFY(elements.size() > pdf::PDFDocumentToolParameterDefinition::MAXIMUM_TEXT_LENGTH);

    QVariantMap normalized;
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(definitions, QVariantMap({ { QStringLiteral("elements"), elements } }), &normalized).empty());
    QCOMPARE(normalized.value(QStringLiteral("elements")).toString(), elements);
    QVERIFY(tool.checkGivenParameters({ { QStringLiteral("elements"), elements } }).empty());

    // Both edges of the length (the JSON padded by white space)
    const QString longest = elements + QString(pdf::PDFDocumentToolSignByHand::MAXIMUM_ELEMENTS_LENGTH - elements.size(), QLatin1Char(' '));
    QVERIFY(pdf::PDFDocumentToolParameters::normalize(definitions, QVariantMap({ { QStringLiteral("elements"), longest } }), &normalized).empty());
    QVERIFY(tool.checkGivenParameters({ { QStringLiteral("elements"), longest } }).empty());
    const std::vector<pdf::PDFDocumentToolParameterError> errors = pdf::PDFDocumentToolParameters::normalize(definitions, QVariantMap({ { QStringLiteral("elements"), longest + QLatin1Char(' ') } }), &normalized);
    QCOMPARE(errors.size(), size_t(1));
    QCOMPARE(errors.front().parameter, QStringLiteral("elements"));
}

QTEST_MAIN(DocumentToolSignByHandTest)

#include "tst_documenttoolsignbyhandtest.moc"
