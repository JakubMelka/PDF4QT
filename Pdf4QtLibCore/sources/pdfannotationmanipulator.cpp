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
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"
#include "pdfmeasure.h"

#include <QUuid>
#include <QBuffer>
#include <QPainterPath>
#include <QRegularExpression>

#include <map>
#include <set>
#include <cmath>
#include <algorithm>

namespace pdf
{

PDFAnnotationManipulator::GeometryKind PDFAnnotationManipulator::getGeometryKind(AnnotationType type)
{
    switch (type)
    {
        case AnnotationType::Line:
        case AnnotationType::Polygon:
        case AnnotationType::Polyline:
        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::Squiggly:
        case AnnotationType::StrikeOut:
        case AnnotationType::Ink:
        case AnnotationType::Redact:
            return GeometryKind::Points;

        case AnnotationType::Square:
        case AnnotationType::Circle:
        case AnnotationType::FreeText:
        case AnnotationType::Caret:
            return GeometryKind::Box;

        case AnnotationType::Text:
        case AnnotationType::FileAttachment:
            return GeometryKind::Icon;

        case AnnotationType::Stamp:
        case AnnotationType::Watermark:
        case AnnotationType::PrinterMark:
            return GeometryKind::Appearance;

        default:
            break;
    }

    return GeometryKind::NotSupported;
}

PDFAnnotationManipulator::Capabilities PDFAnnotationManipulator::getCapabilities(const PDFAnnotation* annotation)
{
    Capabilities capabilities = NoCapability;

    if (!annotation)
    {
        return capabilities;
    }

    const AnnotationType type = annotation->getType();
    switch (getGeometryKind(type))
    {
        case GeometryKind::Points:
        case GeometryKind::Appearance:
            capabilities = Move | Resize | RotateRightAngle | RotateArbitrary | Mirror;
            break;

        case GeometryKind::Box:
        {
            capabilities = Move | Resize;

            // Rotation by the right angle
            if (isBoxTransformedExactly(type, QTransform(0.0, 1.0, -1.0, 0.0, 0.0, 0.0)))
            {
                capabilities |= RotateRightAngle;
            }
            break;
        }

        case GeometryKind::Icon:
            capabilities = Move;
            break;

        default:
            break;
    }

    if (getEditablePoints(annotation).isValid())
    {
        // Mirroring moves the callout line of a free text annotation to the other
        // side of the text box (the remaining annotations with points are mirrored anyway)
        capabilities |= EditPoints;
        capabilities |= Mirror;
    }

    return capabilities;
}

bool PDFAnnotationManipulator::isAxisAligned(const QTransform& transform)
{
    const bool diagonal = qFuzzyIsNull(transform.m12()) && qFuzzyIsNull(transform.m21());
    const bool antiDiagonal = qFuzzyIsNull(transform.m11()) && qFuzzyIsNull(transform.m22());
    return diagonal || antiDiagonal;
}

bool PDFAnnotationManipulator::isPositiveAxisAligned(const QTransform& transform)
{
    return qFuzzyIsNull(transform.m12()) && qFuzzyIsNull(transform.m21()) && transform.m11() > 0.0 && transform.m22() > 0.0;
}

bool PDFAnnotationManipulator::isBoxTransformedExactly(AnnotationType type, const QTransform& transform)
{
    // Jakub Melka: scaling and mirroring of the rectangle is always exact. Rotation
    // by the right angle swaps the width and the height of the rectangle, which is
    // the rotation of a square and of a circle (ellipse). It is not the rotation
    // of a text box (the text would be just laid out into a box with different size)
    // and neither of a caret (the symbol always points upwards).
    const bool isDiagonal = qFuzzyIsNull(transform.m12()) && qFuzzyIsNull(transform.m21());
    const bool isAntiDiagonal = qFuzzyIsNull(transform.m11()) && qFuzzyIsNull(transform.m22());
    const bool isRotatable = type == AnnotationType::Square || type == AnnotationType::Circle;
    return isDiagonal || (isAntiDiagonal && isRotatable);
}

QRectF PDFAnnotationManipulator::readRectangle(const PDFObjectStorage* storage, const PDFDictionary* dictionary, const char* key)
{
    PDFDocumentDataLoaderDecorator loader(storage);
    return loader.readRectangle(dictionary->get(key), QRectF()).normalized();
}

PDFObject PDFAnnotationManipulator::createRectangle(const QRectF& rectangle)
{
    PDFObjectFactory factory;
    factory << rectangle;
    return factory.takeObject();
}

PDFObject PDFAnnotationManipulator::createNumberArray(const std::vector<PDFReal>& numbers)
{
    PDFObjectFactory factory;
    factory << numbers;
    return factory.takeObject();
}

PDFObject PDFAnnotationManipulator::createUniqueName()
{
    PDFObjectFactory factory;
    factory << QUuid::createUuid().toString(QUuid::WithoutBraces);
    return factory.takeObject();
}

QRectF PDFAnnotationManipulator::centerRectangle(const QRectF& rectangle, const QPointF& center)
{
    QRectF result = rectangle;
    result.moveCenter(center);
    return result;
}

bool PDFAnnotationManipulator::transformPointArray(PDFDictionary& dictionary,
                                                   const PDFObjectStorage* storage,
                                                   const char* key,
                                                   const QTransform& transform)
{
    if (!dictionary.hasKey(key))
    {
        return false;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    std::vector<PDFReal> numbers = loader.readNumberArrayFromDictionary(&dictionary, key);
    if (numbers.empty() || numbers.size() % 2 != 0)
    {
        return false;
    }

    for (size_t i = 0; i < numbers.size(); i += 2)
    {
        const QPointF point = transform.map(QPointF(numbers[i], numbers[i + 1]));
        numbers[i] = point.x();
        numbers[i + 1] = point.y();
    }

    dictionary.setEntry(PDFInplaceOrMemoryString(key), createNumberArray(numbers));
    return true;
}

void PDFAnnotationManipulator::transformPointArrays(PDFDictionary& dictionary,
                                                    const PDFObjectStorage* storage,
                                                    const char* key,
                                                    const QTransform& transform)
{
    if (!dictionary.hasKey(key))
    {
        return;
    }

    const PDFObject arrays = storage->getObject(dictionary.get(key));
    if (!arrays.isArray())
    {
        return;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    PDFObjectFactory factory;
    factory.beginArray();

    // Jakub Melka: each item is an array of coordinates of points - a stroke of an ink
    // list, or an operation of a path (a point, or a curve with its control points)
    const PDFArray* array = arrays.getArray();
    for (size_t i = 0, count = array->getCount(); i < count; ++i)
    {
        std::vector<PDFReal> numbers = loader.readNumberArray(array->getItem(i));
        const size_t pointCount = numbers.size() / 2;
        numbers.resize(pointCount * 2);

        for (size_t j = 0; j < pointCount; ++j)
        {
            const QPointF point = transform.map(QPointF(numbers[j * 2], numbers[j * 2 + 1]));
            numbers[j * 2] = point.x();
            numbers[j * 2 + 1] = point.y();
        }

        factory << numbers;
    }

    factory.endArray();
    dictionary.setEntry(PDFInplaceOrMemoryString(key), factory.takeObject());
}

void PDFAnnotationManipulator::reversePointArray(PDFDictionary& dictionary, const PDFObjectStorage* storage, const char* key)
{
    PDFDocumentDataLoaderDecorator loader(storage);
    const std::vector<PDFReal> numbers = loader.readNumberArrayFromDictionary(&dictionary, key);

    std::vector<PDFReal> reversedNumbers;
    reversedNumbers.reserve(numbers.size());
    for (size_t i = numbers.size() / 2; i > 0; --i)
    {
        reversedNumbers.push_back(numbers[2 * i - 2]);
        reversedNumbers.push_back(numbers[2 * i - 1]);
    }

    dictionary.setEntry(PDFInplaceOrMemoryString(key), createNumberArray(reversedNumbers));
}

void PDFAnnotationManipulator::scaleNumber(PDFDictionary& dictionary,
                                           const PDFObjectStorage* storage,
                                           const char* key,
                                           PDFReal factor)
{
    if (!dictionary.hasKey(key))
    {
        return;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    const PDFReal value = loader.readNumberFromDictionary(&dictionary, key, 0.0);
    dictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObject::createReal(value * factor));
}

void PDFAnnotationManipulator::transformLineParameters(PDFDictionary& dictionary,
                                                       const PDFObjectStorage* storage,
                                                       const QLineF& line,
                                                       const QTransform& transform)
{
    const QTransform linearTransform(transform.m11(), transform.m12(), transform.m21(), transform.m22(), 0.0, 0.0);
    const QPointF vector = line.p2() - line.p1();
    QPointF newVector = linearTransform.map(vector);
    const PDFReal length = std::hypot(vector.x(), vector.y());
    const PDFReal newLength = std::hypot(newVector.x(), newVector.y());

    if (qFuzzyIsNull(length) || qFuzzyIsNull(newLength))
    {
        return;
    }

    // Jakub Melka: the caption is drawn along the line, so it is upside down, if the line
    // goes from the right to the left. The caption cannot be mirrored, so if mirroring
    // reverses a line, which was readable, then we swap the end points of the line (and
    // its line endings) - it is the same line, but its caption stays readable.
    auto isReadable = [](const QPointF& direction)
    {
        return direction.x() > 1e-9 || (std::abs(direction.x()) <= 1e-9 && direction.y() > 0.0);
    };

    const bool isReversed = transform.determinant() < 0.0 && isReadable(vector) && !isReadable(newVector);
    if (isReversed)
    {
        newVector = -newVector;
        reversePointArray(dictionary, storage, "L");

        PDFDocumentDataLoaderDecorator lineEndingLoader(storage);
        std::vector<QByteArray> lineEndings = lineEndingLoader.readNameArrayFromDictionary(&dictionary, "LE");
        if (lineEndings.size() == 2)
        {
            PDFObjectFactory factory;
            factory.beginArray();
            factory << WrapName(lineEndings.back()) << WrapName(lineEndings.front());
            factory.endArray();
            dictionary.setEntry(PDFInplaceOrMemoryString("LE"), factory.takeObject());
        }
    }

    // Jakub Melka: leader lines are perpendicular to the line and the sign of their
    // length selects the side of the line - a positive length means the side, which
    // is on the left, when we look from the start of the line to its end. We transform
    // the leader line as a vector and measure it along the normal of the transformed
    // line. It scales the length correctly, when the scaling is not uniform, and it
    // changes the sign, when the transformation mirrors the line (the transformed
    // leader line is on the other side of the transformed line then).
    const QPointF direction = vector / length;
    const QPointF normal(-direction.y(), direction.x());
    const QPointF newDirection = newVector / newLength;
    const QPointF newNormal(-newDirection.y(), newDirection.x());
    const PDFReal lengthFactor = isReversed ? -newLength / length : newLength / length;
    const PDFReal normalFactor = QPointF::dotProduct(linearTransform.map(normal), newNormal);

    // Extension and offset of the leader lines are not oriented, they follow the leader line
    scaleNumber(dictionary, storage, "LL", normalFactor);
    scaleNumber(dictionary, storage, "LLE", std::abs(normalFactor));
    scaleNumber(dictionary, storage, "LLO", std::abs(normalFactor));

    // Offset of the caption - along the line and perpendicular to the line
    PDFDocumentDataLoaderDecorator loader(storage);
    std::vector<PDFReal> captionOffset = loader.readNumberArrayFromDictionary(&dictionary, "CO");
    if (captionOffset.size() == 2)
    {
        captionOffset[0] *= lengthFactor;
        captionOffset[1] *= normalFactor;
        dictionary.setEntry(PDFInplaceOrMemoryString("CO"), createNumberArray(captionOffset));
    }
}

void PDFAnnotationManipulator::transformRectangleDifferences(PDFDictionary& dictionary,
                                                             const PDFObjectStorage* storage,
                                                             const QRectF& oldRectangle,
                                                             const QRectF& newRectangle,
                                                             const QTransform& transform)
{
    if (!dictionary.hasKey("RD"))
    {
        return;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    std::vector<PDFReal> differences = loader.readNumberArrayFromDictionary(&dictionary, "RD");
    if (differences.size() != 4)
    {
        return;
    }

    // Jakub Melka: the same convention as in PDFAnnotation::parse - the differences
    // are applied to the normalized rectangle in the order left, top, right, bottom
    // of the QRectF (which has the y axis pointing upwards in the page space).
    const QRectF oldInnerRectangle = oldRectangle.adjusted(differences[0], differences[1], -differences[2], -differences[3]);
    if (!oldInnerRectangle.isValid())
    {
        return;
    }

    const QRectF newInnerRectangle = transform.mapRect(oldInnerRectangle);
    differences[0] = std::max(0.0, newInnerRectangle.left() - newRectangle.left());
    differences[1] = std::max(0.0, newInnerRectangle.top() - newRectangle.top());
    differences[2] = std::max(0.0, newRectangle.right() - newInnerRectangle.right());
    differences[3] = std::max(0.0, newRectangle.bottom() - newInnerRectangle.bottom());
    dictionary.setEntry(PDFInplaceOrMemoryString("RD"), createNumberArray(differences));
}

PDFObject PDFAnnotationManipulator::transformAppearanceStream(PDFDocumentBuilder* builder,
                                                              const PDFObject& streamObject,
                                                              const PDFObject& originalEntry,
                                                              const QRectF& rectangle,
                                                              const QTransform& transform,
                                                              QRectF* newRectangle)
{
    const PDFStream* stream = streamObject.getStream();
    const PDFDictionary* streamDictionary = stream->getDictionary();
    const PDFObjectStorage* storage = builder->getStorage();
    PDFDocumentDataLoaderDecorator loader(storage);

    const QRectF boundingBox = readRectangle(storage, streamDictionary, "BBox");
    if (!boundingBox.isValid())
    {
        // We do not know the shape of the form, we cannot transform it
        return originalEntry;
    }

    QTransform matrix;
    const std::vector<PDFReal> matrixNumbers = loader.readNumberArrayFromDictionary(streamDictionary, "Matrix");
    if (matrixNumbers.size() == 6)
    {
        matrix = QTransform(matrixNumbers[0], matrixNumbers[1], matrixNumbers[2], matrixNumbers[3], matrixNumbers[4], matrixNumbers[5]);
    }

    // Jakub Melka: the appearance of the annotation is displayed using the algorithm
    // 8.1 of the PDF specification - the bounding box of the form is transformed by
    // the form matrix and the bounding rectangle of the result is mapped onto the
    // annotation rectangle (the "fit" transformation). We compute the shape, which is
    // currently visible on the page, and transform it. The scaling of the fit is folded
    // into the new form matrix, so the new annotation rectangle is exactly the bounding
    // rectangle of the transformed shape and the viewer does not distort it.
    const QPolygonF transformedBox = matrix.map(QPolygonF(boundingBox));
    const QRectF transformedBoxBounds = transformedBox.boundingRect();
    if (transformedBoxBounds.isEmpty())
    {
        return originalEntry;
    }

    const PDFReal scaleX = rectangle.width() / transformedBoxBounds.width();
    const PDFReal scaleY = rectangle.height() / transformedBoxBounds.height();
    const QTransform fit = QTransform::fromTranslate(-transformedBoxBounds.left(), -transformedBoxBounds.top()) *
                           QTransform::fromScale(scaleX, scaleY) *
                           QTransform::fromTranslate(rectangle.left(), rectangle.top());
    const QPolygonF visibleShape = fit.map(transformedBox);

    if (newRectangle)
    {
        *newRectangle = transform.map(visibleShape).boundingRect();
    }

    const QTransform linear(transform.m11(), transform.m12(), transform.m21(), transform.m22(), 0.0, 0.0);
    const QTransform newMatrix = matrix * QTransform::fromScale(scaleX, scaleY) * linear;

    PDFDictionary newStreamDictionary = *streamDictionary;
    newStreamDictionary.setEntry(PDFInplaceOrMemoryString("Matrix"), createNumberArray({ newMatrix.m11(), newMatrix.m12(), newMatrix.m21(), newMatrix.m22(), newMatrix.dx(), newMatrix.dy() }));

    QByteArray content = *stream->getContent();
    const PDFObjectReference newStream = builder->addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(newStreamDictionary), std::move(content))));
    return PDFObject::createReference(newStream);
}

void PDFAnnotationManipulator::transformAppearanceStreams(PDFDocumentBuilder* builder,
                                                          PDFDictionary& dictionary,
                                                          const QRectF& rectangle,
                                                          const QTransform& transform,
                                                          QRectF& newRectangle)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const PDFObject appearanceObject = storage->getObject(dictionary.get("AP"));
    if (!appearanceObject.isDictionary())
    {
        return;
    }

