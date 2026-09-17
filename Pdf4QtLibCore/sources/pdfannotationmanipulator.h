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

#ifndef PDFANNOTATIONMANIPULATOR_H
#define PDFANNOTATIONMANIPULATOR_H

#include "pdfglobal.h"
#include "pdfobject.h"
#include "pdfannotation.h"
#include "pdfdocument.h"

#include <QRectF>
#include <QPolygonF>
#include <QTransform>
#include <QByteArray>

#include <vector>

namespace pdf
{
class PDFDocumentBuilder;

/// Geometric editing of annotations (moving, scaling, rotating and mirroring)
/// and transfer of annotations between pages and documents (clipboard, drag and
/// drop). All functions work on the object level of the document builder, so
/// they can be used without any GUI and they are the single place, where the
/// rules of the annotation geometry are implemented.
class PDF4QTLIBCORESHARED_EXPORT PDFAnnotationManipulator
{
public:
    PDFAnnotationManipulator() = delete;

    /// Describes, how the geometry of an annotation is stored and which
    /// transformations can be applied to it faithfully.
    enum class GeometryKind
    {
        /// The annotation is not supported by the manipulator (links, widgets, popups, ...)
        NotSupported,

        /// Geometry is a set of points (lines, polygons, ink paths, text markup
        /// quadrilaterals). Any affine transformation is applied to it exactly and
        /// the appearance stream is regenerated from the transformed points.
        Points,

        /// Geometry is the axis aligned rectangle of the annotation (squares,
        /// circles, free text, carets). Scaling, mirroring and rotations by
        /// a multiple of 90 degrees are exact, a general rotation just moves
        /// the annotation, because such shapes cannot be rotated in PDF.
        Box,

        /// The annotation is an icon with a fixed size (sticky notes, file
        /// attachments). Only the position is transformed, the size is preserved.
        Icon,

        /// The annotation is defined solely by its appearance stream (stamps,
        /// watermarks, printer's marks). The appearance stream is preserved and
        /// rotations/mirroring are applied through the matrix of the form.
        Appearance
    };

    /// Returns the geometry kind of the annotation type
    static GeometryKind getGeometryKind(AnnotationType type);

    /// Returns true, if annotations of the given type can be transformed
    static bool isTransformable(AnnotationType type) { return getGeometryKind(type) != GeometryKind::NotSupported; }

    /// Returns true, if the linear part of the matrix maps the coordinate axes onto
    /// the coordinate axes (translations, scaling, mirroring and rotations by
    /// a multiple of 90 degrees).
    static bool isAxisAligned(const QTransform& transform);

    /// Returns true, if the matrix consists of a translation and positive scaling
    /// only (it neither rotates nor mirrors).
    static bool isPositiveAxisAligned(const QTransform& transform);

    /// Applies an affine transformation, given in the page coordinate system, to the
    /// annotation. Which entries are transformed depends on the geometry kind of the
    /// annotation (see \ref GeometryKind). The popup annotation, if present, is moved
    /// together with its parent. The appearance streams are regenerated, when the
    /// shape of the annotation changed and the library can draw the annotation;
    /// a pure translation never touches them, so the appearance created by other
    /// software is preserved. Appearance streams are never modified in place (they
    /// can be shared with other annotations), new stream objects are created instead.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param transform Transformation in page coordinates
    /// \returns true, if the annotation has been modified
    static bool transformAnnotation(PDFDocumentBuilder* builder, PDFObjectReference annotation, const QTransform& transform);

    /// Returns the outline of the annotation rectangle after the transformation is
    /// applied, obeying the rules of the geometry kind of the annotation type. Use it
    /// to preview the result of \ref transformAnnotation.
    /// \param type Annotation type
    /// \param rectangle Annotation rectangle
    /// \param transform Transformation in page coordinates
    static QPolygonF getTransformedOutline(AnnotationType type, const QRectF& rectangle, const QTransform& transform);

    /// Points of an annotation, which can be edited one by one - end points of
    /// a line, vertices of a polygon or of a polyline and points of the callout
    /// line of a free text annotation.
    struct EditablePoints
    {
        std::vector<QPointF> points;
        bool isClosed = false;      ///< Points form a closed shape (polygon)
        bool isCountFixed = true;   ///< Points cannot be inserted or removed
        size_t minimalCount = 0;    ///< Minimal number of the points

        bool isValid() const { return !points.empty(); }
        bool canInsertPoint() const { return isValid() && !isCountFixed; }
        bool canRemovePoint() const { return isValid() && !isCountFixed && points.size() > minimalCount; }
    };

