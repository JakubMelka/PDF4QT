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

#include <QFlags>
#include <QRectF>
#include <QPolygonF>
#include <QTransform>
#include <QByteArray>

#include <limits>
#include <vector>

namespace pdf
{
class PDFMeasure;
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

    /// Operations, which change an annotation in a way the user expects. An user
    /// interface should offer only the operations supported by the annotation
    /// (for example, it makes no sense to offer resize handles for a sticky
    /// note, because its icon has a fixed size). If an annotation is transformed
    /// together with other annotations by a transformation it does not support,
    /// then it is just moved to the transformed position (see \ref transformAnnotation).
    enum Capability
    {
        NoCapability        = 0x0000,
        Move                = 0x0001,   ///< The annotation can be moved
        Resize              = 0x0002,   ///< The annotation can be scaled (also non-uniformly)
        RotateRightAngle    = 0x0004,   ///< The annotation can be rotated by a multiple of 90 degrees
        RotateArbitrary     = 0x0008,   ///< The annotation can be rotated by any angle
        Mirror              = 0x0010,   ///< Mirroring changes the annotation
        EditPoints          = 0x0020    ///< The annotation has points, which can be edited one by one
    };
    Q_DECLARE_FLAGS(Capabilities, Capability)

    /// Returns the geometry kind of the annotation type
    static GeometryKind getGeometryKind(AnnotationType type);

    /// Returns the operations supported by the annotation
    /// \param annotation Annotation
    static Capabilities getCapabilities(const PDFAnnotation* annotation);

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
    /// The measured value displayed by a measurement annotation (dimension line,
    /// measured perimeter, area or angle) is recomputed from the new geometry.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param transform Transformation in page coordinates
    /// \returns true, if the annotation has been modified
    static bool transformAnnotation(PDFDocumentBuilder* builder, PDFObjectReference annotation, const QTransform& transform);

    /// Moves and resizes the annotation, so its rectangle is the given rectangle. The
    /// rectangle is not just overwritten - the annotation is transformed, so its geometry
    /// (points, callout line, ...) follows the rectangle. An annotation, which cannot be
    /// resized, is moved to the center of the rectangle. The rectangle of an annotation is
    /// its geometry with a margin (width of the line, line endings), which is not scaled with
    /// the geometry, so the transformation is repeated, until the rectangle is reached.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param rectangle New rectangle of the annotation in page coordinates
    /// \returns true, if the annotation has been modified
    static bool setRectangle(PDFDocumentBuilder* builder, PDFObjectReference annotation, const QRectF& rectangle);

    /// Returns the transformation, which is really applied to an annotation of the given
    /// type, when the annotation is transformed by \ref transformAnnotation - it is either
    /// the transformation itself, or just a translation to the transformed position, if
    /// the annotation does not support the transformation. Use it to preview the result.
    /// \param type Annotation type
    /// \param rectangle Annotation rectangle
    /// \param transform Transformation in page coordinates
    static QTransform getEffectiveTransform(AnnotationType type, const QRectF& rectangle, const QTransform& transform);

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
        bool isCalloutLine = false; ///< Points are the callout line of a free text annotation
        bool isQuadEnds = false;    ///< Points are the ends of the marked regions (two points for each quadrilateral)
        std::vector<size_t> strokeSizes; ///< Points are the points of the strokes of an ink (count of the points of each stroke)
        size_t minimalCount = 0;    ///< Minimal number of the points
        size_t maximalCount = 0;    ///< Maximal number of the points

        bool isValid() const { return !points.empty(); }

        /// Returns true, if the points are the points of the strokes of an ink
        bool isStrokePoints() const { return !strokeSizes.empty(); }

        /// Returns true, if a point can be inserted (into any segment)
        bool canInsertPoint() const { return isValid() && points.size() < maximalCount; }