    const PDFDictionary* appearanceDictionary = appearanceObject.getDictionary();
    PDFObjectFactory factory;
    factory.beginDictionary();

    for (size_t i = 0, count = appearanceDictionary->getCount(); i < count; ++i)
    {
        const PDFInplaceOrMemoryString& key = appearanceDictionary->getKey(i);
        const PDFObject& entry = appearanceDictionary->getValue(i);
        const PDFObject dereferencedEntry = storage->getObject(entry);

        // The annotation rectangle is derived from the normal appearance
        QRectF* rectanglePointer = (key == "N") ? &newRectangle : nullptr;

        factory.beginDictionaryItem(key.getString());
        if (dereferencedEntry.isStream())
        {
            factory << transformAppearanceStream(builder, dereferencedEntry, entry, rectangle, transform, rectanglePointer);
        }
        else if (dereferencedEntry.isDictionary())
        {
            // Appearance sub-dictionary with appearance states
            const PDFDictionary* stateDictionary = dereferencedEntry.getDictionary();
            factory.beginDictionary();

            for (size_t j = 0, stateCount = stateDictionary->getCount(); j < stateCount; ++j)
            {
                const PDFObject& stateEntry = stateDictionary->getValue(j);
                const PDFObject dereferencedStateEntry = storage->getObject(stateEntry);

                factory.beginDictionaryItem(stateDictionary->getKey(j).getString());
                if (dereferencedStateEntry.isStream())
                {
                    factory << transformAppearanceStream(builder, dereferencedStateEntry, stateEntry, rectangle, transform, rectanglePointer);

                    // Only the first state defines the rectangle
                    rectanglePointer = nullptr;
                }
                else
                {
                    factory << stateEntry;
                }
                factory.endDictionaryItem();
            }

            factory.endDictionary();
        }
        else
        {
            factory << entry;
        }
        factory.endDictionaryItem();
    }

    factory.endDictionary();
    dictionary.setEntry(PDFInplaceOrMemoryString("AP"), factory.takeObject());
}

void PDFAnnotationManipulator::translatePopup(PDFDocumentBuilder* builder,
                                              const PDFDictionary* annotationDictionary,
                                              const QPointF& offset)
{
    if (qFuzzyIsNull(offset.x()) && qFuzzyIsNull(offset.y()))
    {
        return;
    }

    const PDFObject& popupObject = annotationDictionary->get("Popup");
    if (!popupObject.isReference())
    {
        return;
    }

    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* popupDictionary = getPopupDictionary(storage, annotationDictionary);
    if (!popupDictionary)
    {
        return;
    }

    const QRectF popupRectangle = readRectangle(storage, popupDictionary, "Rect");
    if (!popupRectangle.isValid())
    {
        return;
    }

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Rect");
    factory << popupRectangle.translated(offset);
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->mergeTo(popupObject.getReference(), factory.takeObject());
}

