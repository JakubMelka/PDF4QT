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

#include "pdfannotation.h"
#include "pdfannotationproperties.h"
#include "pdfdocumentbuilder.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

/// Samples of the tests: one annotation of every edited kind on one page
class AnnotationPropertiesTestSamples
{
public:
    pdf::PDFDocumentBuilder builder;
    pdf::PDFObjectReference page;
    pdf::PDFObjectReference square;
    pdf::PDFObjectReference circle;
    pdf::PDFObjectReference line;
    pdf::PDFObjectReference polyline;
    pdf::PDFObjectReference polygon;
    pdf::PDFObjectReference ink;
    pdf::PDFObjectReference highlight;
    pdf::PDFObjectReference caret;
    pdf::PDFObjectReference freeText;
    pdf::PDFObjectReference callout;
    pdf::PDFObjectReference text;
    pdf::PDFObjectReference stamp;
    pdf::PDFObjectReference attachment;
    pdf::PDFObjectReference redact;
    pdf::PDFObjectReference webLink;
    pdf::PDFObjectReference pageLink;
    pdf::PDFObjectReference sound;
    pdf::PDFObjectReference popup;

    AnnotationPropertiesTestSamples()
    {
        page = builder.appendPage(QRectF(0, 0, 600, 800));
        square = builder.createAnnotationSquare(page, QRectF(10, 10, 100, 80), 2.0, QColor(), QColor(Qt::red), QStringLiteral("Tester"), QStringLiteral("square"), QStringLiteral("Square"));
        circle = builder.createAnnotationCircle(page, QRectF(120, 10, 100, 80), 2.0, QColor(Qt::yellow), QColor(Qt::blue), QStringLiteral("Tester"), QStringLiteral("circle"), QStringLiteral("Circle"));
        line = builder.createAnnotationLine(page, QRectF(10, 100, 200, 40), QPointF(10, 120), QPointF(200, 120), 2.0, QColor(), QColor(Qt::red), QStringLiteral("Tester"),
                                            QStringLiteral("line"), QStringLiteral("Line"), pdf::AnnotationLineEnding::OpenArrow, pdf::AnnotationLineEnding::ClosedArrow);
        polyline = builder.createAnnotationPolyline(page, QPolygonF({ QPointF(10, 150), QPointF(60, 190), QPointF(110, 150) }), 2.0, QColor(), QColor(Qt::blue), QStringLiteral("Tester"),
                                                    QStringLiteral("polyline"), QStringLiteral("Polyline"), pdf::AnnotationLineEnding::None, pdf::AnnotationLineEnding::Circle);
        polygon = builder.createAnnotationPolygon(page, QPolygonF({ QPointF(120, 150), QPointF(170, 190), QPointF(220, 150) }), 2.0, QColor(Qt::green), QColor(Qt::blue),
                                                  QStringLiteral("Tester"), QStringLiteral("polygon"), QStringLiteral("Polygon"));
        ink = builder.createAnnotationInk(page, QPolygonF({ QPointF(10, 200), QPointF(40, 230), QPointF(70, 200) }), 2.0, QColor(Qt::blue), QStringLiteral("Tester"),
                                          QStringLiteral("ink"), QStringLiteral("Ink"));
        highlight = builder.createAnnotationHighlight(page, QRectF(10, 250, 100, 16), QColor(Qt::yellow), QStringLiteral("Tester"), QStringLiteral("highlight"), QStringLiteral("Highlight"));
        caret = builder.createAnnotationCaret(page, QRectF(120, 250, 16, 16), 1.0, QColor(Qt::blue), QStringLiteral("Tester"), QStringLiteral("caret"), QStringLiteral("Caret"));

        pdf::PDFFreeTextStyle style;
        style.fontFamily = QStringLiteral("Courier");
        style.fontSize = 12.0;
        style.textColor = QColor(Qt::darkBlue);
        style.textAlignment = pdf::TextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        freeText = builder.createAnnotationFreeText(page, QRectF(10, 300, 150, 60), QStringLiteral("Tester"), QStringLiteral("freetext"), QStringLiteral("Free text"), style, false);
        callout = builder.createAnnotationFreeText(page, QRectF(200, 300, 200, 100), QRectF(260, 300, 140, 40), QStringLiteral("Tester"), QStringLiteral("callout"), QStringLiteral("Callout"),
                                                   pdf::TextAlignment(Qt::AlignRight | Qt::AlignTop), QPointF(200, 390), QPointF(260, 320), pdf::AnnotationLineEnding::None,
                                                   pdf::AnnotationLineEnding::OpenArrow);
        text = builder.createAnnotationText(page, QRectF(10, 400, 20, 20), pdf::TextAnnotationIcon::Key, QStringLiteral("Tester"), QStringLiteral("text"), QStringLiteral("Note"), true);
        stamp = builder.createAnnotationStamp(page, QRectF(50, 400, 120, 40), pdf::Stamp::Approved, QStringLiteral("Tester"), QStringLiteral("stamp"), QStringLiteral("Stamp"));
        attachment = builder.createAnnotationFileAttachment(page, QPointF(200, 400), pdf::PDFObjectReference(), pdf::FileAttachmentIcon::Paperclip, QStringLiteral("Tester"), QStringLiteral("File"));
        redact = builder.createAnnotationRedact(page, QRectF(300, 400, 50, 20), QColor(Qt::black), QColor(Qt::red));
        webLink = builder.createAnnotationLink(page, QRectF(10, 500, 100, 20), QStringLiteral("https://example.com/"), pdf::LinkHighlightMode::Outline);

        // A link to a page: the web link turned into a link with a destination
        pageLink = builder.createAnnotationLink(page, QRectF(10, 530, 100, 20), QStringLiteral("https://example.com/"), pdf::LinkHighlightMode::Invert);
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("A");
        factory << pdf::PDFObject();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Dest");
        factory << pdf::PDFDestination::createFit(page);
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.mergeTo(pageLink, factory.takeObject());

        // A sound annotation: a square with the subtype Sound
        sound = builder.createAnnotationSquare(page, QRectF(400, 10, 20, 20), 1.0, QColor(), QColor(Qt::red), QStringLiteral("Tester"), QStringLiteral("sound"), QStringLiteral("Sound"));
        pdf::PDFObjectFactory soundFactory;
        soundFactory.beginDictionary();
        soundFactory.beginDictionaryItem("Subtype");
        soundFactory << pdf::WrapName("Sound");
        soundFactory.endDictionaryItem();
        soundFactory.beginDictionaryItem("Name");
        soundFactory << pdf::WrapName("Mic");
        soundFactory.endDictionaryItem();
        soundFactory.endDictionary();
        builder.mergeTo(sound, soundFactory.takeObject());

        popup = builder.createAnnotationPopup(page, square, QRectF(300, 600, 100, 60), false);
    }

    /// Merges entries into the annotation
    void merge(pdf::PDFObjectReference annotation, pdf::PDFObject object)
    {
        builder.mergeTo(annotation, std::move(object));
    }