    /// Returns the points of the annotation, which can be edited one by one. If
    /// the annotation has no such points (or they are not stored as plain points,
    /// for example a polygon defined by a curved path), then invalid points are returned.
    /// \param annotation Annotation
    static EditablePoints getEditablePoints(const PDFAnnotation* annotation);

    /// Sets the points returned by \ref getEditablePoints. The number of points
    /// can be changed only if the annotation allows it, and it cannot drop below
    /// the minimal count. The annotation rectangle is updated (for free text
    /// annotations the text rectangle stays where it is) and the appearance
    /// stream is regenerated.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param points New points
    /// \returns true, if the annotation has been modified
    static bool setEditablePoints(PDFDocumentBuilder* builder, PDFObjectReference annotation, const std::vector<QPointF>& points);

    /// Creates a copy of the annotation on a page of the same document. The popup
    /// annotation is copied too, replies are not. The appearance streams are shared
    /// between the original and the copy. Returns the reference of the copy.
    /// \param builder Document builder
    /// \param annotation Annotation to be copied
    /// \param targetPage Page, onto which the copy is placed
    static PDFObjectReference copyAnnotation(PDFDocumentBuilder* builder, PDFObjectReference annotation, PDFObjectReference targetPage);

    /// Moves the annotation (together with its popup) from one page to another
    /// page of the same document. Position of the annotation is not changed.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param sourcePage Page, on which the annotation currently is
    /// \param targetPage Page, onto which the annotation is moved
    /// \returns true, if the annotation has been moved
    static bool moveAnnotationToPage(PDFDocumentBuilder* builder, PDFObjectReference annotation, PDFObjectReference sourcePage, PDFObjectReference targetPage);

    /// Annotations, which were serialized by \ref serializeAnnotations. They are
    /// stored in a small document with a single page. Only the annotations are
    /// listed, their popup annotations are inserted together with them.
    struct SerializedAnnotations
    {
        PDFDocument document;
        std::vector<PDFObjectReference> annotations;
        QRectF boundingRectangle;

        bool isValid() const { return !annotations.empty(); }
    };

    /// Serializes the annotations into a self-contained byte array (a PDF file
    /// with a single page holding copies of the annotations, so the annotations
    /// can be transferred to another document, or to another instance of the
    /// application - for example using the clipboard). Popup annotations are
    /// serialized together with their parents, replies and links to the source
    /// document (page, structure tree, optional content) are dropped.
    /// \param document Document
    /// \param annotations Annotations
    /// \returns Serialized data, or empty array, if nothing could be serialized
    static QByteArray serializeAnnotations(const PDFDocument* document, const std::vector<PDFObjectReference>& annotations);

    /// Parses data created by \ref serializeAnnotations
    /// \param data Data
    static SerializedAnnotations deserializeAnnotations(const QByteArray& data);

    /// Inserts serialized annotations onto the page of the document. Each annotation
    /// gets a new unique name and it is translated by the given offset. Returns the
    /// references of the inserted annotations (without popups).
    /// \param builder Document builder
    /// \param targetPage Page, onto which the annotations are inserted
    /// \param annotations Serialized annotations
    /// \param offset Translation of the inserted annotations
    static std::vector<PDFObjectReference> insertAnnotations(PDFDocumentBuilder* builder,
                                                             PDFObjectReference targetPage,
                                                             const SerializedAnnotations& annotations,
                                                             const QPointF& offset);

    /// Mime type of the serialized annotations
    static const char* getMimeType() { return "application/x-pdf4qt-annotations"; }

private:
    /// Reads a normalized rectangle from the dictionary
    static QRectF readRectangle(const PDFObjectStorage* storage, const PDFDictionary* dictionary, const char* key);

    /// Creates a rectangle object
    static PDFObject createRectangle(const QRectF& rectangle);

    /// Creates an array of numbers
    static PDFObject createNumberArray(const std::vector<PDFReal>& numbers);

    /// Creates an object with a unique annotation name (entry NM)
    static PDFObject createUniqueName();

    /// Returns the bounding rectangle of the points (it can have zero width
    /// or height). At least one point is required.
    static QRectF getPointsBoundingRectangle(const std::vector<QPointF>& points);

    /// Moves the rectangle, so it is centered at the given point
    static QRectF centerRectangle(const QRectF& rectangle, const QPointF& center);

    /// Transforms an array of point coordinates stored under the key. Returns false,
    /// if the entry is not present or it is malformed.
    static bool transformPointArray(PDFDictionary& dictionary, const PDFObjectStorage* storage, const char* key, const QTransform& transform);