bool PDFAnnotationManipulator::transformAnnotation(PDFDocumentBuilder* builder,
                                                   PDFObjectReference annotation,
                                                   const QTransform& transform)
{
    if (!builder || !annotation.isValid())
    {
        return false;
    }

    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(annotation));
    if (!dictionary)
    {
        return false;
    }

    // Parsing fails only if the dictionary is missing, which is already checked
    const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(storage, annotation);
    Q_ASSERT(parsedAnnotation);

    const GeometryKind kind = getGeometryKind(parsedAnnotation->getType());
    if (kind == GeometryKind::NotSupported)
    {
        return false;
    }

    const QRectF rectangle = readRectangle(storage, dictionary, "Rect");
    if (rectangle.isNull() || (kind != GeometryKind::Points && !rectangle.isValid()))
    {
        return false;
    }

    const bool isTranslation = transform.type() <= QTransform::TxTranslate;

    PDFDictionary modifiedDictionary = *dictionary;
    QRectF newRectangle = rectangle;
    bool regenerateAppearance = false;

    if (kind == GeometryKind::Points)
    {
        // Jakub Melka: a polygon, a polyline and an ink can be defined by a path
        // (PDF 2.0), which has precedence over the vertices (over the ink list)
        transformPointArray(modifiedDictionary, storage, "QuadPoints", transform);
        transformPointArray(modifiedDictionary, storage, "Vertices", transform);
        transformPointArray(modifiedDictionary, storage, "L", transform);
        transformPointArrays(modifiedDictionary, storage, "InkList", transform);
        transformPointArrays(modifiedDictionary, storage, "Path", transform);

        if (!isTranslation)
        {
            if (const PDFLineAnnotation* lineAnnotation = dynamic_cast<const PDFLineAnnotation*>(parsedAnnotation.data()))
            {
                transformLineParameters(modifiedDictionary, storage, lineAnnotation->getLine(), transform);
            }

            // The measured angle is oriented (from the first arm to the second one),
            // so mirroring would turn it into its complement to the full angle.
            // Reversed order of the points keeps the angle.
            if (transform.determinant() < 0.0 && isAngularMeasurement(parsedAnnotation.data()))
            {
                reversePointArray(modifiedDictionary, storage, "Vertices");
            }
        }

        newRectangle = transform.mapRect(rectangle);
        regenerateAppearance = !isTranslation;
    }
    else if (kind == GeometryKind::Box)
    {
        if (isBoxTransformedExactly(parsedAnnotation->getType(), transform))
        {
            transformPointArray(modifiedDictionary, storage, "CL", transform);
            newRectangle = transform.mapRect(rectangle);
            transformRectangleDifferences(modifiedDictionary, storage, rectangle, newRectangle, transform);
            regenerateAppearance = !isTranslation;
        }
        else
        {
            // The shape cannot be rotated, so the annotation is just moved
            // to the transformed position.
            newRectangle = moveBox(modifiedDictionary, storage, parsedAnnotation.data(), rectangle, transform, &regenerateAppearance);
        }
    }
    else if (kind == GeometryKind::Icon)
    {
        newRectangle = centerRectangle(rectangle, transform.map(rectangle.center()));
    }
    else
    {
        Q_ASSERT(kind == GeometryKind::Appearance);

        newRectangle = transform.mapRect(rectangle);
        if (!isPositiveAxisAligned(transform))
        {
            transformAppearanceStreams(builder, modifiedDictionary, rectangle, transform, newRectangle);
        }
    }

    // Popup window follows its parent annotation
    translatePopup(builder, dictionary, newRectangle.center() - rectangle.center());

    modifiedDictionary.setEntry(PDFInplaceOrMemoryString("Rect"), createRectangle(newRectangle));
    builder->setObject(annotation, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(modifiedDictionary))));

    // Measured value follows the geometry, and it is a part of the appearance
    if (!isTranslation && updateMeasurement(builder, annotation, parsedAnnotation.data()))
    {
        regenerateAppearance = true;
    }

    if (regenerateAppearance)
    {
        builder->updateAnnotationAppearanceStreams(annotation);
    }

    return true;
}

QRectF PDFAnnotationManipulator::getFreeTextRectangle(const PDFObjectStorage* storage, const PDFDictionary* dictionary, const QRectF& rectangle)
{
    // Jakub Melka: the annotation rectangle covers the text box and the callout
    // line, the text box is defined by the rectangle differences.
    PDFDocumentDataLoaderDecorator loader(storage);
    const std::vector<PDFReal> differences = loader.readNumberArrayFromDictionary(dictionary, "RD");
    if (differences.size() == 4)
    {
        const QRectF innerRectangle = rectangle.adjusted(differences[0], differences[1], -differences[2], -differences[3]);
        if (innerRectangle.isValid())
        {
            return innerRectangle;
        }
    }

    return rectangle;
}

QRectF PDFAnnotationManipulator::setFreeTextGeometry(PDFDictionary& dictionary,
                                                     const QRectF& textRectangle,
                                                     const std::vector<QPointF>& calloutLine,
                                                     PDFReal margin)
{
    std::vector<PDFReal> numbers;
    numbers.reserve(calloutLine.size() * 2);
    for (const QPointF& point : calloutLine)
    {
        numbers.push_back(point.x());
        numbers.push_back(point.y());
    }

    // The margin is a space for the line ending
    const QRectF rectangle = textRectangle.united(getPointsBoundingRectangle(calloutLine).adjusted(-margin, -margin, margin, margin));
    dictionary.setEntry(PDFInplaceOrMemoryString("CL"), createNumberArray(numbers));
    dictionary.setEntry(PDFInplaceOrMemoryString("RD"), createNumberArray({ textRectangle.left() - rectangle.left(),
                                                                             textRectangle.top() - rectangle.top(),
                                                                             rectangle.right() - textRectangle.right(),
                                                                             rectangle.bottom() - textRectangle.bottom() }));
    return rectangle;
}

QRectF PDFAnnotationManipulator::moveBox(PDFDictionary& dictionary,
                                         const PDFObjectStorage* storage,
                                         const PDFAnnotation* annotation,
                                         const QRectF& rectangle,
                                         const QTransform& transform,
                                         bool* regenerateAppearance)
{
    EditablePoints calloutLine = getEditablePoints(annotation);
    if (!calloutLine.isCalloutLine)
    {
        return centerRectangle(rectangle, transform.map(rectangle.center()));
    }

    // Jakub Melka: the callout line points to a place on the page, so its tip is
    // transformed exactly. The text box cannot be rotated, it is moved and the
    // rest of the callout line (which is attached to the text box) moves with it,
    // so the callout line stays connected to the text box.
    const QRectF textRectangle = getFreeTextRectangle(storage, &dictionary, rectangle);
    const QPointF offset = transform.map(textRectangle.center()) - textRectangle.center();

    calloutLine.points.front() = transform.map(calloutLine.points.front());
    for (size_t i = 1; i < calloutLine.points.size(); ++i)
    {
        calloutLine.points[i] += offset;
    }

    *regenerateAppearance = true;
    const PDFReal margin = std::max(1.0, annotation->getBorder().getWidth()) * 5.0;
    return setFreeTextGeometry(dictionary, textRectangle.translated(offset), calloutLine.points, margin);
}

QTransform PDFAnnotationManipulator::getEffectiveTransform(AnnotationType type, const QRectF& rectangle, const QTransform& transform)
{
    const GeometryKind kind = getGeometryKind(type);
    if (kind == GeometryKind::NotSupported)
    {
        return QTransform();
    }

    const bool isBoxExact = kind == GeometryKind::Box && isBoxTransformedExactly(type, transform);
    if (kind == GeometryKind::Points || kind == GeometryKind::Appearance || isBoxExact)
    {
        return transform;
    }

    // The annotation is just moved to the transformed position
    const QPointF center = rectangle.normalized().center();
    const QPointF offset = transform.map(center) - center;
    return QTransform::fromTranslate(offset.x(), offset.y());
}

QPolygonF PDFAnnotationManipulator::getTransformedOutline(AnnotationType type, const QRectF& rectangle, const QTransform& transform)
{
    return getEffectiveTransform(type, rectangle, transform).map(QPolygonF(rectangle.normalized()));
}

PDFAnnotationManipulator::EditablePoints PDFAnnotationManipulator::getEditablePoints(const PDFAnnotation* annotation)
{
    EditablePoints result;

    if (!annotation)
    {
        return result;
    }

    if (const PDFLineAnnotation* lineAnnotation = dynamic_cast<const PDFLineAnnotation*>(annotation))
    {
        const QLineF& line = lineAnnotation->getLine();
        if (!line.isNull())
        {
            result.points = { line.p1(), line.p2() };
            result.minimalCount = 2;
            result.maximalCount = 2;
        }
    }
    else if (const PDFPolygonalGeometryAnnotation* polygonalAnnotation = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(annotation))
    {
        // Jakub Melka: the path (PDF 2.0) has precedence over the vertices, when the
        // annotation is drawn, so it has precedence here too - otherwise the user would
        // edit points, which are not the points of the displayed shape. A path with
        // curves cannot be edited point by point.
        const bool isPolygon = annotation->getType() == AnnotationType::Polygon;
        const std::vector<QPointF> points = polygonalAnnotation->getPath().isEmpty() ? polygonalAnnotation->getVertices()
                                                                                     : getPathPoints(polygonalAnnotation->getPath());

        size_t minimalCount = isPolygon ? 3 : 2;
        size_t maximalCount = std::numeric_limits<size_t>::max();

        if (!isPolygon && polygonalAnnotation->getIntent() == PDFPolygonalGeometryAnnotation::Intent::Dimension)
        {
            // Three points of a measurement are an angle (the second point is its vertex),
            // any other number of points is a measured length. Inserting or removing of
            // a point must not turn one measurement into the other one.
            minimalCount = points.size() <= 3 ? std::max<size_t>(points.size(), 2) : 4;
            maximalCount = points.size() <= 3 ? points.size() : maximalCount;
        }

        if (points.size() >= minimalCount)
        {
            result.points = points;
            result.isClosed = isPolygon;
            result.minimalCount = minimalCount;
            result.maximalCount = maximalCount;
        }
    }
    else if (const PDFFreeTextAnnotation* freeTextAnnotation = dynamic_cast<const PDFFreeTextAnnotation*>(annotation))
    {
        // The callout line has two points, or three points (with a knee)
        const PDFAnnotationCalloutLine& calloutLine = freeTextAnnotation->getCalloutLine();
        switch (calloutLine.getType())
        {
            case PDFAnnotationCalloutLine::Type::StartEnd:
                result.points = { calloutLine.getPoint(0), calloutLine.getPoint(1) };
                break;

            case PDFAnnotationCalloutLine::Type::StartKneeEnd:
                result.points = { calloutLine.getPoint(0), calloutLine.getPoint(1), calloutLine.getPoint(2) };
                break;

            default:
                break;
        }

        result.isCalloutLine = result.isValid();
        result.minimalCount = 2;
        result.maximalCount = 3;
    }
    else if (const PDFAnnotationQuadrilaterals* quadrilaterals = getQuadrilaterals(annotation))
    {
        // Jakub Melka: each marked region (usually a line of a text) has a point in the middle
        // of its start and in the middle of its end. The corners are stored in the order
        // top left, top right, bottom left, bottom right (the same order is expected by
        // the renderer). Too many points would cover the whole annotation.
        constexpr size_t MAXIMAL_QUADRILATERAL_COUNT = 32;
        const PDFAnnotationQuadrilaterals::Quadrilaterals& quads = quadrilaterals->getQuadrilaterals();
        if (quads.size() <= MAXIMAL_QUADRILATERAL_COUNT)
        {
            for (const PDFAnnotationQuadrilaterals::Quadrilateral& quad : quads)
            {
                result.points.push_back((quad[0] + quad[2]) * 0.5);
                result.points.push_back((quad[1] + quad[3]) * 0.5);
            }

            result.isQuadEnds = true;
            result.minimalCount = result.points.size();
            result.maximalCount = result.points.size();
        }
    }

    return result;
}