    /// Properties of the annotation of the builder
    pdf::PDFAnnotationProperties read(pdf::PDFObjectReference annotation) const
    {
        return pdf::PDFAnnotationPropertyEditor::read(builder.getStorage(), annotation);
    }

    /// Decoded content and the blend mode of the graphic state PDF4QTBlendMode of the
    /// normal appearance stream of the annotation (empty without the state)
    std::pair<QByteArray, QByteArray> getAppearanceBlendMode(pdf::PDFObjectReference annotation) const
    {
        const pdf::PDFObjectStorage* storage = builder.getStorage();
        const pdf::PDFDictionary* appearance = storage->getDictionaryFromObject(get(annotation, "AP"));
        const pdf::PDFObject normal = appearance ? storage->getObject(appearance->get("N")) : pdf::PDFObject();
        const pdf::PDFStream* stream = normal.isStream() ? normal.getStream() : nullptr;
        if (!stream)
        {
            return {};
        }
        const pdf::PDFDictionary* resources = storage->getDictionaryFromObject(stream->getDictionary()->get("Resources"));
        const pdf::PDFDictionary* graphicStates = resources ? storage->getDictionaryFromObject(resources->get("ExtGState")) : nullptr;
        const pdf::PDFDictionary* graphicState = graphicStates ? storage->getDictionaryFromObject(graphicStates->get("PDF4QTBlendMode")) : nullptr;
        const pdf::PDFObject blendMode = graphicState ? storage->getObject(graphicState->get("BM")) : pdf::PDFObject();
        return { storage->getDecodedStream(stream), blendMode.isName() ? blendMode.getString() : QByteArray() };
    }

    /// The entry of the annotation of the builder
    pdf::PDFObject get(pdf::PDFObjectReference annotation, const char* key) const
    {
        const pdf::PDFObjectStorage* storage = builder.getStorage();
        const pdf::PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(annotation));
        return dictionary ? storage->getObject(dictionary->get(key)) : pdf::PDFObject();
    }
};

/// Tests of PDFAnnotationPropertyEditor of Pdf4QtLibCore (E10, package B of step 4)
class AnnotationPropertiesTest : public QObject
{
    Q_OBJECT

private slots:
    void readEachType();
    void writeEachProperty();
    void changeMaskWritesOnlyChanged();
    void appearanceRegenerated();
    void flagsRoundTrip();
    void hostileDictionaries();
    void linkProperties();
    void helpers();
    void blendMode();
};

void AnnotationPropertiesTest::readEachType()
{
    using P = pdf::PDFAnnotationProperties;

    AnnotationPropertiesTestSamples samples;

    const P square = samples.read(samples.square);
    QCOMPARE(square.type, pdf::AnnotationType::Square);
    QCOMPARE(square.author, QStringLiteral("Tester"));
    QCOMPARE(square.subject, QStringLiteral("square"));
    QCOMPARE(square.contents, QStringLiteral("Square"));
    QCOMPARE(square.color, QColor(Qt::red));
    QVERIFY(!square.interiorColor.isValid());
    QCOMPARE(square.borderWidth, 2.0);
    QVERIFY(square.supportedFields.testFlag(P::InteriorColor));
    QVERIFY(square.supportedFields.testFlag(P::Border));
    QVERIFY(!square.supportedFields.testFlag(P::LineEndings));
    QVERIFY(square.creationDate.isValid());

    const P circle = samples.read(samples.circle);
    QCOMPARE(circle.interiorColor, QColor(Qt::yellow));

    const P line = samples.read(samples.line);
    QCOMPARE(line.type, pdf::AnnotationType::Line);
    QCOMPARE(line.startLineEnding, pdf::AnnotationLineEnding::OpenArrow);
    QCOMPARE(line.endLineEnding, pdf::AnnotationLineEnding::ClosedArrow);
    QVERIFY(line.supportedFields.testFlag(P::LeaderLines));
    QVERIFY(line.supportedFields.testFlag(P::Caption));

    const P polyline = samples.read(samples.polyline);
    QVERIFY(polyline.supportedFields.testFlag(P::LineEndings));
    QVERIFY(!polyline.supportedFields.testFlag(P::LeaderLines));

    QVERIFY(samples.read(samples.polygon).supportedFields.testFlag(P::InteriorColor));
    QVERIFY(!samples.read(samples.ink).supportedFields.testFlag(P::InteriorColor));
    QVERIFY(samples.read(samples.ink).supportedFields.testFlag(P::Border));
    QCOMPARE(samples.read(samples.highlight).supportedFields, P::Fields(P::Author | P::Subject | P::Contents | P::Opacity | P::BlendMode | P::Flags | P::Color));
    QCOMPARE(samples.read(samples.caret).type, pdf::AnnotationType::Caret);

    const P freeText = samples.read(samples.freeText);
    QCOMPARE(freeText.freeTextStyle.fontFamily, QStringLiteral("Courier"));
    QCOMPARE(freeText.freeTextStyle.fontSize, 12.0);
    QCOMPARE(freeText.freeTextStyle.textColor, QColor(Qt::darkBlue));
    QVERIFY(freeText.freeTextStyle.textAlignment.testFlag(Qt::AlignHCenter));
    QVERIFY(!freeText.hasCalloutLine);
    QVERIFY(!freeText.supportedFields.testFlag(P::CalloutLineEnding));

    const P callout = samples.read(samples.callout);
    QVERIFY(callout.hasCalloutLine);
    QVERIFY(callout.supportedFields.testFlag(P::CalloutLineEnding));
    QVERIFY(callout.freeTextStyle.textAlignment.testFlag(Qt::AlignRight));

    const P text = samples.read(samples.text);
    QCOMPARE(text.iconName, QByteArray("Key"));
    QVERIFY(text.isOpen);
    QVERIFY(text.supportedFields.testFlag(P::IconName));
    QVERIFY(text.supportedFields.testFlag(P::Open));

    QCOMPARE(samples.read(samples.stamp).iconName, QByteArray("Approved"));
    QCOMPARE(samples.read(samples.attachment).iconName, QByteArray("Paperclip"));
    QVERIFY(samples.read(samples.redact).supportedFields.testFlag(P::InteriorColor));
    QCOMPARE(samples.read(samples.sound).type, pdf::AnnotationType::Sound);
    QCOMPARE(samples.read(samples.sound).iconName, QByteArray("Mic"));

    // Popup: only the contents and the flags
    const P popup = samples.read(samples.popup);
    QCOMPARE(popup.type, pdf::AnnotationType::Popup);
    QCOMPARE(popup.supportedFields, P::Fields(P::Contents | P::Flags));

    // Invalid inputs
    QCOMPARE(pdf::PDFAnnotationPropertyEditor::read(nullptr, samples.square).type, pdf::AnnotationType::Invalid);
    QCOMPARE(samples.read(pdf::PDFObjectReference()).type, pdf::AnnotationType::Invalid);
    QCOMPARE(samples.read(samples.page).supportedFields, P::Fields(P::None));
    QCOMPARE(samples.read(pdf::PDFObjectReference(99999, 0)).type, pdf::AnnotationType::Invalid);

    // The icon names of the types
    QCOMPARE(pdf::PDFAnnotationPropertyEditor::getIconNames(pdf::AnnotationType::Text).size(), size_t(7));
    QCOMPARE(pdf::PDFAnnotationPropertyEditor::getIconNames(pdf::AnnotationType::Stamp).size(), size_t(14));
    QCOMPARE(pdf::PDFAnnotationPropertyEditor::getIconNames(pdf::AnnotationType::FileAttachment).size(), size_t(4));
    QCOMPARE(pdf::PDFAnnotationPropertyEditor::getIconNames(pdf::AnnotationType::Sound).size(), size_t(2));
    QVERIFY(pdf::PDFAnnotationPropertyEditor::getIconNames(pdf::AnnotationType::Square).empty());
    QCOMPARE(pdf::PDFAnnotationPropertyEditor::getSupportedFields(pdf::AnnotationType::Invalid, false), P::Fields(P::None));
    QCOMPARE(pdf::PDFAnnotationPropertyEditor::getSupportedFields(pdf::AnnotationType::Widget, false), P::Fields(P::Contents | P::Flags));
}