        /// Returns true, if the point can be removed. A callout line can lose
        /// only its knee - its end points are the essence of the callout.
        bool canRemovePoint(size_t index) const { return index < points.size() && points.size() > minimalCount && (!isCalloutLine || index == 1); }
    };

    /// Returns the points of the annotation, which can be edited one by one. They
    /// are always the points, from which the annotation is drawn: if a polygon
    /// (polyline) is defined by the entry Path (PDF 2.0), then the points of the
    /// path are returned, the entry Vertices is ignored the same way, as it is
    /// ignored by the renderer. If the annotation has no such points, or the path
    /// contains curves, then invalid points are returned. Number of points of an
    /// angular measurement is fixed, because three points define the angle.
    ///
    /// Text markup annotations (highlight, underline, ...) and redactions mark regions
    /// of the page (usually lines of a text) by quadrilaterals. Each region has two
    /// points - the middle of its start and the middle of its end - so a single marked
    /// line can be made longer or shorter. The points move only along the region.
    ///
    /// An ink has the points of its strokes, if it is defined by the ink list (not by
    /// a path), and if it has a reasonable count of points (a stroke drawn by hand has
    /// hundreds of points, which cannot be edited one by one). The count of the points
    /// of the strokes cannot be changed.
    /// \param annotation Annotation
    static EditablePoints getEditablePoints(const PDFAnnotation* annotation);

    /// Sets the points returned by \ref getEditablePoints. The number of points
    /// can be changed only if the annotation allows it (see the minimal and the
    /// maximal count). The annotation rectangle is updated (for free text
    /// annotations the text rectangle stays where it is), the measured value
    /// of a measurement annotation is recomputed and the appearance stream is
    /// regenerated.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param points New points
    /// \returns true, if the annotation has been modified
    static bool setEditablePoints(PDFDocumentBuilder* builder, PDFObjectReference annotation, const std::vector<QPointF>& points);

    /// Parts of an annotation, which can be removed one by one - regions marked by
    /// a text markup annotation or by a redaction, strokes of an ink annotation.
    struct Parts
    {
        std::vector<QPolygonF> shapes;  ///< Shapes of the parts (page coordinates)
        bool isFilled = false;          ///< Parts are areas (otherwise they are lines)
        bool isSupported = false;       ///< The annotation consists of parts (even if it has no part now)

        /// Returns true, if a part can be removed (the last part cannot be removed,
        /// the whole annotation should be deleted instead)
        bool canRemovePart() const { return shapes.size() > 1; }
    };

    /// Adds a reply to the annotation. The reply is a text annotation, which is not displayed
    /// on the page - it is a part of the comment thread displayed in the popup window of the
    /// annotation. A reply to a reply is allowed.
    /// \param builder Document builder
    /// \param annotation Markup annotation, to which the reply is added
    /// \param author Author of the reply
    /// \param contents Text of the reply
    /// \returns Reference of the reply, or invalid reference, if the reply cannot be added
    static PDFObjectReference addReply(PDFDocumentBuilder* builder, PDFObjectReference annotation, const QString& author, const QString& contents);

    /// Replaces the file, which is attached by a file attachment annotation. The file
    /// is embedded into the document. The old embedded file is not referenced any more.
    /// \param builder Document builder
    /// \param annotation File attachment annotation
    /// \param fileName Name of the file (without a path)
    /// \param data Content of the file
    /// \returns true, if the annotation has been modified
    static bool setFileAttachment(PDFDocumentBuilder* builder, PDFObjectReference annotation, const QString& fileName, const QByteArray& data);

    /// Returns the parts of the annotation
    /// \param storage Object storage
    /// \param annotation Annotation
    static Parts getParts(const PDFObjectStorage* storage, PDFObjectReference annotation);

    /// Replaces the parts of the annotation - the marked areas (each of them has four corners,
    /// which go around the area, starting with the top left and the top right one), or the strokes
    /// of an ink (each of them has at least two points). The annotation must have a part.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param shapes Shapes of the parts (page coordinates)
    /// \returns true, if the annotation has been modified
    static bool setParts(PDFDocumentBuilder* builder, PDFObjectReference annotation, const std::vector<QPolygonF>& shapes);

    /// Adds a part to the annotation - a marked area to a text markup (redaction), or a stroke to an ink
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param shape Shape of the part (page coordinates, see \ref setParts)
    /// \returns true, if the annotation has been modified
    static bool addPart(PDFDocumentBuilder* builder, PDFObjectReference annotation, const QPolygonF& shape);

    /// Erases the parts of the strokes of an ink, which are in the circle. The strokes are
    /// cut at the border of the circle, so a stroke can be split into several strokes. If
    /// nothing would remain, then the ink is not modified (it should be deleted instead).
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param center Center of the erased circle (page coordinates)
    /// \param radius Radius of the erased circle
    /// \returns true, if the annotation has been modified
    static bool eraseInk(PDFDocumentBuilder* builder, PDFObjectReference annotation, const QPointF& center, PDFReal radius);

    /// Removes a part of the annotation (see \ref getParts)
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param index Index of the part
    /// \returns true, if the annotation has been modified
    static bool removePart(PDFDocumentBuilder* builder, PDFObjectReference annotation, size_t index);

    /// Returns the text box of a free text annotation. If the annotation has
    /// a callout line, then its rectangle is larger, than the text box.
    /// \param storage Object storage
    /// \param annotation Annotation
    static QRectF getFreeTextRectangle(const PDFObjectStorage* storage, PDFObjectReference annotation);

    /// Sets the text box of a free text annotation. The tip of the callout line stays,
    /// where it is (it points to a place on the page), the rest of the callout line
    /// follows the text box, so it stays connected to it.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param textRectangle New text box
    /// \returns true, if the annotation has been modified
    static bool setFreeTextRectangle(PDFDocumentBuilder* builder, PDFObjectReference annotation, const QRectF& textRectangle);

    /// Adds a callout line to a free text annotation (or replaces the existing one), or
    /// removes it, if no points are given. The text box is not changed.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param calloutLine Callout line (two or three points, the first one is the tip), or empty array
    /// \returns true, if the annotation has been modified
    static bool setFreeTextCalloutLine(PDFDocumentBuilder* builder, PDFObjectReference annotation, const std::vector<QPointF>& calloutLine);

    /// Returns the page, on which the annotation is. The entry P of the annotation
    /// is optional (and it can be wrong), so it is used only as a hint - if the page
    /// does not list the annotation, then the page tree is searched. Returns invalid
    /// reference, if the annotation is on no page.
    /// \param storage Object storage
    /// \param annotation Annotation
    static PDFObjectReference findAnnotationPage(const PDFObjectStorage* storage, PDFObjectReference annotation);

    /// Returns the replies to the annotation (annotations of the page, whose entry
    /// IRT refers to the annotation), including the replies to the replies. Each
    /// reply is listed after the annotation it replies to. The annotation with
    /// its replies and their popup windows forms a single comment thread.
    /// \param storage Object storage
    /// \param page Page, on which the annotation is
    /// \param annotation Annotation
    static std::vector<PDFObjectReference> getReplies(const PDFObjectStorage* storage, PDFObjectReference page, PDFObjectReference annotation);

    /// Creates a copy of the annotation on a page of the same document. The whole
    /// comment thread is copied - the popup annotation, the replies to the annotation
    /// and their popups. The appearance streams are shared between the original and
    /// the copy. Returns the reference of the copy.
    /// \param builder Document builder
    /// \param annotation Annotation to be copied
    /// \param targetPage Page, onto which the copy is placed
    static PDFObjectReference copyAnnotation(PDFDocumentBuilder* builder, PDFObjectReference annotation, PDFObjectReference targetPage);

    /// Moves the annotation (together with its popup, with the replies to it and
    /// with their popups) from one page to another page of the same document.
    /// Position of the annotation is not changed.
    /// \param builder Document builder
    /// \param annotation Annotation
    /// \param sourcePage Page, on which the annotation currently is
    /// \param targetPage Page, onto which the annotation is moved
    /// \returns true, if the annotation has been moved
    static bool moveAnnotationToPage(PDFDocumentBuilder* builder, PDFObjectReference annotation, PDFObjectReference sourcePage, PDFObjectReference targetPage);

    /// Annotations, which were serialized by \ref serializeAnnotations. They are
    /// stored in a small document with a single page. Only the annotations are
    /// listed, their popup annotations and the replies to them are inserted
    /// together with them.
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
    /// application - for example using the clipboard). The whole comment threads
    /// are serialized (popup annotations, replies and their popups), so cutting and
    /// pasting an annotation does not lose the discussion attached to it. Links to
    /// the source document (page, structure tree, optional content) are dropped.
    /// \param document Document
    /// \param annotations Annotations
    /// \returns Serialized data, or empty array, if nothing could be serialized
    static QByteArray serializeAnnotations(const PDFDocument* document, const std::vector<PDFObjectReference>& annotations);

    /// Parses data created by \ref serializeAnnotations
    /// \param data Data
    static SerializedAnnotations deserializeAnnotations(const QByteArray& data);

    /// Inserts serialized annotations onto the page of the document. Each annotation
    /// gets a new unique name and it is translated by the given offset. Returns the
    /// references of the inserted annotations (without popups and replies).
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

    /// Transforms an array of arrays of point coordinates stored under the key
    /// (ink list, or path with its control points)
    static void transformPointArrays(PDFDictionary& dictionary, const PDFObjectStorage* storage, const char* key, const QTransform& transform);

    /// Reverses the order of the points stored under the key
    static void reversePointArray(PDFDictionary& dictionary, const PDFObjectStorage* storage, const char* key);

    /// Multiplies the number stored under the key by the factor
    static void scaleNumber(PDFDictionary& dictionary, const PDFObjectStorage* storage, const char* key, PDFReal factor);

    /// Transforms the entries of a line annotation, which are relative to the line
    /// (leader lines, caption offset). Lengths of the leader lines are oriented
    /// (the sign selects the side of the line), so mirroring changes the sign. If
    /// mirroring makes the caption of the line unreadable (upside down), then the
    /// end points of the line (entry L must be already transformed) are swapped.
    static void transformLineParameters(PDFDictionary& dictionary, const PDFObjectStorage* storage, const QLineF& line, const QTransform& transform);

    /// Returns true, if the transformation is applied to the rectangle based
    /// annotation exactly (otherwise the annotation is just moved)
    static bool isBoxTransformedExactly(AnnotationType type, const QTransform& transform);

    /// Returns the text rectangle of a free text annotation (annotation
    /// rectangle reduced by the rectangle differences)
    static QRectF getFreeTextRectangle(const PDFObjectStorage* storage, const PDFDictionary* dictionary, const QRectF& rectangle);

    /// Sets the geometry of a free text annotation with a callout line. Returns
    /// the new annotation rectangle (the dictionary is updated except the entry Rect).
    static QRectF setFreeTextGeometry(PDFDictionary& dictionary, const QRectF& textRectangle, const std::vector<QPointF>& calloutLine, PDFReal margin);

    /// Moves the rectangle based annotation to the transformed position. The tip
    /// of the callout line of a free text annotation is transformed exactly, the
    /// text box with the rest of the callout line follows it. Returns the new
    /// annotation rectangle.
    static QRectF moveBox(PDFDictionary& dictionary,
                          const PDFObjectStorage* storage,
                          const PDFAnnotation* annotation,
                          const QRectF& rectangle,
                          const QTransform& transform,
                          bool* regenerateAppearance);

    /// Returns the quadrilaterals marked by the annotation (text markup, redaction), or nullptr
    static const PDFAnnotationQuadrilaterals* getQuadrilaterals(const PDFAnnotation* annotation);

    /// Moves the ends of the quadrilaterals to the points (see \ref getEditablePoints). Returns
    /// the coordinates of the new quadrilaterals, or empty array, if some quadrilateral would
    /// be turned inside out.
    static std::vector<PDFReal> moveQuadrilateralEnds(const PDFAnnotationQuadrilaterals& quadrilaterals, const std::vector<QPointF>& points);

    /// Returns the numbers as points
    static std::vector<QPointF> getPointsFromNumbers(const std::vector<PDFReal>& numbers);

    /// Returns true, if the annotation is an angular measurement (dimension
    /// polyline with three points, the second point is the vertex of the angle)
    static bool isAngularMeasurement(const PDFAnnotation* annotation);

    /// Returns the points of the displayed shape of a polygon (polyline). The path
    /// has precedence over the vertices, as it has, when the annotation is drawn.
    /// Returns empty array, if the path contains curves.
    static std::vector<QPointF> getPolygonalPoints(const PDFPolygonalGeometryAnnotation* annotation);

    /// Reverses the order of the items of an array (of the points of a path without curves)
    static void reverseArray(PDFDictionary& dictionary, const PDFObjectStorage* storage, const char* key);

    /// Returns the points of the path. Returns empty array, if the path contains
    /// curves. The point, which closes the path, is not returned.
    static std::vector<QPointF> getPathPoints(const QPainterPath& path);

    /// Number found in a text
    struct NumberToken
    {
        qsizetype position = 0;     ///< Position of the number in the text
        qsizetype length = 0;       ///< Length of the number in the text
        PDFReal value = 0.0;        ///< Value (the last dot or comma is the decimal separator)
        int decimals = 0;           ///< Number of digits after the decimal separator
        QChar decimalSeparator;     ///< Decimal separator
        QChar groupSeparator;       ///< Separator of the digit groups (null character, if it is not used)
    };

    /// Finds the numbers in the text
    static std::vector<NumberToken> parseNumberTokens(const QString& text);

    /// Formats the value the same way, as the number token is formatted
    static QString formatNumberToken(const NumberToken& token, PDFReal value);

    /// Quantity, which must be distinguished from another quantity measured by the same annotation
    enum class MeasuredQuantity
    {
        Unknown,
        Length,
        Area
    };

    /// Value measured by a measurement annotation
    struct MeasuredValue
    {
        PDFReal value = 0.0;                                    ///< Value in the displayed units
        MeasuredQuantity quantity = MeasuredQuantity::Unknown;  ///< Quantity, if the annotation measures several quantities (perimeter and area of a polygon)
        QString unit;                                           ///< Label of the unit defined by the measure (can be empty)
        QString text;                                           ///< Value formatted by the number format of the measure (can be empty)
    };

    /// Returns the quantities measured by a measurement annotation - length of
    /// a dimension line or of a polyline, angle of an angular dimension, perimeter
    /// and area of a polygon. Returns empty array, if the annotation is not
    /// a measurement. The values are in the units of the measure, or in the units
    /// of the default user space, if the measure is not valid. The displayed
    /// shape is measured (the path has precedence over the vertices).
    static std::vector<MeasuredValue> getMeasuredValues(const PDFAnnotation* annotation, const PDFMeasure& measure);

    /// Returns the quantity, which the number in the text displays according to the text
    /// around the number - symbol of the quantity before it ("A = "), unit of area after it.
    /// \param before Text before the number
    /// \param after Text after the number (without leading spaces)
    static MeasuredQuantity getQuantityCue(const QString& before, const QString& after);

    /// Returns, how much the number in the text looks like the measured value (the greater
    /// score, the better; zero means, that nothing speaks for it, nor against it). Returns
    /// a negative number, if the number cannot be the measured value.
    static int getMeasurementFieldScore(const QString& text, const NumberToken& token, const MeasuredValue& value);

    /// Returns the text of a measurement annotation with the measured values updated,
    /// or the unchanged text, if no measured value was recognized in it.
    static QString updateMeasurementText(const QString& text, bool isMeasureValid, const std::vector<MeasuredValue>& oldValues, const std::vector<MeasuredValue>& newValues);

    /// Updates the measured value displayed by the measurement annotation after its
    /// geometry has been changed. Returns true, if the annotation has been modified.
    static bool updateMeasurement(PDFDocumentBuilder* builder, PDFObjectReference annotation, const PDFAnnotation* oldAnnotation);

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
    /// \param isReply The annotation is copied as a reply (its reply type is preserved)
    static PDFDictionary prepareAnnotationForCopy(const PDFDictionary& dictionary, bool removeOptionalContent, bool isReply);

    /// Returns true, if the page lists the annotation in its annotation array
    static bool isAnnotationOnPage(const PDFObjectStorage* storage, PDFObjectReference page, PDFObjectReference annotation);

    /// Returns a copy of the popup dictionary prepared for copying (links to the
    /// page and parent annotation are removed).
    static PDFDictionary preparePopupForCopy(const PDFDictionary& dictionary);

    /// Returns true, if the dictionary is an annotation dictionary, which can be
    /// copied (it has a subtype and it is not a popup annotation)
    static bool isAnnotationDictionary(const PDFObjectStorage* storage, const PDFDictionary* dictionary);

    /// Returns the popup dictionary of the annotation, or nullptr
    static const PDFDictionary* getPopupDictionary(const PDFObjectStorage* storage, const PDFDictionary* annotationDictionary);

    /// Links the annotation with the page, with its (optional) popup and
    /// with the (optional) annotation it replies to
    static void linkAnnotation(PDFDocumentBuilder* builder,
                               PDFObjectReference annotation,
                               PDFObjectReference popup,
                               PDFObjectReference page,
                               bool createName,
                               PDFObjectReference inReplyTo);

    /// Appends annotations (at least one) to the annotation array of the page
    static void appendAnnotationsToPage(PDFDocumentBuilder* builder, PDFObjectReference page, const std::vector<PDFObjectReference>& annotations);

    /// Removes annotation from the annotation array of the page. Returns true, if
    /// the annotation was found in the array.
    static bool removeAnnotationFromPage(PDFDocumentBuilder* builder, PDFObjectReference page, PDFObjectReference annotation);

    /// Copies annotations (with their popups, replies and popups of the replies)
    /// from the storage into the builder. Returns references of the annotations
    /// (without popups and replies) in the builder.
    /// \param builder Target document builder
    /// \param storage Source storage
    /// \param annotations Source annotations
    /// \param targetPage Page in the target document
    /// \param removeOptionalContent Remove optional content membership
    /// \param[out] allAnnotations Copied annotations including the replies (without popups)
    static std::vector<PDFObjectReference> importAnnotations(PDFDocumentBuilder* builder,
                                                             const PDFObjectStorage& storage,
                                                             const std::vector<PDFObjectReference>& annotations,
                                                             PDFObjectReference targetPage,
                                                             bool removeOptionalContent,
                                                             std::vector<PDFObjectReference>& allAnnotations);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PDFAnnotationManipulator::Capabilities)

}   // namespace pdf

#endif // PDFANNOTATIONMANIPULATOR_H