const PDFAnnotationQuadrilaterals* PDFAnnotationManipulator::getQuadrilaterals(const PDFAnnotation* annotation)
{
    if (const PDFHighlightAnnotation* highlightAnnotation = dynamic_cast<const PDFHighlightAnnotation*>(annotation))
    {
        return &highlightAnnotation->getHiglightArea();
    }

    if (const PDFRedactAnnotation* redactAnnotation = dynamic_cast<const PDFRedactAnnotation*>(annotation))
    {
        return &redactAnnotation->getRedactionRegion();
    }

    return nullptr;
}

std::vector<PDFReal> PDFAnnotationManipulator::moveQuadrilateralEnds(const PDFAnnotationQuadrilaterals& quadrilaterals, const std::vector<QPointF>& points)
{
    std::vector<PDFReal> numbers;

    const PDFAnnotationQuadrilaterals::Quadrilaterals& quads = quadrilaterals.getQuadrilaterals();
    Q_ASSERT(points.size() == quads.size() * 2);

    for (size_t i = 0; i < quads.size(); ++i)
    {
        PDFAnnotationQuadrilaterals::Quadrilateral quad = quads[i];

        // Direction of the marked line (from its start to its end)
        const QPointF start = (quad[0] + quad[2]) * 0.5;
        const QPointF end = (quad[1] + quad[3]) * 0.5;
        const QPointF vector = end - start;
        const PDFReal length = std::hypot(vector.x(), vector.y());
        const QPointF direction = qFuzzyIsNull(length) ? QPointF(1.0, 0.0) : vector / length;

        // The ends move only along the line, so the line keeps its height and its slope
        const PDFReal startShift = QPointF::dotProduct(points[2 * i] - start, direction);
        const PDFReal endShift = QPointF::dotProduct(points[2 * i + 1] - end, direction);

        if (length + endShift - startShift < 1.0)
        {
            // The end would get before the start
            return std::vector<PDFReal>();
        }

        quad[0] += direction * startShift;
        quad[2] += direction * startShift;
        quad[1] += direction * endShift;
        quad[3] += direction * endShift;

        for (const QPointF& corner : quad)
        {
            numbers.push_back(corner.x());
            numbers.push_back(corner.y());
        }
    }

    return numbers;
}

std::vector<QPointF> PDFAnnotationManipulator::getPointsFromNumbers(const std::vector<PDFReal>& numbers)
{
    std::vector<QPointF> points;
    points.reserve(numbers.size() / 2);

    for (size_t i = 1; i < numbers.size(); i += 2)
    {
        points.emplace_back(numbers[i - 1], numbers[i]);
    }

    return points;
}

std::vector<QPointF> PDFAnnotationManipulator::getPathPoints(const QPainterPath& path)
{
    std::vector<QPointF> points;

    for (int i = 0, count = path.elementCount(); i < count; ++i)
    {
        const QPainterPath::Element element = path.elementAt(i);
        if (element.isCurveTo())
        {
            // Path with curves
            return std::vector<QPointF>();
        }

        points.emplace_back(element.x, element.y);
    }

    // Closed path ends with its first point (a path, which is
    // not empty, has always at least two points)
    Q_ASSERT(points.size() > 1);
    if (points.front() == points.back())
    {
        points.pop_back();
    }

    return points;
}

bool PDFAnnotationManipulator::isAngularMeasurement(const PDFAnnotation* annotation)
{
    const PDFPolygonalGeometryAnnotation* polygonalAnnotation = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(annotation);
    return polygonalAnnotation &&
           polygonalAnnotation->getType() == AnnotationType::Polyline &&
           polygonalAnnotation->getIntent() == PDFPolygonalGeometryAnnotation::Intent::Dimension &&
           polygonalAnnotation->getVertices().size() == 3;
}

bool PDFAnnotationManipulator::setEditablePoints(PDFDocumentBuilder* builder, PDFObjectReference annotation, const std::vector<QPointF>& points)
{
    if (!builder || !annotation.isValid())
    {
        return false;
    }

    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(annotation));
    if (!dictionary)
    {
        return false;
    }

    const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(storage, annotation);
    const EditablePoints currentPoints = getEditablePoints(parsedAnnotation.data());
    if (!currentPoints.isValid())
    {
        return false;
    }

    if (points.size() < currentPoints.minimalCount || points.size() > currentPoints.maximalCount)
    {
        return false;
    }

    std::vector<PDFReal> numbers;
    numbers.reserve(points.size() * 2);
    for (const QPointF& point : points)
    {
        numbers.push_back(point.x());
        numbers.push_back(point.y());
    }

    const QRectF rectangle = readRectangle(storage, dictionary, "Rect");
    QRectF oldPointsBounds = getPointsBoundingRectangle(currentPoints.points);
    QRectF newPointsBounds = getPointsBoundingRectangle(points);
    PDFDictionary modifiedDictionary = *dictionary;
    QRectF newRectangle;

    if (currentPoints.isQuadEnds)
    {
        // The points are the ends of the marked regions, the geometry are their corners
        const PDFAnnotationQuadrilaterals* quadrilaterals = getQuadrilaterals(parsedAnnotation.data());
        const std::vector<PDFReal> quadPoints = moveQuadrilateralEnds(*quadrilaterals, points);
        if (quadPoints.empty())
        {
            return false;
        }

        modifiedDictionary.setEntry(PDFInplaceOrMemoryString("QuadPoints"), createNumberArray(quadPoints));
        oldPointsBounds = quadrilaterals->getPath().boundingRect();
        newPointsBounds = getPointsBoundingRectangle(getPointsFromNumbers(quadPoints));
    }

    if (currentPoints.isCalloutLine)
    {
        // Jakub Melka: the text box must not move, when the callout line is changed
        const PDFReal margin = std::max(1.0, parsedAnnotation->getBorder().getWidth()) * 5.0;
        newRectangle = setFreeTextGeometry(modifiedDictionary, getFreeTextRectangle(storage, dictionary, rectangle), points, margin);
    }
    else
    {
        if (parsedAnnotation->getType() == AnnotationType::Line)
        {
            modifiedDictionary.setEntry(PDFInplaceOrMemoryString("L"), createNumberArray(numbers));
        }
        else if (!currentPoints.isQuadEnds)
        {
            // Jakub Melka: the shape is drawn from the path, if it is present (the points
            // were read from it). The vertices are kept consistent with the path.
            const PDFPolygonalGeometryAnnotation* polygonalAnnotation = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(parsedAnnotation.data());
            Q_ASSERT(polygonalAnnotation);

            const bool hasPath = !polygonalAnnotation->getPath().isEmpty();
            if (hasPath)
            {
                PDFObjectFactory pathFactory;
                pathFactory.beginArray();
                for (const QPointF& point : points)
                {
                    pathFactory << std::vector<PDFReal>{ point.x(), point.y() };
                }
                pathFactory.endArray();
                modifiedDictionary.setEntry(PDFInplaceOrMemoryString("Path"), pathFactory.takeObject());
            }

            if (!hasPath || dictionary->hasKey("Vertices"))
            {
                modifiedDictionary.setEntry(PDFInplaceOrMemoryString("Vertices"), createNumberArray(numbers));
            }
        }

        // Keep the margin between the points and the rectangle (line width, line
        // endings). Regenerated appearance stream sets the exact rectangle later.
        const PDFReal margin = std::max({ 1.0,
                                          oldPointsBounds.left() - rectangle.left(),
                                          oldPointsBounds.top() - rectangle.top(),
                                          rectangle.right() - oldPointsBounds.right(),
                                          rectangle.bottom() - oldPointsBounds.bottom() });
        newRectangle = newPointsBounds.adjusted(-margin, -margin, margin, margin);
    }

    modifiedDictionary.setEntry(PDFInplaceOrMemoryString("Rect"), createRectangle(newRectangle));
    builder->setObject(annotation, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(modifiedDictionary))));
    updateMeasurement(builder, annotation, parsedAnnotation.data());
    builder->updateAnnotationAppearanceStreams(annotation);
    return true;
}

