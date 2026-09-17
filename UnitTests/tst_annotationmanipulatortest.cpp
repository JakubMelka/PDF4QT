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

private:
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
    QVERIFY(!points.canInsertPoint());
    QVERIFY(!points.canRemovePoint());

    points = getPoints(polygon);
    QVERIFY(points.isValid());
    QCOMPARE(points.points.size(), size_t(3));
    QVERIFY(points.isClosed);
    QVERIFY(points.canInsertPoint());
    QVERIFY(!points.canRemovePoint());   // Triangle cannot lose a point

    points = getPoints(polyline);
    QVERIFY(points.isValid());
    QVERIFY(!points.isClosed);
    QVERIFY(points.canInsertPoint());
    QVERIFY(points.canRemovePoint());

    points = getPoints(freeText);
    QVERIFY(points.isValid());
    QVERIFY(points.points == (std::vector<QPointF>{ QPointF(20, 20), QPointF(60, 60), QPointF(110, 120) }));
    QVERIFY(!points.canInsertPoint());
    QVERIFY(!points.canRemovePoint());

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

    // The callout line is rotated, so the appearance must be regenerated
    PDFDocument modified = transform(document, callout, rotation);
    const QPointF start = rotation.map(QPointF(20, 20));
    const QPointF end = rotation.map(QPointF(110, 120));
    QVERIFY(fuzzyCompare(numbers(modified, callout, "CL"), std::vector<PDFReal>{ start.x(), start.y(), end.x(), end.y() }));
    QVERIFY(normalAppearance(modified, callout) != normalAppearance(document, callout));
    QVERIFY(fuzzyCompare(QRectF(QPointF(), rectangle(modified, callout).size()), QRectF(QPointF(), rectangle(document, callout).size())));

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
        builder.setObject(streamReference, PDFObject::createStream(std::make_shared<PDFStream>(std::move(dictionary), QByteArray(*stream->getContent()))));
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

QTEST_MAIN(AnnotationManipulatorTest)

#include "tst_annotationmanipulatortest.moc"