void AnnotationPropertiesTest::writeEachProperty()
{
    using P = pdf::PDFAnnotationProperties;
    using E = pdf::PDFAnnotationPropertyEditor;

    AnnotationPropertiesTestSamples samples;

    // The comment, the colors, the opacity, the dashed border, the flags
    P square = samples.read(samples.square);
    square.author = QStringLiteral("Author");
    square.subject = QStringLiteral("Subject");
    square.contents = QStringLiteral("Contents\nsecond line");
    square.color = QColor(Qt::green);
    square.interiorColor = QColor(Qt::white);
    square.opacity = 0.5;
    square.borderWidth = 4.0;
    square.borderStyle = pdf::AnnotationBorderStyle::Dashed;
    square.dashPattern = { 2.0, 1.0 };
    QVERIFY(E::write(&samples.builder, samples.square, square, P::All));
    P result = samples.read(samples.square);
    QCOMPARE(result.author, QStringLiteral("Author"));
    QCOMPARE(result.subject, QStringLiteral("Subject"));
    QCOMPARE(result.contents, QStringLiteral("Contents\nsecond line"));
    QCOMPARE(result.color, QColor(Qt::green));
    QCOMPARE(result.interiorColor, QColor(Qt::white));
    QCOMPARE(result.opacity, 0.5);
    QCOMPARE(result.borderWidth, 4.0);
    QCOMPARE(result.borderStyle, pdf::AnnotationBorderStyle::Dashed);
    QCOMPARE(result.dashPattern, std::vector<pdf::PDFReal>({ 2.0, 1.0 }));
    QVERIFY(samples.get(samples.square, "M").isString());

    // No color and no fill; the solid border removes the dash pattern
    square.color = QColor();
    square.interiorColor = QColor();
    square.borderStyle = pdf::AnnotationBorderStyle::Solid;
    QVERIFY(E::write(&samples.builder, samples.square, square, P::Color | P::InteriorColor | P::Border));
    result = samples.read(samples.square);
    QVERIFY(!result.color.isValid());
    QVERIFY(!result.interiorColor.isValid());
    QVERIFY(samples.get(samples.square, "IC").isNull());
    QCOMPARE(result.borderStyle, pdf::AnnotationBorderStyle::Solid);

    // The other border styles
    for (const pdf::AnnotationBorderStyle style : { pdf::AnnotationBorderStyle::Beveled, pdf::AnnotationBorderStyle::Inset, pdf::AnnotationBorderStyle::Underline })
    {
        square.borderStyle = style;
        QVERIFY(E::write(&samples.builder, samples.square, square, P::Border));
        QCOMPARE(samples.read(samples.square).borderStyle, style);
    }

    // The opacity with ca: both are written
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("ca");
    factory << 1.0;
    factory.endDictionaryItem();
    factory.endDictionary();
    samples.merge(samples.circle, factory.takeObject());
    P circle = samples.read(samples.circle);
    circle.opacity = 0.25;
    QVERIFY(E::write(&samples.builder, samples.circle, circle, P::Opacity));
    QCOMPARE(samples.get(samples.circle, "ca").getReal(), 0.25);
    QCOMPARE(samples.get(samples.circle, "CA").getReal(), 0.25);

    // The line endings, the leader lines and the caption of a line
    P line = samples.read(samples.line);
    line.startLineEnding = pdf::AnnotationLineEnding::Diamond;
    line.endLineEnding = pdf::AnnotationLineEnding::Slash;
    line.leaderLineLength = 12.0;
    line.leaderLineExtension = 3.0;
    line.isCaptionShown = true;
    line.isCaptionAbove = true;
    QVERIFY(E::write(&samples.builder, samples.line, line, P::LineEndings | P::LeaderLines | P::Caption));
    result = samples.read(samples.line);
    QCOMPARE(result.startLineEnding, pdf::AnnotationLineEnding::Diamond);
    QCOMPARE(result.endLineEnding, pdf::AnnotationLineEnding::Slash);
    QCOMPARE(result.leaderLineLength, 12.0);
    QCOMPARE(result.leaderLineExtension, 3.0);
    QVERIFY(result.isCaptionShown);
    QVERIFY(result.isCaptionAbove);
    line.isCaptionAbove = false;
    QVERIFY(E::write(&samples.builder, samples.line, line, P::Caption));
    QVERIFY(!samples.read(samples.line).isCaptionAbove);

    // The icon and the state of a sticky note; an empty icon name is not written
    P text = samples.read(samples.text);
    text.iconName = "Help";
    text.isOpen = false;
    QVERIFY(E::write(&samples.builder, samples.text, text, P::IconName | P::Open));
    QCOMPARE(samples.read(samples.text).iconName, QByteArray("Help"));
    QVERIFY(!samples.read(samples.text).isOpen);
    text.iconName.clear();
    QVERIFY(E::write(&samples.builder, samples.text, text, P::IconName));
    QCOMPARE(samples.read(samples.text).iconName, QByteArray("Help"));

    // The text of a text box: the font, the size, the color, the alignment, the callout end
    P callout = samples.read(samples.callout);
    callout.freeTextStyle.fontFamily = QStringLiteral("Times Roman");
    callout.freeTextStyle.fontSize = 20.0;
    callout.freeTextStyle.textColor = QColor(Qt::red);
    callout.freeTextStyle.textAlignment = Qt::AlignLeft;
    callout.calloutLineEnding = pdf::AnnotationLineEnding::Square;
    QVERIFY(E::write(&samples.builder, samples.callout, callout, P::DefaultAppearance | P::Alignment | P::CalloutLineEnding));
    result = samples.read(samples.callout);
    QCOMPARE(result.freeTextStyle.fontFamily, QStringLiteral("Times Roman"));
    QCOMPARE(result.freeTextStyle.fontSize, 20.0);
    QCOMPARE(result.freeTextStyle.textColor, QColor(Qt::red));
    QVERIFY(result.freeTextStyle.textAlignment.testFlag(Qt::AlignLeft));
    QCOMPARE(result.calloutLineEnding, pdf::AnnotationLineEnding::Square);
    for (const Qt::Alignment alignment : { Qt::Alignment(Qt::AlignHCenter), Qt::Alignment(Qt::AlignRight) })
    {
        callout.freeTextStyle.textAlignment = alignment;
        QVERIFY(E::write(&samples.builder, samples.callout, callout, P::Alignment));
        QVERIFY(samples.read(samples.callout).freeTextStyle.textAlignment.testFlag(Qt::AlignmentFlag(int(alignment))));
    }

    // Nothing to write: no builder, no field, a field the type does not have
    QVERIFY(!E::write(nullptr, samples.square, square, P::All));
    QVERIFY(!E::write(&samples.builder, samples.square, square, P::None));
    QVERIFY(!E::write(&samples.builder, samples.highlight, square, P::Border | P::LineEndings));
    QVERIFY(!E::write(&samples.builder, pdf::PDFObjectReference(99999, 0), square, P::All));
}