QRectF PDFAnnotationManipulator::getPointsBoundingRectangle(const std::vector<QPointF>& points)
{
    Q_ASSERT(!points.empty());

    PDFReal left = points.front().x();
    PDFReal right = left;
    PDFReal top = points.front().y();
    PDFReal bottom = top;

    for (const QPointF& point : points)
    {
        left = std::min(left, point.x());
        right = std::max(right, point.x());
        top = std::min(top, point.y());
        bottom = std::max(bottom, point.y());
    }

    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

std::vector<PDFAnnotationManipulator::NumberToken> PDFAnnotationManipulator::parseNumberTokens(const QString& text)
{
    std::vector<NumberToken> tokens;

    // Returns the number of digits, which start at the position
    auto countDigits = [&text](qsizetype position)
    {
        qsizetype count = 0;
        while (position + count < text.size() && text[position + count] >= QChar('0') && text[position + count] <= QChar('9'))
        {
            ++count;
        }
        return count;
    };

    // Jakub Melka: the number is a sequence of digits, which can contain dots and
    // commas (each of them can be a decimal separator, or a separator of thousands)
    // and separators of thousands used by various locales (spaces, apostrophe). We
    // do not know the locale, which formatted the number, so the last dot (comma)
    // is considered to be the decimal separator. If it is a separator of thousands
    // in fact, then the value is a thousand times smaller - we count with it, when
    // the value is compared, and it does not matter, when the value is scaled.
    const QString decimalSeparators = QString::fromUtf8(".,");
    const QString groupSeparators = QString(QChar(0x0020)) + QChar(0x00A0) + QChar(0x2009) + QChar(0x202F) + QChar(0x0027);

    qsizetype position = 0;
    while (position < text.size())
    {
        qsizetype length = countDigits(position);
        if (length == 0)
        {
            ++position;
            continue;
        }

        qsizetype decimalPosition = -1;
        while (position + length < text.size())
        {
            const QChar character = text[position + length];
            const qsizetype followingDigits = countDigits(position + length + 1);

            if (decimalSeparators.contains(character) && followingDigits > 0)
            {
                decimalPosition = position + length;
            }
            else if (!groupSeparators.contains(character) || followingDigits != 3)
            {
                break;
            }

            length += followingDigits + 1;
        }

        NumberToken token;
        token.position = position;
        token.length = length;
        token.decimalSeparator = QChar('.');

        QString number;
        for (qsizetype i = position; i < position + length; ++i)
        {
            const QChar character = text[i];
            if (i == decimalPosition)
            {
                token.decimals = int(position + length - decimalPosition - 1);
                token.decimalSeparator = character;
                number += QChar('.');
            }
            else if (countDigits(i) > 0)
            {
                number += character;
            }
            else if (token.groupSeparator.isNull())
            {
                token.groupSeparator = character;
            }
        }

        token.value = number.toDouble();
        tokens.push_back(token);
        position += length;
    }

    return tokens;
}

QString PDFAnnotationManipulator::formatNumberToken(const NumberToken& token, PDFReal value)
{
    const QString number = QString::number(value, 'f', token.decimals);
    const qsizetype integerLength = number.size() - (token.decimals > 0 ? token.decimals + 1 : 0);

    QString result = number.left(integerLength);
    if (!token.groupSeparator.isNull())
    {
        for (qsizetype i = result.size() - 3; i > 0; i -= 3)
        {
            result.insert(i, token.groupSeparator);
        }
    }

    if (token.decimals > 0)
    {
        result += token.decimalSeparator;
        result += number.right(token.decimals);
    }

    return result;
}

std::vector<PDFReal> PDFAnnotationManipulator::getMeasuredValues(const PDFAnnotation* annotation, const PDFMeasure& measure)
{
    std::vector<PDFReal> values;
    std::vector<QPointF> points;
    bool isClosed = false;

    if (const PDFLineAnnotation* lineAnnotation = dynamic_cast<const PDFLineAnnotation*>(annotation))
    {
        if (lineAnnotation->getIntent() == PDFLineAnnotation::Intent::Dimension)
        {
            points = { lineAnnotation->getLine().p1(), lineAnnotation->getLine().p2() };
        }
    }
    else if (const PDFPolygonalGeometryAnnotation* polygonalAnnotation = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(annotation))
    {
        if (polygonalAnnotation->getIntent() == PDFPolygonalGeometryAnnotation::Intent::Dimension)
        {
            points = polygonalAnnotation->getVertices();
            isClosed = annotation->getType() == AnnotationType::Polygon;
        }
    }

    if (points.size() < 2)
    {
        return values;
    }

    // Jakub Melka: the first number format of the array converts the value to the
    // largest unit, in which the value is displayed (see PDF 2.0 specification, 12.9.2).
    // Invalid measure has no formats, so the values stay in the default user space units.
    auto getFactor = [](const std::vector<PDFNumberFormat>& formats)
    {
        return formats.empty() ? 1.0 : formats.front().getConversionFactor();
    };

    const PDFReal scaleX = getFactor(measure.getXFormat());
    // If the measure does not define the y axis, then it has the format of the x axis
    const PDFReal scaleY = getFactor(measure.getYFormat()) * measure.getFactorYX();

    if (isAngularMeasurement(annotation))
    {
        // The same angle, as it is drawn by the annotation
        const QLineF firstArm(points[1], points[0]);
        const QLineF secondArm(points[1], points[2]);
        values.push_back(firstArm.angleTo(secondArm) * getFactor(measure.getAngleFormat()));
        return values;
    }

    if (isClosed)
    {
        points.push_back(points.front());
    }

    // Length (perimeter) and area. Both axes can have a different scale.
    PDFReal length = 0.0;
    PDFReal area = 0.0;
    for (size_t i = 1; i < points.size(); ++i)
    {
        const QPointF start(points[i - 1].x() * scaleX, points[i - 1].y() * scaleY);
        const QPointF end(points[i].x() * scaleX, points[i].y() * scaleY);
        length += std::hypot(end.x() - start.x(), end.y() - start.y());
        area += start.x() * end.y() - start.y() * end.x();
    }

    // If the distance format is missing, then the distance is displayed
    // using the format of the x axis, which has been applied already.
    values.push_back(length * getFactor(measure.getDistanceFormat()));

    if (isClosed)
    {
        values.push_back(std::abs(area) * 0.5 * getFactor(measure.getAreaFormat()));
    }

    return values;
}

QString PDFAnnotationManipulator::updateMeasurementText(const QString& text,
                                                        bool isMeasureValid,
                                                        bool isPolygon,
                                                        const std::vector<PDFReal>& oldValues,
                                                        const std::vector<PDFReal>& newValues)
{
    Q_ASSERT(oldValues.size() == newValues.size());

    const std::vector<NumberToken> tokens = parseNumberTokens(text);

    auto replaceToken = [&text](const NumberToken& token, PDFReal value)
    {
        QString result = text;
        result.replace(token.position, token.length, formatNumberToken(token, value));
        return result;
    };

    if (isMeasureValid)
    {
        // Jakub Melka: we know the scale, so we know the value, which was displayed
        // for the old geometry. If the text contains it, then it is replaced by the
        // value of the new geometry. If it does not, then the text is a comment
        // written by the user and we leave it alone.
        for (size_t i = 0; i < oldValues.size(); ++i)
        {
            for (const NumberToken& token : tokens)
            {
                const PDFReal tolerance = 0.5 * std::pow(10.0, -token.decimals) + 1e-6 * std::max(1.0, oldValues[i]);
                if (std::abs(token.value - oldValues[i]) <= tolerance)
                {
                    return replaceToken(token, newValues[i]);
                }

                // Three digits after the dot (comma) can be a group of thousands
                if (token.decimals == 3 && std::abs(token.value * 1000.0 - oldValues[i]) <= 0.5 + 1e-6 * oldValues[i])
                {
                    NumberToken groupedToken = token;
                    groupedToken.groupSeparator = token.decimalSeparator;
                    return replaceToken(groupedToken, newValues[i] / 1000.0);
                }
            }
        }

        return text;
    }

    // Jakub Melka: the scale is not known. We can still scale the displayed value
    // by the ratio of the new and the old geometry, but we must be sure, that the
    // text is a measured value. We accept a single number followed by a unit
    // ("12.5 mm"), which can be introduced by a symbol of the quantity ("A = 12.5 m2").
    if (tokens.size() != 1)
    {
        return text;
    }

    const NumberToken& token = tokens.front();
    const QString prefix = text.left(token.position).trimmed();
    const QString suffix = text.mid(token.position + token.length).trimmed();

    static const QRegularExpression prefixExpression(QStringLiteral("^(\\w{1,3}\\s*[=:])?$"), QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression suffixExpression(QStringLiteral("^\\S*$"));
    if (!prefixExpression.match(prefix).hasMatch() || !suffixExpression.match(suffix).hasMatch())
    {
        return text;
    }

    size_t index = 0;
    if (isPolygon)
    {
        // Polygon measures its perimeter, or its area. The scale is not known,
        // so they can be distinguished only by the symbol of the quantity.
        if (prefix.startsWith(QChar('A'), Qt::CaseInsensitive))
        {
            index = 1;
        }
        else if (!prefix.startsWith(QChar('P'), Qt::CaseInsensitive))
        {
            return text;
        }
    }

    if (qFuzzyIsNull(oldValues[index]))
    {
        return text;
    }

    return replaceToken(token, token.value * newValues[index] / oldValues[index]);
}

bool PDFAnnotationManipulator::updateMeasurement(PDFDocumentBuilder* builder, PDFObjectReference annotation, const PDFAnnotation* oldAnnotation)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const PDFAnnotationPtr newAnnotation = PDFAnnotation::parse(storage, annotation);
    Q_ASSERT(newAnnotation);

    // Jakub Melka: measure, which does not define the scale of the x axis, is useless
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObject(annotation));
    PDFMeasure measure = PDFMeasure::parse(storage, dictionary->get("Measure"));
    const bool isMeasureValid = measure.isRectilinear() && measure.getUnitsPerUserSpaceUnit() > 0.0;
    if (!isMeasureValid)
    {
        measure = PDFMeasure();
    }

    const std::vector<PDFReal> oldValues = getMeasuredValues(oldAnnotation, measure);
    const std::vector<PDFReal> newValues = getMeasuredValues(newAnnotation.data(), measure);
    if (oldValues.empty())
    {
        return false;
    }

    // The same quantities are measured - points cannot be added to (removed from)
    // an annotation in a way, which would change the kind of the measurement
    Q_ASSERT(oldValues.size() == newValues.size());

    const QString contents = newAnnotation->getContents();
    const QString newContents = updateMeasurementText(contents, isMeasureValid, newAnnotation->getType() == AnnotationType::Polygon, oldValues, newValues);
    if (newContents == contents)
    {
        return false;
    }

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Contents");
    factory << newContents;
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->mergeTo(annotation, factory.takeObject());
    return true;
}

