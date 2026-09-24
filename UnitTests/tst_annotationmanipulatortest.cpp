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

#include "pdfannotationmanipulator.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfdocumentreader.h"
#include "pdffile.h"

#include <QtTest>
#include <QBuffer>

#include <set>

using namespace pdf;

/// Tests of the annotation manipulator - transformation of the annotation
/// geometry, copying and moving of annotations and the serialization used
/// by the clipboard and by the drag and drop between documents.
class AnnotationManipulatorTest : public QObject
{
    Q_OBJECT

private slots:
    void geometryKinds();
    void axisAlignment();
    void translateLineKeepsAppearance();
    void scaleLineRegeneratesAppearance();
    void rotateLine();
    void translateSquareKeepsAppearance();
    void scaleSquareScalesDifferences();
    void rotateSquareByRightAngleSwapsSize();
    void rotateSquareByArbitraryAngleMovesOnly();
    void flipSquareKeepsRectangle();
    void scaleIconKeepsSize();
    void rotateStampUsesFormMatrix();
    void rotateStretchedStamp();
    void flipStampUsesFormMatrix();
    void rotateHighlightQuadPoints();
    void translateInkList();
    void scaleFreeTextCallout();
    void popupFollowsParent();
    void unsupportedAnnotationIsNotTransformed();
    void transformedOutline();
    void editablePoints();
    void setPolygonPoints();
    void setLinePoints();
    void setFreeTextCalloutPoints();
    void copyAnnotationSharesAppearanceAndCopiesPopup();
    void copyAnnotationDropsReplyLinks();
    void moveAnnotationToPage();
    void serializeAndInsert();
    void serializeRejectsInvalidData();
    void malformedGeometryIsIgnored();
    void rectangleValidation();
    void rotateFreeTextWithCallout();
    void appearanceWithoutStreams();
    void appearanceWithStates();
    void popupEdgeCases();
    void editablePointsOfMalformedAnnotations();
    void setFreeTextCalloutWithoutTextRectangle();
    void copyAndMoveArgumentValidation();
    void moveLastAnnotationOfPage();
    void serializeSpecialCases();
    void deserializeSpecialCases();
    void insertArgumentValidation();
    void capabilities();
    void boxRotationRules();
    void transformPath();
    void editablePointsOfPath();
    void mirrorLineWithLeaderLines();
    void lineParametersSpecialCases();
    void measurementLine();
    void measurementNumberFormats();
    void measurementPolygon();
    void reviewMeasurementAmbiguousArea();
    void reviewMeasurementPreservesCommentNumber();
    void reviewMeasurementUsesPath();
    void measurementField();
    void measurementFieldOfPolygon();
    void measurementOfEditedShape();
    void measurementOfPath();
    void setRectangle();
    void addedAndReplacedParts();
    void erasedInk();
    void inkPoints();
    void addedReply();
    void replacedFileAttachment();
    void measurementAngle();
    void measurementPointCount();
    void measurementIsNotGuessed();
    void findAnnotationPage();
    void findAnnotationPageInDamagedTree();
    void replies();
    void copyThread();
    void moveThread();
    void serializeThread();
    void appearanceWithoutPageEntry();
    void effectiveTransform();
    void markedRegionEnds();
    void markedRegionEndsSpecialCases();
    void removableParts();
    void freeTextRectangle();
    void freeTextCalloutLine();

private:
    /// Text stored under the key of the annotation dictionary
    static QString text(const PDFDocument& document, PDFObjectReference annotation, const char* key);

    /// Creates a number format dictionary
    static PDFObject numberFormat(const char* unit, PDFReal factor);

    /// Creates a measure dictionary. Zero factor means, that the entry is missing.
    static PDFObject measure(PDFReal x, PDFReal y, PDFReal distance, PDFReal area, PDFReal angle);

    /// Creates an array of number formats (pairs of the unit and the conversion factor)
    static PDFObject numberFormats(std::initializer_list<std::pair<const char*, PDFReal>> formats);

    /// Creates a measure dictionary with the number format arrays
    static PDFObject measureOf(std::initializer_list<std::pair<const char*, PDFObject>> formats);

    /// Creates a path (array of arrays of numbers)
    static PDFObject pathArray(std::initializer_list<std::vector<PDFReal>> items);

    /// Turns the annotation into a measurement
    static void setMeasurement(PDFDocumentBuilder& builder, PDFObjectReference annotation, const char* intent, const QString& contents, PDFObject measure);

    /// Applies the transformation and returns the contents of the annotation
    static QString transformedContents(const PDFDocument& document, PDFObjectReference annotation, const QTransform& transform);

    /// Creates a dimension line (from the point 50, 150 to the point 150, 150)
    /// with the contents and returns its contents after the transformation.
    static QString transformedLineContents(const QString& contents, PDFObject measure, const QTransform& transform);

    /// Merges a single entry into the dictionary of the object
    static void setEntry(PDFDocumentBuilder& builder, PDFObjectReference reference, const char* key, PDFObject value);

    /// Creates an array of numbers
    static PDFObject numberArray(std::initializer_list<PDFReal> numbers);

    /// Writes the document into a byte array
    static QByteArray write(const PDFDocument& document);

    /// Rectangle of the annotation
    static QRectF rectangle(const PDFDocument& document, PDFObjectReference annotation);

    /// Number array stored under the key of the annotation dictionary
    static std::vector<PDFReal> numbers(const PDFDocument& document, PDFObjectReference annotation, const char* key);

    /// Number stored under the key of the annotation dictionary
    static PDFReal number(const PDFDocument& document, PDFObjectReference annotation, const char* key);

    /// Object stored under the key of the annotation dictionary
    static PDFObject entry(const PDFDocument& document, PDFObjectReference annotation, const char* key);

    /// Reference of the normal appearance stream of the annotation
    static PDFObjectReference normalAppearance(const PDFDocument& document, PDFObjectReference annotation);

    /// Annotations of the page (references)
    static std::vector<PDFObjectReference> pageAnnotations(const PDFDocument& document, size_t pageIndex);

    /// Applies the transformation to the annotation and returns the modified document
    static PDFDocument transform(const PDFDocument& document, PDFObjectReference annotation, const QTransform& transform, bool expectedResult = true);

    static bool fuzzyCompare(const QRectF& left, const QRectF& right, qreal tolerance = 0.01);
    static bool fuzzyCompare(const std::vector<PDFReal>& left, const std::vector<PDFReal>& right, qreal tolerance = 0.01);
    static QTransform rotationAroundCenter(const QRectF& rectangle, qreal degrees);
};

void AnnotationManipulatorTest::setEntry(PDFDocumentBuilder& builder, PDFObjectReference reference, const char* key, PDFObject value)
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem(key);
    factory << value;
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(reference, factory.takeObject());
}

PDFObject AnnotationManipulatorTest::numberArray(std::initializer_list<PDFReal> numbers)
{
    PDFObjectFactory factory;
    factory << numbers;
    return factory.takeObject();
}

QByteArray AnnotationManipulatorTest::write(const PDFDocument& document)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    PDFDocumentWriter writer(nullptr);
    writer.write(&buffer, &document);
    buffer.close();
    return buffer.data();
}

QRectF AnnotationManipulatorTest::rectangle(const PDFDocument& document, PDFObjectReference annotation)
{
    PDFDocumentDataLoaderDecorator loader(&document);
    const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotation));
    return dictionary ? loader.readRectangle(dictionary->get("Rect"), QRectF()).normalized() : QRectF();
}

std::vector<PDFReal> AnnotationManipulatorTest::numbers(const PDFDocument& document, PDFObjectReference annotation, const char* key)
{
    PDFDocumentDataLoaderDecorator loader(&document);
    const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotation));
    return dictionary ? loader.readNumberArrayFromDictionary(dictionary, key) : std::vector<PDFReal>();
}

PDFReal AnnotationManipulatorTest::number(const PDFDocument& document, PDFObjectReference annotation, const char* key)
{
    PDFDocumentDataLoaderDecorator loader(&document);
    const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotation));
    return dictionary ? loader.readNumberFromDictionary(dictionary, key, 0.0) : 0.0;
}

PDFObject AnnotationManipulatorTest::entry(const PDFDocument& document, PDFObjectReference annotation, const char* key)
{
    const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotation));
    return dictionary ? dictionary->get(key) : PDFObject();
}

PDFObjectReference AnnotationManipulatorTest::normalAppearance(const PDFDocument& document, PDFObjectReference annotation)
{
    const PDFDictionary* appearance = document.getDictionaryFromObject(entry(document, annotation, "AP"));
    if (!appearance)
    {
        return PDFObjectReference();
    }

    const PDFObject& normal = appearance->get("N");
    return normal.isReference() ? normal.getReference() : PDFObjectReference();
}

std::vector<PDFObjectReference> AnnotationManipulatorTest::pageAnnotations(const PDFDocument& document, size_t pageIndex)
{
    return document.getCatalog()->getPage(pageIndex)->getAnnotations();
}

PDFDocument AnnotationManipulatorTest::transform(const PDFDocument& document, PDFObjectReference annotation, const QTransform& transform, bool expectedResult)
{
    PDFDocumentBuilder builder(&document);
    const bool result = PDFAnnotationManipulator::transformAnnotation(&builder, annotation, transform);
    if (result != expectedResult)
    {
        qWarning() << "Unexpected result of the transformation:" << result;
    }
    return builder.build();
}

bool AnnotationManipulatorTest::fuzzyCompare(const QRectF& left, const QRectF& right, qreal tolerance)
{
    const bool result = std::abs(left.left() - right.left()) <= tolerance &&
                        std::abs(left.top() - right.top()) <= tolerance &&
                        std::abs(left.width() - right.width()) <= tolerance &&
                        std::abs(left.height() - right.height()) <= tolerance;
    if (!result)
    {
        qWarning() << "Rectangles differ:" << left << right;
    }
    return result;
}

bool AnnotationManipulatorTest::fuzzyCompare(const std::vector<PDFReal>& left, const std::vector<PDFReal>& right, qreal tolerance)
{
    if (left.size() != right.size())
    {
        qWarning() << "Sizes differ:" << left.size() << right.size();
        return false;
    }

    for (size_t i = 0; i < left.size(); ++i)
    {
        if (std::abs(left[i] - right[i]) > tolerance)
        {
            qWarning() << "Numbers differ at" << i << ":" << left[i] << right[i];
            return false;
        }
    }

    return true;
}

QTransform AnnotationManipulatorTest::rotationAroundCenter(const QRectF& rectangle, qreal degrees)
{
    const QPointF center = rectangle.center();
    return QTransform::fromTranslate(-center.x(), -center.y()) * QTransform().rotate(degrees) * QTransform::fromTranslate(center.x(), center.y());
}

void AnnotationManipulatorTest::geometryKinds()
{
    using Kind = PDFAnnotationManipulator::GeometryKind;

    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Line), Kind::Points);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Polyline), Kind::Points);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Underline), Kind::Points);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Squiggly), Kind::Points);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::StrikeOut), Kind::Points);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Caret), Kind::Box);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Watermark), Kind::Appearance);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::PrinterMark), Kind::Appearance);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Screen), Kind::NotSupported);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Polygon), Kind::Points);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Ink), Kind::Points);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Highlight), Kind::Points);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Redact), Kind::Points);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Square), Kind::Box);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Circle), Kind::Box);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::FreeText), Kind::Box);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Text), Kind::Icon);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::FileAttachment), Kind::Icon);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Stamp), Kind::Appearance);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Link), Kind::NotSupported);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Widget), Kind::NotSupported);
    QCOMPARE(PDFAnnotationManipulator::getGeometryKind(AnnotationType::Popup), Kind::NotSupported);

    QVERIFY(PDFAnnotationManipulator::isTransformable(AnnotationType::Stamp));
    QVERIFY(!PDFAnnotationManipulator::isTransformable(AnnotationType::Link));
}

void AnnotationManipulatorTest::axisAlignment()
{
    QVERIFY(PDFAnnotationManipulator::isAxisAligned(QTransform()));
    QVERIFY(PDFAnnotationManipulator::isAxisAligned(QTransform::fromTranslate(3.0, 4.0)));
    QVERIFY(PDFAnnotationManipulator::isAxisAligned(QTransform::fromScale(2.0, -1.0)));
    QVERIFY(PDFAnnotationManipulator::isAxisAligned(QTransform().rotate(90.0)));
    QVERIFY(PDFAnnotationManipulator::isAxisAligned(QTransform().rotate(270.0)));
    QVERIFY(!PDFAnnotationManipulator::isAxisAligned(QTransform().rotate(45.0)));
    QVERIFY(!PDFAnnotationManipulator::isAxisAligned(QTransform().shear(1.0, 0.0)));

    // Each element of the linear part decides on its own
    QVERIFY(!PDFAnnotationManipulator::isAxisAligned(QTransform(1.0, 0.5, 0.0, 1.0, 0.0, 0.0)));
    QVERIFY(!PDFAnnotationManipulator::isAxisAligned(QTransform(1.0, 0.0, 0.5, 1.0, 0.0, 0.0)));
    QVERIFY(!PDFAnnotationManipulator::isAxisAligned(QTransform(0.0, 1.0, 1.0, 0.5, 0.0, 0.0)));
    QVERIFY(!PDFAnnotationManipulator::isAxisAligned(QTransform(0.5, 1.0, 1.0, 0.0, 0.0, 0.0)));
    QVERIFY(PDFAnnotationManipulator::isAxisAligned(QTransform(0.0, 2.0, -3.0, 0.0, 5.0, 6.0)));

    QVERIFY(!PDFAnnotationManipulator::isPositiveAxisAligned(QTransform(1.0, 0.5, 0.0, 1.0, 0.0, 0.0)));
    QVERIFY(!PDFAnnotationManipulator::isPositiveAxisAligned(QTransform(1.0, 0.0, 0.5, 1.0, 0.0, 0.0)));
    QVERIFY(!PDFAnnotationManipulator::isPositiveAxisAligned(QTransform::fromScale(1.0, -1.0)));
    QVERIFY(!PDFAnnotationManipulator::isPositiveAxisAligned(QTransform::fromScale(0.0, 1.0)));
    QVERIFY(PDFAnnotationManipulator::isPositiveAxisAligned(QTransform::fromTranslate(3.0, 4.0)));

    QVERIFY(PDFAnnotationManipulator::isPositiveAxisAligned(QTransform()));
    QVERIFY(PDFAnnotationManipulator::isPositiveAxisAligned(QTransform::fromScale(2.0, 0.5)));
    QVERIFY(!PDFAnnotationManipulator::isPositiveAxisAligned(QTransform::fromScale(-1.0, 1.0)));
    QVERIFY(!PDFAnnotationManipulator::isPositiveAxisAligned(QTransform().rotate(90.0)));
    QVERIFY(!PDFAnnotationManipulator::isPositiveAxisAligned(QTransform().rotate(30.0)));
}

void AnnotationManipulatorTest::translateLineKeepsAppearance()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 200, 100), QPointF(10, 10), QPointF(110, 60), 2.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);
    const PDFDocument document = builder.build();
    const QRectF originalRectangle = rectangle(document, line);
    const PDFObjectReference originalAppearance = normalAppearance(document, line);
    QVERIFY(originalAppearance.isValid());

    const PDFDocument modified = transform(document, line, QTransform::fromTranslate(5.0, 7.0));
    QVERIFY(fuzzyCompare(numbers(modified, line, "L"), { 15.0, 17.0, 115.0, 67.0 }));
    QVERIFY(fuzzyCompare(rectangle(modified, line), originalRectangle.translated(5.0, 7.0)));

    // Pure translation must not touch the appearance stream
    QCOMPARE(normalAppearance(modified, line), originalAppearance);
}

void AnnotationManipulatorTest::scaleLineRegeneratesAppearance()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 200, 100), QPointF(10, 10), QPointF(110, 60), 2.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None,
                                                                 20.0, 5.0, 3.0, false, false);
    const PDFDocument document = builder.build();
    const PDFObjectReference originalAppearance = normalAppearance(document, line);

    const PDFDocument modified = transform(document, line, QTransform::fromScale(2.0, 2.0));
    QVERIFY(fuzzyCompare(numbers(modified, line, "L"), { 20.0, 20.0, 220.0, 120.0 }));

    // Leader line lengths are scaled by the mean scale factor
    QVERIFY(std::abs(number(modified, line, "LL") - 40.0) < 0.01);
    QVERIFY(std::abs(number(modified, line, "LLO") - 10.0) < 0.01);
    QVERIFY(std::abs(number(modified, line, "LLE") - 6.0) < 0.01);

    // The appearance is regenerated and the rectangle covers the new line
    QVERIFY(normalAppearance(modified, line).isValid());
    QVERIFY(normalAppearance(modified, line) != originalAppearance);
    const QRectF newRectangle = rectangle(modified, line);
    QVERIFY(newRectangle.contains(QPointF(20.0, 20.0)));
    QVERIFY(newRectangle.contains(QPointF(220.0, 120.0)));
}

void AnnotationManipulatorTest::rotateLine()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 200, 100), QPointF(10, 10), QPointF(110, 10), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFDocument document = builder.build();

    // Rotation of the line by 30 degrees around its start point
    const QTransform rotation = QTransform::fromTranslate(-10.0, -10.0) * QTransform().rotate(30.0) * QTransform::fromTranslate(10.0, 10.0);
    const PDFDocument modified = transform(document, line, rotation);

    const QPointF end = rotation.map(QPointF(110.0, 10.0));
    QVERIFY(fuzzyCompare(numbers(modified, line, "L"), { 10.0, 10.0, end.x(), end.y() }));
    QVERIFY(rectangle(modified, line).contains(end));
}

void AnnotationManipulatorTest::translateSquareKeepsAppearance()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();
    const PDFObjectReference originalAppearance = normalAppearance(document, square);
    QVERIFY(originalAppearance.isValid());

    const PDFDocument modified = transform(document, square, QTransform::fromTranslate(-20.0, 15.0));
    QVERIFY(fuzzyCompare(rectangle(modified, square), QRectF(80, 115, 50, 30)));
    QCOMPARE(normalAppearance(modified, square), originalAppearance);
}

void AnnotationManipulatorTest::scaleSquareScalesDifferences()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");

    // Rectangle differences (padding between the rectangle and the drawn square)
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("RD");
    factory << std::initializer_list<PDFReal>{ 1.0, 2.0, 3.0, 4.0 };
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(square, factory.takeObject());

    const PDFDocument document = builder.build();
    const PDFObjectReference originalAppearance = normalAppearance(document, square);

    const PDFDocument modified = transform(document, square, QTransform::fromScale(2.0, 3.0));
    QVERIFY(fuzzyCompare(rectangle(modified, square), QRectF(200, 300, 100, 90)));
    QVERIFY(fuzzyCompare(numbers(modified, square, "RD"), { 2.0, 6.0, 6.0, 12.0 }));

    // Shape has changed, so the appearance stream is regenerated
    QVERIFY(normalAppearance(modified, square).isValid());
    QVERIFY(normalAppearance(modified, square) != originalAppearance);
}

void AnnotationManipulatorTest::rotateSquareByRightAngleSwapsSize()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    const QRectF original = rectangle(document, square);
    const PDFDocument modified = transform(document, square, rotationAroundCenter(original, 90.0));

    QRectF expected(0, 0, original.height(), original.width());
    expected.moveCenter(original.center());
    QVERIFY(fuzzyCompare(rectangle(modified, square), expected));
}

void AnnotationManipulatorTest::rotateSquareByArbitraryAngleMovesOnly()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();
    const PDFObjectReference originalAppearance = normalAppearance(document, square);

    // Rotation around the origin of the page - the square keeps its size and its
    // appearance, only the center is rotated
    const QRectF original = rectangle(document, square);
    const QTransform rotation = QTransform().rotate(45.0);
    const PDFDocument modified = transform(document, square, rotation);

    QRectF expected = original;
    expected.moveCenter(rotation.map(original.center()));
    QVERIFY(fuzzyCompare(rectangle(modified, square), expected));
    QCOMPARE(normalAppearance(modified, square), originalAppearance);
}

void AnnotationManipulatorTest::flipSquareKeepsRectangle()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    const QRectF original = rectangle(document, square);
    const QPointF center = original.center();
    const QTransform flip = QTransform::fromTranslate(-center.x(), -center.y()) * QTransform::fromScale(-1.0, 1.0) * QTransform::fromTranslate(center.x(), center.y());
    const PDFDocument modified = transform(document, square, flip);
    QVERIFY(fuzzyCompare(rectangle(modified, square), original));
}

void AnnotationManipulatorTest::scaleIconKeepsSize()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Comment, "Title", "Subject", "Contents", false);
    const PDFDocument document = builder.build();
    const PDFObjectReference originalAppearance = normalAppearance(document, note);

    const QRectF original = rectangle(document, note);
    const PDFDocument modified = transform(document, note, QTransform::fromScale(2.0, 2.0));

    QRectF expected = original;
    expected.moveCenter(original.center() * 2.0);
    QVERIFY(fuzzyCompare(rectangle(modified, note), expected));
    QCOMPARE(normalAppearance(modified, note), originalAppearance);
}

void AnnotationManipulatorTest::rotateStampUsesFormMatrix()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference stamp = builder.createAnnotationStamp(page, QRectF(100, 100, 200, 50), Stamp::Approved, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    const QRectF original = rectangle(document, stamp);
    QVERIFY(original.isValid());
    const PDFObjectReference originalAppearance = normalAppearance(document, stamp);
    QVERIFY(originalAppearance.isValid());

    const PDFDocument modified = transform(document, stamp, rotationAroundCenter(original, 90.0));

    // Rectangle is the bounding rectangle of the rotated stamp
    QRectF expected(0, 0, original.height(), original.width());
    expected.moveCenter(original.center());
    QVERIFY(fuzzyCompare(rectangle(modified, stamp), expected));

    // The appearance stream is not modified in place - a new stream with the
    // rotation matrix is created, the original stream stays without matrix
    const PDFObjectReference newAppearance = normalAppearance(modified, stamp);
    QVERIFY(newAppearance.isValid());
    QVERIFY(newAppearance != originalAppearance);

    PDFDocumentDataLoaderDecorator loader(&modified);
    const PDFObject& newStream = modified.getObjectByReference(newAppearance);
    QVERIFY(newStream.isStream());
    const std::vector<PDFReal> matrix = loader.readNumberArrayFromDictionary(newStream.getStream()->getDictionary(), "Matrix");
    QCOMPARE(matrix.size(), size_t(6));
    QVERIFY(fuzzyCompare(std::vector<PDFReal>{ matrix[0], matrix[1], matrix[2], matrix[3] }, std::vector<PDFReal>{ 0.0, 1.0, -1.0, 0.0 }));

    const PDFObject& originalStream = modified.getObjectByReference(originalAppearance);
    QVERIFY(originalStream.isStream());
    QVERIFY(!originalStream.getStream()->getDictionary()->hasKey("Matrix"));

    // Second rotation restores the original size and the matrix is composed
    const PDFDocument rotatedTwice = transform(modified, stamp, rotationAroundCenter(rectangle(modified, stamp), 90.0));
    QVERIFY(fuzzyCompare(rectangle(rotatedTwice, stamp), original));
    const PDFObject& twiceStream = rotatedTwice.getObjectByReference(normalAppearance(rotatedTwice, stamp));
    const std::vector<PDFReal> twiceMatrix = loader.readNumberArrayFromDictionary(twiceStream.getStream()->getDictionary(), "Matrix");
    QCOMPARE(twiceMatrix.size(), size_t(6));
    QVERIFY(fuzzyCompare(std::vector<PDFReal>{ twiceMatrix[0], twiceMatrix[1], twiceMatrix[2], twiceMatrix[3] }, std::vector<PDFReal>{ -1.0, 0.0, 0.0, -1.0 }));
}

