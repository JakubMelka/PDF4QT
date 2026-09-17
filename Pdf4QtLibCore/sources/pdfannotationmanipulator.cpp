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

#include <QUuid>
#include <QBuffer>

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

void PDFAnnotationManipulator::transformInkList(PDFDictionary& dictionary,
                                                const PDFObjectStorage* storage,
                                                const QTransform& transform)
{
    if (!dictionary.hasKey("InkList"))
    {
        return;
    }

    const PDFObject inkList = storage->getObject(dictionary.get("InkList"));
    if (!inkList.isArray())
    {
        return;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    PDFObjectFactory factory;
    factory.beginArray();

    const PDFArray* inkListArray = inkList.getArray();
    for (size_t i = 0, count = inkListArray->getCount(); i < count; ++i)
    {
        std::vector<PDFReal> numbers = loader.readNumberArray(inkListArray->getItem(i));
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
    dictionary.setEntry(PDFInplaceOrMemoryString("InkList"), factory.takeObject());
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
    const bool axisAligned = isAxisAligned(transform);

    PDFDictionary modifiedDictionary = *dictionary;
    QRectF newRectangle = rectangle;
    bool regenerateAppearance = false;

    if (kind == GeometryKind::Points)
    {
        transformPointArray(modifiedDictionary, storage, "QuadPoints", transform);
        transformPointArray(modifiedDictionary, storage, "Vertices", transform);
        transformPointArray(modifiedDictionary, storage, "L", transform);
        transformInkList(modifiedDictionary, storage, transform);

        if (!isTranslation)
        {
            // Lengths of the leader lines are scaled by the mean scale factor
            const PDFReal factor = std::sqrt(std::abs(transform.determinant()));
            scaleNumber(modifiedDictionary, storage, "LL", factor);
            scaleNumber(modifiedDictionary, storage, "LLE", factor);
            scaleNumber(modifiedDictionary, storage, "LLO", factor);
        }

        newRectangle = transform.mapRect(rectangle);
        regenerateAppearance = !isTranslation;
    }
    else if (kind == GeometryKind::Box)
    {
        const bool hasCalloutLine = transformPointArray(modifiedDictionary, storage, "CL", transform);

        if (axisAligned)
        {
            newRectangle = transform.mapRect(rectangle);
            transformRectangleDifferences(modifiedDictionary, storage, rectangle, newRectangle, transform);
            regenerateAppearance = !isTranslation;
        }
        else
        {
            // Rectangle based shapes cannot be rotated, so the annotation
            // is just moved to the transformed position.
            newRectangle = centerRectangle(rectangle, transform.map(rectangle.center()));
            regenerateAppearance = hasCalloutLine;
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

    if (regenerateAppearance)
    {
        builder->updateAnnotationAppearanceStreams(annotation);
    }

    return true;
}

QPolygonF PDFAnnotationManipulator::getTransformedOutline(AnnotationType type, const QRectF& rectangle, const QTransform& transform)
{
    const QRectF normalizedRectangle = rectangle.normalized();
    const QPolygonF outline(normalizedRectangle);

    switch (getGeometryKind(type))
    {
        case GeometryKind::Points:
        case GeometryKind::Appearance:
            return transform.map(outline);

        case GeometryKind::Box:
            if (isAxisAligned(transform))
            {
                return transform.map(outline);
            }
            return QPolygonF(centerRectangle(normalizedRectangle, transform.map(normalizedRectangle.center())));

        case GeometryKind::Icon:
            return QPolygonF(centerRectangle(normalizedRectangle, transform.map(normalizedRectangle.center())));

        default:
            break;
    }

    return outline;
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
        }
    }
    else if (const PDFPolygonalGeometryAnnotation* polygonalAnnotation = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(annotation))
    {
        // Jakub Melka: if the vertices are missing, then the shape is defined
        // by a path with curves (PDF 2.0), which cannot be edited point by point
        const bool isPolygon = annotation->getType() == AnnotationType::Polygon;
        const size_t minimalCount = isPolygon ? 3 : 2;
        if (polygonalAnnotation->getVertices().size() >= minimalCount)
        {
            result.points = polygonalAnnotation->getVertices();
            result.isClosed = isPolygon;
            result.isCountFixed = false;
            result.minimalCount = minimalCount;
        }
    }
    else if (const PDFFreeTextAnnotation* freeTextAnnotation = dynamic_cast<const PDFFreeTextAnnotation*>(annotation))
    {
        const PDFAnnotationCalloutLine& calloutLine = freeTextAnnotation->getCalloutLine();
        switch (calloutLine.getType())
        {
            case PDFAnnotationCalloutLine::Type::StartEnd:
                result.points = { calloutLine.getPoint(0), calloutLine.getPoint(1) };
                result.minimalCount = 2;
                break;

            case PDFAnnotationCalloutLine::Type::StartKneeEnd:
                result.points = { calloutLine.getPoint(0), calloutLine.getPoint(1), calloutLine.getPoint(2) };
                result.minimalCount = 3;
                break;

            default:
                break;
        }
    }

    return result;
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

    const bool isCountValid = currentPoints.isCountFixed ? (points.size() == currentPoints.points.size())
                                                         : (points.size() >= currentPoints.minimalCount);
    if (!isCountValid)
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
    const QRectF newPointsBounds = getPointsBoundingRectangle(points);
    PDFDictionary modifiedDictionary = *dictionary;
    QRectF newRectangle;

    if (parsedAnnotation->getType() == AnnotationType::FreeText)
    {
        modifiedDictionary.setEntry(PDFInplaceOrMemoryString("CL"), createNumberArray(numbers));

        // Jakub Melka: the annotation rectangle covers the text box and the callout
        // line, the text box is defined by the rectangle differences. The text box
        // must not move, when the callout line is changed.
        PDFDocumentDataLoaderDecorator loader(storage);
        QRectF textRectangle = rectangle;
        const std::vector<PDFReal> differences = loader.readNumberArrayFromDictionary(dictionary, "RD");
        if (differences.size() == 4)
        {
            const QRectF innerRectangle = rectangle.adjusted(differences[0], differences[1], -differences[2], -differences[3]);
            if (innerRectangle.isValid())
            {
                textRectangle = innerRectangle;
            }
        }

        // Space for the line ending
        const PDFReal margin = std::max(1.0, parsedAnnotation->getBorder().getWidth()) * 5.0;
        newRectangle = textRectangle.united(newPointsBounds.adjusted(-margin, -margin, margin, margin));
        modifiedDictionary.setEntry(PDFInplaceOrMemoryString("RD"), createNumberArray({ textRectangle.left() - newRectangle.left(),
                                                                                         textRectangle.top() - newRectangle.top(),
                                                                                         newRectangle.right() - textRectangle.right(),
                                                                                         newRectangle.bottom() - textRectangle.bottom() }));
    }
    else
    {
        const char* key = parsedAnnotation->getType() == AnnotationType::Line ? "L" : "Vertices";
        modifiedDictionary.setEntry(PDFInplaceOrMemoryString(key), createNumberArray(numbers));

        // Keep the margin between the points and the rectangle (line width, line
        // endings). Regenerated appearance stream sets the exact rectangle later.
        const QRectF oldPointsBounds = getPointsBoundingRectangle(currentPoints.points);
        const PDFReal margin = std::max({ 1.0,
                                          oldPointsBounds.left() - rectangle.left(),
                                          oldPointsBounds.top() - rectangle.top(),
                                          rectangle.right() - oldPointsBounds.right(),
                                          rectangle.bottom() - oldPointsBounds.bottom() });
        newRectangle = newPointsBounds.adjusted(-margin, -margin, margin, margin);
    }

    modifiedDictionary.setEntry(PDFInplaceOrMemoryString("Rect"), createRectangle(newRectangle));
    builder->setObject(annotation, PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(modifiedDictionary))));
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

PDFDictionary PDFAnnotationManipulator::prepareAnnotationForCopy(const PDFDictionary& dictionary, bool removeOptionalContent)
{
    PDFDictionary result = dictionary;
    result.removeEntry("P");
    result.removeEntry("Popup");
    result.removeEntry("IRT");
    result.removeEntry("RT");
    result.removeEntry("StructParent");

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
                                              bool createName)
{
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("P");
    factory << page;
    factory.endDictionaryItem();
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

    // Jakub Melka: the copy is shallow - appearance streams (which can be large,
    // for example image stamps) are shared with the original annotation. The
    // manipulator never modifies the streams in place, so sharing is safe.
    PDFDictionary popupDictionary;
    const bool hasPopup = getPopupDictionary(storage, dictionary) != nullptr;
    if (hasPopup)
    {
        popupDictionary = preparePopupForCopy(*getPopupDictionary(storage, dictionary));
    }

    const PDFObjectReference copiedAnnotation = builder->addObject(PDFObject::createDictionary(std::make_shared<PDFDictionary>(prepareAnnotationForCopy(*dictionary, false))));
    PDFObjectReference copiedPopup;
    if (hasPopup)
    {
        copiedPopup = builder->addObject(PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(popupDictionary))));
    }

    linkAnnotation(builder, copiedAnnotation, copiedPopup, targetPage, dictionary->hasKey("NM"));

    std::vector<PDFObjectReference> pageAnnotations = { copiedAnnotation };
    if (copiedPopup.isValid())
    {
        pageAnnotations.push_back(copiedPopup);
    }
    appendAnnotationsToPage(builder, targetPage, pageAnnotations);

    return copiedAnnotation;
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

    PDFObjectReference popup;
    if (getPopupDictionary(storage, dictionary))
    {
        popup = dictionary->get("Popup").getReference();
    }

    removeAnnotationFromPage(builder, sourcePage, annotation);
    std::vector<PDFObjectReference> movedAnnotations = { annotation };

    if (popup.isValid())
    {
        removeAnnotationFromPage(builder, sourcePage, popup);
        movedAnnotations.push_back(popup);
    }

    linkAnnotation(builder, annotation, popup, targetPage, false);
    appendAnnotationsToPage(builder, targetPage, movedAnnotations);
    return true;
}

