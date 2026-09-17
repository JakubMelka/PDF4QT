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

#include <QtTest>

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
    void copyAnnotationSharesAppearanceAndCopiesPopup();
    void copyAnnotationDropsReplyLinks();
    void moveAnnotationToPage();
    void serializeAndInsert();
    void serializeRejectsInvalidData();

private:
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

QTEST_MAIN(AnnotationManipulatorTest)

#include "tst_annotationmanipulatortest.moc"