void AnnotationManipulatorTest::rotateStretchedStamp()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference stamp = builder.createAnnotationStamp(page, QRectF(100, 100, 200, 50), Stamp::Draft, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    // Stretch the stamp non-uniformly (the form is fitted into the rectangle by the viewer),
    // then rotate it. The rotated stamp must keep the stretched proportions - the
    // scaling of the fit must be folded into the form matrix.
    const PDFDocument stretched = transform(document, stamp, QTransform::fromScale(3.0, 1.0));
    const QRectF stretchedRectangle = rectangle(stretched, stamp);
    QVERIFY(fuzzyCompare(stretchedRectangle, QRectF(rectangle(document, stamp).left() * 3.0, rectangle(document, stamp).top(), rectangle(document, stamp).width() * 3.0, rectangle(document, stamp).height())));

    const PDFDocument rotated = transform(stretched, stamp, rotationAroundCenter(stretchedRectangle, 90.0));
    QRectF expected(0, 0, stretchedRectangle.height(), stretchedRectangle.width());
    expected.moveCenter(stretchedRectangle.center());
    QVERIFY(fuzzyCompare(rectangle(rotated, stamp), expected));

    // Matrix maps the form bounding box onto a box of the same size as the rectangle
    PDFDocumentDataLoaderDecorator loader(&rotated);
    const PDFObject& stream = rotated.getObjectByReference(normalAppearance(rotated, stamp));
    QVERIFY(stream.isStream());
    const std::vector<PDFReal> matrixNumbers = loader.readNumberArrayFromDictionary(stream.getStream()->getDictionary(), "Matrix");
    QCOMPARE(matrixNumbers.size(), size_t(6));
    const QTransform matrix(matrixNumbers[0], matrixNumbers[1], matrixNumbers[2], matrixNumbers[3], matrixNumbers[4], matrixNumbers[5]);
    const QRectF boundingBox = loader.readRectangle(stream.getStream()->getDictionary()->get("BBox"), QRectF());
    const QRectF transformedBox = matrix.map(QPolygonF(boundingBox)).boundingRect();
    QVERIFY(std::abs(transformedBox.width() - expected.width()) < 0.01);
    QVERIFY(std::abs(transformedBox.height() - expected.height()) < 0.01);
}

void AnnotationManipulatorTest::flipStampUsesFormMatrix()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference stamp = builder.createAnnotationStamp(page, QRectF(100, 100, 200, 50), Stamp::Final, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    const QRectF original = rectangle(document, stamp);
    const QPointF center = original.center();
    const QTransform flip = QTransform::fromTranslate(-center.x(), -center.y()) * QTransform::fromScale(-1.0, 1.0) * QTransform::fromTranslate(center.x(), center.y());
    const PDFDocument modified = transform(document, stamp, flip);

    QVERIFY(fuzzyCompare(rectangle(modified, stamp), original));

    PDFDocumentDataLoaderDecorator loader(&modified);
    const PDFObject& stream = modified.getObjectByReference(normalAppearance(modified, stamp));
    QVERIFY(stream.isStream());
    const std::vector<PDFReal> matrix = loader.readNumberArrayFromDictionary(stream.getStream()->getDictionary(), "Matrix");
    QCOMPARE(matrix.size(), size_t(6));
    QVERIFY(fuzzyCompare(std::vector<PDFReal>{ matrix[0], matrix[1], matrix[2], matrix[3] }, std::vector<PDFReal>{ -1.0, 0.0, 0.0, 1.0 }));
}

void AnnotationManipulatorTest::rotateHighlightQuadPoints()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF quadrilateral = { QPointF(10, 30), QPointF(60, 30), QPointF(10, 10), QPointF(60, 10) };
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, quadrilateral, Qt::yellow);
    const PDFDocument document = builder.build();

    const PDFDocument modified = transform(document, highlight, QTransform().rotate(180.0));
    QVERIFY(fuzzyCompare(numbers(modified, highlight, "QuadPoints"), { -10.0, -30.0, -60.0, -30.0, -10.0, -10.0, -60.0, -10.0 }));

    const QRectF newRectangle = rectangle(modified, highlight);
    QVERIFY(newRectangle.contains(QPointF(-60.0, -30.0)));
    QVERIFY(newRectangle.contains(QPointF(-10.0, -10.0)));
}

void AnnotationManipulatorTest::translateInkList()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF inkPoints = { QPointF(10, 10), QPointF(20, 30), QPointF(40, 15) };
    const PDFObjectReference ink = builder.createAnnotationInk(page, inkPoints, 2.0, Qt::red, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    const PDFDocument modified = transform(document, ink, QTransform::fromTranslate(1.0, 2.0));

    PDFDocumentDataLoaderDecorator loader(&modified);
    const PDFObject inkList = modified.getObject(entry(modified, ink, "InkList"));
    QVERIFY(inkList.isArray());
    QCOMPARE(inkList.getArray()->getCount(), size_t(1));
    QVERIFY(fuzzyCompare(loader.readNumberArray(inkList.getArray()->getItem(0)), { 11.0, 12.0, 21.0, 32.0, 41.0, 17.0 }));
}

void AnnotationManipulatorTest::scaleFreeTextCallout()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, QRectF(100, 100, 100, 50), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                         Qt::AlignLeft, QPointF(20, 20), QPointF(100, 120), AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);
    const PDFDocument document = builder.build();

    const std::vector<PDFReal> originalDifferences = numbers(document, freeText, "RD");
    QCOMPARE(originalDifferences.size(), size_t(4));

    const PDFDocument modified = transform(document, freeText, QTransform::fromScale(2.0, 2.0));
    QVERIFY(fuzzyCompare(numbers(modified, freeText, "CL"), { 40.0, 40.0, 200.0, 240.0 }));

    const std::vector<PDFReal> differences = numbers(modified, freeText, "RD");
    QCOMPARE(differences.size(), size_t(4));
    for (size_t i = 0; i < 4; ++i)
    {
        QVERIFY(std::abs(differences[i] - originalDifferences[i] * 2.0) < 0.01);
    }
}

void AnnotationManipulatorTest::popupFollowsParent()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFDocument document = builder.build();

    const PDFObject popupObject = entry(document, note, "Popup");
    QVERIFY(popupObject.isReference());
    const PDFObjectReference popup = popupObject.getReference();
    const QRectF originalPopupRectangle = rectangle(document, popup);
    QVERIFY(originalPopupRectangle.isValid());

    const PDFDocument modified = transform(document, note, QTransform::fromTranslate(10.0, 20.0));
    QVERIFY(fuzzyCompare(rectangle(modified, popup), originalPopupRectangle.translated(10.0, 20.0)));
}

void AnnotationManipulatorTest::unsupportedAnnotationIsNotTransformed()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFDocument document = builder.build();

    // Popup annotations are transformed only through their parents
    const PDFObjectReference popup = entry(document, note, "Popup").getReference();
    const QRectF originalPopupRectangle = rectangle(document, popup);
    const PDFDocument modified = transform(document, popup, QTransform::fromTranslate(10.0, 20.0), false);
    QVERIFY(fuzzyCompare(rectangle(modified, popup), originalPopupRectangle));

    // Invalid references are rejected
    PDFDocumentBuilder invalidBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::transformAnnotation(&invalidBuilder, PDFObjectReference(), QTransform::fromTranslate(1.0, 1.0)));
    QVERIFY(!PDFAnnotationManipulator::transformAnnotation(&invalidBuilder, page, QTransform::fromTranslate(1.0, 1.0)));
    QVERIFY(!PDFAnnotationManipulator::transformAnnotation(nullptr, note, QTransform::fromTranslate(1.0, 1.0)));
}

void AnnotationManipulatorTest::transformedOutline()
{
    const QRectF rectangle(10, 10, 40, 20);
    const QTransform rotation = QTransform().rotate(30.0);
    const QTransform scale = QTransform::fromScale(2.0, 2.0);

    // Points and appearance annotations follow the transformation exactly
    QPolygonF outline = PDFAnnotationManipulator::getTransformedOutline(AnnotationType::Line, rectangle, rotation);
    QVERIFY(fuzzyCompare(outline.boundingRect(), rotation.map(QPolygonF(rectangle)).boundingRect()));
    outline = PDFAnnotationManipulator::getTransformedOutline(AnnotationType::Stamp, rectangle, scale);
    QVERIFY(fuzzyCompare(outline.boundingRect(), QRectF(20, 20, 80, 40)));

    // Boxes are only moved by a general rotation, but scaled by scaling
    outline = PDFAnnotationManipulator::getTransformedOutline(AnnotationType::Square, rectangle, rotation);
    QRectF expected = rectangle;
    expected.moveCenter(rotation.map(rectangle.center()));
    QVERIFY(fuzzyCompare(outline.boundingRect(), expected));
    outline = PDFAnnotationManipulator::getTransformedOutline(AnnotationType::Square, rectangle, scale);
    QVERIFY(fuzzyCompare(outline.boundingRect(), QRectF(20, 20, 80, 40)));

    // Icons keep their size
    outline = PDFAnnotationManipulator::getTransformedOutline(AnnotationType::Text, rectangle, scale);
    expected = rectangle;
    expected.moveCenter(rectangle.center() * 2.0);
    QVERIFY(fuzzyCompare(outline.boundingRect(), expected));

    // Unsupported annotations are not transformed at all
    outline = PDFAnnotationManipulator::getTransformedOutline(AnnotationType::Link, rectangle, scale);
    QVERIFY(fuzzyCompare(outline.boundingRect(), rectangle));
}

void AnnotationManipulatorTest::editablePoints()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 200, 100), QPointF(10, 10), QPointF(110, 60), 2.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, triangle, 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference polyline = builder.createAnnotationPolyline(page, triangle, 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents",
                                                                         AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, QRectF(100, 100, 100, 50), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                         Qt::AlignLeft, QPointF(20, 20), QPointF(60, 60), QPointF(110, 120), AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    auto getPoints = [&document](PDFObjectReference annotation)
    {
        return PDFAnnotationManipulator::getEditablePoints(PDFAnnotation::parse(&document.getStorage(), annotation).data());
    };

    PDFAnnotationManipulator::EditablePoints points = getPoints(line);
    QVERIFY(points.isValid());
    QVERIFY(points.points == (std::vector<QPointF>{ QPointF(10, 10), QPointF(110, 60) }));
    QVERIFY(!points.isClosed);
    QVERIFY(!points.isCalloutLine);
    QVERIFY(!points.canInsertPoint());
    QVERIFY(!points.canRemovePoint(0));
    QVERIFY(!points.canRemovePoint(1));

    points = getPoints(polygon);
    QVERIFY(points.isValid());
    QCOMPARE(points.points.size(), size_t(3));
    QVERIFY(points.isClosed);
    QVERIFY(points.canInsertPoint());
    QVERIFY(!points.canRemovePoint(0));   // Triangle cannot lose a point

    points = getPoints(polyline);
    QVERIFY(points.isValid());
    QVERIFY(!points.isClosed);
    QVERIFY(points.canInsertPoint());
    QVERIFY(points.canRemovePoint(0));
    QVERIFY(points.canRemovePoint(2));
    QVERIFY(!points.canRemovePoint(3));   // There is no such point

    // Callout line with a knee - it cannot get more points, only the knee can be removed
    points = getPoints(freeText);
    QVERIFY(points.isValid());
    QVERIFY(points.isCalloutLine);
    QVERIFY(points.points == (std::vector<QPointF>{ QPointF(20, 20), QPointF(60, 60), QPointF(110, 120) }));
    QVERIFY(!points.canInsertPoint());
    QVERIFY(!points.canRemovePoint(0));
    QVERIFY(points.canRemovePoint(1));
    QVERIFY(!points.canRemovePoint(2));

    // Callout line without a knee - the knee can be inserted
    points.points.erase(std::next(points.points.begin()));
    QVERIFY(points.canInsertPoint());
    QVERIFY(!points.canRemovePoint(1));

    // Invalid points
    QVERIFY(!PDFAnnotationManipulator::EditablePoints().canInsertPoint());

    QVERIFY(!getPoints(square).isValid());
    QVERIFY(!PDFAnnotationManipulator::getEditablePoints(nullptr).isValid());
}

void AnnotationManipulatorTest::setPolygonPoints()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, triangle, 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();
    const PDFObjectReference originalAppearance = normalAppearance(document, polygon);

    // Move a vertex and insert a new one
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, polygon, { QPointF(50, 50), QPointF(250, 80), QPointF(200, 200), QPointF(100, 150) }));
    const PDFDocument modified = modifyBuilder.build();

    QVERIFY(fuzzyCompare(numbers(modified, polygon, "Vertices"), std::vector<PDFReal>{ 50.0, 50.0, 250.0, 80.0, 200.0, 200.0, 100.0, 150.0 }));
    const QRectF newRectangle = rectangle(modified, polygon);
    QVERIFY(newRectangle.contains(QPointF(250, 80)));
    QVERIFY(newRectangle.contains(QPointF(200, 200)));
    QVERIFY(newRectangle.contains(QPointF(50, 50)));
    QVERIFY(newRectangle.width() < 215.0);
    QVERIFY(normalAppearance(modified, polygon).isValid());
    QVERIFY(normalAppearance(modified, polygon) != originalAppearance);

    // Polygon must keep at least three points
    PDFDocumentBuilder invalidBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&invalidBuilder, polygon, { QPointF(50, 50), QPointF(250, 80) }));
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&invalidBuilder, polygon, { }));
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&invalidBuilder, PDFObjectReference(), { QPointF(), QPointF(), QPointF() }));
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(nullptr, polygon, { QPointF(), QPointF(), QPointF() }));
    QVERIFY(fuzzyCompare(numbers(invalidBuilder.build(), polygon, "Vertices"), numbers(document, polygon, "Vertices")));
}

void AnnotationManipulatorTest::setLinePoints()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 200, 100), QPointF(10, 10), QPointF(110, 60), 2.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, line, { QPointF(10, 10), QPointF(300, 300) }));

    // Number of the points of a line is fixed, a square has no points at all
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, line, { QPointF(10, 10), QPointF(300, 300), QPointF(50, 50) }));
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, square, { QPointF(10, 10), QPointF(300, 300) }));
    const PDFDocument modified = modifyBuilder.build();

    QVERIFY(fuzzyCompare(numbers(modified, line, "L"), std::vector<PDFReal>{ 10.0, 10.0, 300.0, 300.0 }));
    QVERIFY(rectangle(modified, line).contains(QPointF(300, 300)));
    QVERIFY(rectangle(modified, line).contains(QPointF(10, 10)));
    QVERIFY(fuzzyCompare(rectangle(modified, square), rectangle(document, square)));
}

void AnnotationManipulatorTest::setFreeTextCalloutPoints()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, QRectF(10, 10, 190, 140), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                         Qt::AlignLeft, QPointF(20, 20), QPointF(110, 120), AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);
    const PDFDocument document = builder.build();

    auto getTextRectangle = [](const PDFDocument& currentDocument, PDFObjectReference annotation)
    {
        const std::vector<PDFReal> differences = numbers(currentDocument, annotation, "RD");
        const QRectF annotationRectangle = rectangle(currentDocument, annotation);
        return differences.size() == 4 ? annotationRectangle.adjusted(differences[0], differences[1], -differences[2], -differences[3]) : QRectF();
    };

    const QRectF originalTextRectangle = getTextRectangle(document, freeText);
    QVERIFY(fuzzyCompare(originalTextRectangle, QRectF(110, 110, 80, 30)));

    // The start of the callout line is moved far away - the rectangle grows,
    // but the text rectangle stays, where it is
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, freeText, { QPointF(300, 350), QPointF(110, 120) }));
    const PDFDocument modified = modifyBuilder.build();

    QVERIFY(fuzzyCompare(numbers(modified, freeText, "CL"), std::vector<PDFReal>{ 300.0, 350.0, 110.0, 120.0 }));
    QVERIFY(rectangle(modified, freeText).contains(QPointF(300, 350)));
    QVERIFY(fuzzyCompare(getTextRectangle(modified, freeText), originalTextRectangle));
}

void AnnotationManipulatorTest::copyAnnotationSharesAppearanceAndCopiesPopup()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page1, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("NM");
    factory << QString("original-name");
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(note, factory.takeObject());
    const PDFDocument document = builder.build();

    PDFDocumentBuilder copyBuilder(&document);
    const PDFObjectReference copy = PDFAnnotationManipulator::copyAnnotation(&copyBuilder, note, page2);
    QVERIFY(copy.isValid());
    QVERIFY(copy != note);
    const PDFDocument modified = copyBuilder.build();

    // Copy is placed on the second page, together with its own popup
    const std::vector<PDFObjectReference> annotations = pageAnnotations(modified, 1);
    QCOMPARE(annotations.size(), size_t(2));
    QCOMPARE(annotations[0], copy);

    const PDFObject copiedPopupObject = entry(modified, copy, "Popup");
    QVERIFY(copiedPopupObject.isReference());
    const PDFObjectReference copiedPopup = copiedPopupObject.getReference();
    QCOMPARE(annotations[1], copiedPopup);
    QVERIFY(copiedPopup != entry(document, note, "Popup").getReference());
    QCOMPARE(entry(modified, copiedPopup, "Parent").getReference(), copy);
    QCOMPARE(entry(modified, copiedPopup, "P").getReference(), page2);
    QCOMPARE(entry(modified, copy, "P").getReference(), page2);

    // Name of the copy is unique, appearance streams are shared
    QVERIFY(entry(modified, copy, "NM").isString());
    QVERIFY(entry(modified, copy, "NM") != entry(modified, note, "NM"));
    QCOMPARE(normalAppearance(modified, copy), normalAppearance(modified, note));

    // Original annotation is untouched
    QVERIFY(pageAnnotations(modified, 0) == pageAnnotations(document, 0));
    QCOMPARE(entry(modified, note, "Popup").getReference(), entry(document, note, "Popup").getReference());
}

void AnnotationManipulatorTest::copyAnnotationDropsReplyLinks()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference reply = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Reply", "Subject", "Contents");

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("IRT");
    factory << square;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("StructParent");
    factory << PDFInteger(3);
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.mergeTo(reply, factory.takeObject());
    const PDFDocument document = builder.build();

    PDFDocumentBuilder copyBuilder(&document);
    const PDFObjectReference copy = PDFAnnotationManipulator::copyAnnotation(&copyBuilder, reply, page);
    const PDFDocument modified = copyBuilder.build();

    QVERIFY(entry(modified, copy, "IRT").isNull());
    QVERIFY(entry(modified, copy, "StructParent").isNull());
    QCOMPARE(entry(modified, reply, "IRT").getReference(), square);
    QCOMPARE(pageAnnotations(modified, 0).size(), size_t(3));
}

void AnnotationManipulatorTest::moveAnnotationToPage()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page1, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference square = builder.createAnnotationSquare(page1, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();
    const PDFObjectReference popup = entry(document, note, "Popup").getReference();

    PDFDocumentBuilder moveBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::moveAnnotationToPage(&moveBuilder, note, page1, page2));
    QVERIFY(!PDFAnnotationManipulator::moveAnnotationToPage(&moveBuilder, square, page1, page1));
    const PDFDocument modified = moveBuilder.build();

    QVERIFY(pageAnnotations(modified, 0) == std::vector<PDFObjectReference>{ square });
    QVERIFY(pageAnnotations(modified, 1) == (std::vector<PDFObjectReference>{ note, popup }));
    QCOMPARE(entry(modified, note, "P").getReference(), page2);
    QCOMPARE(entry(modified, popup, "P").getReference(), page2);
    QCOMPARE(entry(modified, popup, "Parent").getReference(), note);
    QVERIFY(fuzzyCompare(rectangle(modified, note), rectangle(document, note)));
}