std::vector<PDFObjectReference> PDFAnnotationManipulator::importAnnotations(PDFDocumentBuilder* builder,
                                                                            const PDFObjectStorage& storage,
                                                                            const std::vector<PDFObjectReference>& annotations,
                                                                            PDFObjectReference targetPage,
                                                                            bool removeOptionalContent)
{
    std::vector<PDFObjectReference> result;

    // 1) Prepare the dictionaries, which are copied. References to the source page,
    //    to the popup and to the replies are removed, otherwise the deep copy
    //    would drag the whole source document along.
    std::vector<PDFObject> objects;
    std::vector<std::pair<size_t, size_t>> annotationAndPopupIndices;
    for (const PDFObjectReference& annotation : annotations)
    {
        const PDFDictionary* dictionary = storage.getDictionaryFromObject(storage.getObjectByReference(annotation));
        if (!dictionary || !isAnnotationDictionary(&storage, dictionary))
        {
            continue;
        }

        const size_t annotationIndex = objects.size();
        objects.emplace_back(PDFObject::createDictionary(std::make_shared<PDFDictionary>(prepareAnnotationForCopy(*dictionary, removeOptionalContent))));

        size_t popupIndex = std::numeric_limits<size_t>::max();
        if (const PDFDictionary* popupDictionary = getPopupDictionary(&storage, dictionary))
        {
            popupIndex = objects.size();
            objects.emplace_back(PDFObject::createDictionary(std::make_shared<PDFDictionary>(preparePopupForCopy(*popupDictionary))));
        }

        annotationAndPopupIndices.emplace_back(annotationIndex, popupIndex);
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
    for (const auto& [annotationIndex, popupIndex] : annotationAndPopupIndices)
    {
        const PDFObjectReference copiedAnnotation = copiedObjects[annotationIndex].getReference();
        PDFObjectReference copiedPopup;
        if (popupIndex != std::numeric_limits<size_t>::max())
        {
            copiedPopup = copiedObjects[popupIndex].getReference();
        }

        linkAnnotation(builder, copiedAnnotation, copiedPopup, targetPage, false);

        pageAnnotations.push_back(copiedAnnotation);
        if (copiedPopup.isValid())
        {
            pageAnnotations.push_back(copiedPopup);
        }
        result.push_back(copiedAnnotation);
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
    const std::vector<PDFObjectReference> importedAnnotations = importAnnotations(&builder, document->getStorage(), annotations, page, true);
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
        if (!dictionary || loader.readNameFromDictionary(dictionary, "Subtype") == "Popup")
        {
            // Popups are inserted together with their parent annotations
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

    result = importAnnotations(builder, annotations.document.getStorage(), annotations.annotations, targetPage, true);

    const QTransform translation = QTransform::fromTranslate(offset.x(), offset.y());
    for (const PDFObjectReference& annotation : result)
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