PDFAnnotationManipulator::Parts PDFAnnotationManipulator::getParts(const PDFObjectStorage* storage, PDFObjectReference annotation)
{
    Parts parts;

    const PDFDictionary* dictionary = storage ? storage->getDictionaryFromObject(storage->getObject(annotation)) : nullptr;
    if (!dictionary)
    {
        return parts;
    }

    const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(storage, annotation);
    Q_ASSERT(parsedAnnotation);

    if (const PDFAnnotationQuadrilaterals* quadrilaterals = getQuadrilaterals(parsedAnnotation.data()))
    {
        // Marked regions. The corners are stored in the order top left, top right,
        // bottom left, bottom right, so the polygon goes around the region.
        parts.isFilled = true;
        for (const PDFAnnotationQuadrilaterals::Quadrilateral& quad : quadrilaterals->getQuadrilaterals())
        {
            parts.shapes.push_back(QPolygonF({ quad[0], quad[1], quad[3], quad[2] }));
        }
    }
    else if (parsedAnnotation->getType() == AnnotationType::Ink && !storage->getObject(dictionary->get("Path")).isArray())
    {
        // Strokes of the ink list. An ink defined by a path (PDF 2.0) is a single stroke.
        PDFDocumentDataLoaderDecorator loader(storage);
        const PDFObject inkList = storage->getObject(dictionary->get("InkList"));
        if (inkList.isArray())
        {
            for (const PDFObject& stroke : *inkList.getArray())
            {
                const std::vector<QPointF> points = getPointsFromNumbers(loader.readNumberArray(stroke));
                parts.shapes.push_back(QPolygonF(QList<QPointF>(points.cbegin(), points.cend())));
            }
        }
    }

    return parts;
}

bool PDFAnnotationManipulator::removePart(PDFDocumentBuilder* builder, PDFObjectReference annotation, size_t index)
{
    if (!builder)
    {
        return false;
    }

    const PDFObjectStorage* storage = builder->getStorage();
    Parts parts = getParts(storage, annotation);
    if (!parts.canRemovePart() || index >= parts.shapes.size())
    {
        return false;
    }

    parts.shapes.erase(std::next(parts.shapes.begin(), index));

    // Remaining parts and their bounding rectangle
    PDFObjectFactory factory;
    std::vector<PDFReal> quadPoints;
    QRectF boundingRectangle;

    factory.beginArray();
    for (const QPolygonF& shape : parts.shapes)
    {
        std::vector<PDFReal> numbers;
        for (const QPointF& point : shape)
        {
            numbers.push_back(point.x());
            numbers.push_back(point.y());
        }

        if (parts.isFilled)
        {
            // Back to the order top left, top right, bottom left, bottom right
            std::swap(numbers[4], numbers[6]);
            std::swap(numbers[5], numbers[7]);
            quadPoints.insert(quadPoints.end(), numbers.cbegin(), numbers.cend());
        }

        factory << numbers;
        boundingRectangle = boundingRectangle.united(shape.boundingRect());
    }
    factory.endArray();

    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObject(annotation));
    PDFDictionary modifiedDictionary = *dictionary;
    if (parts.isFilled)
    {
        modifiedDictionary.setEntry(PDFInplaceOrMemoryString("QuadPoints"), createNumberArray(quadPoints));
    }
    else
    {
        modifiedDictionary.setEntry(PDFInplaceOrMemoryString("InkList"), factory.takeObject());
    }

    // Regenerated appearance stream sets the exact rectangle
    const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(storage, annotation);
    const PDFReal margin = std::max(1.0, parsedAnnotation->getBorder().getWidth());
    modifiedDictionary.setEntry(PDFInplaceOrMemoryString("Rect"), createRectangle(boundingRectangle.adjusted(-margin, -margin, margin, margin)));
    builder->setObject(annotation, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(modifiedDictionary))));
    builder->updateAnnotationAppearanceStreams(annotation);
    return true;
}

QRectF PDFAnnotationManipulator::getFreeTextRectangle(const PDFObjectStorage* storage, PDFObjectReference annotation)
{
    const PDFDictionary* dictionary = storage ? storage->getDictionaryFromObject(storage->getObject(annotation)) : nullptr;
    if (!dictionary)
    {
        return QRectF();
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    if (loader.readNameFromDictionary(dictionary, "Subtype") != "FreeText")
    {
        return QRectF();
    }

    return getFreeTextRectangle(storage, dictionary, readRectangle(storage, dictionary, "Rect"));
}

bool PDFAnnotationManipulator::setFreeTextRectangle(PDFDocumentBuilder* builder, PDFObjectReference annotation, const QRectF& textRectangle)
{
    const QRectF newTextRectangle = textRectangle.normalized();
    const QRectF oldTextRectangle = builder ? getFreeTextRectangle(builder->getStorage(), annotation) : QRectF();
    if (!oldTextRectangle.isValid() || !newTextRectangle.isValid())
    {
        return false;
    }

    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObject(annotation));
    const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(storage, annotation);
    const QRectF rectangle = readRectangle(storage, dictionary, "Rect");

    PDFDictionary modifiedDictionary = *dictionary;
    QRectF newRectangle;

    EditablePoints calloutLine = getEditablePoints(parsedAnnotation.data());
    if (calloutLine.isCalloutLine)
    {
        // Jakub Melka: the tip of the callout line points to a place on the page, so it
        // stays. The rest of the line is attached to the text box, so it keeps its
        // position relative to the text box (the end of the line stays on its border).
        const QTransform textBoxTransform = QTransform::fromTranslate(-oldTextRectangle.left(), -oldTextRectangle.top()) *
                                            QTransform::fromScale(newTextRectangle.width() / oldTextRectangle.width(), newTextRectangle.height() / oldTextRectangle.height()) *
                                            QTransform::fromTranslate(newTextRectangle.left(), newTextRectangle.top());
        for (size_t i = 1; i < calloutLine.points.size(); ++i)
        {
            calloutLine.points[i] = textBoxTransform.map(calloutLine.points[i]);
        }

        const PDFReal margin = std::max(1.0, parsedAnnotation->getBorder().getWidth()) * 5.0;
        newRectangle = setFreeTextGeometry(modifiedDictionary, newTextRectangle, calloutLine.points, margin);
    }
    else
    {
        // The rectangle differences (space for a border effect) are kept
        newRectangle = newTextRectangle.adjusted(rectangle.left() - oldTextRectangle.left(),
                                                 rectangle.top() - oldTextRectangle.top(),
                                                 rectangle.right() - oldTextRectangle.right(),
                                                 rectangle.bottom() - oldTextRectangle.bottom());
    }

    translatePopup(builder, dictionary, newRectangle.center() - rectangle.center());
    modifiedDictionary.setEntry(PDFInplaceOrMemoryString("Rect"), createRectangle(newRectangle));
    builder->setObject(annotation, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(modifiedDictionary))));
    builder->updateAnnotationAppearanceStreams(annotation);
    return true;
}

bool PDFAnnotationManipulator::setFreeTextCalloutLine(PDFDocumentBuilder* builder, PDFObjectReference annotation, const std::vector<QPointF>& calloutLine)
{
    const QRectF textRectangle = builder ? getFreeTextRectangle(builder->getStorage(), annotation) : QRectF();
    const bool isCountValid = calloutLine.empty() || calloutLine.size() == 2 || calloutLine.size() == 3;
    if (!textRectangle.isValid() || !isCountValid)
    {
        return false;
    }

    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObject(annotation));
    const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(storage, annotation);

    PDFDictionary modifiedDictionary = *dictionary;
    QRectF newRectangle = textRectangle;

    if (calloutLine.empty())
    {
        // The annotation rectangle is the text box again
        modifiedDictionary.removeEntry("CL");
        modifiedDictionary.removeEntry("RD");
        modifiedDictionary.removeEntry("LE");
        modifiedDictionary.setEntry(PDFInplaceOrMemoryString("IT"), PDFObject::createName("FreeText"));
    }
    else
    {
        const PDFReal margin = std::max(1.0, parsedAnnotation->getBorder().getWidth()) * 5.0;
        newRectangle = setFreeTextGeometry(modifiedDictionary, textRectangle, calloutLine, margin);
        modifiedDictionary.setEntry(PDFInplaceOrMemoryString("IT"), PDFObject::createName("FreeTextCallout"));

        if (!dictionary->hasKey("LE"))
        {
            // The tip of a new callout line is an arrow
            PDFObjectFactory factory;
            factory.beginArray();
            factory << WrapName("OpenArrow") << WrapName("None");
            factory.endArray();
            modifiedDictionary.setEntry(PDFInplaceOrMemoryString("LE"), factory.takeObject());
        }
    }

    modifiedDictionary.setEntry(PDFInplaceOrMemoryString("Rect"), createRectangle(newRectangle));
    builder->setObject(annotation, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(modifiedDictionary))));
    builder->updateAnnotationAppearanceStreams(annotation);
    return true;
}

PDFDictionary PDFAnnotationManipulator::prepareAnnotationForCopy(const PDFDictionary& dictionary, bool removeOptionalContent, bool isReply)
{
    // Jakub Melka: the link to the replied annotation is always removed (the deep
    // copy would follow it). It is restored, when the copies are linked together.
    PDFDictionary result = dictionary;
    result.removeEntry("P");
    result.removeEntry("Popup");
    result.removeEntry("IRT");
    result.removeEntry("StructParent");

    if (!isReply)
    {
        result.removeEntry("RT");
    }

    if (removeOptionalContent)
    {
        result.removeEntry("OC");
    }

    return result;
}