void AnnotationManipulatorTest::serializeAndInsert()
{
    // Source document
    PDFDocumentBuilder sourceBuilder;
    const PDFObjectReference sourcePage = sourceBuilder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = sourceBuilder.createAnnotationText(sourcePage, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Note text", true);
    const PDFObjectReference square = sourceBuilder.createAnnotationSquare(sourcePage, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Square text");
    const PDFObjectReference stamp = sourceBuilder.createAnnotationStamp(sourcePage, QRectF(200, 200, 100, 40), Stamp::Approved, "Title", "Subject", "Contents");
    const PDFDocument source = sourceBuilder.build();

    const QRectF noteRectangle = rectangle(source, note);
    const QRectF squareRectangle = rectangle(source, square);
    const QRectF stampRectangle = rectangle(source, stamp);
    const QRectF expectedBoundingRectangle = noteRectangle.united(squareRectangle).united(stampRectangle);

    const QByteArray data = PDFAnnotationManipulator::serializeAnnotations(&source, { note, square, stamp });
    QVERIFY(!data.isEmpty());
    QVERIFY(data.startsWith("%PDF"));

    const PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(data);
    QVERIFY(serialized.isValid());
    QCOMPARE(serialized.annotations.size(), size_t(3));
    QVERIFY(fuzzyCompare(serialized.boundingRectangle, expectedBoundingRectangle));

    // Target document - the annotations are inserted with an offset
    PDFDocumentBuilder targetBuilder;
    const PDFObjectReference targetPage = targetBuilder.appendPage(QRectF(0, 0, 600, 600));
    const PDFDocument target = targetBuilder.build();

    PDFDocumentBuilder insertBuilder(&target);
    const std::vector<PDFObjectReference> inserted = PDFAnnotationManipulator::insertAnnotations(&insertBuilder, targetPage, serialized, QPointF(100.0, 50.0));
    QCOMPARE(inserted.size(), size_t(3));
    const PDFDocument modified = insertBuilder.build();

    // Three annotations and one popup are on the page
    const std::vector<PDFObjectReference> annotations = pageAnnotations(modified, 0);
    QCOMPARE(annotations.size(), size_t(4));

    QVERIFY(fuzzyCompare(rectangle(modified, inserted[0]), noteRectangle.translated(100.0, 50.0)));
    QVERIFY(fuzzyCompare(rectangle(modified, inserted[1]), squareRectangle.translated(100.0, 50.0)));
    QVERIFY(fuzzyCompare(rectangle(modified, inserted[2]), stampRectangle.translated(100.0, 50.0)));

    PDFDocumentDataLoaderDecorator loader(&modified);
    std::set<QString> names;
    for (const PDFObjectReference& annotation : inserted)
    {
        QCOMPARE(entry(modified, annotation, "P").getReference(), targetPage);
        QVERIFY(entry(modified, annotation, "IRT").isNull());

        const PDFDictionary* dictionary = modified.getDictionaryFromObject(modified.getObjectByReference(annotation));
        QVERIFY(dictionary);
        names.insert(loader.readTextStringFromDictionary(dictionary, "NM", QString()));
    }
    QCOMPARE(names.size(), size_t(3));
    QVERIFY(!names.count(QString()));

    // Popup of the note is inserted and linked, and it is moved with the note
    const PDFObject popupObject = entry(modified, inserted[0], "Popup");
    QVERIFY(popupObject.isReference());
    const PDFObjectReference popup = popupObject.getReference();
    QCOMPARE(entry(modified, popup, "Parent").getReference(), inserted[0]);
    QCOMPARE(entry(modified, popup, "P").getReference(), targetPage);
    QVERIFY(std::find(annotations.cbegin(), annotations.cend(), popup) != annotations.cend());
    QVERIFY(fuzzyCompare(rectangle(modified, popup), rectangle(source, entry(source, note, "Popup").getReference()).translated(100.0, 50.0)));

    // Appearance streams are copied into the target document
    const PDFObjectReference stampAppearance = normalAppearance(modified, inserted[2]);
    QVERIFY(stampAppearance.isValid());
    QVERIFY(modified.getObjectByReference(stampAppearance).isStream());

    // Contents of the annotations survive the round trip
    const PDFDictionary* squareDictionary = modified.getDictionaryFromObject(modified.getObjectByReference(inserted[1]));
    QCOMPARE(loader.readTextStringFromDictionary(squareDictionary, "Contents", QString()), QString("Square text"));
}

void AnnotationManipulatorTest::serializeRejectsInvalidData()
{
    QVERIFY(!PDFAnnotationManipulator::deserializeAnnotations(QByteArray()).isValid());
    QVERIFY(!PDFAnnotationManipulator::deserializeAnnotations(QByteArray("this is not a pdf")).isValid());

    // Document without annotations serializes to nothing
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFDocument document = builder.build();
    QVERIFY(PDFAnnotationManipulator::serializeAnnotations(&document, {}).isEmpty());
    QVERIFY(PDFAnnotationManipulator::serializeAnnotations(&document, { PDFObjectReference() }).isEmpty());
    QVERIFY(PDFAnnotationManipulator::serializeAnnotations(nullptr, { page }).isEmpty());

    // A page without annotations is a valid document, but there is nothing to insert
    const QByteArray emptyPage = PDFAnnotationManipulator::serializeAnnotations(&document, { page });
    QVERIFY(emptyPage.isEmpty());
}

void AnnotationManipulatorTest::malformedGeometryIsIgnored()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF quadrilateral = { QPointF(10, 30), QPointF(60, 30), QPointF(10, 10), QPointF(60, 10) };
    const PDFObjectReference oddHighlight = builder.createAnnotationHighlight(page, quadrilateral, Qt::yellow);
    const PDFObjectReference emptyHighlight = builder.createAnnotationHighlight(page, quadrilateral, Qt::yellow);
    const PDFObjectReference ink = builder.createAnnotationInk(page, QPolygonF{ QPointF(10, 10), QPointF(20, 30) }, 2.0, Qt::red, "Title", "Subject", "Contents");
    const PDFObjectReference oddInk = builder.createAnnotationInk(page, QPolygonF{ QPointF(10, 10), QPointF(20, 30) }, 2.0, Qt::red, "Title", "Subject", "Contents");
    const PDFObjectReference shortDifferences = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference largeDifferences = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");

    setEntry(builder, oddHighlight, "QuadPoints", numberArray({ 1.0, 2.0, 3.0 }));
    setEntry(builder, emptyHighlight, "QuadPoints", PDFObject::createName("NotAnArray"));
    setEntry(builder, ink, "InkList", PDFObject::createInteger(5));
    setEntry(builder, shortDifferences, "RD", numberArray({ 1.0, 2.0, 3.0 }));
    setEntry(builder, largeDifferences, "RD", numberArray({ 100.0, 100.0, 100.0, 100.0 }));

    // Ink list with an odd number of coordinates - the last number is dropped
    PDFObjectFactory inkFactory;
    inkFactory.beginArray();
    inkFactory << std::initializer_list<PDFReal>{ 10.0, 10.0, 20.0, 30.0, 40.0 };
    inkFactory.endArray();
    setEntry(builder, oddInk, "InkList", inkFactory.takeObject());

    const PDFDocument document = builder.build();
    const QTransform scale = QTransform::fromScale(2.0, 2.0);

    PDFDocument modified = transform(document, oddHighlight, scale);
    QVERIFY(fuzzyCompare(numbers(modified, oddHighlight, "QuadPoints"), std::vector<PDFReal>{ 1.0, 2.0, 3.0 }));

    modified = transform(document, emptyHighlight, scale);
    QVERIFY(entry(modified, emptyHighlight, "QuadPoints").isName());

    modified = transform(document, ink, scale);
    QVERIFY(entry(modified, ink, "InkList").isInt());

    modified = transform(document, oddInk, scale);
    PDFDocumentDataLoaderDecorator loader(&modified);
    const PDFObject inkList = modified.getObject(entry(modified, oddInk, "InkList"));
    QVERIFY(inkList.isArray());
    QCOMPARE(inkList.getArray()->getCount(), size_t(1));
    QVERIFY(fuzzyCompare(loader.readNumberArray(inkList.getArray()->getItem(0)), std::vector<PDFReal>{ 20.0, 20.0, 40.0, 60.0 }));

    modified = transform(document, shortDifferences, scale);
    QVERIFY(fuzzyCompare(numbers(modified, shortDifferences, "RD"), std::vector<PDFReal>{ 1.0, 2.0, 3.0 }));
    QVERIFY(fuzzyCompare(rectangle(modified, shortDifferences), QRectF(200, 200, 100, 60)));

    modified = transform(document, largeDifferences, scale);
    QVERIFY(fuzzyCompare(numbers(modified, largeDifferences, "RD"), std::vector<PDFReal>{ 100.0, 100.0, 100.0, 100.0 }));
}

void AnnotationManipulatorTest::rectangleValidation()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference nullSquare = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference flatSquare = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference flatLine = builder.createAnnotationLine(page, QRectF(0, 0, 200, 100), QPointF(10, 10), QPointF(10, 60), 2.0, Qt::red, Qt::blue,
                                                                     "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));

    setEntry(builder, nullSquare, "Rect", numberArray({ 10.0, 10.0, 10.0, 10.0 }));
    setEntry(builder, flatSquare, "Rect", numberArray({ 10.0, 10.0, 10.0, 50.0 }));
    setEntry(builder, flatLine, "Rect", numberArray({ 10.0, 10.0, 10.0, 60.0 }));
    const PDFDocument document = builder.build();

    const QTransform translation = QTransform::fromTranslate(5.0, 5.0);

    // Annotation without an area cannot be transformed, annotation defined by points
    // can have a rectangle with zero width (vertical line)
    PDFDocument modified = transform(document, nullSquare, translation, false);
    QVERIFY(fuzzyCompare(numbers(modified, nullSquare, "Rect"), std::vector<PDFReal>{ 10.0, 10.0, 10.0, 10.0 }));
    modified = transform(document, flatSquare, translation, false);
    QVERIFY(fuzzyCompare(numbers(modified, flatSquare, "Rect"), std::vector<PDFReal>{ 10.0, 10.0, 10.0, 50.0 }));
    modified = transform(document, flatLine, translation, true);
    QVERIFY(fuzzyCompare(numbers(modified, flatLine, "Rect"), std::vector<PDFReal>{ 15.0, 15.0, 15.0, 65.0 }));
    QVERIFY(fuzzyCompare(numbers(modified, flatLine, "L"), std::vector<PDFReal>{ 15.0, 15.0, 15.0, 65.0 }));

    // Object, which is not a dictionary
    PDFDocumentBuilder invalidBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::transformAnnotation(&invalidBuilder, number, translation));
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&invalidBuilder, number, { QPointF(), QPointF(1, 1) }));
}

void AnnotationManipulatorTest::rotateFreeTextWithCallout()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(10, 10, 190, 140), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(20, 20), QPointF(110, 120), AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);
    const PDFObjectReference plain = builder.createAnnotationFreeText(page, QRectF(210, 210, 100, 50), "Title", "Subject", "Contents", Qt::AlignLeft);
    const PDFDocument document = builder.build();

    const QTransform rotation = QTransform().rotate(30.0);

    // The tip of the callout line is rotated exactly. The text box cannot be rotated, so it
    // is moved, and the end of the callout line follows it - the line stays connected to the box.
    PDFDocument modified = transform(document, callout, rotation);
    const QRectF textRectangle(110, 110, 80, 30);
    const QPointF offset = rotation.map(textRectangle.center()) - textRectangle.center();
    const QPointF start = rotation.map(QPointF(20, 20));
    const QPointF end = QPointF(110, 120) + offset;
    QVERIFY(fuzzyCompare(numbers(modified, callout, "CL"), std::vector<PDFReal>{ start.x(), start.y(), end.x(), end.y() }));
    QVERIFY(normalAppearance(modified, callout) != normalAppearance(document, callout));

    const std::vector<PDFReal> differences = numbers(modified, callout, "RD");
    QCOMPARE(differences.size(), size_t(4));
    const QRectF newTextRectangle = rectangle(modified, callout).adjusted(differences[0], differences[1], -differences[2], -differences[3]);
    QVERIFY(fuzzyCompare(newTextRectangle, textRectangle.translated(offset)));
    QVERIFY(rectangle(modified, callout).contains(start));

    // Free text without a callout line is only moved, its appearance stays
    modified = transform(document, plain, rotation);
    QCOMPARE(normalAppearance(modified, plain), normalAppearance(document, plain));
    QRectF expected = rectangle(document, plain);
    expected.moveCenter(rotation.map(expected.center()));
    QVERIFY(fuzzyCompare(rectangle(modified, plain), expected));
}

void AnnotationManipulatorTest::appearanceWithoutStreams()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference noAppearance = builder.createAnnotationStamp(page, QRectF(100, 100, 200, 50), Stamp::Approved, "Title", "Subject", "Contents");
    const PDFObjectReference noBoundingBox = builder.createAnnotationStamp(page, QRectF(100, 100, 200, 50), Stamp::Approved, "Title", "Subject", "Contents");
    const PDFObjectReference zeroMatrix = builder.createAnnotationStamp(page, QRectF(100, 100, 200, 50), Stamp::Approved, "Title", "Subject", "Contents");

    setEntry(builder, noAppearance, "AP", PDFObject());

    {
        const PDFDocument temporary = builder.build();
        const PDFObjectReference streamReference = normalAppearance(temporary, noBoundingBox);
        const PDFStream* stream = temporary.getObjectByReference(streamReference).getStream();
        PDFDictionary dictionary = *stream->getDictionary();
        dictionary.removeEntry("BBox");
        builder.setObject(streamReference, PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(*stream->getContent()))));
        setEntry(builder, normalAppearance(temporary, zeroMatrix), "Matrix", numberArray({ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 }));
    }

    const PDFDocument document = builder.build();

    for (const PDFObjectReference stamp : { noAppearance, noBoundingBox, zeroMatrix })
    {
        // The form cannot be transformed, so only the rectangle is rotated
        const QRectF original = rectangle(document, stamp);
        const PDFDocument modified = transform(document, stamp, rotationAroundCenter(original, 90.0));

        QRectF expected(0, 0, original.height(), original.width());
        expected.moveCenter(original.center());
        QVERIFY(fuzzyCompare(rectangle(modified, stamp), expected));
        QCOMPARE(normalAppearance(modified, stamp), normalAppearance(document, stamp));
    }
}

void AnnotationManipulatorTest::appearanceWithStates()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference stamp = builder.createAnnotationStamp(page, QRectF(100, 100, 200, 50), Stamp::Approved, "Title", "Subject", "Contents");
    const PDFObjectReference streamReference = normalAppearance(builder.build(), stamp);

    // Appearance dictionary with appearance states, a rollover appearance and an invalid entry
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("N");
    factory.beginDictionary();
    factory.beginDictionaryItem("On");
    factory << streamReference;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Off");
    factory << PDFInteger(5);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Second");
    factory << streamReference;
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endDictionaryItem();
    factory.beginDictionaryItem("R");
    factory << streamReference;
    factory.endDictionaryItem();
    factory.beginDictionaryItem("D");
    factory << PDFInteger(7);
    factory.endDictionaryItem();
    factory.endDictionary();
    setEntry(builder, stamp, "AP", PDFObject());
    setEntry(builder, stamp, "AP", factory.takeObject());
    const PDFDocument document = builder.build();

    const QRectF original = rectangle(document, stamp);
    const PDFDocument modified = transform(document, stamp, rotationAroundCenter(original, 90.0));

    QRectF expected(0, 0, original.height(), original.width());
    expected.moveCenter(original.center());
    QVERIFY(fuzzyCompare(rectangle(modified, stamp), expected));

    const PDFDictionary* appearance = modified.getDictionaryFromObject(entry(modified, stamp, "AP"));
    QVERIFY(appearance);
    const PDFDictionary* states = modified.getDictionaryFromObject(appearance->get("N"));
    QVERIFY(states);

    auto hasRotationMatrix = [&modified, streamReference](const PDFObject& object)
    {
        if (!object.isReference() || object.getReference() == streamReference)
        {
            return false;
        }

        const PDFObject& stream = modified.getObjectByReference(object.getReference());
        return stream.isStream() && stream.getStream()->getDictionary()->hasKey("Matrix");
    };

    QVERIFY(hasRotationMatrix(states->get("On")));
    QVERIFY(hasRotationMatrix(states->get("Second")));
    QVERIFY(hasRotationMatrix(appearance->get("R")));
    QCOMPARE(states->get("Off"), PDFObject::createInteger(5));
    QCOMPARE(appearance->get("D"), PDFObject::createInteger(7));
}

void AnnotationManipulatorTest::popupEdgeCases()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference brokenPopupNote = builder.createAnnotationText(page, QRectF(150, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference noRectanglePopupNote = builder.createAnnotationText(page, QRectF(250, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));

    PDFDocument document = builder.build();
    const PDFObjectReference popup = entry(document, note, "Popup").getReference();
    const PDFObjectReference noRectanglePopup = entry(document, noRectanglePopupNote, "Popup").getReference();
    setEntry(builder, brokenPopupNote, "Popup", PDFObject::createReference(number));
    setEntry(builder, noRectanglePopup, "Rect", PDFObject());
    document = builder.build();

    const QRectF popupRectangle = rectangle(document, popup);

    // Movement in a single direction, and no movement at all (rotation around the center)
    PDFDocument modified = transform(document, note, QTransform::fromTranslate(0.0, 7.0));
    QVERIFY(fuzzyCompare(rectangle(modified, popup), popupRectangle.translated(0.0, 7.0)));
    modified = transform(document, note, QTransform::fromTranslate(7.0, 0.0));
    QVERIFY(fuzzyCompare(rectangle(modified, popup), popupRectangle.translated(7.0, 0.0)));
    modified = transform(document, note, rotationAroundCenter(rectangle(document, note), 90.0));
    QVERIFY(fuzzyCompare(rectangle(modified, popup), popupRectangle));

    // Popup, which is not a dictionary, and popup without a rectangle are left alone
    modified = transform(document, brokenPopupNote, QTransform::fromTranslate(5.0, 5.0));
    QVERIFY(fuzzyCompare(rectangle(modified, brokenPopupNote), rectangle(document, brokenPopupNote).translated(5.0, 5.0)));
    QCOMPARE(modified.getObjectByReference(number), PDFObject::createInteger(5));

    modified = transform(document, noRectanglePopupNote, QTransform::fromTranslate(5.0, 5.0));
    QVERIFY(fuzzyCompare(rectangle(modified, noRectanglePopupNote), rectangle(document, noRectanglePopupNote).translated(5.0, 5.0)));
    QVERIFY(entry(modified, noRectanglePopup, "Rect").isNull());
}

void AnnotationManipulatorTest::editablePointsOfMalformedAnnotations()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 200, 100), QPointF(10, 10), QPointF(110, 60), 2.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, triangle, 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference polyline = builder.createAnnotationPolyline(page, triangle, 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents",
                                                                         AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, QRectF(210, 210, 100, 50), "Title", "Subject", "Contents", Qt::AlignLeft);

    setEntry(builder, line, "L", PDFObject());
    setEntry(builder, polygon, "Vertices", numberArray({ 50.0, 50.0, 150.0, 50.0 }));
    setEntry(builder, polyline, "Vertices", numberArray({ 50.0, 50.0 }));
    const PDFDocument document = builder.build();

    for (const PDFObjectReference annotation : { line, polygon, polyline, freeText })
    {
        QVERIFY(!PDFAnnotationManipulator::getEditablePoints(PDFAnnotation::parse(&document.getStorage(), annotation).data()).isValid());

        PDFDocumentBuilder modifyBuilder(&document);
        QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, annotation, { QPointF(1, 1), QPointF(2, 2), QPointF(3, 3) }));
    }
}

void AnnotationManipulatorTest::setFreeTextCalloutWithoutTextRectangle()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference noDifferences = builder.createAnnotationFreeText(page, QRectF(100, 100, 100, 50), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                              Qt::AlignLeft, QPointF(120, 120), QPointF(150, 130), AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference largeDifferences = builder.createAnnotationFreeText(page, QRectF(100, 100, 100, 50), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                                 Qt::AlignLeft, QPointF(120, 120), QPointF(150, 130), AnnotationLineEnding::None, AnnotationLineEnding::None);
    setEntry(builder, noDifferences, "RD", PDFObject());
    setEntry(builder, largeDifferences, "RD", numberArray({ 100.0, 100.0, 100.0, 100.0 }));
    const PDFDocument document = builder.build();

    for (const PDFObjectReference freeText : { noDifferences, largeDifferences })
    {
        // The text rectangle is not known, so the whole rectangle is considered to be the text
        PDFDocumentBuilder modifyBuilder(&document);
        QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, freeText, { QPointF(300, 300), QPointF(150, 130) }));
        const PDFDocument modified = modifyBuilder.build();

        const std::vector<PDFReal> differences = numbers(modified, freeText, "RD");
        QCOMPARE(differences.size(), size_t(4));
        const QRectF newRectangle = rectangle(modified, freeText);
        QVERIFY(newRectangle.contains(QPointF(300, 300)));
        QVERIFY(fuzzyCompare(newRectangle.adjusted(differences[0], differences[1], -differences[2], -differences[3]), QRectF(100, 100, 100, 50)));
    }
}

void AnnotationManipulatorTest::copyAndMoveArgumentValidation()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page1, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));
    const PDFDocument document = builder.build();

    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::copyAnnotation(nullptr, square, page2).isValid());
    QVERIFY(!PDFAnnotationManipulator::copyAnnotation(&modifyBuilder, PDFObjectReference(), page2).isValid());
    QVERIFY(!PDFAnnotationManipulator::copyAnnotation(&modifyBuilder, square, PDFObjectReference()).isValid());
    QVERIFY(!PDFAnnotationManipulator::copyAnnotation(&modifyBuilder, number, page2).isValid());

    QVERIFY(!PDFAnnotationManipulator::moveAnnotationToPage(nullptr, square, page1, page2));
    QVERIFY(!PDFAnnotationManipulator::moveAnnotationToPage(&modifyBuilder, PDFObjectReference(), page1, page2));
    QVERIFY(!PDFAnnotationManipulator::moveAnnotationToPage(&modifyBuilder, square, PDFObjectReference(), page2));
    QVERIFY(!PDFAnnotationManipulator::moveAnnotationToPage(&modifyBuilder, square, page1, PDFObjectReference()));
    QVERIFY(!PDFAnnotationManipulator::moveAnnotationToPage(&modifyBuilder, square, page2, page2));
    QVERIFY(!PDFAnnotationManipulator::moveAnnotationToPage(&modifyBuilder, number, page1, page2));

    // Nothing has been modified
    const PDFDocument modified = modifyBuilder.build();
    QVERIFY(pageAnnotations(modified, 0) == std::vector<PDFObjectReference>{ square });
    QVERIFY(pageAnnotations(modified, 1).empty());

    // Copy of an annotation without a name and without a popup
    PDFDocumentBuilder copyBuilder(&document);
    const PDFObjectReference copy = PDFAnnotationManipulator::copyAnnotation(&copyBuilder, square, page2);
    const PDFDocument copied = copyBuilder.build();
    QVERIFY(pageAnnotations(copied, 1) == std::vector<PDFObjectReference>{ copy });
    QVERIFY(entry(copied, copy, "NM").isNull());
    QVERIFY(entry(copied, copy, "Popup").isNull());
}

void AnnotationManipulatorTest::moveLastAnnotationOfPage()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference page3 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page1, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));
    const PDFDocument document = builder.build();

    // The annotation array of the source page disappears with its last annotation
    PDFDocumentBuilder moveBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::moveAnnotationToPage(&moveBuilder, square, page1, page2));
    PDFDocument modified = moveBuilder.build();
    QVERIFY(pageAnnotations(modified, 0).empty());
    QVERIFY(entry(modified, page1, "Annots").isNull());
    QVERIFY(pageAnnotations(modified, 1) == std::vector<PDFObjectReference>{ square });

    // The source page is wrong (the annotation is not on it), or it is not a page at all -
    // the annotation is appended to the target page anyway
    QVERIFY(PDFAnnotationManipulator::moveAnnotationToPage(&moveBuilder, square, page1, page3));
    modified = moveBuilder.build();
    QVERIFY(pageAnnotations(modified, 2) == std::vector<PDFObjectReference>{ square });
    QCOMPARE(entry(modified, square, "P").getReference(), page3);

    PDFDocumentBuilder brokenBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::moveAnnotationToPage(&brokenBuilder, square, number, page2));
    modified = brokenBuilder.build();
    QVERIFY(pageAnnotations(modified, 1) == std::vector<PDFObjectReference>{ square });
}