void AnnotationPropertiesTest::changeMaskWritesOnlyChanged()
{
    using P = pdf::PDFAnnotationProperties;
    using E = pdf::PDFAnnotationPropertyEditor;

    AnnotationPropertiesTestSamples samples;
    const P original = samples.read(samples.line);
    QCOMPARE(E::getChangedFields(original, original), P::Fields(P::None));

    // Every property changed alone gives its field only
    auto check = [&](P changed, P::Field field)
    {
        const P::Fields fields = E::getChangedFields(original, changed);
        QCOMPARE(fields, P::Fields(field));
    };
    P changed = original;
    changed.author = QStringLiteral("x");
    check(changed, P::Author);
    changed = original;
    changed.subject = QStringLiteral("x");
    check(changed, P::Subject);
    changed = original;
    changed.contents = QStringLiteral("x");
    check(changed, P::Contents);
    changed = original;
    changed.color = QColor(Qt::cyan);
    check(changed, P::Color);
    changed = original;
    changed.interiorColor = QColor(Qt::cyan);
    check(changed, P::InteriorColor);
    changed = original;
    changed.opacity = 0.3;
    check(changed, P::Opacity);
    changed = original;
    changed.borderWidth = 7.0;
    check(changed, P::Border);
    changed = original;
    changed.flags.setFlag(pdf::PDFAnnotation::NoZoom);
    check(changed, P::Flags);
    changed = original;
    changed.endLineEnding = pdf::AnnotationLineEnding::Butt;
    check(changed, P::LineEndings);
    changed = original;
    changed.leaderLineExtension = 4.0;
    check(changed, P::LeaderLines);
    changed = original;
    changed.isCaptionShown = !original.isCaptionShown;
    check(changed, P::Caption);

    // A field the type does not have is never reported (an icon of a line)
    changed = original;
    changed.iconName = "Comment";
    changed.isOpen = true;
    changed.freeTextStyle.fontSize = 30.0;
    changed.freeTextStyle.textAlignment = Qt::AlignRight;
    changed.calloutLineEnding = pdf::AnnotationLineEnding::Square;
    changed.highlightMode = pdf::LinkHighlightMode::Push;
    QCOMPARE(E::getChangedFields(original, changed), P::Fields(P::None));

    // Dash pattern changes count only for a dashed border
    changed = original;
    changed.dashPattern = { 5.0 };
    QCOMPARE(E::getChangedFields(original, changed), P::Fields(P::None));
    P dashed = original;
    dashed.borderStyle = pdf::AnnotationBorderStyle::Dashed;
    changed = dashed;
    changed.dashPattern = { 5.0 };
    QCOMPARE(E::getChangedFields(dashed, changed), P::Fields(P::Border));

    // Written: only the changed entry is touched, the others stay (also unknown entries)
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Custom");
    factory << pdf::WrapName("Kept");
    factory.endDictionaryItem();
    factory.endDictionary();
    samples.merge(samples.line, factory.takeObject());
    changed = original;
    changed.contents = QStringLiteral("Only this");
    changed.author = QStringLiteral("Not written");
    QVERIFY(E::write(&samples.builder, samples.line, changed, E::getChangedFields(original, changed) & ~P::Fields(P::Author)));
    const P result = samples.read(samples.line);
    QCOMPARE(result.contents, QStringLiteral("Only this"));
    QCOMPARE(result.author, original.author);
    QCOMPARE(result.color, original.color);
    QCOMPARE(result.startLineEnding, original.startLineEnding);
    QCOMPARE(samples.get(samples.line, "Custom").getString(), QByteArray("Kept"));
}

void AnnotationPropertiesTest::appearanceRegenerated()
{
    using P = pdf::PDFAnnotationProperties;

    AnnotationPropertiesTestSamples samples;

    // The appearance stream is created again with the new color
    const pdf::PDFObject before = samples.get(samples.square, "AP");
    QVERIFY(before.isDictionary());
    P square = samples.read(samples.square);
    square.color = QColor(Qt::magenta);
    QVERIFY(pdf::PDFAnnotationPropertyEditor::write(&samples.builder, samples.square, square, P::Color));
    const pdf::PDFObject after = samples.get(samples.square, "AP");
    QVERIFY(after.isDictionary());
    QVERIFY(after != before);

    // The built document parses the changed annotation
    pdf::PDFDocument document = samples.builder.build();
    const pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(&document.getStorage(), samples.square);
    QVERIFY(annotation);
    QCOMPARE(pdf::PDFAnnotationPropertyEditor::readColor(annotation->getColor()), QColor(Qt::magenta));
    QVERIFY(after.getDictionary()->hasKey("N"));
}

void AnnotationPropertiesTest::flagsRoundTrip()
{
    using P = pdf::PDFAnnotationProperties;
    using F = pdf::PDFAnnotation;

    AnnotationPropertiesTestSamples samples;

    // Every flag of the dialog alone and all together
    const F::Flag flags[] = { F::Invisible, F::Hidden, F::Print, F::NoZoom, F::NoRotate, F::NoView, F::ReadOnly, F::Locked, F::ToggleNoView, F::LockedContents };
    for (const F::Flag flag : flags)
    {
        P square = samples.read(samples.square);
        square.flags = F::Flags(flag);
        QVERIFY(pdf::PDFAnnotationPropertyEditor::write(&samples.builder, samples.square, square, P::Flags));
        QCOMPARE(samples.read(samples.square).flags, F::Flags(flag));
    }

    P square = samples.read(samples.square);
    square.flags = F::Flags(0x3FF);
    QVERIFY(pdf::PDFAnnotationPropertyEditor::write(&samples.builder, samples.square, square, P::Flags));
    QCOMPARE(samples.read(samples.square).flags.toInt(), 0x3FF);

    // Unknown bits are dropped
    square.flags = F::Flags(0xFFFFF);
    QVERIFY(pdf::PDFAnnotationPropertyEditor::write(&samples.builder, samples.square, square, P::Flags));
    QCOMPARE(samples.get(samples.square, "F").getInteger(), pdf::PDFInteger(0x3FF));
}