    /// Transforms the ink list (array of point coordinate arrays)
    static void transformInkList(PDFDictionary& dictionary, const PDFObjectStorage* storage, const QTransform& transform);

    /// Multiplies the number stored under the key by the factor
    static void scaleNumber(PDFDictionary& dictionary, const PDFObjectStorage* storage, const char* key, PDFReal factor);

    /// Recomputes the rectangle differences (entry RD), so the inner rectangle
    /// is transformed the same way as the annotation rectangle.
    static void transformRectangleDifferences(PDFDictionary& dictionary,
                                              const PDFObjectStorage* storage,
                                              const QRectF& oldRectangle,
                                              const QRectF& newRectangle,
                                              const QTransform& transform);

    /// Applies the linear part of the transformation to the appearance streams
    /// of the annotation. New stream objects are created, the original streams
    /// are left untouched. If the shape of the normal appearance can be determined,
    /// then the new annotation rectangle is computed exactly from it.
    /// \param builder Document builder
    /// \param[in,out] dictionary Annotation dictionary (entry AP is replaced)
    /// \param rectangle Current annotation rectangle
    /// \param transform Transformation
    /// \param[in,out] newRectangle New annotation rectangle
    static void transformAppearanceStreams(PDFDocumentBuilder* builder,
                                           PDFDictionary& dictionary,
                                           const QRectF& rectangle,
                                           const QTransform& transform,
                                           QRectF& newRectangle);

    /// Transforms a single appearance stream. Returns the object to be stored
    /// in the appearance dictionary (the original object, if the stream cannot
    /// be transformed, or a reference to the new stream).
    static PDFObject transformAppearanceStream(PDFDocumentBuilder* builder,
                                               const PDFObject& streamObject,
                                               const PDFObject& originalEntry,
                                               const QRectF& rectangle,
                                               const QTransform& transform,
                                               QRectF* newRectangle);

    /// Moves the popup annotation of the annotation by the offset
    static void translatePopup(PDFDocumentBuilder* builder, const PDFDictionary* annotationDictionary, const QPointF& offset);

    /// Returns a copy of the annotation dictionary prepared for copying - links to
    /// the page, popup, replies and the structure tree are removed.
    /// \param dictionary Annotation dictionary
    /// \param removeOptionalContent Remove also the optional content membership
    static PDFDictionary prepareAnnotationForCopy(const PDFDictionary& dictionary, bool removeOptionalContent);

    /// Returns a copy of the popup dictionary prepared for copying (links to the
    /// page and parent annotation are removed).
    static PDFDictionary preparePopupForCopy(const PDFDictionary& dictionary);

    /// Returns true, if the dictionary is an annotation dictionary, which can be
    /// copied (it has a subtype and it is not a popup annotation)
    static bool isAnnotationDictionary(const PDFObjectStorage* storage, const PDFDictionary* dictionary);

    /// Returns the popup dictionary of the annotation, or nullptr
    static const PDFDictionary* getPopupDictionary(const PDFObjectStorage* storage, const PDFDictionary* annotationDictionary);

    /// Links the annotation with the page and its (optional) popup
    static void linkAnnotation(PDFDocumentBuilder* builder, PDFObjectReference annotation, PDFObjectReference popup, PDFObjectReference page, bool createName);

    /// Appends annotations (at least one) to the annotation array of the page
    static void appendAnnotationsToPage(PDFDocumentBuilder* builder, PDFObjectReference page, const std::vector<PDFObjectReference>& annotations);

    /// Removes annotation from the annotation array of the page. Returns true, if
    /// the annotation was found in the array.
    static bool removeAnnotationFromPage(PDFDocumentBuilder* builder, PDFObjectReference page, PDFObjectReference annotation);

    /// Copies annotations (with their popups) from the storage into the builder.
    /// Returns references of the annotations (without popups) in the builder.
    /// \param builder Target document builder
    /// \param storage Source storage
    /// \param annotations Source annotations
    /// \param targetPage Page in the target document
    /// \param removeOptionalContent Remove optional content membership
    static std::vector<PDFObjectReference> importAnnotations(PDFDocumentBuilder* builder,
                                                             const PDFObjectStorage& storage,
                                                             const std::vector<PDFObjectReference>& annotations,
                                                             PDFObjectReference targetPage,
                                                             bool removeOptionalContent);
};

}   // namespace pdf

#endif // PDFANNOTATIONMANIPULATOR_H