void AnnotationManipulatorTest::serializeSpecialCases()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference noRectangle = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    setEntry(builder, noRectangle, "Rect", PDFObject());
    setEntry(builder, noRectangle, "OC", PDFObject::createReference(page));
    const PDFDocument document = builder.build();
    const PDFObjectReference popup = entry(document, note, "Popup").getReference();

    // Popup cannot be serialized alone, it goes with its parent
    QVERIFY(PDFAnnotationManipulator::serializeAnnotations(&document, { popup }).isEmpty());

    // Annotation without a rectangle is serialized (the page gets a minimal media box)
    const QByteArray data = PDFAnnotationManipulator::serializeAnnotations(&document, { noRectangle, page });
    QVERIFY(!data.isEmpty());
    const PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(data);
    QVERIFY(serialized.isValid());
    QCOMPARE(serialized.annotations.size(), size_t(1));
    QVERIFY(!serialized.boundingRectangle.isValid());

    // Optional content membership is not transferred to another document
    const PDFDictionary* dictionary = serialized.document.getDictionaryFromObject(serialized.document.getObjectByReference(serialized.annotations.front()));
    QVERIFY(dictionary);
    QVERIFY(!dictionary->hasKey("OC"));
    QVERIFY(dictionary->hasKey("P"));
}

void AnnotationManipulatorTest::deserializeSpecialCases()
{
    // Document without pages
    {
        PDFDocumentBuilder builder;
        QVERIFY(!PDFAnnotationManipulator::deserializeAnnotations(write(builder.build())).isValid());
    }

    // Page without annotations
    {
        PDFDocumentBuilder builder;
        builder.appendPage(QRectF(0, 0, 400, 400));
        QVERIFY(!PDFAnnotationManipulator::deserializeAnnotations(write(builder.build())).isValid());
    }

    // Annotation array with an object, which is not a dictionary
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
        const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));

        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Annots");
        factory << std::initializer_list<PDFObjectReference>{ number };
        factory.endDictionaryItem();
        factory.endDictionary();
        builder.appendTo(page, factory.takeObject());
        const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
        Q_UNUSED(square);

        const PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(write(builder.build()));
        QVERIFY(serialized.isValid());
        QCOMPARE(serialized.annotations.size(), size_t(1));
        QVERIFY(fuzzyCompare(serialized.boundingRectangle, QRectF(100, 100, 50, 30)));
    }
}

void AnnotationManipulatorTest::insertArgumentValidation()
{
    PDFDocumentBuilder sourceBuilder;
    const PDFObjectReference sourcePage = sourceBuilder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = sourceBuilder.createAnnotationSquare(sourcePage, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument source = sourceBuilder.build();
    const PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(PDFAnnotationManipulator::serializeAnnotations(&source, { square }));
    QVERIFY(serialized.isValid());

    PDFDocumentBuilder targetBuilder;
    const PDFObjectReference targetPage = targetBuilder.appendPage(QRectF(0, 0, 400, 400));

    QVERIFY(PDFAnnotationManipulator::insertAnnotations(nullptr, targetPage, serialized, QPointF()).empty());
    QVERIFY(PDFAnnotationManipulator::insertAnnotations(&targetBuilder, PDFObjectReference(), serialized, QPointF()).empty());
    QVERIFY(PDFAnnotationManipulator::insertAnnotations(&targetBuilder, targetPage, PDFAnnotationManipulator::SerializedAnnotations(), QPointF()).empty());
    QVERIFY(pageAnnotations(targetBuilder.build(), 0).empty());

    // Zero offset - the annotation keeps its position
    const std::vector<PDFObjectReference> inserted = PDFAnnotationManipulator::insertAnnotations(&targetBuilder, targetPage, serialized, QPointF());
    QCOMPARE(inserted.size(), size_t(1));
    const PDFDocument target = targetBuilder.build();
    QVERIFY(fuzzyCompare(rectangle(target, inserted.front()), QRectF(100, 100, 50, 30)));
    QVERIFY(entry(target, inserted.front(), "NM").isString());
}

QString AnnotationManipulatorTest::text(const PDFDocument& document, PDFObjectReference annotation, const char* key)
{
    PDFDocumentDataLoaderDecorator loader(&document);
    const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotation));
    return dictionary ? loader.readTextStringFromDictionary(dictionary, key, QString()) : QString();
}

PDFObject AnnotationManipulatorTest::numberFormat(const char* unit, PDFReal factor)
{
    PDFObjectFactory factory;
    factory.beginArray();
    factory.beginDictionary();
    factory.beginDictionaryItem("U");
    factory << QString::fromLatin1(unit);
    factory.endDictionaryItem();
    factory.beginDictionaryItem("C");
    factory << factor;
    factory.endDictionaryItem();
    factory.endDictionary();
    factory.endArray();
    return factory.takeObject();
}

PDFObject AnnotationManipulatorTest::measure(PDFReal x, PDFReal y, PDFReal distance, PDFReal area, PDFReal angle)
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Measure");
    factory.endDictionaryItem();

    const std::array<std::pair<const char*, PDFReal>, 5> formats = { std::pair<const char*, PDFReal>{ "X", x },
                                                                      std::pair<const char*, PDFReal>{ "Y", y },
                                                                      std::pair<const char*, PDFReal>{ "D", distance },
                                                                      std::pair<const char*, PDFReal>{ "A", area },
                                                                      std::pair<const char*, PDFReal>{ "T", angle } };
    for (const auto& [key, factor] : formats)
    {
        if (factor != 0.0)
        {
            factory.beginDictionaryItem(key);
            factory << numberFormat("u", factor);
            factory.endDictionaryItem();
        }
    }

    factory.endDictionary();
    return factory.takeObject();
}

PDFObject AnnotationManipulatorTest::numberFormats(std::initializer_list<std::pair<const char*, PDFReal>> formats)
{
    PDFObjectFactory factory;
    factory.beginArray();
    for (const auto& [unit, factor] : formats)
    {
        factory.beginDictionary();
        factory.beginDictionaryItem("U");
        factory << QString::fromLatin1(unit);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("C");
        factory << factor;
        factory.endDictionaryItem();
        factory.endDictionary();
    }
    factory.endArray();
    return factory.takeObject();
}

PDFObject AnnotationManipulatorTest::measureOf(std::initializer_list<std::pair<const char*, PDFObject>> formats)
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Measure");
    factory.endDictionaryItem();
    for (const auto& [key, format] : formats)
    {
        factory.beginDictionaryItem(key);
        factory << format;
        factory.endDictionaryItem();
    }
    factory.endDictionary();
    return factory.takeObject();
}

PDFObject AnnotationManipulatorTest::pathArray(std::initializer_list<std::vector<PDFReal>> items)
{
    PDFObjectFactory factory;
    factory.beginArray();
    for (const std::vector<PDFReal>& item : items)
    {
        factory << item;
    }
    factory.endArray();
    return factory.takeObject();
}

void AnnotationManipulatorTest::setMeasurement(PDFDocumentBuilder& builder, PDFObjectReference annotation, const char* intent, const QString& contents, PDFObject measure)
{
    PDFObjectFactory factory;
    factory << contents;
    setEntry(builder, annotation, "Contents", factory.takeObject());
    setEntry(builder, annotation, "IT", PDFObject::createName(intent));
    setEntry(builder, annotation, "Measure", measure);
    builder.updateAnnotationAppearanceStreams(annotation);
}

QString AnnotationManipulatorTest::transformedContents(const PDFDocument& document, PDFObjectReference annotation, const QTransform& transform)
{
    return text(AnnotationManipulatorTest::transform(document, annotation, transform), annotation, "Contents");
}

QString AnnotationManipulatorTest::transformedLineContents(const QString& contents, PDFObject measure, const QTransform& transform)
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "", AnnotationLineEnding::None, AnnotationLineEnding::None);
    setMeasurement(builder, line, "LineDimension", contents, measure);
    return transformedContents(builder.build(), line, transform);
}

void AnnotationManipulatorTest::capabilities()
{
    using Manipulator = PDFAnnotationManipulator;

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 200, 100), QPointF(10, 10), QPointF(110, 60), 2.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, QRectF(100, 100, 50, 10), Qt::yellow);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference circle = builder.createAnnotationCircle(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference caret = builder.createAnnotationCaret(page, QRectF(100, 100, 20, 20), 1.0, Qt::blue, "Title", "Subject", "Contents");
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, QRectF(210, 210, 100, 50), "Title", "Subject", "Contents", Qt::AlignLeft);
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(10, 10, 190, 140), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(20, 20), QPointF(110, 120), AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", false);
    const PDFObjectReference stamp = builder.createAnnotationStamp(page, QRectF(100, 100, 200, 50), Stamp::Approved, "Title", "Subject", "Contents");
    const PDFObjectReference link = builder.createAnnotationLink(page, QRectF(100, 100, 50, 30), "https://example.com", LinkHighlightMode::Invert);
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, triangle, 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    auto getCapabilities = [&document](PDFObjectReference annotation)
    {
        return Manipulator::getCapabilities(PDFAnnotation::parse(&document.getStorage(), annotation).data());
    };

    const Manipulator::Capabilities all = Manipulator::Move | Manipulator::Resize | Manipulator::RotateRightAngle | Manipulator::RotateArbitrary | Manipulator::Mirror;

    // Annotations defined by points and by the appearance stream support everything
    QCOMPARE(getCapabilities(line), all | Manipulator::EditPoints);
    QCOMPARE(getCapabilities(polygon), all | Manipulator::EditPoints);
    QCOMPARE(getCapabilities(highlight), all | Manipulator::EditPoints);
    QCOMPARE(getCapabilities(stamp), all);

    // A rectangle can be rotated by the right angle only, mirroring does not change it
    QCOMPARE(getCapabilities(square), Manipulator::Capabilities(Manipulator::Move | Manipulator::Resize | Manipulator::RotateRightAngle));
    QCOMPARE(getCapabilities(circle), Manipulator::Capabilities(Manipulator::Move | Manipulator::Resize | Manipulator::RotateRightAngle));

    // Text box and caret cannot be rotated at all. Mirroring moves the callout line.
    QCOMPARE(getCapabilities(freeText), Manipulator::Capabilities(Manipulator::Move | Manipulator::Resize));
    QCOMPARE(getCapabilities(caret), Manipulator::Capabilities(Manipulator::Move | Manipulator::Resize));
    QCOMPARE(getCapabilities(callout), Manipulator::Capabilities(Manipulator::Move | Manipulator::Resize | Manipulator::Mirror | Manipulator::EditPoints));

    // An icon can only be moved
    QCOMPARE(getCapabilities(note), Manipulator::Capabilities(Manipulator::Move));

    QCOMPARE(getCapabilities(link), Manipulator::Capabilities(Manipulator::NoCapability));
    QCOMPARE(Manipulator::getCapabilities(nullptr), Manipulator::Capabilities(Manipulator::NoCapability));
}

void AnnotationManipulatorTest::boxRotationRules()
{
    const QRectF rectangle(100, 100, 60, 20);
    const QRectF rotatedRectangle(120, 80, 20, 60);
    const QTransform rightAngle = rotationAroundCenter(rectangle, 90.0);
    const QTransform scaling = QTransform::fromScale(2.0, 3.0);

    auto outline = [&rectangle](AnnotationType type, const QTransform& transform)
    {
        return PDFAnnotationManipulator::getTransformedOutline(type, rectangle, transform).boundingRect();
    };

    // Scaling is exact for every rectangle
    QVERIFY(fuzzyCompare(outline(AnnotationType::Square, scaling), QRectF(200, 300, 120, 60)));
    QVERIFY(fuzzyCompare(outline(AnnotationType::FreeText, scaling), QRectF(200, 300, 120, 60)));

    // Rotation by the right angle swaps the size of a square and of a circle,
    // a text box and a caret keep their size (they are moved only)
    QVERIFY(fuzzyCompare(outline(AnnotationType::Square, rightAngle), rotatedRectangle));
    QVERIFY(fuzzyCompare(outline(AnnotationType::Circle, rightAngle), rotatedRectangle));
    QVERIFY(fuzzyCompare(outline(AnnotationType::FreeText, rightAngle), rectangle));
    QVERIFY(fuzzyCompare(outline(AnnotationType::Caret, rightAngle), rectangle));

    // Transformations, which are neither scaling, nor a rotation by the right angle, move only
    const QTransform shearX(1.0, 0.0, 0.5, 1.0, 0.0, 0.0);
    const QTransform shearY(1.0, 0.5, 0.0, 1.0, 0.0, 0.0);
    const QTransform almostRightAngle(0.0, 1.0, -1.0, 0.5, 0.0, 0.0);
    const QTransform almostRightAngle2(0.5, 1.0, -1.0, 0.0, 0.0, 0.0);
    for (const QTransform& transform : { shearX, shearY, almostRightAngle, almostRightAngle2, QTransform().rotate(30.0) })
    {
        QRectF expected = rectangle;
        expected.moveCenter(transform.map(rectangle.center()));
        QVERIFY(fuzzyCompare(outline(AnnotationType::Square, transform), expected));
    }

    // The same rules are applied by the transformation itself
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference freeText = builder.createAnnotationFreeText(page, rectangle, "Title", "Subject", "Contents", Qt::AlignLeft);
    const PDFObjectReference caret = builder.createAnnotationCaret(page, rectangle, 1.0, Qt::blue, "Title", "Subject", "Contents");
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(10, 10, 190, 140), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(20, 20), QPointF(60, 60), QPointF(110, 120), AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);
    const PDFDocument document = builder.build();

    PDFDocument modified = transform(document, freeText, rightAngle);
    QVERIFY(fuzzyCompare(AnnotationManipulatorTest::rectangle(modified, freeText), rectangle));
    QCOMPARE(normalAppearance(modified, freeText), normalAppearance(document, freeText));

    modified = transform(document, caret, rightAngle);
    QVERIFY(fuzzyCompare(AnnotationManipulatorTest::rectangle(modified, caret), rectangle));

    // Callout line with a knee - the tip is rotated, the knee and the end follow the text box
    const QTransform calloutRotation = rotationAroundCenter(QRectF(10, 10, 190, 140), 90.0);
    const QRectF textRectangle(110, 110, 80, 30);
    const QPointF offset = calloutRotation.map(textRectangle.center()) - textRectangle.center();
    const QPointF tip = calloutRotation.map(QPointF(20, 20));
    modified = transform(document, callout, calloutRotation);
    QVERIFY(fuzzyCompare(numbers(modified, callout, "CL"), std::vector<PDFReal>{ tip.x(), tip.y(), 60.0 + offset.x(), 60.0 + offset.y(), 110.0 + offset.x(), 120.0 + offset.y() }));
}

void AnnotationManipulatorTest::transformPath()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, triangle, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference ink = builder.createAnnotationInk(page, triangle, 1.0, Qt::black, "Title", "Subject", "Contents");

    // Path with a line and with a curve (PDF 2.0), it has precedence over the vertices
    PDFObjectFactory factory;
    factory.beginArray();
    factory << std::vector<PDFReal>{ 50, 50 } << std::vector<PDFReal>{ 150, 50 } << std::vector<PDFReal>{ 150, 100, 120, 150, 100, 150 };
    factory.endArray();
    const PDFObject path = factory.takeObject();

    setEntry(builder, polygon, "Path", path);
    setEntry(builder, polygon, "Vertices", PDFObject());
    setEntry(builder, ink, "Path", path);
    builder.updateAnnotationAppearanceStreams(polygon);
    const PDFDocument document = builder.build();

    // Polygon defined just by the path has an appearance
    QVERIFY(normalAppearance(document, polygon).isValid());

    auto pathNumbers = [](const PDFDocument& currentDocument, PDFObjectReference annotation)
    {
        std::vector<PDFReal> result;
        PDFDocumentDataLoaderDecorator loader(&currentDocument);
        const PDFObject pathObject = currentDocument.getObject(entry(currentDocument, annotation, "Path"));
        for (const PDFObject& item : *pathObject.getArray())
        {
            const std::vector<PDFReal> itemNumbers = loader.readNumberArray(item);
            result.insert(result.end(), itemNumbers.cbegin(), itemNumbers.cend());
        }
        return result;
    };

    const QTransform scaling = QTransform::fromScale(2.0, 3.0);
    const std::vector<PDFReal> expected = { 100, 150, 300, 150, 300, 300, 240, 450, 200, 450 };

    PDFDocument modified = transform(document, polygon, scaling);
    QVERIFY(fuzzyCompare(pathNumbers(modified, polygon), expected));
    QVERIFY(normalAppearance(modified, polygon) != normalAppearance(document, polygon));
    QVERIFY(rectangle(modified, polygon).contains(QPointF(200, 440)));

    modified = transform(document, ink, scaling);
    QVERIFY(fuzzyCompare(pathNumbers(modified, ink), expected));

    // The path is moved with the annotation
    modified = transform(document, polygon, QTransform::fromTranslate(10.0, 20.0));
    QVERIFY(fuzzyCompare(pathNumbers(modified, polygon), std::vector<PDFReal>{ 60, 70, 160, 70, 160, 120, 130, 170, 110, 170 }));
}

void AnnotationManipulatorTest::editablePointsOfPath()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference pathOnly = builder.createAnnotationPolygon(page, triangle, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference pathAndVertices = builder.createAnnotationPolygon(page, triangle, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference curvedPath = builder.createAnnotationPolygon(page, triangle, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference openPath = builder.createAnnotationPolyline(page, triangle, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents",
                                                                         AnnotationLineEnding::None, AnnotationLineEnding::None);

    PDFObjectFactory factory;
    factory.beginArray();
    factory << std::vector<PDFReal>{ 10, 10 } << std::vector<PDFReal>{ 210, 10 } << std::vector<PDFReal>{ 210, 110 } << std::vector<PDFReal>{ 10, 110 };
    factory.endArray();
    const PDFObject linePath = factory.takeObject();

    factory.beginArray();
    factory << std::vector<PDFReal>{ 10, 10 } << std::vector<PDFReal>{ 210, 10 } << std::vector<PDFReal>{ 210, 60, 110, 110, 10, 110 };
    factory.endArray();
    const PDFObject curvePath = factory.takeObject();

    setEntry(builder, pathOnly, "Path", linePath);
    setEntry(builder, pathOnly, "Vertices", PDFObject());
    setEntry(builder, pathAndVertices, "Path", linePath);
    setEntry(builder, curvedPath, "Path", curvePath);
    setEntry(builder, openPath, "Path", linePath);
    const PDFDocument document = builder.build();

    auto getPoints = [](const PDFDocument& currentDocument, PDFObjectReference annotation)
    {
        return PDFAnnotationManipulator::getEditablePoints(PDFAnnotation::parse(&currentDocument.getStorage(), annotation).data());
    };

    // The shape is drawn from the path, so the points of the path are edited (not the vertices).
    // The point, which closes the polygon, is not an editable point.
    const std::vector<QPointF> rectanglePoints = { QPointF(10, 10), QPointF(210, 10), QPointF(210, 110), QPointF(10, 110) };
    QVERIFY(getPoints(document, pathOnly).points == rectanglePoints);
    QVERIFY(getPoints(document, pathAndVertices).points == rectanglePoints);
    QVERIFY(getPoints(document, openPath).points == rectanglePoints);
    QVERIFY(!getPoints(document, openPath).isClosed);

    // Curves cannot be edited point by point
    QVERIFY(!getPoints(document, curvedPath).isValid());
    PDFDocumentBuilder curveBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&curveBuilder, curvedPath, rectanglePoints));

    // The path is updated, the vertices only if they are present
    const std::vector<QPointF> newPoints = { QPointF(10, 10), QPointF(310, 10), QPointF(10, 110) };
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, pathOnly, newPoints));
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, pathAndVertices, newPoints));
    const PDFDocument modified = modifyBuilder.build();

    QVERIFY(getPoints(modified, pathOnly).points == newPoints);
    QVERIFY(getPoints(modified, pathAndVertices).points == newPoints);
    QVERIFY(entry(modified, pathOnly, "Vertices").isNull());
    QVERIFY(fuzzyCompare(numbers(modified, pathAndVertices, "Vertices"), std::vector<PDFReal>{ 10, 10, 310, 10, 10, 110 }));
    QVERIFY(rectangle(modified, pathOnly).contains(QPointF(305, 12)));
    QVERIFY(normalAppearance(modified, pathOnly) != normalAppearance(document, pathOnly));
}

void AnnotationManipulatorTest::mirrorLineWithLeaderLines()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "", AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None, 20.0, 4.0, 6.0, false, false);
    const PDFObjectReference reversedLine = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(150, 150), QPointF(50, 150), 1.0, Qt::red, Qt::blue,
                                                                         "Title", "Subject", "", AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None, 20.0, 4.0, 6.0, false, false);
    const PDFObjectReference verticalLine = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(50, 50), QPointF(50, 150), 1.0, Qt::red, Qt::blue,
                                                                         "Title", "Subject", "", AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None, 20.0, 4.0, 6.0, false, false);
    const PDFObjectReference downwardLine = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(50, 150), QPointF(50, 50), 1.0, Qt::red, Qt::blue,
                                                                         "Title", "Subject", "", AnnotationLineEnding::OpenArrow, AnnotationLineEnding::None, 20.0, 4.0, 6.0, false, false);
    setEntry(builder, line, "CO", numberArray({ 10.0, 5.0 }));
    setEntry(builder, verticalLine, "LE", PDFObject());
    builder.updateAnnotationAppearanceStreams(line);
    const PDFDocument document = builder.build();
    const QRectF originalRectangle = rectangle(document, line);

    // Leader lines are above the line
    QVERIFY(originalRectangle.bottom() > 170.0);
    QVERIFY(originalRectangle.top() > 140.0);

    // Horizontal mirroring reverses the direction of the line, so the leader lines must
    // change the side of the line to stay above it. Their extension and offset are not oriented.
    const QPointF center = originalRectangle.center();
    const QTransform mirrorX = QTransform::fromTranslate(-center.x(), -center.y()) * QTransform::fromScale(-1.0, 1.0) * QTransform::fromTranslate(center.x(), center.y());
    // The caption of the mirrored line would be upside down, so the end points of the line
    // are swapped. It is the same line, the leader lines stay above it, the line endings
    // and the offset of the caption along the line follow the swapped end points.
    PDFDocument modified = transform(document, line, mirrorX);
    PDFDocumentDataLoaderDecorator loader(&modified);
    QVERIFY(fuzzyCompare(numbers(modified, line, "L"), std::vector<PDFReal>{ 50.0, 150.0, 150.0, 150.0 }));
    QCOMPARE(number(modified, line, "LL"), 20.0);
    QCOMPARE(number(modified, line, "LLO"), 4.0);
    QCOMPARE(number(modified, line, "LLE"), 6.0);
    QVERIFY(fuzzyCompare(numbers(modified, line, "CO"), std::vector<PDFReal>{ -10.0, 5.0 }));
    QVERIFY(loader.readNameArrayFromDictionary(modified.getDictionaryFromObject(modified.getObjectByReference(line)), "LE") == (std::vector<QByteArray>{ "None", "OpenArrow" }));
    QVERIFY(fuzzyCompare(rectangle(modified, line), originalRectangle));

    // Line, which goes from the right to the left, keeps the order of its end points - its
    // caption was upside down already. The leader lines change the side of the mirrored line.
    modified = transform(document, reversedLine, mirrorX);
    QVERIFY(fuzzyCompare(numbers(modified, reversedLine, "L"), std::vector<PDFReal>{ 50.0, 150.0, 150.0, 150.0 }));
    QCOMPARE(number(modified, reversedLine, "LL"), -20.0);
    QVERIFY(loader.readNameArrayFromDictionary(modified.getDictionaryFromObject(modified.getObjectByReference(reversedLine)), "LE") == (std::vector<QByteArray>{ "OpenArrow", "None" }));

    // Vertical lines - the line, which goes upwards, is the readable one
    const QTransform mirrorVertical = QTransform::fromScale(1.0, -1.0);
    modified = transform(document, verticalLine, mirrorVertical);
    QVERIFY(fuzzyCompare(numbers(modified, verticalLine, "L"), std::vector<PDFReal>{ 50.0, -150.0, 50.0, -50.0 }));
    QCOMPARE(number(modified, verticalLine, "LL"), 20.0);
    QVERIFY(entry(modified, verticalLine, "LE").isNull());

    modified = transform(document, downwardLine, mirrorVertical);
    QVERIFY(fuzzyCompare(numbers(modified, downwardLine, "L"), std::vector<PDFReal>{ 50.0, -150.0, 50.0, -50.0 }));
    QCOMPARE(number(modified, downwardLine, "LL"), -20.0);

    // Vertical mirroring keeps the direction of the line, the leader lines go below it
    const QTransform mirrorY = QTransform::fromTranslate(0.0, -150.0) * QTransform::fromScale(1.0, -1.0) * QTransform::fromTranslate(0.0, 150.0);
    modified = transform(document, line, mirrorY);
    QVERIFY(fuzzyCompare(numbers(modified, line, "L"), std::vector<PDFReal>{ 50.0, 150.0, 150.0, 150.0 }));
    QCOMPARE(number(modified, line, "LL"), -20.0);
    QVERIFY(rectangle(modified, line).top() < 130.0);
    QVERIFY(rectangle(modified, line).bottom() < 160.0);

    // Scaling is not uniform - the leader lines are perpendicular to the line, so they
    // are scaled by the factor of the y axis, the offset of the caption along the line by the x axis
    modified = transform(document, line, QTransform::fromScale(2.0, 3.0));
    QCOMPARE(number(modified, line, "LL"), 60.0);
    QCOMPARE(number(modified, line, "LLO"), 12.0);
    QCOMPARE(number(modified, line, "LLE"), 18.0);
    QVERIFY(fuzzyCompare(numbers(modified, line, "CO"), std::vector<PDFReal>{ 20.0, 15.0 }));

    // Rotation changes nothing
    modified = transform(document, line, QTransform().rotate(90.0));
    QVERIFY(std::abs(number(modified, line, "LL") - 20.0) < 1e-6);
    QVERIFY(fuzzyCompare(numbers(modified, line, "CO"), std::vector<PDFReal>{ 10.0, 5.0 }));

    // Translation does not touch the entries at all
    modified = transform(document, line, QTransform::fromTranslate(5.0, 5.0));
    QCOMPARE(entry(modified, line, "LL"), entry(document, line, "LL"));
}