void AnnotationPropertiesTest::hostileDictionaries()
{
    using P = pdf::PDFAnnotationProperties;
    using E = pdf::PDFAnnotationPropertyEditor;

    AnnotationPropertiesTestSamples samples;

    // Hostile entries: wrong types, huge numbers, invalid names, a long text
    pdf::PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("T");
    factory << QString(5000, QLatin1Char('a'));
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Contents");
    factory << QString(100000, QLatin1Char('c'));
    factory.endDictionaryItem();
    factory.beginDictionaryItem("C");
    factory << std::initializer_list<pdf::PDFReal>{ 5.0, -3.0 };
    factory.endDictionaryItem();
    factory.beginDictionaryItem("IC");
    factory << std::initializer_list<pdf::PDFReal>{ 0.0, 1.0, 0.0, 0.0 };
    factory.endDictionaryItem();
    factory.beginDictionaryItem("CA");
    factory << 1.0e30;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Name");
    factory << pdf::WrapName("Bad/Name");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("LE");
    factory << pdf::WrapName("Nonsense");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("LL");
    factory << -1.0e12;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("LLE");
    factory << 1.0e12;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("DA");
    factory << pdf::WrapString("/ 99999 Tf");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Q");
    factory << pdf::PDFInteger(77);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("F");
    factory << pdf::PDFInteger(-1);
    factory.endDictionaryItem();
    factory.endDictionary();
    samples.merge(samples.square, factory.takeObject());

    const P square = samples.read(samples.square);
    QCOMPARE(square.type, pdf::AnnotationType::Square);
    QCOMPARE(square.author.size(), P::MAXIMUM_TITLE_LENGTH);
    QCOMPARE(square.contents.size(), P::MAXIMUM_TEXT_LENGTH);
    QVERIFY(!square.color.isValid());
    QCOMPARE(square.interiorColor, QColor::fromCmykF(0.0f, 1.0f, 0.0f, 0.0f).toRgb());
    QCOMPARE(square.opacity, 1.0);
    QVERIFY(square.iconName.isEmpty());
    QCOMPARE(square.calloutLineEnding, pdf::AnnotationLineEnding::None);
    QCOMPARE(square.leaderLineLength, -P::MAXIMUM_LEADER_LINE);
    QCOMPARE(square.leaderLineExtension, P::MAXIMUM_LEADER_LINE);
    QCOMPARE(square.freeTextStyle.fontSize, P::MAXIMUM_FONT_SIZE);
    QVERIFY(square.freeTextStyle.textAlignment.testFlag(Qt::AlignLeft));
    QCOMPARE(square.flags.toInt(), 0x3FF);

    // Hostile values given by the caller are bounded before they are written
    P hostile = square;
    hostile.author = QString(5000, QLatin1Char('x')) + QChar(u'\0');
    hostile.opacity = std::numeric_limits<pdf::PDFReal>::quiet_NaN();
    hostile.borderWidth = 1.0e9;
    hostile.borderStyle = static_cast<pdf::AnnotationBorderStyle>(77);
    hostile.dashPattern = std::vector<pdf::PDFReal>(100, 0.0);
    hostile.startLineEnding = static_cast<pdf::AnnotationLineEnding>(-5);
    hostile.endLineEnding = static_cast<pdf::AnnotationLineEnding>(500);
    hostile.iconName = QByteArray(300, 'a');
    hostile.freeTextStyle.fontFamily = QStringLiteral("   ");
    hostile.freeTextStyle.fontSize = -std::numeric_limits<pdf::PDFReal>::infinity();
    hostile.freeTextStyle.textColor = QColor();
    hostile.highlightMode = static_cast<pdf::LinkHighlightMode>(42);
    hostile.leaderLineLength = std::numeric_limits<pdf::PDFReal>::infinity();
    const P bounded = E::bound(hostile);
    QCOMPARE(bounded.author.size(), P::MAXIMUM_TITLE_LENGTH);
    QVERIFY(!bounded.author.contains(QChar(u'\0')));
    QCOMPARE(bounded.opacity, 1.0);
    QCOMPARE(bounded.borderWidth, P::MAXIMUM_BORDER_WIDTH);
    QCOMPARE(bounded.borderStyle, pdf::AnnotationBorderStyle::Solid);
    QCOMPARE(bounded.dashPattern, std::vector<pdf::PDFReal>({ 3.0 }));
    QCOMPARE(bounded.startLineEnding, pdf::AnnotationLineEnding::None);
    QCOMPARE(bounded.endLineEnding, pdf::AnnotationLineEnding::None);
    QVERIFY(bounded.iconName.isEmpty());
    QCOMPARE(bounded.freeTextStyle.fontFamily, QStringLiteral("Helvetica"));
    QCOMPARE(bounded.freeTextStyle.fontSize, 10.0);
    QCOMPARE(bounded.freeTextStyle.textColor, QColor(Qt::black));
    QCOMPARE(bounded.highlightMode, pdf::LinkHighlightMode::Invert);
    QCOMPARE(bounded.leaderLineLength, 0.0);

    // A long dash pattern keeps 16 items
    hostile.dashPattern = std::vector<pdf::PDFReal>(100, 2.0);
    QCOMPARE(E::bound(hostile).dashPattern.size(), P::MAXIMUM_DASH_ITEMS);

    // The write of the bounded values works and the document parses
    QVERIFY(E::write(&samples.builder, samples.square, hostile, P::All));
    pdf::PDFDocument document = samples.builder.build();
    QVERIFY(pdf::PDFAnnotation::parse(&document.getStorage(), samples.square));

    // Names: delimiters and control characters are refused
    P names = samples.read(samples.text);
    for (const QByteArray& name : { QByteArray("A B"), QByteArray("A(B"), QByteArray("A\x01"), QByteArray("A#20"), QByteArray() })
    {
        names.iconName = name;
        QVERIFY(E::bound(names).iconName.isEmpty());
    }
    names.iconName = "Comment";
    QCOMPARE(E::bound(names).iconName, QByteArray("Comment"));

    // Colors of the arrays
    QCOMPARE(E::readColor({ 0.5 }), QColor::fromRgbF(0.5f, 0.5f, 0.5f));
    QCOMPARE(E::readColor({ 1.0, 0.0, 0.0 }), QColor(Qt::red));
    QCOMPARE(E::readColor({ std::numeric_limits<pdf::PDFReal>::quiet_NaN(), 2.0, -1.0 }), QColor::fromRgbF(0.0f, 1.0f, 0.0f));
    QVERIFY(!E::readColor({}).isValid());
    QVERIFY(!E::readColor({ 1.0, 1.0 }).isValid());
    QVERIFY(!E::readColor({ 1.0, 1.0, 1.0, 1.0, 1.0 }).isValid());
}