PDFDictionary PDFAnnotationManipulator::preparePopupForCopy(const PDFDictionary& dictionary)
{
    PDFDictionary result = dictionary;
    result.removeEntry("P");
    result.removeEntry("Parent");
    return result;
}

bool PDFAnnotationManipulator::isAnnotationDictionary(const PDFObjectStorage* storage, const PDFDictionary* dictionary)
{
    // Jakub Melka: the type entry is optional, but the subtype is required. Popup
    // annotations are handled together with their parent annotations.
    PDFDocumentDataLoaderDecorator loader(storage);
    const QByteArray subtype = loader.readNameFromDictionary(dictionary, "Subtype");
    return !subtype.isEmpty() && subtype != "Popup";
}

bool PDFAnnotationManipulator::isAnnotationOnPage(const PDFObjectStorage* storage, PDFObjectReference page, PDFObjectReference annotation)
{
    const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObject(page));
    if (!pageDictionary)
    {
        return false;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    const std::vector<PDFObjectReference> annotations = loader.readReferenceArrayFromDictionary(pageDictionary, "Annots");
    return std::find(annotations.cbegin(), annotations.cend(), annotation) != annotations.cend();
}

PDFObjectReference PDFAnnotationManipulator::findAnnotationPage(const PDFObjectStorage* storage, PDFObjectReference annotation)
{
    const PDFDictionary* dictionary = storage ? storage->getDictionaryFromObject(storage->getObject(annotation)) : nullptr;
    if (!dictionary)
    {
        return PDFObjectReference();
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    const PDFObjectReference hintedPage = loader.readReferenceFromDictionary(dictionary, "P");
    if (isAnnotationOnPage(storage, hintedPage, annotation))
    {
        return hintedPage;
    }

    // Search the page tree. Visited nodes are remembered - a damaged tree can contain a cycle.
    std::set<PDFObjectReference> visitedNodes;
    std::vector<PDFObjectReference> nodes;

    if (const PDFDictionary* trailerDictionary = storage->getDictionaryFromObject(storage->getTrailerDictionary()))
    {
        if (const PDFDictionary* catalogDictionary = storage->getDictionaryFromObject(trailerDictionary->get("Root")))
        {
            nodes.push_back(loader.readReferenceFromDictionary(catalogDictionary, "Pages"));
        }
    }

    while (!nodes.empty())
    {
        const PDFObjectReference node = nodes.back();
        nodes.pop_back();

        const PDFDictionary* nodeDictionary = storage->getDictionaryFromObject(storage->getObject(node));
        if (!nodeDictionary || !visitedNodes.insert(node).second)
        {
            continue;
        }

        if (isAnnotationOnPage(storage, node, annotation))
        {
            return node;
        }

        const std::vector<PDFObjectReference> kids = loader.readReferenceArrayFromDictionary(nodeDictionary, "Kids");
        nodes.insert(nodes.end(), kids.crbegin(), kids.crend());
    }

    return PDFObjectReference();
}

std::vector<PDFObjectReference> PDFAnnotationManipulator::getReplies(const PDFObjectStorage* storage, PDFObjectReference page, PDFObjectReference annotation)
{
    std::vector<PDFObjectReference> replies;

    const PDFDictionary* pageDictionary = storage ? storage->getDictionaryFromObject(storage->getObject(page)) : nullptr;
    if (!pageDictionary)
    {
        return replies;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    const std::vector<PDFObjectReference> pageAnnotations = loader.readReferenceArrayFromDictionary(pageDictionary, "Annots");

    // Replies can have their own replies, so the thread is collected transitively
    std::set<PDFObjectReference> thread = { annotation };
    bool isModified = true;
    while (isModified)
    {
        isModified = false;

        for (const PDFObjectReference& pageAnnotation : pageAnnotations)
        {
            const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObject(pageAnnotation));
            if (!dictionary || thread.count(pageAnnotation))
            {
                continue;
            }

            const PDFObject& inReplyTo = dictionary->get("IRT");
            if (inReplyTo.isReference() && thread.count(inReplyTo.getReference()))
            {
                thread.insert(pageAnnotation);
                replies.push_back(pageAnnotation);
                isModified = true;
            }
        }
    }

    return replies;
}

const PDFDictionary* PDFAnnotationManipulator::getPopupDictionary(const PDFObjectStorage* storage, const PDFDictionary* annotationDictionary)
{
    const PDFObject& popupObject = annotationDictionary->get("Popup");
    if (!popupObject.isReference())
    {
        return nullptr;
    }

    return storage->getDictionaryFromObject(storage->getObjectByReference(popupObject.getReference()));
}

void PDFAnnotationManipulator::linkAnnotation(PDFDocumentBuilder* builder,
                                              PDFObjectReference annotation,
                                              PDFObjectReference popup,
                                              PDFObjectReference page,
                                              bool createName,
                                              PDFObjectReference inReplyTo)
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("P");
    factory << page;
    factory.endDictionaryItem();
    if (inReplyTo.isValid())
    {
        factory.beginDictionaryItem("IRT");
        factory << inReplyTo;
        factory.endDictionaryItem();
    }
    if (popup.isValid())
    {
        factory.beginDictionaryItem("Popup");
        factory << popup;
        factory.endDictionaryItem();
    }
    if (createName)
    {
        factory.beginDictionaryItem("NM");
        factory << createUniqueName();
        factory.endDictionaryItem();
    }
    factory.endDictionary();
    builder->mergeTo(annotation, factory.takeObject());

    if (popup.isValid())
    {
        factory.beginDictionary();
        factory.beginDictionaryItem("P");
        factory << page;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Parent");
        factory << annotation;
        factory.endDictionaryItem();
        factory.endDictionary();
        builder->mergeTo(popup, factory.takeObject());
    }
}

void PDFAnnotationManipulator::appendAnnotationsToPage(PDFDocumentBuilder* builder,
                                                       PDFObjectReference page,
                                                       const std::vector<PDFObjectReference>& annotations)
{
    Q_ASSERT(!annotations.empty());

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    factory << annotations;
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->appendTo(page, factory.takeObject());
}

bool PDFAnnotationManipulator::removeAnnotationFromPage(PDFDocumentBuilder* builder,
                                                        PDFObjectReference page,
                                                        PDFObjectReference annotation)
{
    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page));
    if (!pageDictionary)
    {
        return false;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    std::vector<PDFObjectReference> annotations = loader.readReferenceArrayFromDictionary(pageDictionary, "Annots");
    const auto newEnd = std::remove(annotations.begin(), annotations.end(), annotation);
    if (newEnd == annotations.end())
    {
        return false;
    }
    annotations.erase(newEnd, annotations.end());

    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Annots");
    if (!annotations.empty())
    {
        factory << annotations;
    }
    else
    {
        factory << PDFObject();
    }
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->mergeTo(page, factory.takeObject());
    return true;
}

PDFObjectReference PDFAnnotationManipulator::copyAnnotation(PDFDocumentBuilder* builder,
                                                            PDFObjectReference annotation,
                                                            PDFObjectReference targetPage)
{
    if (!builder || !annotation.isValid() || !targetPage.isValid())
    {
        return PDFObjectReference();
    }

    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(annotation));
    if (!dictionary)
    {
        return PDFObjectReference();
    }

    // Jakub Melka: the whole comment thread is copied - the annotation, the replies
    // to it and the popup windows. The copy is shallow - appearance streams (which can
    // be large, for example image stamps) are shared with the original annotation. The
    // manipulator never modifies the streams in place, so sharing is safe.
    std::vector<PDFObjectReference> thread = getReplies(storage, findAnnotationPage(storage, annotation), annotation);
    thread.insert(thread.begin(), annotation);

    std::map<PDFObjectReference, PDFObjectReference> copies;
    std::vector<PDFObjectReference> pageAnnotations;
    for (const PDFObjectReference& sourceAnnotation : thread)
    {
        // Dictionaries are copied, because adding of an object invalidates the pointers
        const bool isReply = sourceAnnotation != annotation;
        const PDFDictionary sourceDictionary = *storage->getDictionaryFromObject(storage->getObject(sourceAnnotation));

        PDFDictionary popupDictionary;
        const bool hasPopup = getPopupDictionary(storage, &sourceDictionary) != nullptr;
        if (hasPopup)
        {
            popupDictionary = preparePopupForCopy(*getPopupDictionary(storage, &sourceDictionary));
        }

        PDFObjectReference inReplyTo;
        if (isReply)
        {
            inReplyTo = copies.at(sourceDictionary.get("IRT").getReference());
        }

        const PDFObjectReference copiedAnnotation = builder->addObject(PDFObject::createDictionary(std::make_shared<PDFDictionary>(prepareAnnotationForCopy(sourceDictionary, false, isReply))));
        PDFObjectReference copiedPopup;
        if (hasPopup)
        {
            copiedPopup = builder->addObject(PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(popupDictionary))));
        }

        linkAnnotation(builder, copiedAnnotation, copiedPopup, targetPage, sourceDictionary.hasKey("NM"), inReplyTo);
        copies[sourceAnnotation] = copiedAnnotation;

        pageAnnotations.push_back(copiedAnnotation);
        if (copiedPopup.isValid())
        {
            pageAnnotations.push_back(copiedPopup);
        }
    }

    appendAnnotationsToPage(builder, targetPage, pageAnnotations);
    return copies.at(annotation);
}