void AnnotationManipulatorTest::lineParametersSpecialCases()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None, 20.0, 0.0, 0.0, false, false);
    const PDFObjectReference nullLine = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                     "Title", "Subject", "Contents", AnnotationLineEnding::None, AnnotationLineEnding::None, 20.0, 0.0, 0.0, false, false);
    setEntry(builder, nullLine, "L", numberArray({ 50.0, 150.0, 50.0, 150.0 }));
    setEntry(builder, line, "CO", numberArray({ 10.0 }));
    const PDFDocument document = builder.build();

    // Line without a length has no direction, so the leader lines cannot be transformed
    PDFDocument modified = transform(document, nullLine, QTransform::fromScale(2.0, 2.0));
    QCOMPARE(number(modified, nullLine, "LL"), 20.0);

    // The transformed line has no length
    modified = transform(document, line, QTransform::fromScale(0.0, 2.0));
    QCOMPARE(number(modified, line, "LL"), 20.0);

    // Malformed offset of the caption is left alone
    modified = transform(document, line, QTransform::fromScale(2.0, 2.0));
    QCOMPARE(number(modified, line, "LL"), 40.0);
    QVERIFY(fuzzyCompare(numbers(modified, line, "CO"), std::vector<PDFReal>{ 10.0 }));
}

void AnnotationManipulatorTest::measurementLine()
{
    const QTransform scaling = QTransform::fromScale(2.0, 2.0);

    // The measure defines the scale, the measured length is 100 units of the user space
    QCOMPARE(transformedLineContents("200.00 mm", measure(2.0, 0.0, 0.0, 0.0, 0.0), scaling), QString("400.00 mm"));

    // Distance format converts the units of the x axis
    QCOMPARE(transformedLineContents("Length 100.0 cm (approx.)", measure(2.0, 0.0, 0.5, 0.0, 0.0), scaling), QString("Length 200.0 cm (approx.)"));

    // Only the measured value is replaced, other numbers are left alone
    QCOMPARE(transformedLineContents("Wall 3: 200.00 mm", measure(2.0, 0.0, 0.0, 0.0, 0.0), scaling), QString("Wall 3: 400.00 mm"));

    // The text does not contain the measured value, it is a comment of the user
    QCOMPARE(transformedLineContents("See detail 7", measure(2.0, 0.0, 0.0, 0.0, 0.0), scaling), QString("See detail 7"));
    QCOMPARE(transformedLineContents("", measure(2.0, 0.0, 0.0, 0.0, 0.0), scaling), QString(""));

    // Rotation and translation do not change the length
    QCOMPARE(transformedLineContents("200.00 mm", measure(2.0, 0.0, 0.0, 0.0, 0.0), QTransform().rotate(30.0)), QString("200.00 mm"));
    QCOMPARE(transformedLineContents("200.00 mm", measure(2.0, 0.0, 0.0, 0.0, 0.0), QTransform::fromTranslate(10.0, 10.0)), QString("200.00 mm"));

    // Anisotropic scale - the y axis has another scale, so rotation changes the measured length
    QCOMPARE(transformedLineContents("100 m", measure(1.0, 3.0, 0.0, 0.0, 0.0), QTransform().rotate(90.0)), QString("300 m"));

    // The scale is not known (the measure is missing, it is not rectilinear, or it has no x axis),
    // the displayed value is scaled by the ratio of the lengths
    QCOMPARE(transformedLineContents("100 pt", PDFObject(), scaling), QString("200 pt"));
    QCOMPARE(transformedLineContents("100 pt", measure(0.0, 0.0, 1.0, 0.0, 0.0), scaling), QString("200 pt"));

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("GEO");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("X");
    factory << numberFormat("m", 5.0);
    factory.endDictionaryItem();
    factory.endDictionary();
    QCOMPARE(transformedLineContents("100 pt", factory.takeObject(), scaling), QString("200 pt"));

    // The appearance displays the measured value, so it is regenerated
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "", AnnotationLineEnding::None, AnnotationLineEnding::None, 0.0, 0.0, 0.0, true, true);
    setMeasurement(builder, line, "LineDimension", "100 pt", PDFObject());
    const PDFDocument document = builder.build();

    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, line, { QPointF(50, 150), QPointF(250, 150) }));
    const PDFDocument modified = modifyBuilder.build();
    QCOMPARE(text(modified, line, "Contents"), QString("200 pt"));
    QVERIFY(normalAppearance(modified, line) != normalAppearance(document, line));

    // Line, which is not a measurement, keeps its contents
    PDFDocumentBuilder plainBuilder;
    const PDFObjectReference plainPage = plainBuilder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference plainLine = plainBuilder.createAnnotationLine(plainPage, QRectF(0, 0, 400, 400), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                           "Title", "Subject", "100 pt", AnnotationLineEnding::None, AnnotationLineEnding::None);
    QCOMPARE(transformedContents(plainBuilder.build(), plainLine, scaling), QString("100 pt"));
}

void AnnotationManipulatorTest::measurementNumberFormats()
{
    const QTransform scaling = QTransform::fromScale(2.0, 2.0);

    // The number keeps its format - decimal places and separators
    QCOMPARE(transformedLineContents("1,234.50 pt", PDFObject(), scaling), QString("2,469.00 pt"));
    QCOMPARE(transformedLineContents("1.234,50 pt", PDFObject(), scaling), QString("2.469,00 pt"));
    QCOMPARE(transformedLineContents("1 234,50 mm", PDFObject(), scaling), QString("2 469,00 mm"));
    QCOMPARE(transformedLineContents("1'234.5", PDFObject(), scaling), QString("2'469.0"));
    QCOMPARE(transformedLineContents(QString("1") + QChar(0x00A0) + QString("234,5 mm"), PDFObject(), scaling), QString("2") + QChar(0x00A0) + QString("469,0 mm"));
    QCOMPARE(transformedLineContents("999 999 mm", PDFObject(), scaling), QString("1 999 998 mm"));
    QCOMPARE(transformedLineContents("1 234 567 mm", PDFObject(), scaling), QString("2 469 134 mm"));
    QCOMPARE(transformedLineContents("12 m", PDFObject(), scaling), QString("24 m"));
    QCOMPARE(transformedLineContents("0.5", PDFObject(), scaling), QString("1.0"));
    QCOMPARE(transformedLineContents("d = 7.25 m", PDFObject(), scaling), QString("d = 14.50 m"));
    QCOMPARE(transformedLineContents("L: 8 m", PDFObject(), scaling), QString("L: 16 m"));

    // Characters around the digits are not a part of the number
    QCOMPARE(transformedLineContents("5m", PDFObject(), scaling), QString("10m"));
    QCOMPARE(transformedLineContents("/5:", PDFObject(), scaling), QString("/5:"));
    QCOMPARE(transformedLineContents("5.", PDFObject(), scaling), QString("10."));

    // A space followed by another count of digits than three does not group the digits,
    // so there are two numbers, and we do not know, which one is the measured value
    QCOMPARE(transformedLineContents("12 34 m", PDFObject(), scaling), QString("12 34 m"));

    // The separator of thousands cannot be distinguished from the decimal separator. If
    // the scale is known, then both interpretations are compared with the measured value.
    QCOMPARE(transformedLineContents("1,234 mm", measure(12.34, 0.0, 0.0, 0.0, 0.0), scaling), QString("2,468 mm"));
    QCOMPARE(transformedLineContents("1,234 mm", measure(12.34, 0.0, 0.0, 0.0, 0.0), QTransform::fromScale(1000.0, 1000.0)), QString("1,234,000 mm"));
    QCOMPARE(transformedLineContents("1.234 mm", measure(0.01234, 0.0, 0.0, 0.0, 0.0), scaling), QString("2.468 mm"));
    QCOMPARE(transformedLineContents("1,235 mm", measure(99.0, 0.0, 0.0, 0.0, 0.0), scaling), QString("1,235 mm"));

    // If the scale is not known, then it does not matter - the digits are the same
    QCOMPARE(transformedLineContents("1,234 mm", PDFObject(), scaling), QString("2,468 mm"));
}

void AnnotationManipulatorTest::measurementPolygon()
{
    const QPolygonF squarePolygon = { QPointF(100, 100), QPointF(200, 100), QPointF(200, 200), QPointF(100, 200) };
    const QTransform scaling = QTransform::fromScale(2.0, 2.0);

    auto transformedPolygonContents = [&](const QString& contents, PDFObject measureObject, const QTransform& transform)
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
        const PDFObjectReference polygon = builder.createAnnotationPolygon(page, squarePolygon, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "");
        setMeasurement(builder, polygon, "PolygonDimension", contents, measureObject);
        return transformedContents(builder.build(), polygon, transform);
    };

    // Perimeter is 400 units of the user space, area is 10000 square units
    QCOMPARE(transformedPolygonContents("p = 40.00 m", measure(0.1, 0.0, 0.0, 0.0, 0.0), scaling), QString("p = 80.00 m"));
    QCOMPARE(transformedPolygonContents("A = 100.00 sq m", measure(0.1, 0.0, 0.0, 0.0, 0.0), scaling), QString("A = 400.00 sq m"));
    QCOMPARE(transformedPolygonContents("A = 50.00 a", measure(0.1, 0.0, 0.0, 0.5, 0.0), scaling), QString("A = 200.00 a"));
    QCOMPARE(transformedPolygonContents("A = 300.00 sq m", measure(0.1, 0.3, 0.0, 0.0, 0.0), QTransform::fromScale(1.0, 2.0)), QString("A = 600.00 sq m"));
    QCOMPARE(transformedPolygonContents("Room 12", measure(0.1, 0.0, 0.0, 0.0, 0.0), scaling), QString("Room 12"));

    // The scale is not known - the quantity is recognized by its symbol
    QCOMPARE(transformedPolygonContents("p = 40 m", PDFObject(), scaling), QString("p = 80 m"));
    QCOMPARE(transformedPolygonContents("P = 40 m", PDFObject(), scaling), QString("P = 80 m"));
    QCOMPARE(transformedPolygonContents("A = 100 a", PDFObject(), scaling), QString("A = 400 a"));
    QCOMPARE(transformedPolygonContents("a = 100 a", PDFObject(), scaling), QString("a = 400 a"));
    QCOMPARE(transformedPolygonContents("100 a", PDFObject(), scaling), QString("100 a"));
    QCOMPARE(transformedPolygonContents("S = 100 a", PDFObject(), scaling), QString("S = 100 a"));
}

void AnnotationManipulatorTest::measurementAngle()
{
    // The right angle with the vertex at the point (100, 100)
    const QPolygonF angle = { QPointF(100, 150), QPointF(100, 100), QPointF(150, 100) };

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference withMeasure = builder.createAnnotationPolyline(page, angle, 1.0, QColor(), Qt::black, "Title", "Subject", "",
                                                                            AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference withoutMeasure = builder.createAnnotationPolyline(page, angle, 1.0, QColor(), Qt::black, "Title", "Subject", "",
                                                                               AnnotationLineEnding::None, AnnotationLineEnding::None);
    const PDFObjectReference plain = builder.createAnnotationPolyline(page, angle, 1.0, QColor(), Qt::black, "Title", "Subject", "90.00 deg",
                                                                      AnnotationLineEnding::None, AnnotationLineEnding::None);
    setMeasurement(builder, withMeasure, "PolyLineDimension", "100.00 grad", measure(1.0, 0.0, 0.0, 0.0, 100.0 / 90.0));
    setMeasurement(builder, withoutMeasure, "PolyLineDimension", "90.00 deg", PDFObject());
    const PDFDocument document = builder.build();

    // Rotation does not change the angle
    QCOMPARE(transformedContents(document, withMeasure, QTransform().rotate(30.0)), QString("100.00 grad"));

    // Mirroring would change the orientation of the angle (the angle would become 270 degrees),
    // so the order of the points is reversed and the measured angle stays the same
    const QTransform mirror = QTransform::fromScale(-1.0, 1.0);
    PDFDocument modified = transform(document, withMeasure, mirror);
    QCOMPARE(text(modified, withMeasure, "Contents"), QString("100.00 grad"));
    QVERIFY(fuzzyCompare(numbers(modified, withMeasure, "Vertices"), std::vector<PDFReal>{ -150, 100, -100, 100, -100, 150 }));

    // Polyline, which is not a measurement, keeps the order of its points
    modified = transform(document, plain, mirror);
    QVERIFY(fuzzyCompare(numbers(modified, plain, "Vertices"), std::vector<PDFReal>{ -100, 150, -100, 100, -150, 100 }));
    QCOMPARE(text(modified, plain, "Contents"), QString("90.00 deg"));

    // The arm of the angle is moved
    const std::vector<QPointF> halfAngle = { QPointF(100, 150), QPointF(100, 100), QPointF(150, 150) };
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, withMeasure, halfAngle));
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, withoutMeasure, halfAngle));
    modified = modifyBuilder.build();
    QCOMPARE(text(modified, withMeasure, "Contents"), QString("50.00 grad"));
    QCOMPARE(text(modified, withoutMeasure, "Contents"), QString("45.00 deg"));
}

void AnnotationManipulatorTest::measurementPointCount()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));

    auto createPolyline = [&](int count, bool isMeasurement)
    {
        QPolygonF polyline;
        for (int i = 0; i < count; ++i)
        {
            polyline << QPointF(50.0 + 50.0 * i, (i % 2) ? 150.0 : 100.0);
        }

        const PDFObjectReference annotation = builder.createAnnotationPolyline(page, polyline, 1.0, QColor(), Qt::black, "Title", "Subject", "",
                                                                               AnnotationLineEnding::None, AnnotationLineEnding::None);
        if (isMeasurement)
        {
            setMeasurement(builder, annotation, "PolyLineDimension", "1 m", PDFObject());
        }
        return annotation;
    };

    const PDFObjectReference distance = createPolyline(2, true);
    const PDFObjectReference angle = createPolyline(3, true);
    const PDFObjectReference length4 = createPolyline(4, true);
    const PDFObjectReference length5 = createPolyline(5, true);
    const PDFObjectReference plain = createPolyline(3, false);
    const PDFObjectReference single = createPolyline(1, true);
    const PDFDocument document = builder.build();

    auto getPoints = [&document](PDFObjectReference annotation)
    {
        return PDFAnnotationManipulator::getEditablePoints(PDFAnnotation::parse(&document.getStorage(), annotation).data());
    };

    // Three points of a measurement are an angle, another count is a length. Points cannot
    // be inserted or removed in a way, which would turn one measurement into the other.
    QVERIFY(!getPoints(distance).canInsertPoint());
    QVERIFY(!getPoints(distance).canRemovePoint(0));
    QVERIFY(!getPoints(angle).canInsertPoint());
    QVERIFY(!getPoints(angle).canRemovePoint(1));
    QVERIFY(getPoints(length4).canInsertPoint());
    QVERIFY(!getPoints(length4).canRemovePoint(1));
    QVERIFY(getPoints(length5).canInsertPoint());
    QVERIFY(getPoints(length5).canRemovePoint(1));
    QVERIFY(getPoints(plain).canInsertPoint());
    QVERIFY(getPoints(plain).canRemovePoint(1));
    QVERIFY(!getPoints(single).isValid());

    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, angle, { QPointF(0, 0), QPointF(10, 0), QPointF(10, 10), QPointF(0, 10) }));
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, angle, { QPointF(0, 0), QPointF(10, 0) }));
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, angle, { QPointF(0, 0), QPointF(10, 0), QPointF(10, 10) }));

    // Length of the polyline is measured
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, length4, { QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(0, 100), QPointF(0, 50) }));
    const PDFDocument modified = modifyBuilder.build();
    const PDFReal oldLength = 3.0 * std::hypot(50.0, 50.0);
    QCOMPARE(text(modified, length4, "Contents"), QString("%1 m").arg(qRound(350.0 / oldLength)));
}

void AnnotationManipulatorTest::measurementIsNotGuessed()
{
    const QTransform scaling = QTransform::fromScale(2.0, 2.0);

    // The scale is not known, so the text must look like a measured value - a number
    // with an optional symbol of the quantity and with an optional unit
    QCOMPARE(transformedLineContents("Wall 3", PDFObject(), scaling), QString("Wall 3"));
    QCOMPARE(transformedLineContents("100 pt long", PDFObject(), scaling), QString("100 pt long"));
    QCOMPARE(transformedLineContents("100 x 200", PDFObject(), scaling), QString("100 x 200"));
    QCOMPARE(transformedLineContents("no number", PDFObject(), scaling), QString("no number"));
    QCOMPARE(transformedLineContents("Length = 100 pt", PDFObject(), scaling), QString("Length = 100 pt"));

    // Line without a length - the ratio of the lengths is not defined
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(50, 150), QPointF(150, 150), 1.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "", AnnotationLineEnding::None, AnnotationLineEnding::None);
    setMeasurement(builder, line, "LineDimension", "0 pt", PDFObject());
    setEntry(builder, line, "L", numberArray({ 50.0, 150.0, 50.0, 150.0 }));
    QCOMPARE(transformedContents(builder.build(), line, scaling), QString("0 pt"));
}

void AnnotationManipulatorTest::findAnnotationPage()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference withPage = builder.createAnnotationSquare(page2, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference withoutPage = builder.createAnnotationSquare(page2, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference wrongPage = builder.createAnnotationSquare(page2, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference onNoPage = builder.createAnnotationSquare(page1, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));

    setEntry(builder, withoutPage, "P", PDFObject());
    setEntry(builder, wrongPage, "P", PDFObject::createReference(page1));
    setEntry(builder, page1, "Annots", PDFObject());
    const PDFDocument document = builder.build();
    const PDFObjectStorage* storage = &document.getStorage();

    // The entry P is just a hint - it is optional and it can be wrong
    QCOMPARE(PDFAnnotationManipulator::findAnnotationPage(storage, withPage), page2);
    QCOMPARE(PDFAnnotationManipulator::findAnnotationPage(storage, withoutPage), page2);
    QCOMPARE(PDFAnnotationManipulator::findAnnotationPage(storage, wrongPage), page2);
    QVERIFY(!PDFAnnotationManipulator::findAnnotationPage(storage, onNoPage).isValid());
    QVERIFY(!PDFAnnotationManipulator::findAnnotationPage(storage, number).isValid());
    QVERIFY(!PDFAnnotationManipulator::findAnnotationPage(storage, PDFObjectReference()).isValid());
    QVERIFY(!PDFAnnotationManipulator::findAnnotationPage(nullptr, withPage).isValid());
}

void AnnotationManipulatorTest::findAnnotationPageInDamagedTree()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));
    setEntry(builder, square, "P", PDFObject());

    // Intermediate node of the page tree, which refers also to an object, which is not
    // a dictionary, and back to the root of the tree (a cycle)
    const PDFDocument flatDocument = builder.build();
    PDFDocumentDataLoaderDecorator loader(&flatDocument);
    const PDFObjectReference root = loader.readReferenceFromDictionary(flatDocument.getDictionaryFromObject(flatDocument.getObjectByReference(builder.getCatalogReference())), "Pages");
    QVERIFY(root.isValid());

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("Pages");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Kids");
    factory << std::vector<PDFObjectReference>{ number, root, page };
    factory.endDictionaryItem();
    factory.endDictionary();
    const PDFObjectReference node = builder.addObject(factory.takeObject());

    factory.beginArray();
    factory << node;
    factory.endArray();
    setEntry(builder, root, "Kids", factory.takeObject());

    const PDFObjectStorage* storage = builder.getStorage();
    QCOMPARE(PDFAnnotationManipulator::findAnnotationPage(storage, square), page);

    // The annotation is not in the tree, so the whole tree (with the cycle) is searched
    setEntry(builder, page, "Annots", PDFObject());
    QVERIFY(!PDFAnnotationManipulator::findAnnotationPage(storage, square).isValid());

    // Catalog without the page tree
    setEntry(builder, builder.getCatalogReference(), "Pages", PDFObject());
    QVERIFY(!PDFAnnotationManipulator::findAnnotationPage(storage, square).isValid());

    // Storage without a catalog, and without a trailer dictionary
    PDFObjectStorage emptyStorage;
    const PDFObjectReference orphan = emptyStorage.addObject(storage->getObject(square));
    QVERIFY(!PDFAnnotationManipulator::findAnnotationPage(&emptyStorage, orphan).isValid());

    emptyStorage.updateTrailerDictionary(PDFObject::createDictionary(PDFDictionary()));
    QVERIFY(!PDFAnnotationManipulator::findAnnotationPage(&emptyStorage, orphan).isValid());
}