void AnnotationPropertiesTest::linkProperties()
{
    using P = pdf::PDFAnnotationProperties;
    using E = pdf::PDFAnnotationPropertyEditor;

    AnnotationPropertiesTestSamples samples;

    // A web link: its address, no destination
    const P webLink = samples.read(samples.webLink);
    QCOMPARE(webLink.type, pdf::AnnotationType::Link);
    QCOMPARE(webLink.uri, QStringLiteral("https://example.com/"));
    QVERIFY(!webLink.isDestinationEditable);
    QVERIFY(!webLink.supportedFields.testFlag(P::Destination));
    QCOMPARE(webLink.highlightMode, pdf::LinkHighlightMode::Outline);
    QVERIFY(!webLink.creationDate.isValid());

    // A link to a page: its destination is changed, the description (Contents) too
    P pageLink = samples.read(samples.pageLink);
    QVERIFY(pageLink.isDestinationEditable);
    QVERIFY(pageLink.supportedFields.testFlag(P::Destination));
    QCOMPARE(pageLink.destination.getDestinationType(), pdf::DestinationType::Fit);
    QVERIFY(pageLink.uri.isEmpty());

    P changed = pageLink;
    changed.destination = pdf::PDFDestination::createXYZ(samples.page, 72.0, 700.0, std::numeric_limits<pdf::PDFReal>::quiet_NaN());
    changed.highlightMode = pdf::LinkHighlightMode::Push;
    changed.contents = QStringLiteral("Go to the chart");
    const P::Fields fields = E::getChangedFields(pageLink, changed);
    QCOMPARE(fields, P::Fields(P::Destination | P::HighlightMode | P::Contents));
    QVERIFY(E::write(&samples.builder, samples.pageLink, changed, fields));
    const P result = samples.read(samples.pageLink);
    QCOMPARE(result.destination.getDestinationType(), pdf::DestinationType::XYZ);
    QCOMPARE(result.destination.getLeft(), 72.0);
    QCOMPARE(result.destination.getTop(), 700.0);
    QVERIFY(std::isnan(result.destination.getZoom()));
    QCOMPARE(result.highlightMode, pdf::LinkHighlightMode::Push);
    QCOMPARE(result.contents, QStringLiteral("Go to the chart"));
    QVERIFY(samples.get(samples.pageLink, "Dest").isArray());

    // Every destination type is written and read back
    const pdf::PDFDestination destinations[] = {
        pdf::PDFDestination::createFitH(samples.page, 500.0), pdf::PDFDestination::createFitV(samples.page, 20.0),
        pdf::PDFDestination::createFitR(samples.page, 10.0, 700.0, 300.0, 400.0), pdf::PDFDestination::createFitB(samples.page),
        pdf::PDFDestination::createFitBH(samples.page, 600.0), pdf::PDFDestination::createFitBV(samples.page, 30.0),
        pdf::PDFDestination::createNamed("chapter")
    };
    for (const pdf::PDFDestination& destination : destinations)
    {
        changed.destination = destination;
        QVERIFY(E::write(&samples.builder, samples.pageLink, changed, P::Destination));
        QCOMPARE(samples.read(samples.pageLink).destination.getDestinationType(), destination.getDestinationType());
    }

    // Hostile destinations are bounded (huge coordinates, a negative page, a huge zoom)
    pdf::PDFDestination hostile = pdf::PDFDestination::createXYZ(pdf::PDFObjectReference(), 1.0e20, -1.0e20, 1.0e9);
    hostile.setPageIndex(-5);
    changed.destination = hostile;
    const P bounded = E::bound(changed);
    QCOMPARE(bounded.destination.getLeft(), P::MAXIMUM_COORDINATE);
    QCOMPARE(bounded.destination.getTop(), -P::MAXIMUM_COORDINATE);
    QCOMPARE(bounded.destination.getZoom(), P::MAXIMUM_ZOOM);
    QCOMPARE(bounded.destination.getPageIndex(), pdf::PDFInteger(0));
    pdf::PDFDestination unknown;
    unknown.setDestinationType(static_cast<pdf::DestinationType>(99));
    changed.destination = unknown;
    QVERIFY(!E::bound(changed).destination.isValid());

    // An invalid destination removes Dest
    changed.destination = pdf::PDFDestination();
    QVERIFY(E::write(&samples.builder, samples.pageLink, changed, P::Destination));
    QVERIFY(samples.get(samples.pageLink, "Dest").isNull());

    // A web link refuses the destination
    P web = samples.read(samples.webLink);
    web.destination = pdf::PDFDestination::createFit(samples.page);
    QVERIFY(!E::write(&samples.builder, samples.webLink, web, P::Destination));
    QVERIFY(samples.get(samples.webLink, "Dest").isNull());
}

void AnnotationPropertiesTest::helpers()
{
    using P = pdf::PDFAnnotationProperties;
    using E = pdf::PDFAnnotationPropertyEditor;

    // Every type has its fields (the markup types share the comment and the opacity)
    const pdf::AnnotationType markupTypes[] = {
        pdf::AnnotationType::Text, pdf::AnnotationType::FreeText, pdf::AnnotationType::Line, pdf::AnnotationType::Polyline,
        pdf::AnnotationType::Square, pdf::AnnotationType::Circle, pdf::AnnotationType::Polygon, pdf::AnnotationType::Ink,
        pdf::AnnotationType::Highlight, pdf::AnnotationType::Underline, pdf::AnnotationType::Squiggly, pdf::AnnotationType::StrikeOut,
        pdf::AnnotationType::Caret, pdf::AnnotationType::Stamp, pdf::AnnotationType::FileAttachment, pdf::AnnotationType::Sound,
        pdf::AnnotationType::Redact
    };
    for (const pdf::AnnotationType type : markupTypes)
    {
        const P::Fields fields = E::getSupportedFields(type, true);
        QVERIFY(fields.testFlag(P::Author));
        QVERIFY(fields.testFlag(P::Opacity));
        QVERIFY(fields.testFlag(P::Color));
    }
    QVERIFY(E::getSupportedFields(pdf::AnnotationType::FreeText, true).testFlag(P::CalloutLineEnding));
    QVERIFY(!E::getSupportedFields(pdf::AnnotationType::FreeText, false).testFlag(P::CalloutLineEnding));
    QCOMPARE(E::getSupportedFields(pdf::AnnotationType::Link, false), P::Fields(P::Contents | P::Flags | P::Color | P::Border | P::HighlightMode));

    // A text box style of the free text is kept (a family with spaces, centered)
    P properties;
    properties.freeTextStyle.fontFamily = QStringLiteral("  Courier  ");
    properties.freeTextStyle.textAlignment = Qt::AlignHCenter | Qt::AlignVCenter;
    const P bounded = E::bound(properties);
    QCOMPARE(bounded.freeTextStyle.fontFamily, QStringLiteral("Courier"));
    QCOMPARE(bounded.freeTextStyle.textAlignment, pdf::TextAlignment(Qt::AlignHCenter | Qt::AlignTop));

    // The one rule of the text box style (also of the tool and of its storage): a family
    // longer than a PDF name is Helvetica, not a cut family; left and right is left
    pdf::PDFFreeTextStyle style;
    style.fontFamily = QString(P::MAXIMUM_FONT_FAMILY_LENGTH + 1, QChar(u'F'));
    style.textAlignment = pdf::TextAlignment(Qt::AlignLeft | Qt::AlignRight);
    const pdf::PDFFreeTextStyle boundedStyle = E::boundFreeTextStyle(style);
    QCOMPARE(boundedStyle.fontFamily, QStringLiteral("Helvetica"));
    QCOMPARE(boundedStyle.textAlignment, pdf::TextAlignment(Qt::AlignLeft | Qt::AlignTop));
    style.fontFamily = QString(P::MAXIMUM_FONT_FAMILY_LENGTH, QChar(u'F'));
    style.textAlignment = pdf::TextAlignment(Qt::AlignRight | Qt::AlignBottom);
    QCOMPARE(E::boundFreeTextStyle(style).fontFamily, style.fontFamily);
    QCOMPARE(E::boundFreeTextStyle(style).textAlignment, pdf::TextAlignment(Qt::AlignRight | Qt::AlignTop));
}