bool PDFAnnotationManipulator::moveAnnotationToPage(PDFDocumentBuilder* builder,
                                                    PDFObjectReference annotation,
                                                    PDFObjectReference sourcePage,
                                                    PDFObjectReference targetPage)
{
    if (!builder || !annotation.isValid() || !sourcePage.isValid() || !targetPage.isValid() || sourcePage == targetPage)
    {
        return false;
    }

    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(annotation));
    if (!dictionary)
    {
        return false;
    }

    // Jakub Melka: replies are displayed together with the annotation, to which they
    // reply, and they are searched only on the page of that annotation. So the whole
    // comment thread must be moved, otherwise the replies would be lost for the user.
    std::vector<PDFObjectReference> thread = getReplies(storage, sourcePage, annotation);
    thread.insert(thread.begin(), annotation);

    std::vector<PDFObjectReference> movedAnnotations;
    for (const PDFObjectReference& movedAnnotation : thread)
    {
        PDFObjectReference popup;
        const PDFDictionary* movedDictionary = storage->getDictionaryFromObject(storage->getObject(movedAnnotation));
        if (getPopupDictionary(storage, movedDictionary))
        {
            popup = movedDictionary->get("Popup").getReference();
        }

        removeAnnotationFromPage(builder, sourcePage, movedAnnotation);
        movedAnnotations.push_back(movedAnnotation);

        if (popup.isValid())
        {
            removeAnnotationFromPage(builder, sourcePage, popup);
            movedAnnotations.push_back(popup);
        }

        linkAnnotation(builder, movedAnnotation, popup, targetPage, false, PDFObjectReference());
    }

    appendAnnotationsToPage(builder, targetPage, movedAnnotations);
    return true;
}

std::vector<PDFObjectReference> PDFAnnotationManipulator::importAnnotations(PDFDocumentBuilder* builder,
                                                                            const PDFObjectStorage& storage,
                                                                            const std::vector<PDFObjectReference>& annotations,
                                                                            PDFObjectReference targetPage,
                                                                            bool removeOptionalContent,
                                                                            std::vector<PDFObjectReference>& allAnnotations)
{
    constexpr size_t INVALID_INDEX = std::numeric_limits<size_t>::max();

    /// Annotation of a comment thread, which is copied
    struct ImportedAnnotation
    {
        size_t annotationIndex = INVALID_INDEX; ///< Index of the annotation in the copied objects
        size_t popupIndex = INVALID_INDEX;      ///< Index of the popup in the copied objects
        size_t inReplyTo = INVALID_INDEX;       ///< Index of the imported annotation, to which this one replies
    };

    std::vector<PDFObjectReference> result;

    // 1) Prepare the dictionaries, which are copied. The whole comment threads are
    //    copied (annotations, replies to them and the popup windows). References to the
    //    source page, to the popup and to the replied annotation are removed, otherwise
    //    the deep copy would drag the whole source document along.
    std::vector<PDFObject> objects;
    std::vector<ImportedAnnotation> importedAnnotations;
    std::map<PDFObjectReference, size_t> importedAnnotationIndices;
    for (const PDFObjectReference& annotation : annotations)
    {
        const PDFDictionary* dictionary = storage.getDictionaryFromObject(storage.getObjectByReference(annotation));
        if (!dictionary || !isAnnotationDictionary(&storage, dictionary))
        {
            continue;
        }

        std::vector<PDFObjectReference> thread = getReplies(&storage, findAnnotationPage(&storage, annotation), annotation);
        thread.insert(thread.begin(), annotation);

        for (const PDFObjectReference& sourceAnnotation : thread)
        {
            if (importedAnnotationIndices.count(sourceAnnotation))
            {
                // The annotation has been copied already (as a part of another thread)
                continue;
            }

            const bool isReply = sourceAnnotation != annotation;
            const PDFDictionary* sourceDictionary = storage.getDictionaryFromObject(storage.getObjectByReference(sourceAnnotation));

            ImportedAnnotation importedAnnotation;
            importedAnnotation.annotationIndex = objects.size();
            objects.emplace_back(PDFObject::createDictionary(std::make_shared<PDFDictionary>(prepareAnnotationForCopy(*sourceDictionary, removeOptionalContent, isReply))));

            if (const PDFDictionary* popupDictionary = getPopupDictionary(&storage, sourceDictionary))
            {
                importedAnnotation.popupIndex = objects.size();
                objects.emplace_back(PDFObject::createDictionary(std::make_shared<PDFDictionary>(preparePopupForCopy(*popupDictionary))));
            }

            if (isReply)
            {
                importedAnnotation.inReplyTo = importedAnnotationIndices.at(sourceDictionary->get("IRT").getReference());
            }

            importedAnnotationIndices[sourceAnnotation] = importedAnnotations.size();
            importedAnnotations.push_back(importedAnnotation);
        }
    }

    if (objects.empty())
    {
        return result;
    }

    // 2) Deep copy of the prepared dictionaries (with their appearance streams etc.)
    const std::vector<PDFObject> copiedObjects = builder->copyFrom(objects, storage, true);
    Q_ASSERT(copiedObjects.size() == objects.size());

    // 3) Link the copies with the target page and with each other
    std::vector<PDFObjectReference> pageAnnotations;
    for (const ImportedAnnotation& importedAnnotation : importedAnnotations)
    {
        const PDFObjectReference copiedAnnotation = copiedObjects[importedAnnotation.annotationIndex].getReference();
        PDFObjectReference copiedPopup;
        if (importedAnnotation.popupIndex != INVALID_INDEX)
        {
            copiedPopup = copiedObjects[importedAnnotation.popupIndex].getReference();
        }

        PDFObjectReference inReplyTo;
        if (importedAnnotation.inReplyTo != INVALID_INDEX)
        {
            inReplyTo = copiedObjects[importedAnnotations[importedAnnotation.inReplyTo].annotationIndex].getReference();
        }
        else
        {
            result.push_back(copiedAnnotation);
        }

        linkAnnotation(builder, copiedAnnotation, copiedPopup, targetPage, false, inReplyTo);

        pageAnnotations.push_back(copiedAnnotation);
        if (copiedPopup.isValid())
        {
            pageAnnotations.push_back(copiedPopup);
        }
        allAnnotations.push_back(copiedAnnotation);
    }

    appendAnnotationsToPage(builder, targetPage, pageAnnotations);
    return result;
}

QByteArray PDFAnnotationManipulator::serializeAnnotations(const PDFDocument* document, const std::vector<PDFObjectReference>& annotations)
{
    if (!document || annotations.empty())
    {
        return QByteArray();
    }

    QRectF boundingRectangle;
    for (const PDFObjectReference& annotation : annotations)
    {
        const PDFDictionary* dictionary = document->getDictionaryFromObject(document->getObjectByReference(annotation));
        if (dictionary)
        {
            boundingRectangle = boundingRectangle.united(readRectangle(&document->getStorage(), dictionary, "Rect"));
        }
    }

    if (!boundingRectangle.isValid())
    {
        // Media box of the page must be a valid rectangle
        boundingRectangle = QRectF(boundingRectangle.topLeft(), QSizeF(1.0, 1.0));
    }

    PDFDocumentBuilder builder;
    const PDFObjectReference page = builder.appendPage(boundingRectangle);
    std::vector<PDFObjectReference> allAnnotations;
    const std::vector<PDFObjectReference> importedAnnotations = importAnnotations(&builder, document->getStorage(), annotations, page, true, allAnnotations);
    if (importedAnnotations.empty())
    {
        return QByteArray();
    }

    const PDFDocument serializedDocument = builder.build();

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    PDFDocumentWriter writer(nullptr);
    // Jakub Melka: the document is not encrypted and the buffer is writable,
    // so there is no reason, why the writing should fail.
    [[maybe_unused]] const PDFOperationResult result = writer.write(&buffer, &serializedDocument);
    Q_ASSERT(result);
    buffer.close();

    return buffer.data();
}

PDFAnnotationManipulator::SerializedAnnotations PDFAnnotationManipulator::deserializeAnnotations(const QByteArray& data)
{
    SerializedAnnotations result;

    if (data.isEmpty())
    {
        return result;
    }

    PDFDocumentReader reader(nullptr, nullptr, false, false);
    PDFDocument document = reader.readFromBuffer(data);
    if (reader.getReadingResult() != PDFDocumentReader::Result::OK || document.getCatalog()->getPageCount() == 0)
    {
        return result;
    }

    const PDFPage* page = document.getCatalog()->getPage(0);
    PDFDocumentDataLoaderDecorator loader(&document);
    for (const PDFObjectReference& annotation : page->getAnnotations())
    {
        const PDFDictionary* dictionary = document.getDictionaryFromObject(document.getObjectByReference(annotation));
        if (!dictionary || loader.readNameFromDictionary(dictionary, "Subtype") == "Popup" || dictionary->get("IRT").isReference())
        {
            // Popups are inserted together with their parent annotations,
            // replies together with the annotations, to which they reply
            continue;
        }

        result.boundingRectangle = result.boundingRectangle.united(readRectangle(&document.getStorage(), dictionary, "Rect"));
        result.annotations.push_back(annotation);
    }

    if (result.annotations.empty())
    {
        return result;
    }

    result.document = std::move(document);
    return result;
}

std::vector<PDFObjectReference> PDFAnnotationManipulator::insertAnnotations(PDFDocumentBuilder* builder,
                                                                            PDFObjectReference targetPage,
                                                                            const SerializedAnnotations& annotations,
                                                                            const QPointF& offset)
{
    std::vector<PDFObjectReference> result;

    if (!builder || !targetPage.isValid() || !annotations.isValid())
    {
        return result;
    }

    std::vector<PDFObjectReference> allAnnotations;
    result = importAnnotations(builder, annotations.document.getStorage(), annotations.annotations, targetPage, true, allAnnotations);

    // Replies are moved together with the annotations, to which they reply
    const QTransform translation = QTransform::fromTranslate(offset.x(), offset.y());
    for (const PDFObjectReference& annotation : allAnnotations)
    {
        // Pasted annotations get a new name, so the names stay unique on the page
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("NM");
        factory << createUniqueName();
        factory.endDictionaryItem();
        factory.endDictionary();
        builder->mergeTo(annotation, factory.takeObject());

        if (translation.type() != QTransform::TxNone)
        {
            transformAnnotation(builder, annotation, translation);
        }
    }

    return result;
}

}   // namespace pdf