void AnnotationManipulatorTest::replies()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference other = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference secondReply = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Second", false);
    const PDFObjectReference firstReply = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "First", false);
    const PDFObjectReference otherReply = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Other", false);
    const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));

    // The reply to the reply precedes the reply in the annotation array
    setEntry(builder, firstReply, "IRT", PDFObject::createReference(note));
    setEntry(builder, secondReply, "IRT", PDFObject::createReference(firstReply));
    setEntry(builder, otherReply, "IRT", PDFObject::createReference(other));

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    factory << std::vector<PDFObjectReference>{ number };
    factory.endDictionaryItem();
    factory.endDictionary();
    builder.appendTo(page, factory.takeObject());

    const PDFObjectStorage* storage = builder.getStorage();
    QVERIFY(PDFAnnotationManipulator::getReplies(storage, page, note) == (std::vector<PDFObjectReference>{ firstReply, secondReply }));
    QVERIFY(PDFAnnotationManipulator::getReplies(storage, page, firstReply) == (std::vector<PDFObjectReference>{ secondReply }));
    QVERIFY(PDFAnnotationManipulator::getReplies(storage, page, other) == (std::vector<PDFObjectReference>{ otherReply }));
    QVERIFY(PDFAnnotationManipulator::getReplies(storage, page, secondReply).empty());
    QVERIFY(PDFAnnotationManipulator::getReplies(storage, PDFObjectReference(), note).empty());
    QVERIFY(PDFAnnotationManipulator::getReplies(storage, number, note).empty());
    QVERIFY(PDFAnnotationManipulator::getReplies(nullptr, page, note).empty());
}

void AnnotationManipulatorTest::copyThread()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page1, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference firstReply = builder.createAnnotationText(page1, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "First", true);
    const PDFObjectReference secondReply = builder.createAnnotationSquare(page1, QRectF(50, 50, 20, 20), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Second");
    setEntry(builder, note, "P", PDFObject());
    setEntry(builder, firstReply, "IRT", PDFObject::createReference(note));
    setEntry(builder, firstReply, "RT", PDFObject::createName("R"));
    setEntry(builder, secondReply, "IRT", PDFObject::createReference(firstReply));
    setEntry(builder, secondReply, "RT", PDFObject::createName("Group"));
    const PDFDocument document = builder.build();

    PDFDocumentBuilder copyBuilder(&document);
    const PDFObjectReference copy = PDFAnnotationManipulator::copyAnnotation(&copyBuilder, note, page2);
    const PDFDocument modified = copyBuilder.build();

    // The note with its popup, the first reply with its popup and the second reply
    const std::vector<PDFObjectReference> annotations = pageAnnotations(modified, 1);
    QCOMPARE(annotations.size(), size_t(5));
    QCOMPARE(annotations[0], copy);
    QCOMPARE(pageAnnotations(modified, 0).size(), size_t(5));

    const PDFObjectReference copiedFirstReply = annotations[2];
    const PDFObjectReference copiedSecondReply = annotations[4];
    QCOMPARE(text(modified, copiedFirstReply, "Contents"), QString("First"));
    QCOMPARE(text(modified, copiedSecondReply, "Contents"), QString("Second"));

    // The thread of the copies is linked the same way, as the original one
    QVERIFY(entry(modified, copy, "IRT").isNull());
    QCOMPARE(entry(modified, copiedFirstReply, "IRT").getReference(), copy);
    QCOMPARE(entry(modified, copiedSecondReply, "IRT").getReference(), copiedFirstReply);
    QCOMPARE(entry(modified, copiedFirstReply, "RT").getString(), QByteArray("R"));
    QCOMPARE(entry(modified, copiedSecondReply, "RT").getString(), QByteArray("Group"));

    for (const PDFObjectReference& annotation : annotations)
    {
        QCOMPARE(entry(modified, annotation, "P").getReference(), page2);
    }

    QCOMPARE(entry(modified, annotations[1], "Parent").getReference(), copy);
    QCOMPARE(entry(modified, annotations[3], "Parent").getReference(), copiedFirstReply);
    QCOMPARE(entry(modified, copiedFirstReply, "Popup").getReference(), annotations[3]);
    QVERIFY(entry(modified, copiedSecondReply, "Popup").isNull());

    // The original thread is untouched
    QCOMPARE(entry(modified, firstReply, "IRT").getReference(), note);
}

void AnnotationManipulatorTest::moveThread()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page1 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference page2 = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page1, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference reply = builder.createAnnotationText(page1, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Reply", true);
    const PDFObjectReference other = builder.createAnnotationSquare(page1, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    setEntry(builder, reply, "IRT", PDFObject::createReference(note));
    const PDFDocument document = builder.build();
    const PDFObjectReference notePopup = entry(document, note, "Popup").getReference();
    const PDFObjectReference replyPopup = entry(document, reply, "Popup").getReference();

    PDFDocumentBuilder moveBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::moveAnnotationToPage(&moveBuilder, note, page1, page2));
    const PDFDocument modified = moveBuilder.build();

    // Replies are displayed with the annotation, so they must be on its page
    QVERIFY(pageAnnotations(modified, 0) == (std::vector<PDFObjectReference>{ other }));
    QVERIFY(pageAnnotations(modified, 1) == (std::vector<PDFObjectReference>{ note, notePopup, reply, replyPopup }));
    QCOMPARE(entry(modified, reply, "IRT").getReference(), note);

    for (const PDFObjectReference& annotation : pageAnnotations(modified, 1))
    {
        QCOMPARE(entry(modified, annotation, "P").getReference(), page2);
    }
}

void AnnotationManipulatorTest::serializeThread()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", true);
    const PDFObjectReference firstReply = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "First", true);
    const PDFObjectReference secondReply = builder.createAnnotationSquare(page, QRectF(50, 50, 20, 20), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Second");
    setEntry(builder, firstReply, "IRT", PDFObject::createReference(note));
    setEntry(builder, firstReply, "RT", PDFObject::createName("R"));
    setEntry(builder, secondReply, "IRT", PDFObject::createReference(firstReply));
    const PDFDocument document = builder.build();

    // The whole thread goes to the clipboard, so cut and paste does not lose the replies
    const PDFAnnotationManipulator::SerializedAnnotations serialized = PDFAnnotationManipulator::deserializeAnnotations(PDFAnnotationManipulator::serializeAnnotations(&document, { note }));
    QVERIFY(serialized.isValid());
    QCOMPARE(serialized.annotations.size(), size_t(1));
    QCOMPARE(serialized.document.getCatalog()->getPage(0)->getAnnotations().size(), size_t(5));

    PDFDocumentBuilder targetBuilder;
    const PDFObjectReference targetPage = targetBuilder.appendPage(QRectF(0, 0, 400, 400));
    const std::vector<PDFObjectReference> inserted = PDFAnnotationManipulator::insertAnnotations(&targetBuilder, targetPage, serialized, QPointF(100, 200));
    const PDFDocument target = targetBuilder.build();

    QCOMPARE(inserted.size(), size_t(1));
    const std::vector<PDFObjectReference> annotations = pageAnnotations(target, 0);
    QCOMPARE(annotations.size(), size_t(5));
    QCOMPARE(annotations[0], inserted.front());
    QCOMPARE(text(target, annotations[2], "Contents"), QString("First"));
    QCOMPARE(text(target, annotations[4], "Contents"), QString("Second"));
    QCOMPARE(entry(target, annotations[2], "IRT").getReference(), annotations[0]);
    QCOMPARE(entry(target, annotations[4], "IRT").getReference(), annotations[2]);
    QCOMPARE(entry(target, annotations[2], "RT").getString(), QByteArray("R"));
    QCOMPARE(entry(target, annotations[3], "Parent").getReference(), annotations[2]);

    // Replies are moved with the annotation and they get new names
    std::set<QByteArray> names;
    for (const size_t index : { size_t(0), size_t(2), size_t(4) })
    {
        // The icon of a note has its own size, so just the position is compared
        const QRectF movedRectangle = rectangle(target, annotations[index]);
        QVERIFY(std::abs(movedRectangle.left() - 150.0) < 0.01);
        QVERIFY(std::abs(movedRectangle.top() - 250.0) < 0.01);
        names.insert(entry(target, annotations[index], "NM").getString());
    }
    QCOMPARE(names.size(), size_t(3));

    // A reply listed before its annotation is copied as a separate annotation with
    // its own replies, it is not copied again, when the thread of the annotation is copied
    const PDFAnnotationManipulator::SerializedAnnotations mixed = PDFAnnotationManipulator::deserializeAnnotations(PDFAnnotationManipulator::serializeAnnotations(&document, { firstReply, note }));
    QCOMPARE(mixed.annotations.size(), size_t(2));
    QCOMPARE(mixed.document.getCatalog()->getPage(0)->getAnnotations().size(), size_t(5));
}

void AnnotationManipulatorTest::appearanceWithoutPageEntry()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF triangle = { QPointF(50, 50), QPointF(150, 50), QPointF(100, 150) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, triangle, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    setEntry(builder, polygon, "P", PDFObject());

    // The media box is inherited from the root of the page tree
    const PDFDocument flatDocument = builder.build();
    PDFDocumentDataLoaderDecorator loader(&flatDocument);
    const PDFObjectReference root = loader.readReferenceFromDictionary(flatDocument.getDictionaryFromObject(flatDocument.getObjectByReference(builder.getCatalogReference())), "Pages");
    setEntry(builder, root, "MediaBox", entry(flatDocument, page, "MediaBox"));
    setEntry(builder, page, "MediaBox", PDFObject());
    const PDFDocument document = builder.build();

    // The appearance must follow the geometry, even if the annotation does not refer to its page
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, polygon, { QPointF(50, 50), QPointF(150, 50), QPointF(300, 300) }));
    PDFDocument modified = modifyBuilder.build();
    QVERIFY(normalAppearance(modified, polygon).isValid());
    QVERIFY(normalAppearance(modified, polygon) != normalAppearance(document, polygon));
    QVERIFY(rectangle(modified, polygon).contains(QPointF(299, 299)));

    modified = transform(document, polygon, QTransform::fromScale(2.0, 2.0));
    QVERIFY(normalAppearance(modified, polygon) != normalAppearance(document, polygon));
}

void AnnotationManipulatorTest::effectiveTransform()
{
    const QRectF rectangle(100, 100, 60, 20);
    const QTransform rotation = QTransform().rotate(30.0);
    const QTransform scaling = QTransform::fromScale(2.0, 3.0);
    const QPointF offset = rotation.map(rectangle.center()) - rectangle.center();
    const QTransform translation = QTransform::fromTranslate(offset.x(), offset.y());

    // Annotations, which support the transformation, are transformed by it, the others are moved
    QCOMPARE(PDFAnnotationManipulator::getEffectiveTransform(AnnotationType::Line, rectangle, rotation), rotation);
    QCOMPARE(PDFAnnotationManipulator::getEffectiveTransform(AnnotationType::Stamp, rectangle, rotation), rotation);
    QCOMPARE(PDFAnnotationManipulator::getEffectiveTransform(AnnotationType::Square, rectangle, scaling), scaling);
    QVERIFY(qFuzzyCompare(PDFAnnotationManipulator::getEffectiveTransform(AnnotationType::Square, rectangle, rotation), translation));
    QVERIFY(qFuzzyCompare(PDFAnnotationManipulator::getEffectiveTransform(AnnotationType::Text, rectangle, rotation), translation));
    QCOMPARE(PDFAnnotationManipulator::getEffectiveTransform(AnnotationType::Link, rectangle, rotation), QTransform());
}

void AnnotationManipulatorTest::markedRegionEnds()
{
    // Two marked lines of a text. Corners of each line: top left, top right, bottom left, bottom right.
    const QPolygonF lines = { QPointF(100, 212), QPointF(300, 212), QPointF(100, 200), QPointF(300, 200),
                              QPointF(100, 192), QPointF(200, 192), QPointF(100, 180), QPointF(200, 180) };

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, lines, Qt::yellow);
    const PDFObjectReference redact = builder.createAnnotationRedact(page, QRectF(50, 50, 100, 20), Qt::black, Qt::red);
    const PDFDocument document = builder.build();

    auto getPoints = [](const PDFDocument& currentDocument, PDFObjectReference annotation)
    {
        return PDFAnnotationManipulator::getEditablePoints(PDFAnnotation::parse(&currentDocument.getStorage(), annotation).data());
    };

    // Each line has a point at its start and at its end, they cannot be inserted or removed
    PDFAnnotationManipulator::EditablePoints points = getPoints(document, highlight);
    QVERIFY(points.isQuadEnds);
    QVERIFY(points.points == (std::vector<QPointF>{ QPointF(100, 206), QPointF(300, 206), QPointF(100, 186), QPointF(200, 186) }));
    QVERIFY(!points.canInsertPoint());
    QVERIFY(!points.canRemovePoint(0));

    points = getPoints(document, redact);
    QVERIFY(points.isQuadEnds);
    QVERIFY(points.points == (std::vector<QPointF>{ QPointF(50, 60), QPointF(150, 60) }));

    // The second line is made longer, the first one shorter. The points move only along the
    // line, so the lines keep their height.
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, highlight, { QPointF(150, 999), QPointF(300, 206), QPointF(100, 186), QPointF(250, 190) }));
    const PDFDocument modified = modifyBuilder.build();

    QVERIFY(fuzzyCompare(numbers(modified, highlight, "QuadPoints"), std::vector<PDFReal>{ 150, 212, 300, 212, 150, 200, 300, 200,
                                                                                          100, 192, 250, 192, 100, 180, 250, 180 }));
    QVERIFY(normalAppearance(modified, highlight) != normalAppearance(document, highlight));
    QVERIFY(rectangle(modified, highlight).contains(QPointF(249, 186)));
    QVERIFY(rectangle(modified, highlight).contains(QPointF(299, 206)));
    QVERIFY(!rectangle(modified, highlight).contains(QPointF(320, 206)));

    // The end of a line cannot get before its start, the number of the points is fixed
    PDFDocumentBuilder invalidBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&invalidBuilder, highlight, { QPointF(100, 206), QPointF(300, 206), QPointF(100, 186), QPointF(90, 186) }));
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&invalidBuilder, highlight, { QPointF(100, 206), QPointF(300, 206), QPointF(199.5, 186), QPointF(200, 186) }));
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&invalidBuilder, highlight, { QPointF(100, 206), QPointF(300, 206) }));
    QVERIFY(fuzzyCompare(numbers(invalidBuilder.build(), highlight, "QuadPoints"), numbers(document, highlight, "QuadPoints")));
}

void AnnotationManipulatorTest::markedRegionEndsSpecialCases()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference degenerated = builder.createAnnotationHighlight(page, QRectF(50, 50, 100, 20), Qt::yellow);
    const PDFObjectReference crowded = builder.createAnnotationHighlight(page, QRectF(50, 50, 100, 20), Qt::yellow);
    const PDFObjectReference withoutRegions = builder.createAnnotationHighlight(page, QRectF(50, 50, 100, 20), Qt::yellow);

    setEntry(builder, degenerated, "QuadPoints", numberArray({ 50, 50, 50, 50, 50, 50, 50, 50 }));
    setEntry(builder, withoutRegions, "QuadPoints", PDFObject());

    PDFObjectFactory factory;
    factory.beginArray();
    for (int i = 0; i < 33; ++i)
    {
        factory << PDFReal(50) << PDFReal(62 + i) << PDFReal(150) << PDFReal(62 + i) << PDFReal(50) << PDFReal(50 + i) << PDFReal(150) << PDFReal(50 + i);
    }
    factory.endArray();
    setEntry(builder, crowded, "QuadPoints", factory.takeObject());
    const PDFDocument document = builder.build();

    auto getPoints = [](const PDFDocument& currentDocument, PDFObjectReference annotation)
    {
        return PDFAnnotationManipulator::getEditablePoints(PDFAnnotation::parse(&currentDocument.getStorage(), annotation).data());
    };

    // Each region has its points, whatever the count of the regions is
    QCOMPARE(getPoints(document, crowded).points.size(), size_t(66));

    // Region without a size has no direction, so the x axis is used. Such a region is not
    // visible, so the annotation rectangle is used as another region by the parser.
    PDFDocumentBuilder modifyBuilder(&document);
    QCOMPARE(getPoints(document, degenerated).points.size(), size_t(4));
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, degenerated, { QPointF(50, 50), QPointF(80, 70), QPointF(50, 60), QPointF(150, 60) }));

    // If the regions are missing, then the annotation rectangle is the marked region
    QVERIFY(getPoints(document, withoutRegions).points == (std::vector<QPointF>{ QPointF(50, 60), QPointF(150, 60) }));
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, withoutRegions, { QPointF(50, 60), QPointF(250, 60) }));
    const PDFDocument modified = modifyBuilder.build();

    QVERIFY(fuzzyCompare(numbers(modified, degenerated, "QuadPoints"), std::vector<PDFReal>{ 50, 50, 80, 50, 50, 50, 80, 50, 50, 50, 150, 50, 50, 70, 150, 70 }));
    QVERIFY(fuzzyCompare(numbers(modified, withoutRegions, "QuadPoints"), std::vector<PDFReal>{ 50, 50, 250, 50, 50, 70, 250, 70 }));
}

void AnnotationManipulatorTest::removableParts()
{
    const QPolygonF lines = { QPointF(100, 212), QPointF(300, 212), QPointF(100, 200), QPointF(300, 200),
                              QPointF(100, 192), QPointF(200, 192), QPointF(100, 180), QPointF(200, 180) };
    const Polygons strokes = { QPolygonF({ QPointF(10, 10), QPointF(50, 50), QPointF(90, 10) }), QPolygonF({ QPointF(200, 300), QPointF(250, 350) }) };

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, lines, Qt::yellow);
    const PDFObjectReference ink = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference inkWithPath = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference inkWithoutList = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));

    PDFObjectFactory factory;
    factory.beginArray();
    factory << std::vector<PDFReal>{ 10, 10 } << std::vector<PDFReal>{ 50, 50 };
    factory.endArray();
    setEntry(builder, inkWithPath, "Path", factory.takeObject());
    setEntry(builder, inkWithoutList, "InkList", PDFObject::createInteger(1));
    const PDFDocument document = builder.build();
    const PDFObjectStorage* storage = &document.getStorage();

    // Marked regions are areas, the polygon goes around the region
    PDFAnnotationManipulator::Parts parts = PDFAnnotationManipulator::getParts(storage, highlight);
    QVERIFY(parts.isFilled);
    QVERIFY(parts.canRemovePart());
    QCOMPARE(parts.shapes.size(), size_t(2));
    QCOMPARE(parts.shapes[1], QPolygonF({ QPointF(100, 192), QPointF(200, 192), QPointF(200, 180), QPointF(100, 180) }));

    // Strokes of an ink are lines
    parts = PDFAnnotationManipulator::getParts(storage, ink);
    QVERIFY(!parts.isFilled);
    QCOMPARE(parts.shapes.size(), size_t(2));
    QCOMPARE(parts.shapes[0], strokes[0]);

    // Ink defined by a path is a single stroke, other annotations have no parts
    QVERIFY(PDFAnnotationManipulator::getParts(storage, inkWithPath).shapes.empty());
    QVERIFY(PDFAnnotationManipulator::getParts(storage, inkWithoutList).shapes.empty());
    QVERIFY(PDFAnnotationManipulator::getParts(storage, square).shapes.empty());
    QVERIFY(PDFAnnotationManipulator::getParts(storage, number).shapes.empty());
    QVERIFY(PDFAnnotationManipulator::getParts(nullptr, highlight).shapes.empty());

    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::removePart(&modifyBuilder, highlight, 0));
    QVERIFY(PDFAnnotationManipulator::removePart(&modifyBuilder, ink, 1));

    // The last part cannot be removed (the annotation should be deleted instead)
    QVERIFY(!PDFAnnotationManipulator::removePart(&modifyBuilder, highlight, 0));
    QVERIFY(!PDFAnnotationManipulator::removePart(&modifyBuilder, ink, 0));
    QVERIFY(!PDFAnnotationManipulator::removePart(&modifyBuilder, square, 0));
    QVERIFY(!PDFAnnotationManipulator::removePart(nullptr, highlight, 0));

    PDFDocumentBuilder invalidBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::removePart(&invalidBuilder, highlight, 2));
    const PDFDocument modified = modifyBuilder.build();

    QVERIFY(fuzzyCompare(numbers(modified, highlight, "QuadPoints"), std::vector<PDFReal>{ 100, 192, 200, 192, 100, 180, 200, 180 }));
    QVERIFY(!rectangle(modified, highlight).contains(QPointF(290, 206)));
    QVERIFY(rectangle(modified, highlight).contains(QPointF(150, 186)));
    QVERIFY(normalAppearance(modified, highlight) != normalAppearance(document, highlight));

    const PDFAnnotationManipulator::Parts remainingStrokes = PDFAnnotationManipulator::getParts(&modified.getStorage(), ink);
    QCOMPARE(remainingStrokes.shapes.size(), size_t(1));
    QCOMPARE(remainingStrokes.shapes[0], strokes[0]);
    QVERIFY(!rectangle(modified, ink).contains(QPointF(225, 325)));
    QVERIFY(rectangle(modified, ink).contains(QPointF(50, 30)));
}