void AnnotationPropertiesTest::blendMode()
{
    using P = pdf::PDFAnnotationProperties;
    using E = pdf::PDFAnnotationPropertyEditor;

    AnnotationPropertiesTestSamples samples;

    // The offered modes: Normal and the separable modes the viewer draws
    const std::vector<pdf::BlendMode> offered = E::getOfferedBlendModes();
    const std::pair<pdf::BlendMode, QByteArray> offeredNames[] = {
        { pdf::BlendMode::Normal, "Normal" }, { pdf::BlendMode::Multiply, "Multiply" }, { pdf::BlendMode::Screen, "Screen" },
        { pdf::BlendMode::Overlay, "Overlay" }, { pdf::BlendMode::Darken, "Darken" }, { pdf::BlendMode::Lighten, "Lighten" },
        { pdf::BlendMode::ColorDodge, "ColorDodge" }, { pdf::BlendMode::ColorBurn, "ColorBurn" }, { pdf::BlendMode::HardLight, "HardLight" },
        { pdf::BlendMode::SoftLight, "SoftLight" }, { pdf::BlendMode::Difference, "Difference" }, { pdf::BlendMode::Exclusion, "Exclusion" }
    };
    QCOMPARE(offered.size(), std::size(offeredNames));
    for (size_t i = 0; i < offered.size(); ++i)
    {
        QCOMPARE(offered[i], offeredNames[i].first);
        QVERIFY(E::isBlendModeOffered(offeredNames[i].second));
        QCOMPARE(E::getBlendMode(offeredNames[i].second), offeredNames[i].first);
        QCOMPARE(E::getBlendModeName(offeredNames[i].first), offeredNames[i].second);
    }
    QCOMPARE(E::getBlendMode("Unknown"), pdf::BlendMode::Invalid);
    for (const char* name : { "Hue", "Saturation", "Color", "Luminosity", "Compatible" })
    {
        QVERIFY(!E::isBlendModeOffered(name));
    }
    QVERIFY(E::isBlendModeOffered("Multiply"));
    QVERIFY(!E::isBlendModeOffered("Unknown"));
    QVERIFY(!E::isBlendModeOffered(QByteArray()));

    // Markup annotations have the blend mode, a link and a popup not
    QVERIFY(E::getSupportedFields(pdf::AnnotationType::Square, false).testFlag(P::BlendMode));
    QVERIFY(!E::getSupportedFields(pdf::AnnotationType::Link, false).testFlag(P::BlendMode));
    QVERIFY(!samples.read(samples.popup).supportedFields.testFlag(P::BlendMode));

    // Read: no entry is Normal, a highlight without BM is Multiply (O20)
    QCOMPARE(samples.read(samples.square).blendMode, QByteArray("Normal"));
    QCOMPARE(samples.read(samples.highlight).blendMode, QByteArray("Multiply"));
    pdf::PDFObjectFactory removeFactory;
    removeFactory.beginDictionary();
    removeFactory.beginDictionaryItem("BM");
    removeFactory << pdf::PDFObject();
    removeFactory.endDictionaryItem();
    removeFactory.endDictionary();
    samples.merge(samples.highlight, removeFactory.takeObject());
    QVERIFY(samples.get(samples.highlight, "BM").isNull());
    QCOMPARE(samples.read(samples.highlight).blendMode, QByteArray("Multiply"));
    QCOMPARE(P().blendMode, QByteArray("Normal"));

    // Every offered mode is written and read back; the appearance stream sets the mode
    // (readers ignoring BM of the annotation), Normal writes no graphic state
    for (const auto& [mode, name] : offeredNames)
    {
        P square = samples.read(samples.square);
        square.blendMode = name;
        QVERIFY(E::write(&samples.builder, samples.square, square, P::BlendMode));
        QCOMPARE(samples.read(samples.square).blendMode, square.blendMode);
        QCOMPARE(samples.get(samples.square, "BM").getString(), square.blendMode);
        const std::pair<QByteArray, QByteArray> appearance = samples.getAppearanceBlendMode(samples.square);
        QVERIFY(!appearance.first.isEmpty());
        QCOMPARE(appearance.first.startsWith("q /PDF4QTBlendMode gs"), mode != pdf::BlendMode::Normal);
        QCOMPARE(appearance.second, mode != pdf::BlendMode::Normal ? square.blendMode : QByteArray());
    }

    // The change mask writes only the blend mode
    P original = samples.read(samples.circle);
    P changed = original;
    changed.blendMode = "Screen";
    QCOMPARE(E::getChangedFields(original, changed), P::Fields(P::BlendMode));
    changed.contents = QStringLiteral("Not written");
    changed.opacity = 0.2;
    QVERIFY(E::write(&samples.builder, samples.circle, changed, P::BlendMode));
    P result = samples.read(samples.circle);
    QCOMPARE(result.blendMode, QByteArray("Screen"));
    QCOMPARE(result.contents, original.contents);
    QCOMPARE(result.opacity, original.opacity);
    QCOMPARE(result.color, original.color);
    QCOMPARE(result.interiorColor, original.interiorColor);

    // The appearance keeps the mode also when it is created again for another change
    changed = result;
    changed.color = QColor(Qt::darkGreen);
    QVERIFY(E::write(&samples.builder, samples.circle, changed, P::Color));
    QCOMPARE(samples.getAppearanceBlendMode(samples.circle).second, QByteArray("Screen"));

    // A highlight written with Normal stays Normal (an explicit entry)
    P highlight = samples.read(samples.highlight);
    highlight.blendMode = "Normal";
    QVERIFY(E::write(&samples.builder, samples.highlight, highlight, P::BlendMode));
    QCOMPARE(samples.read(samples.highlight).blendMode, QByteArray("Normal"));
    QCOMPARE(samples.get(samples.highlight, "BM").getString(), QByteArray("Normal"));

    // Hostile BM values of the document
    struct HostileCase
    {
        pdf::PDFObject value;
        QByteArray expected;
    };
    pdf::PDFObjectFactory arrayFactory;
    arrayFactory.beginArray();
    arrayFactory << pdf::WrapName("Unknown") << pdf::WrapName("Hue") << pdf::WrapName("Multiply");
    arrayFactory.endArray();
    pdf::PDFObjectFactory unrecognizedFactory;
    unrecognizedFactory.beginArray();
    unrecognizedFactory << pdf::WrapName("Unknown") << pdf::PDFInteger(5) << QStringLiteral("Multiply");
    unrecognizedFactory.endArray();
    pdf::PDFObjectFactory skippedFactory;
    skippedFactory.beginArray();
    skippedFactory << pdf::PDFInteger(1) << pdf::WrapName("Darken") << pdf::WrapName("Screen");
    skippedFactory.endArray();
    const pdf::PDFObjectReference nameReference = samples.builder.addObject(pdf::PDFObject::createName("Lighten"));
    const HostileCase cases[] = {
        { arrayFactory.takeObject(), QByteArray("Hue") },                     // the first recognized name wins, also one not drawn
        { unrecognizedFactory.takeObject(), QByteArray("Normal") },          // no recognized name: Normal
        { skippedFactory.takeObject(), QByteArray("Darken") },               // other items are skipped
        { pdf::PDFObject::createInteger(5), QByteArray("Normal") },          // a wrong type
        { pdf::PDFObject::createString("Multiply"), QByteArray("Normal") },  // a string is not a name
        { pdf::PDFObject::createReference(nameReference), QByteArray("Lighten") },
        { pdf::PDFObject::createName("Unknown"), QByteArray("Unknown") },    // an unknown name is shown and kept
        { pdf::PDFObject::createName("Bad#Name"), QByteArray("Normal") },    // not a valid name
        { pdf::PDFObject::createName(QByteArray(200, 'M')), QByteArray("Normal") },
        { pdf::PDFObject::createName("Compatible"), QByteArray("Compatible") },
    };
    for (const HostileCase& testCase : cases)
    {
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("BM");
        factory << testCase.value;
        factory.endDictionaryItem();
        factory.endDictionary();
        samples.merge(samples.polygon, factory.takeObject());
        QCOMPARE(samples.read(samples.polygon).blendMode, testCase.expected);
    }
    QVERIFY(E::readBlendMode(nullptr, pdf::PDFObject::createName("Multiply")).isEmpty());

    // A highlight with a hostile entry is Normal (the entry exists)
    pdf::PDFObjectFactory highlightFactory;
    highlightFactory.beginDictionary();
    highlightFactory.beginDictionaryItem("BM");
    highlightFactory << pdf::PDFInteger(7);
    highlightFactory.endDictionaryItem();
    highlightFactory.endDictionary();
    samples.merge(samples.highlight, highlightFactory.takeObject());
    QCOMPARE(samples.read(samples.highlight).blendMode, QByteArray("Normal"));

    // A mode the viewer does not draw is kept, unless another is written
    pdf::PDFObjectFactory hueFactory;
    hueFactory.beginDictionary();
    hueFactory.beginDictionaryItem("BM");
    hueFactory << pdf::WrapName("Hue");
    hueFactory.endDictionaryItem();
    hueFactory.endDictionary();
    samples.merge(samples.ink, hueFactory.takeObject());
    original = samples.read(samples.ink);
    QCOMPARE(original.blendMode, QByteArray("Hue"));
    changed = original;
    changed.contents = QStringLiteral("Changed");
    QCOMPARE(E::getChangedFields(original, changed), P::Fields(P::Contents));
    QVERIFY(E::write(&samples.builder, samples.ink, changed, E::getChangedFields(original, changed)));
    QCOMPARE(samples.get(samples.ink, "BM").getString(), QByteArray("Hue"));
    QCOMPARE(samples.read(samples.ink).contents, QStringLiteral("Changed"));

    // An unknown name and a Compatible entry write no graphic state; an unknown name given
    // by the caller is never written (alone nothing is written)
    pdf::PDFObjectFactory unknownFactory;
    unknownFactory.beginDictionary();
    unknownFactory.beginDictionaryItem("BM");
    unknownFactory << pdf::WrapName("Unknown");
    unknownFactory.endDictionaryItem();
    unknownFactory.endDictionary();
    samples.merge(samples.square, unknownFactory.takeObject());
    P square = samples.read(samples.square);
    QCOMPARE(square.blendMode, QByteArray("Unknown"));
    square.color = QColor(Qt::cyan);
    QVERIFY(E::write(&samples.builder, samples.square, square, P::Color));
    QVERIFY(samples.getAppearanceBlendMode(samples.square).second.isEmpty());
    square.blendMode = "Compatible";
    QVERIFY(E::write(&samples.builder, samples.square, square, P::BlendMode));
    QCOMPARE(samples.get(samples.square, "BM").getString(), QByteArray("Compatible"));
    QVERIFY(samples.getAppearanceBlendMode(samples.square).second.isEmpty());
    square.blendMode = "Another";
    QVERIFY(!E::write(&samples.builder, samples.square, square, P::BlendMode));
    QCOMPARE(samples.get(samples.square, "BM").getString(), QByteArray("Compatible"));
    square.contents = QStringLiteral("Only the text");
    QVERIFY(E::write(&samples.builder, samples.square, square, P::BlendMode | P::Contents));
    QCOMPARE(samples.get(samples.square, "BM").getString(), QByteArray("Compatible"));
    QCOMPARE(samples.read(samples.square).contents, QStringLiteral("Only the text"));

    // Caller values are bounded: an empty or invalid name is Normal
    square.blendMode = QByteArray();
    QCOMPARE(E::bound(square).blendMode, QByteArray("Normal"));
    square.blendMode = "Multi ply";
    QCOMPARE(E::bound(square).blendMode, QByteArray("Normal"));

    // The built document parses the written mode
    square.blendMode = "Difference";
    QVERIFY(E::write(&samples.builder, samples.square, square, P::BlendMode));
    pdf::PDFDocument document = samples.builder.build();
    const pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(&document.getStorage(), samples.square);
    QVERIFY(annotation);
    QCOMPARE(annotation->getBlendMode(), pdf::BlendMode::Difference);
}

QTEST_MAIN(AnnotationPropertiesTest)
#include "tst_annotationpropertiestest.moc"