void AnnotationManipulatorTest::freeTextRectangle()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference plain = builder.createAnnotationFreeText(page, QRectF(210, 210, 100, 50), "Title", "Subject", "Contents", Qt::AlignLeft);
    const PDFObjectReference cloudy = builder.createAnnotationFreeText(page, QRectF(210, 210, 100, 50), "Title", "Subject", "Contents", Qt::AlignLeft);
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(10, 10, 190, 140), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(20, 20), QPointF(60, 60), QPointF(110, 120), AnnotationLineEnding::None, AnnotationLineEnding::OpenArrow);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference number = builder.addObject(PDFObject::createInteger(5));
    setEntry(builder, cloudy, "RD", numberArray({ 5.0, 5.0, 5.0, 5.0 }));
    const PDFDocument document = builder.build();
    const PDFObjectStorage* storage = &document.getStorage();

    QVERIFY(fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(storage, plain), QRectF(210, 210, 100, 50)));
    QVERIFY(fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(storage, cloudy), QRectF(215, 215, 90, 40)));
    QVERIFY(fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(storage, callout), QRectF(110, 110, 80, 30)));
    QVERIFY(!PDFAnnotationManipulator::getFreeTextRectangle(storage, square).isValid());
    QVERIFY(!PDFAnnotationManipulator::getFreeTextRectangle(storage, number).isValid());
    QVERIFY(!PDFAnnotationManipulator::getFreeTextRectangle(nullptr, plain).isValid());

    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setFreeTextRectangle(&modifyBuilder, plain, QRectF(300, 300, 50, 20)));
    QVERIFY(PDFAnnotationManipulator::setFreeTextRectangle(&modifyBuilder, cloudy, QRectF(300, 300, 50, 20)));
    QVERIFY(PDFAnnotationManipulator::setFreeTextRectangle(&modifyBuilder, callout, QRectF(200, 200, 160, 90)));
    QVERIFY(!PDFAnnotationManipulator::setFreeTextRectangle(&modifyBuilder, square, QRectF(300, 300, 50, 20)));
    QVERIFY(!PDFAnnotationManipulator::setFreeTextRectangle(&modifyBuilder, plain, QRectF()));
    QVERIFY(!PDFAnnotationManipulator::setFreeTextRectangle(nullptr, plain, QRectF(300, 300, 50, 20)));
    const PDFDocument modified = modifyBuilder.build();

    // Space for the border effect is kept
    QVERIFY(fuzzyCompare(rectangle(modified, plain), QRectF(300, 300, 50, 20)));
    QVERIFY(fuzzyCompare(rectangle(modified, cloudy), QRectF(295, 295, 60, 30)));
    QVERIFY(normalAppearance(modified, plain) != normalAppearance(document, plain));

    // The tip of the callout line stays, the knee and the end keep their position relative
    // to the text box (the text box is twice as wide and three times as high)
    QVERIFY(fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(&modified.getStorage(), callout), QRectF(200, 200, 160, 90)));
    QVERIFY(fuzzyCompare(numbers(modified, callout, "CL"), std::vector<PDFReal>{ 20, 20, 100, 50, 200, 230 }));
    QVERIFY(rectangle(modified, callout).contains(QPointF(20, 20)));
    QVERIFY(rectangle(modified, callout).contains(QPointF(359, 289)));
}

void AnnotationManipulatorTest::freeTextCalloutLine()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference plain = builder.createAnnotationFreeText(page, QRectF(210, 210, 100, 50), "Title", "Subject", "Contents", Qt::AlignLeft);
    const PDFObjectReference callout = builder.createAnnotationFreeText(page, QRectF(10, 10, 190, 140), QRectF(110, 110, 80, 30), "Title", "Subject", "Contents",
                                                                        Qt::AlignLeft, QPointF(20, 20), QPointF(110, 120), AnnotationLineEnding::Circle, AnnotationLineEnding::None);
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    // New callout line - the text box stays, the annotation rectangle covers the line
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setFreeTextCalloutLine(&modifyBuilder, plain, { QPointF(100, 100), QPointF(210, 235) }));
    PDFDocument modified = modifyBuilder.build();

    QVERIFY(fuzzyCompare(numbers(modified, plain, "CL"), std::vector<PDFReal>{ 100, 100, 210, 235 }));
    QCOMPARE(entry(modified, plain, "IT").getString(), QByteArray("FreeTextCallout"));
    QVERIFY(fuzzyCompare(PDFAnnotationManipulator::getFreeTextRectangle(&modified.getStorage(), plain), QRectF(210, 210, 100, 50)));
    QVERIFY(rectangle(modified, plain).contains(QPointF(100, 100)));
    QVERIFY(normalAppearance(modified, plain) != normalAppearance(document, plain));

    PDFDocumentDataLoaderDecorator loader(&modified);
    QVERIFY(loader.readNameArrayFromDictionary(modified.getDictionaryFromObject(modified.getObjectByReference(plain)), "LE") == (std::vector<QByteArray>{ "OpenArrow", "None" }));

    // The callout line is replaced (a knee is added), its line ending is kept
    PDFDocumentBuilder replaceBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setFreeTextCalloutLine(&replaceBuilder, callout, { QPointF(30, 30), QPointF(70, 120), QPointF(110, 120) }));
    modified = replaceBuilder.build();
    PDFDocumentDataLoaderDecorator replacedLoader(&modified);
    QVERIFY(fuzzyCompare(numbers(modified, callout, "CL"), std::vector<PDFReal>{ 30, 30, 70, 120, 110, 120 }));
    QVERIFY(replacedLoader.readNameArrayFromDictionary(modified.getDictionaryFromObject(modified.getObjectByReference(callout)), "LE") == (std::vector<QByteArray>{ "Circle", "None" }));

    // The callout line is removed, the annotation rectangle is the text box again
    PDFDocumentBuilder removeBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setFreeTextCalloutLine(&removeBuilder, callout, { }));
    modified = removeBuilder.build();
    QVERIFY(entry(modified, callout, "CL").isNull());
    QVERIFY(entry(modified, callout, "RD").isNull());
    QVERIFY(entry(modified, callout, "LE").isNull());
    QCOMPARE(entry(modified, callout, "IT").getString(), QByteArray("FreeText"));
    QVERIFY(fuzzyCompare(rectangle(modified, callout), QRectF(110, 110, 80, 30)));

    // Invalid arguments
    PDFDocumentBuilder invalidBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::setFreeTextCalloutLine(&invalidBuilder, plain, { QPointF(100, 100) }));
    QVERIFY(!PDFAnnotationManipulator::setFreeTextCalloutLine(&invalidBuilder, plain, { QPointF(1, 1), QPointF(2, 2), QPointF(3, 3), QPointF(4, 4) }));
    QVERIFY(!PDFAnnotationManipulator::setFreeTextCalloutLine(&invalidBuilder, square, { QPointF(1, 1), QPointF(2, 2) }));
    QVERIFY(!PDFAnnotationManipulator::setFreeTextCalloutLine(nullptr, plain, { QPointF(1, 1), QPointF(2, 2) }));
}

void AnnotationManipulatorTest::reviewMeasurementAmbiguousArea()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF square = { QPointF(100, 100), QPointF(200, 100), QPointF(200, 200), QPointF(100, 200) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, square, 1.0, Qt::yellow, Qt::black, "Title", "Area", "");
    // Four metres on each side: perimeter and area both have the numeric value 16.
    setMeasurement(builder, polygon, "PolygonDimension", "A = 16.00 sq m", measure(0.04, 0.0, 1.0, 1.0, 0.0));
    const PDFDocument document = builder.build();
    PDFDocumentBuilder modifiedBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::transformAnnotation(&modifiedBuilder, polygon, QTransform::fromScale(2.0, 2.0)));
    const PDFDocument modified = modifiedBuilder.build();
    QVERIFY(fuzzyCompare(numbers(modified, polygon, "Vertices"), std::vector<PDFReal>{ 200, 200, 400, 200, 400, 400, 200, 400 }));
    QCOMPARE(text(modified, polygon, "Contents"), QString("A = 64.00 sq m"));
}

void AnnotationManipulatorTest::reviewMeasurementPreservesCommentNumber()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 300, 300), QPointF(50, 150), QPointF(150, 150),
        1.0, Qt::red, Qt::blue, "Title", "Subject", "", AnnotationLineEnding::None, AnnotationLineEnding::None);
    setMeasurement(builder, line, "LineDimension", "Wall 100: 100.00 mm", measure(1.0, 0.0, 1.0, 0.0, 0.0));
    const PDFDocument document = builder.build();
    PDFDocumentBuilder modifiedBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::transformAnnotation(&modifiedBuilder, line, QTransform::fromScale(2.0, 2.0)));
    const PDFDocument modified = modifiedBuilder.build();
    QVERIFY(fuzzyCompare(numbers(modified, line, "L"), std::vector<PDFReal>{ 100, 300, 300, 300 }));
    QCOMPARE(text(modified, line, "Contents"), QString("Wall 100: 200.00 mm"));
}

void AnnotationManipulatorTest::reviewMeasurementUsesPath()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF vertices = { QPointF(100, 100), QPointF(200, 100), QPointF(200, 200), QPointF(100, 200) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, vertices, 1.0, Qt::yellow, Qt::black, "Title", "Area", "");
    PDFObjectFactory factory;
    factory.beginArray();
    factory << std::vector<PDFReal>{ 100, 100 } << std::vector<PDFReal>{ 300, 100 }
            << std::vector<PDFReal>{ 300, 200 } << std::vector<PDFReal>{ 100, 200 };
    factory.endArray();
    setEntry(builder, polygon, "Path", factory.takeObject());
    setMeasurement(builder, polygon, "PolygonDimension", "A = 200.00 sq m", measure(0.1, 0.0, 1.0, 1.0, 0.0));
    const PDFDocument document = builder.build();
    PDFDocumentBuilder modifiedBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::transformAnnotation(&modifiedBuilder, polygon, QTransform::fromScale(2.0, 2.0)));
    const PDFDocument modified = modifiedBuilder.build();
    const PDFAnnotationPtr parsed = PDFAnnotation::parse(&modified.getStorage(), polygon);
    const auto* polygonal = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(parsed.data());
    QVERIFY(polygonal);
    QVERIFY(fuzzyCompare(polygonal->getPath().boundingRect(), QRectF(200, 200, 400, 200)));
    QCOMPARE(text(modified, polygon, "Contents"), QString("A = 800.00 sq m"));
}

void AnnotationManipulatorTest::measurementField()
{
    const QTransform scaling = QTransform::fromScale(2.0, 2.0);
    const PDFObject plainMeasure = measure(1.0, 0.0, 0.0, 0.0, 0.0);

    // The line is 100 units long. The value formatted by the number format of the
    // measure is the measured value, whatever other numbers the text contains.
    QCOMPARE(transformedLineContents("Wall 100: 100.00 u", plainMeasure, scaling), QString("Wall 100: 200.00 u"));
    QCOMPARE(transformedLineContents("100 walls, each 100.00 u long", plainMeasure, scaling), QString("100 walls, each 200.00 u long"));

    // Value displayed in several units (5 ft 3 in)
    const PDFObject imperialMeasure = measureOf({ { "X", numberFormats({ { "ft", 0.0525 }, { "in", 12.0 } }) } });
    QCOMPARE(transformedLineContents("Height 5 ft 3.00 in", imperialMeasure, scaling), QString("Height 10 ft 6.00 in"));

    // The formatted value is there twice, and it is a part of a longer number
    QCOMPARE(transformedLineContents("100.00 u / 100.00 u", plainMeasure, scaling), QString("100.00 u / 100.00 u"));
    QCOMPARE(transformedLineContents("1100.00 u", plainMeasure, scaling), QString("1100.00 u"));
    QCOMPARE(transformedLineContents("100.00 um", plainMeasure, scaling), QString("200.00 um"));

    // A number followed by a colon (by an equal sign) is a label
    QCOMPARE(transformedLineContents("Wall 100: 100.0 mm", plainMeasure, scaling), QString("Wall 100: 200.0 mm"));
    QCOMPARE(transformedLineContents("Wall 100 = 100.0 mm", plainMeasure, scaling), QString("Wall 100 = 200.0 mm"));
    QCOMPARE(transformedLineContents("Wall 100:", plainMeasure, scaling), QString("Wall 100:"));

    // Line measures one quantity, so its symbol can be anything (side a of a triangle)
    QCOMPARE(transformedLineContents("L = 100.0 mm", plainMeasure, scaling), QString("L = 200.0 mm"));
    QCOMPARE(transformedLineContents("a = 100.0 mm", plainMeasure, scaling), QString("a = 200.0 mm"));

    // Two equal numbers - the one with the unit of the measure is the measured value
    const PDFObject metreMeasure = measureOf({ { "X", numberFormats({ { "m", 1.0 } }) } });
    QCOMPARE(transformedLineContents("100.0 m (100.0 mm)", metreMeasure, scaling), QString("200.0 m (100.0 mm)"));
    QCOMPARE(transformedLineContents("100.0 mm (100.0 m)", metreMeasure, scaling), QString("100.0 mm (200.0 m)"));
    QCOMPARE(transformedLineContents("100.0 m^2 (100.0 m)", metreMeasure, scaling), QString("100.0 m^2 (200.0 m)"));

    // ...and if nothing distinguishes them, then the text is left alone
    QCOMPARE(transformedLineContents("100.0 mm, 100.0 mm", plainMeasure, scaling), QString("100.0 mm, 100.0 mm"));
    QCOMPARE(transformedLineContents("Wall 100 is 100.0 mm long", plainMeasure, scaling), QString("Wall 100 is 100.0 mm long"));

    // The measure does not define the label of the unit
    const PDFObject noUnitMeasure = measureOf({ { "X", numberFormats({ { "", 1.0 } }) } });
    QCOMPARE(transformedLineContents("100.0 mm", noUnitMeasure, scaling), QString("200.0 mm"));

    // Digits, which are a part of a word, are not numbers
    QCOMPARE(transformedLineContents("W100 100.0 mm", plainMeasure, scaling), QString("W100 200.0 mm"));
}

void AnnotationManipulatorTest::measurementFieldOfPolygon()
{
    const QTransform scaling = QTransform::fromScale(2.0, 2.0);

    auto transformedPolygonContents = [&](const QString& contents, PDFObject measureObject)
    {
        PDFDocumentBuilder builder;
        const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
        const QPolygonF square = { QPointF(100, 100), QPointF(200, 100), QPointF(200, 200), QPointF(100, 200) };
        const PDFObjectReference polygon = builder.createAnnotationPolygon(page, square, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "");
        setMeasurement(builder, polygon, "PolygonDimension", contents, measureObject);
        return transformedContents(builder.build(), polygon, scaling);
    };

    // Perimeter is 40, area is 100. Both values are updated.
    const PDFObject plainMeasure = measure(0.1, 0.0, 0.0, 0.0, 0.0);
    QCOMPARE(transformedPolygonContents("P = 40.00 m, A = 100.00 sq m", plainMeasure), QString("P = 80.00 m, A = 400.00 sq m"));
    QCOMPARE(transformedPolygonContents("A = 100.00 sq m, P = 40.00 m", plainMeasure), QString("A = 400.00 sq m, P = 80.00 m"));

    // Perimeter is found as the formatted value, the area as a number
    QCOMPARE(transformedPolygonContents("P = 40.00 u, A = 100.00 sq m", measure(0.1, 0.0, 1.0, 0.0, 0.0)), QString("P = 80.00 u, A = 400.00 sq m"));
    QCOMPARE(transformedPolygonContents("A = 100.00 sq m, P = 40.00 u", measure(0.1, 0.0, 1.0, 0.0, 0.0)), QString("A = 400.00 sq m, P = 80.00 u"));

    // Number with the symbol of the quantity has precedence over an equal number
    QCOMPARE(transformedPolygonContents("Room 40 P = 40.00 m", plainMeasure), QString("Room 40 P = 80.00 m"));
    QCOMPARE(transformedPolygonContents("Room 100 has 100.00 m2", plainMeasure), QString("Room 100 has 400.00 m2"));

    // The unit of the measure is a stronger evidence, than the symbol of the quantity
    QCOMPARE(transformedPolygonContents("P = 40.0 m, 40.0 u", plainMeasure), QString("P = 40.0 m, 80.0 u"));
    QCOMPARE(transformedPolygonContents("40.0 u, P = 40.0 m", plainMeasure), QString("80.0 u, P = 40.0 m"));
    QCOMPARE(transformedPolygonContents("40.0 u 40.0 u", plainMeasure), QString("40.0 u 40.0 u"));

    // The number is equal to the perimeter, but it is an area according to the text
    QCOMPARE(transformedPolygonContents("A = 40.00 sq m", plainMeasure), QString("A = 40.00 sq m"));
    QCOMPARE(transformedPolygonContents("40.00 m2", plainMeasure), QString("40.00 m2"));
    QCOMPARE(transformedPolygonContents("P = 100.00 m", plainMeasure), QString("P = 100.00 m"));

    // The symbol and the unit contradict each other, so the value decides
    QCOMPARE(transformedPolygonContents("P = 40.00 m2", plainMeasure), QString("P = 80.00 m2"));

    // The perimeter and the area have the same value (the side of the square is 4)
    const PDFObject equalMeasure = measure(0.04, 0.0, 1.0, 1.0, 0.0);
    QCOMPARE(transformedPolygonContents("16.00 u", equalMeasure), QString("16.00 u"));
    QCOMPARE(transformedPolygonContents("16.0 u", equalMeasure), QString("16.0 u"));
    QCOMPARE(transformedPolygonContents("A = 16.00 u", equalMeasure), QString("A = 64.00 u"));
    QCOMPARE(transformedPolygonContents("P = 16.00 u", equalMeasure), QString("P = 32.00 u"));
    QCOMPARE(transformedPolygonContents("Perimeter: 16.00 u, Area: 16.00 u", equalMeasure), QString("Perimeter: 32.00 u, Area: 64.00 u"));

    // The units of the measure distinguish the quantities
    const PDFObject unitsMeasure = measureOf({ { "X", numberFormats({ { "m", 0.04 } }) }, { "D", numberFormats({ { "m", 1.0 } }) }, { "A", numberFormats({ { "sq m", 1.0 } }) } });
    QCOMPARE(transformedPolygonContents("16.00 sq m", unitsMeasure), QString("64.00 sq m"));
    QCOMPARE(transformedPolygonContents("16.00 m", unitsMeasure), QString("32.00 m"));
    QCOMPARE(transformedPolygonContents("16.0 sq m", unitsMeasure), QString("64.0 sq m"));
    QCOMPARE(transformedPolygonContents("16.0 m", unitsMeasure), QString("32.0 m"));

    // The scale is not known - the quantity is recognized by its symbol, or by a unit of area
    QCOMPARE(transformedPolygonContents("100 m2", PDFObject()), QString("400 m2"));
    QCOMPARE(transformedPolygonContents("100 m^2", PDFObject()), QString("400 m^2"));
    QCOMPARE(transformedPolygonContents(QString("100 m") + QChar(0x00B2), PDFObject()), QString("400 m") + QChar(0x00B2));
    QCOMPARE(transformedPolygonContents("100 sq m", PDFObject()), QString("400 sq m"));
    QCOMPARE(transformedPolygonContents("100 sqm", PDFObject()), QString("100 sqm"));
    QCOMPARE(transformedPolygonContents("L = 40 m", PDFObject()), QString("L = 80 m"));
    QCOMPARE(transformedPolygonContents("D: 40 m", PDFObject()), QString("D: 80 m"));
    QCOMPARE(transformedPolygonContents("P = 40 m2", PDFObject()), QString("P = 40 m2"));
    QCOMPARE(transformedPolygonContents("40 m", PDFObject()), QString("40 m"));
}

void AnnotationManipulatorTest::measurementOfEditedShape()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const std::vector<QPointF> square = { QPointF(100, 100), QPointF(200, 100), QPointF(200, 200), QPointF(100, 200) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, QPolygonF(QList<QPointF>(square.cbegin(), square.cend())), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "");
    setMeasurement(builder, polygon, "PolygonDimension", "P = 40.00 m, A = 100.00 sq m", measure(0.1, 0.0, 0.0, 0.0, 0.0));
    PDFDocument document = builder.build();

    // A single vertex is moved...
    std::vector<QPointF> edited = square;
    edited[2] = QPointF(300, 300);
    {
        PDFDocumentBuilder modifyBuilder(&document);
        QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, polygon, edited));
        document = modifyBuilder.build();
    }
    QCOMPARE(text(document, polygon, "Contents"), QString("P = 64.72 m, A = 200.00 sq m"));

    // ...the polygon is scaled (the displayed value is rounded, the new value is exact)...
    document = transform(document, polygon, QTransform::fromScale(0.5, 0.5));
    QCOMPARE(text(document, polygon, "Contents"), QString("P = 32.36 m, A = 50.00 sq m"));
    document = transform(document, polygon, QTransform::fromScale(2.0, 2.0));
    QCOMPARE(text(document, polygon, "Contents"), QString("P = 64.72 m, A = 200.00 sq m"));

    // ...and the vertex is moved back
    {
        PDFDocumentBuilder modifyBuilder(&document);
        QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, polygon, square));
        document = modifyBuilder.build();
    }
    QCOMPARE(text(document, polygon, "Contents"), QString("P = 40.00 m, A = 100.00 sq m"));
}

void AnnotationManipulatorTest::measurementOfPath()
{
    const QTransform scaling = QTransform::fromScale(2.0, 2.0);
    const QPolygonF vertices = { QPointF(100, 100), QPointF(200, 100), QPointF(200, 200), QPointF(100, 200) };

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));

    // Polygon defined just by its path (rectangle 200 x 100)
    const PDFObjectReference pathOnly = builder.createAnnotationPolygon(page, vertices, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "");
    setEntry(builder, pathOnly, "Path", pathArray({ { 100, 100 }, { 300, 100 }, { 300, 200 }, { 100, 200 } }));
    setEntry(builder, pathOnly, "Vertices", PDFObject());
    setMeasurement(builder, pathOnly, "PolygonDimension", "P = 60.00 m, A = 200.00 sq m", measure(0.1, 0.0, 0.0, 0.0, 0.0));

    // Path with a curve (the scale is not known, the value is scaled by the ratio)
    const PDFObjectReference curve = builder.createAnnotationPolygon(page, vertices, 1.0, Qt::yellow, Qt::black, "Title", "Subject", "");
    setEntry(builder, curve, "Path", pathArray({ { 100, 100 }, { 200, 100 }, { 200, 150, 200, 200, 150, 200 }, { 100, 200 } }));
    setMeasurement(builder, curve, "PolygonDimension", "A = 100 a", PDFObject());

    // Angle defined by a path
    const QPolygonF angleVertices = { QPointF(100, 150), QPointF(100, 100), QPointF(150, 100) };
    const PDFObjectReference angle = builder.createAnnotationPolyline(page, angleVertices, 1.0, QColor(), Qt::black, "Title", "Subject", "",
                                                                      AnnotationLineEnding::None, AnnotationLineEnding::None);
    setEntry(builder, angle, "Path", pathArray({ { 100, 150 }, { 100, 100 }, { 150, 100 } }));
    setMeasurement(builder, angle, "PolyLineDimension", "90.00 deg", measure(1.0, 0.0, 0.0, 0.0, 1.0));
    const PDFDocument document = builder.build();

    QCOMPARE(transformedContents(document, pathOnly, scaling), QString("P = 120.00 m, A = 800.00 sq m"));
    QCOMPARE(transformedContents(document, curve, scaling), QString("A = 400 a"));

    // A single point of the path is moved
    const PDFAnnotationPtr parsed = PDFAnnotation::parse(&document.getStorage(), pathOnly);
    std::vector<QPointF> points = PDFAnnotationManipulator::getEditablePoints(parsed.data()).points;
    QCOMPARE(points.size(), size_t(4));
    points[1] = QPointF(200, 100);
    points[2] = QPointF(200, 200);
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, pathOnly, points));
    PDFDocument modified = modifyBuilder.build();
    QCOMPARE(text(modified, pathOnly, "Contents"), QString("P = 40.00 m, A = 100.00 sq m"));
    QVERIFY(entry(modified, pathOnly, "Vertices").isNull());

    // Mirroring keeps the angle - the order of the points of the path is reversed too
    modified = transform(document, angle, QTransform::fromScale(-1.0, 1.0));
    QCOMPARE(text(modified, angle, "Contents"), QString("90.00 deg"));
    QVERIFY(fuzzyCompare(numbers(modified, angle, "Vertices"), std::vector<PDFReal>{ -150, 100, -100, 100, -100, 150 }));

    const PDFObject path = entry(modified, angle, "Path");
    QVERIFY(path.isArray());
    QCOMPARE(path.getArray()->getCount(), size_t(3));
    PDFDocumentDataLoaderDecorator loader(&modified.getStorage());
    QVERIFY(fuzzyCompare(loader.readNumberArray(path.getArray()->getItem(0)), std::vector<PDFReal>{ -150, 100 }));
    QVERIFY(fuzzyCompare(loader.readNumberArray(path.getArray()->getItem(2)), std::vector<PDFReal>{ -100, 150 }));
}

void AnnotationManipulatorTest::setRectangle()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const QPolygonF vertices = { QPointF(100, 100), QPointF(200, 100), QPointF(200, 200), QPointF(100, 200) };
    const PDFObjectReference polygon = builder.createAnnotationPolygon(page, vertices, 4.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference line = builder.createAnnotationLine(page, QRectF(0, 0, 400, 400), QPointF(50, 150), QPointF(150, 250), 2.0, Qt::red, Qt::blue,
                                                                 "Title", "Subject", "Contents", AnnotationLineEnding::OpenArrow, AnnotationLineEnding::ClosedArrow);
    const PDFObjectReference note = builder.createAnnotationText(page, QRectF(50, 50, 20, 20), TextAnnotationIcon::Note, "Title", "Subject", "Contents", false);
    const PDFObjectReference link = builder.createAnnotationLink(page, QRectF(300, 300, 50, 20), QString("https://example.com"), LinkHighlightMode::Invert);
    const PDFObjectReference empty = builder.createAnnotationSquare(page, QRectF(10, 10, 50, 30), 1.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    setEntry(builder, empty, "Rect", numberArray({ 10.0, 10.0, 10.0, 10.0 }));
    const PDFDocument document = builder.build();

    // The rectangle of a polygon is its geometry with the margin of the line. The margin is
    // not scaled with the geometry, but the rectangle, which the user has asked for, is reached.
    for (const PDFObjectReference annotation : { polygon, line })
    {
        const QRectF oldRectangle = rectangle(document, annotation);
        const QRectF newRectangle(oldRectangle.left() + 30.0, oldRectangle.top() - 20.0, oldRectangle.width() * 2.5, oldRectangle.height() * 0.5);

        PDFDocumentBuilder modifyBuilder(&document);
        QVERIFY(PDFAnnotationManipulator::setRectangle(&modifyBuilder, annotation, newRectangle));
        const PDFDocument modified = modifyBuilder.build();
        QVERIFY(fuzzyCompare(rectangle(modified, annotation), newRectangle, 0.01));

        // Nothing to do
        PDFDocumentBuilder sameBuilder(&modified);
        QVERIFY(!PDFAnnotationManipulator::setRectangle(&sameBuilder, annotation, rectangle(modified, annotation)));
    }

    // The geometry follows the rectangle
    {
        PDFDocumentBuilder modifyBuilder(&document);
        const QRectF oldRectangle = rectangle(document, polygon);
        QVERIFY(PDFAnnotationManipulator::setRectangle(&modifyBuilder, polygon, oldRectangle.translated(50.0, 0.0)));
        const PDFDocument modified = modifyBuilder.build();
        QVERIFY(fuzzyCompare(numbers(modified, polygon, "Vertices"), std::vector<PDFReal>{ 150, 100, 250, 100, 250, 200, 150, 200 }));
    }

    // The icon cannot be resized, it is moved to the center of the rectangle
    {
        PDFDocumentBuilder modifyBuilder(&document);
        const QRectF oldRectangle = rectangle(document, note);
        QVERIFY(PDFAnnotationManipulator::setRectangle(&modifyBuilder, note, QRectF(100, 200, 80, 60)));
        const PDFDocument modified = modifyBuilder.build();
        QCOMPARE(rectangle(modified, note).size(), oldRectangle.size());
        QVERIFY(QLineF(rectangle(modified, note).center(), QPointF(140, 230)).length() < 0.001);
    }

    // Invalid input
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::setRectangle(nullptr, polygon, QRectF(0, 0, 10, 10)));
    QVERIFY(!PDFAnnotationManipulator::setRectangle(&modifyBuilder, polygon, QRectF(0, 0, 0, 10)));
    QVERIFY(!PDFAnnotationManipulator::setRectangle(&modifyBuilder, PDFObjectReference(), QRectF(0, 0, 10, 10)));
    QVERIFY(!PDFAnnotationManipulator::setRectangle(&modifyBuilder, empty, QRectF(0, 0, 10, 10)));
    QVERIFY(!PDFAnnotationManipulator::setRectangle(&modifyBuilder, link, QRectF(0, 0, 10, 10)));
}

void AnnotationManipulatorTest::addedAndReplacedParts()
{
    const QPolygonF lines = { QPointF(100, 212), QPointF(300, 212), QPointF(100, 200), QPointF(300, 200) };
    const Polygons strokes = { QPolygonF({ QPointF(10, 10), QPointF(50, 50), QPointF(90, 10) }) };

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, lines, Qt::yellow);
    const PDFObjectReference ink = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference inkWithPath = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference inkWithoutList = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    setEntry(builder, inkWithPath, "Path", pathArray({ { 10, 10 }, { 50, 50 } }));
    setEntry(builder, inkWithoutList, "InkList", PDFObject());
    const PDFDocument document = builder.build();

    QVERIFY(PDFAnnotationManipulator::getParts(&document.getStorage(), highlight).isSupported);
    QVERIFY(PDFAnnotationManipulator::getParts(&document.getStorage(), ink).isSupported);
    QVERIFY(PDFAnnotationManipulator::getParts(&document.getStorage(), inkWithoutList).isSupported);
    QVERIFY(!PDFAnnotationManipulator::getParts(&document.getStorage(), inkWithPath).isSupported);
    QVERIFY(!PDFAnnotationManipulator::getParts(&document.getStorage(), square).isSupported);

    PDFDocumentBuilder modifyBuilder(&document);

    // A marked area is added. The corners go around the area, they are stored
    // in the order top left, top right, bottom left, bottom right.
    const QPolygonF area = { QPointF(100, 192), QPointF(200, 192), QPointF(200, 180), QPointF(100, 180) };
    QVERIFY(PDFAnnotationManipulator::addPart(&modifyBuilder, highlight, area));

    // A stroke is added (also to an ink, which has no stroke)
    const QPolygonF stroke = { QPointF(200, 300), QPointF(250, 350), QPointF(300, 300) };
    QVERIFY(PDFAnnotationManipulator::addPart(&modifyBuilder, ink, stroke));
    QVERIFY(PDFAnnotationManipulator::addPart(&modifyBuilder, inkWithoutList, stroke));

    // Invalid parts
    QVERIFY(!PDFAnnotationManipulator::addPart(nullptr, highlight, area));
    QVERIFY(!PDFAnnotationManipulator::addPart(&modifyBuilder, highlight, QPolygonF({ QPointF(0, 0), QPointF(10, 0), QPointF(10, 10) })));
    QVERIFY(!PDFAnnotationManipulator::addPart(&modifyBuilder, highlight, QPolygonF({ QPointF(0, 0), QPointF(10, 0), QPointF(10, 10), QPointF(0, 10), QPointF(0, 5) })));
    QVERIFY(!PDFAnnotationManipulator::addPart(&modifyBuilder, ink, QPolygonF({ QPointF(0, 0) })));
    QVERIFY(!PDFAnnotationManipulator::addPart(&modifyBuilder, inkWithPath, stroke));
    QVERIFY(!PDFAnnotationManipulator::addPart(&modifyBuilder, square, stroke));
    QVERIFY(!PDFAnnotationManipulator::setParts(nullptr, ink, { stroke }));
    QVERIFY(!PDFAnnotationManipulator::setParts(&modifyBuilder, ink, { }));

    PDFDocument modified = modifyBuilder.build();
    QVERIFY(fuzzyCompare(numbers(modified, highlight, "QuadPoints"), std::vector<PDFReal>{ 100, 212, 300, 212, 100, 200, 300, 200, 100, 192, 200, 192, 100, 180, 200, 180 }));
    QVERIFY(rectangle(modified, highlight).contains(QRectF(100, 180, 200, 32)));
    QVERIFY(normalAppearance(modified, highlight) != normalAppearance(document, highlight));

    PDFAnnotationManipulator::Parts parts = PDFAnnotationManipulator::getParts(&modified.getStorage(), ink);
    QCOMPARE(parts.shapes.size(), size_t(2));
    QCOMPARE(parts.shapes[1], stroke);
    QVERIFY(rectangle(modified, ink).contains(QRectF(10, 10, 290, 340)));
    QCOMPARE(PDFAnnotationManipulator::getParts(&modified.getStorage(), inkWithoutList).shapes.size(), size_t(1));

    // All parts are replaced (another text is marked)
    PDFDocumentBuilder replaceBuilder(&modified);
    QVERIFY(PDFAnnotationManipulator::setParts(&replaceBuilder, highlight, { area }));
    modified = replaceBuilder.build();
    QVERIFY(fuzzyCompare(numbers(modified, highlight, "QuadPoints"), std::vector<PDFReal>{ 100, 192, 200, 192, 100, 180, 200, 180 }));
    QVERIFY(fuzzyCompare(rectangle(modified, highlight), QRectF(100, 180, 100, 12), 1.5));
}

void AnnotationManipulatorTest::erasedInk()
{
    const Polygons strokes = { QPolygonF({ QPointF(0, 100), QPointF(100, 100), QPointF(200, 100) }),
                               QPolygonF({ QPointF(300, 300), QPointF(300, 300), QPointF(350, 350), QPointF(350, 350) }) };

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference ink = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference dots = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference highlight = builder.createAnnotationHighlight(page, QRectF(50, 250, 100, 12), Qt::yellow);

    PDFObjectFactory factory;
    factory.beginArray();
    factory << std::vector<PDFReal>{ 10, 10 } << std::vector<PDFReal>{ } << std::vector<PDFReal>{ 50, 50, 60, 50 };
    factory.endArray();
    setEntry(builder, dots, "InkList", factory.takeObject());
    const PDFDocument document = builder.build();

    auto erase = [&document](PDFObjectReference annotation, const QPointF& center, PDFReal radius, bool expectedResult)
    {
        PDFDocumentBuilder modifyBuilder(&document);
        if (PDFAnnotationManipulator::eraseInk(&modifyBuilder, annotation, center, radius) != expectedResult)
        {
            return Polygons{ QPolygonF({ QPointF(-1, -1) }) };
        }

        const PDFDocument modified = modifyBuilder.build();
        return PDFAnnotationManipulator::getParts(&modified.getStorage(), annotation).shapes;
    };

    // The middle of a segment is erased - the stroke is split, the cuts are at the border of the circle
    Polygons shapes = erase(ink, QPointF(50, 100), 10.0, true);
    QCOMPARE(shapes.size(), size_t(3));
    QCOMPARE(shapes[0], QPolygonF({ QPointF(0, 100), QPointF(40, 100) }));
    QCOMPARE(shapes[1], QPolygonF({ QPointF(60, 100), QPointF(100, 100), QPointF(200, 100) }));
    QCOMPARE(shapes[2], strokes[1]);

    // A vertex is erased
    shapes = erase(ink, QPointF(100, 100), 10.0, true);
    QCOMPARE(shapes.size(), size_t(3));
    QCOMPARE(shapes[0], QPolygonF({ QPointF(0, 100), QPointF(90, 100) }));
    QCOMPARE(shapes[1], QPolygonF({ QPointF(110, 100), QPointF(200, 100) }));

    // The start and the end of the stroke are erased
    shapes = erase(ink, QPointF(0, 100), 10.0, true);
    QCOMPARE(shapes[0], QPolygonF({ QPointF(10, 100), QPointF(100, 100), QPointF(200, 100) }));
    shapes = erase(ink, QPointF(200, 100), 10.0, true);
    QCOMPARE(shapes[0], QPolygonF({ QPointF(0, 100), QPointF(100, 100), QPointF(190, 100) }));

    // The circle covers a whole segment (both its ends are inside), a whole stroke
    shapes = erase(ink, QPointF(50, 100), 60.0, true);
    QCOMPARE(shapes.size(), size_t(2));
    QCOMPARE(shapes[0], QPolygonF({ QPointF(110, 100), QPointF(200, 100) }));
    shapes = erase(ink, QPointF(100, 100), 150.0, true);
    QCOMPARE(shapes.size(), size_t(1));
    QCOMPARE(shapes[0], strokes[1]);

    // Points, which are in the stroke twice (segments without a length)
    shapes = erase(ink, QPointF(300, 300), 5.0, true);
    QCOMPARE(shapes.size(), size_t(2));
    QCOMPARE(shapes[1].size(), 3);
    QVERIFY(QLineF(shapes[1][0], QPointF(300, 300)).length() > 4.99);
    QCOMPARE(shapes[1][2], QPointF(350, 350));

    // The circle touches nothing, or the line of the segment only, or it would erase everything
    QCOMPARE(erase(ink, QPointF(50, 150), 10.0, false).size(), size_t(2));
    QCOMPARE(erase(ink, QPointF(250, 100), 10.0, false).size(), size_t(2));
    QCOMPARE(erase(ink, QPointF(200, 200), 1000.0, false).size(), size_t(2));

    // Strokes with a single point and without points
    shapes = erase(dots, QPointF(10, 10), 5.0, true);
    QCOMPARE(shapes.size(), size_t(1));
    QCOMPARE(shapes[0], QPolygonF({ QPointF(50, 50), QPointF(60, 50) }));
    QCOMPARE(erase(dots, QPointF(200, 200), 5.0, false).size(), size_t(3));

    // Invalid input
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(!PDFAnnotationManipulator::eraseInk(nullptr, ink, QPointF(50, 100), 10.0));
    QVERIFY(!PDFAnnotationManipulator::eraseInk(&modifyBuilder, ink, QPointF(50, 100), 0.0));
    QVERIFY(!PDFAnnotationManipulator::eraseInk(&modifyBuilder, highlight, QPointF(100, 256), 10.0));
    QVERIFY(!PDFAnnotationManipulator::eraseInk(&modifyBuilder, page, QPointF(100, 256), 10.0));
}

void AnnotationManipulatorTest::inkPoints()
{
    const Polygons strokes = { QPolygonF({ QPointF(10, 10), QPointF(50, 50), QPointF(90, 10) }), QPolygonF({ QPointF(200, 300), QPointF(250, 350) }) };

    Polygons longStrokes(1);
    for (int i = 0; i < 65; ++i)
    {
        longStrokes[0] << QPointF(10 + i, 10 + (i % 2));
    }

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference ink = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference inkWithPath = builder.createAnnotationInk(page, strokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference longInk = builder.createAnnotationInk(page, longStrokes, 2.0, Qt::black, "Title", "Subject", "Contents");
    setEntry(builder, inkWithPath, "Path", pathArray({ { 10, 10 }, { 50, 50 } }));
    const PDFDocument document = builder.build();

    auto getPoints = [](const PDFDocument& currentDocument, PDFObjectReference annotation)
    {
        return PDFAnnotationManipulator::getEditablePoints(PDFAnnotation::parse(&currentDocument.getStorage(), annotation).data());
    };

    // Points of the strokes. Their count is fixed.
    const PDFAnnotationManipulator::EditablePoints points = getPoints(document, ink);
    QVERIFY(points.isStrokePoints());
    QCOMPARE(points.points.size(), size_t(5));
    QVERIFY(points.strokeSizes == (std::vector<size_t>{ 3, 2 }));
    QVERIFY(!points.canInsertPoint());
    QVERIFY(!points.canRemovePoint(1));

    // A stroke drawn by hand has too many points, a path can contain curves
    QVERIFY(!getPoints(document, longInk).isValid());
    QVERIFY(!getPoints(document, inkWithPath).isValid());

    // A point is moved
    std::vector<QPointF> newPoints = points.points;
    newPoints[1] = QPointF(50, 150);
    newPoints[4] = QPointF(300, 350);
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, ink, newPoints));
    newPoints.pop_back();
    QVERIFY(!PDFAnnotationManipulator::setEditablePoints(&modifyBuilder, ink, newPoints));
    const PDFDocument modified = modifyBuilder.build();

    const PDFAnnotationManipulator::Parts parts = PDFAnnotationManipulator::getParts(&modified.getStorage(), ink);
    QCOMPARE(parts.shapes.size(), size_t(2));
    QCOMPARE(parts.shapes[0], QPolygonF({ QPointF(10, 10), QPointF(50, 150), QPointF(90, 10) }));
    QCOMPARE(parts.shapes[1], QPolygonF({ QPointF(200, 300), QPointF(300, 350) }));
    QVERIFY(rectangle(modified, ink).contains(QRectF(10, 10, 290, 340)));
    QVERIFY(normalAppearance(modified, ink) != normalAppearance(document, ink));
}

void AnnotationManipulatorTest::addedReply()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference link = builder.createAnnotationLink(page, QRectF(300, 300, 50, 20), QString("https://example.com"), LinkHighlightMode::Invert);
    const PDFObjectReference onNoPage = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFObjectReference otherPage = builder.appendPage(QRectF(0, 0, 400, 400));
    builder.createAnnotationSquare(otherPage, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    PDFDocumentBuilder modifyBuilder(&document);
    const PDFObjectReference reply = PDFAnnotationManipulator::addReply(&modifyBuilder, square, "Reviewer", "I do not agree.");
    QVERIFY(reply.isValid());
    const PDFObjectReference nestedReply = PDFAnnotationManipulator::addReply(&modifyBuilder, reply, "Author", "Why?");
    QVERIFY(nestedReply.isValid());

    // Invalid input
    QVERIFY(!PDFAnnotationManipulator::addReply(nullptr, square, "Reviewer", "Text").isValid());
    QVERIFY(!PDFAnnotationManipulator::addReply(&modifyBuilder, square, "Reviewer", "  ").isValid());
    QVERIFY(!PDFAnnotationManipulator::addReply(&modifyBuilder, PDFObjectReference(), "Reviewer", "Text").isValid());
    QVERIFY(!PDFAnnotationManipulator::addReply(&modifyBuilder, link, "Reviewer", "Text").isValid());

    // Annotation, which is not on any page
    PDFDocumentBuilder orphanBuilder(&document);
    setEntry(orphanBuilder, page, "Annots", PDFObject());
    QVERIFY(!PDFAnnotationManipulator::addReply(&orphanBuilder, onNoPage, "Reviewer", "Text").isValid());

    const PDFDocument modified = modifyBuilder.build();
    const PDFObjectStorage* storage = &modified.getStorage();

    // The replies are a part of the thread of the annotation
    const std::vector<PDFObjectReference> replies = PDFAnnotationManipulator::getReplies(storage, page, square);
    QCOMPARE(replies.size(), size_t(2));
    QVERIFY(std::find(replies.cbegin(), replies.cend(), reply) != replies.cend());
    QVERIFY(std::find(replies.cbegin(), replies.cend(), nestedReply) != replies.cend());

    const PDFAnnotationPtr parsedReply = PDFAnnotation::parse(storage, reply);
    QVERIFY(parsedReply->isReplyTo());
    QCOMPARE(parsedReply->getContents(), QString("I do not agree."));
    QCOMPARE(parsedReply->asMarkupAnnotation()->getWindowTitle(), QString("Reviewer"));
    QCOMPARE(entry(modified, reply, "IRT"), PDFObject::createReference(square));
    QCOMPARE(entry(modified, nestedReply, "IRT"), PDFObject::createReference(reply));
    QCOMPARE(entry(modified, reply, "RT"), PDFObject::createName("R"));
    QCOMPARE(PDFAnnotationManipulator::findAnnotationPage(storage, reply), page);
}

void AnnotationManipulatorTest::replacedFileAttachment()
{
    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(QRectF(0, 0, 400, 400));
    const PDFObjectReference fileSpecification = builder.createFileSpecification("old.txt");
    const PDFObjectReference attachment = builder.createAnnotationFileAttachment(page, QPointF(100, 100), fileSpecification, FileAttachmentIcon::Paperclip, "Title", "Description");
    const PDFObjectReference square = builder.createAnnotationSquare(page, QRectF(100, 100, 50, 30), 2.0, Qt::yellow, Qt::black, "Title", "Subject", "Contents");
    const PDFDocument document = builder.build();

    const QByteArray data = "Content of the new file";
    PDFDocumentBuilder modifyBuilder(&document);
    QVERIFY(PDFAnnotationManipulator::setFileAttachment(&modifyBuilder, attachment, "new.txt", data));
    QVERIFY(!PDFAnnotationManipulator::setFileAttachment(nullptr, attachment, "new.txt", data));
    QVERIFY(!PDFAnnotationManipulator::setFileAttachment(&modifyBuilder, attachment, QString(), data));
    QVERIFY(!PDFAnnotationManipulator::setFileAttachment(&modifyBuilder, square, "new.txt", data));
    QVERIFY(!PDFAnnotationManipulator::setFileAttachment(&modifyBuilder, PDFObjectReference(), "new.txt", data));

    // The document is written and read again, the attached file is the new file
    const PDFDocument modified = modifyBuilder.build();
    PDFDocumentReader reader(nullptr, [](bool*) { return QString(); }, true, false);
    const PDFDocument reopened = reader.readFromBuffer(write(modified));
    QCOMPARE(reader.getReadingResult(), PDFDocumentReader::Result::OK);

    const PDFAnnotationPtr parsed = PDFAnnotation::parse(&reopened.getStorage(), pageAnnotations(reopened, 0).front());
    const PDFFileAttachmentAnnotation* fileAttachment = dynamic_cast<const PDFFileAttachmentAnnotation*>(parsed.data());
    QVERIFY(fileAttachment);

    const PDFFileSpecification& specification = fileAttachment->getFileSpecification();
    QCOMPARE(specification.getPlatformFileName(), QString("new.txt"));
    const PDFEmbeddedFile* embeddedFile = specification.getPlatformFile();
    QVERIFY(embeddedFile && embeddedFile->isValid());
    QCOMPARE(embeddedFile->getSize(), PDFInteger(data.size()));
    QCOMPARE(reopened.getDecodedStream(embeddedFile->getStream()), data);
}

QTEST_MAIN(AnnotationManipulatorTest)

#include "tst_annotationmanipulatortest.moc"
