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

#ifndef PDFMEASUREWRITER_H
#define PDFMEASUREWRITER_H

#include "pdfglobal.h"
#include "pdfmeasure.h"
#include "pdfobject.h"

#include <QColor>
#include <QCoreApplication>
#include <QPointF>
#include <QString>

#include <optional>
#include <vector>

namespace pdf
{

class PDFDocument;
class PDFDocumentBuilder;
class PDFOperationControl;

/// What a measurement measures and how its annotation is encoded (plan of step 7 of phase 2, 4.8, 5.1; owner
/// answer Q7.5 (b) and (b) with R7-11). The drawing modes of the GUI (horizontal, vertical, rectangle …) give
/// the points of one of these kinds.
enum class PDFMeasureKind
{
    Distance,   ///< /Line with /IT /LineDimension (Table 178), 2 points; /Measure /D
    Perimeter,  ///< A closed /PolyLine with /IT /PolyLineDimension (Table 181; the last point repeats the first), at least 3 points; /Measure /D
    Area,       ///< /Polygon with /IT /PolygonDimension (Table 181), at least 3 points; /Measure /A
    Angle       ///< /PolyLine without /IT, /Subj "Angle", 3 points (the vertex in the middle); /Measure /T (the convention of the readers, NOTES.txt)
};

/// The computations of the measurements (plan of step 7 of phase 2, 4.8; the computations of the old
/// DimensionsPlugin, dimensiontool.cpp:219-272, moved to the core): the points of a kind, the value in points
/// (lengths × the effective /UserUnit, B209), its conversion by a scale and a unit, its text. Reentrant.
class PDF4QTLIBCORESHARED_EXPORT PDFMeasureGeometry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFMeasureGeometry)

public:
    PDFMeasureGeometry() = delete;

    /// Places of the shown values ("11.20 m", the frames 471:5975)
    static constexpr int VALUE_DECIMALS = 2;

    /// /Subj of the annotations of the kinds (English: a value of the document, as Acrobat writes "Distance";
    /// the old plugin wrote these words, so its annotations are read as theirs)
    static constexpr const char* SUBJECT_DISTANCE = "Length";
    static constexpr const char* SUBJECT_PERIMETER = "Perimeter";
    static constexpr const char* SUBJECT_AREA = "Area";
    static constexpr const char* SUBJECT_ANGLE = "Angle";

    /// The quantity of a kind: Distance and Perimeter a length, Area an area, Angle an angle
    static PDFMeasureQuantity getQuantity(PDFMeasureKind kind);

    /// The points of a measurement in the form of its kind, std::nullopt when they cannot be (a point that is
    /// not finite, a wrong count): Distance 2 points; Perimeter at least 3 different points, closed (the first
    /// point is repeated at the end - added when missing); Area at least 3 different points, not closed (a
    /// repeated first point at the end is removed); Angle 3 points (the vertex in the middle), the arms of a
    /// non-zero length. At most \p maximumPointCount points (before the closing one).
    static std::optional<std::vector<QPointF>> normalizePoints(PDFMeasureKind kind, const std::vector<QPointF>& points, int maximumPointCount);

    /// The points are complete for the kind (the drawing of the old plugin): Distance 2, Angle 3, Perimeter and
    /// Area at least 3 with the last point equal to the first (the shape is closed by a click on the first point)
    static bool isComplete(PDFMeasureKind kind, const std::vector<QPointF>& points);

    /// Length of the polyline of the points (the sum of its segments)
    static PDFReal getPolylineLength(const std::vector<QPointF>& points);

    /// Area of the polygon of the points (the shoelace formula, closed implicitly; the absolute value)
    static PDFReal getPolygonArea(const std::vector<QPointF>& points);

    /// The angle at \p vertex between the arms to \p first and \p second in degrees: always the smaller one,
    /// 0 to 180 (B181: the order of the arms does not matter); 0 for an arm of zero length
    static PDFReal getAngle(const QPointF& first, const QPointF& vertex, const QPointF& second);

    /// The value of the normalized points (normalizePoints) in points of the paper: a length (Distance,
    /// Perimeter) × \p userUnit, an area × \p userUnit², an angle in degrees (no unit of the page)
    /// \param kind Kind of the measurement
    /// \param points The normalized points in the default user space of the page
    /// \param userUnit The effective /UserUnit of the page (PDFPage::getEffectiveUserUnit, B209)
    static PDFReal getValue(PDFMeasureKind kind, const std::vector<QPointF>& points, PDFReal userUnit);

    /// The value of getValue in \p unit of its quantity by \p scale: a length × the factor of the scale, an
    /// area × its square, an angle without the scale; × the factor of the unit
    static PDFReal convertValue(PDFMeasureKind kind, PDFReal value, const PDFMeasureScale& scale, const PDFMeasureUnit& unit);

    /// The number of a shown value in the C locale with VALUE_DECIMALS places ("11.20"; the application formats
    /// by PDFAppLocale); empty for a value that is not finite
    static QString formatValue(PDFReal value);

    /// The text of a measurement of a formatted number: "11.20 m", "p = 12.56 m", "A = 31.95 sq. m", "90.00 °"
    /// (471:5975); the symbol of the language of the user interface
    static QString getValueText(PDFMeasureKind kind, const QString& numberText, const QString& unitSymbol);

    /// The title of a kind in the language of the user interface ("Length", "Perimeter", "Area", "Angle" -
    /// the rows of the list 471:5975 and the column Type of the CSV)
    static QString getKindTitle(PDFMeasureKind kind);

    /// /Subj of the annotation of a kind (SUBJECT_*)
    static QString getSubject(PDFMeasureKind kind);

    /// The viewport of a page, whose measure gives the scale of a point (ISO 32000-2, 12.9.2: the last
    /// viewport of /VP containing the point; a viewport without a usable rectilinear measure - none, /GEO
    /// (FM-7), an anisotropic one, an unknown unit - is skipped), nullptr = none
    /// \param viewports Viewports of the page (PDFViewport::parseViewports)
    /// \param point The point in the default user space of the page (the first point of a measurement)
    /// \param userUnit The effective /UserUnit of the page
    /// \param scale The scale of the viewport (optional)
    static const PDFViewport* findScaleViewport(const std::vector<PDFViewport>& viewports, const QPointF& point, PDFReal userUnit, PDFMeasureScale* scale);
};

/// A measurement annotation of a document (plan 4.8; FM-4: also of another producer): a /Line with
/// /IT /LineDimension (a distance), a /PolyLine with /IT /PolyLineDimension (closed = a perimeter, else a
/// distance along the line), a /Polygon with /IT /PolygonDimension (an area; with /Subj "Perimeter" a perimeter
/// of the old plugin, B171), a /PolyLine of 3 vertices with /Measure and /Subj "Angle" (an angle, Q7.5; the old
/// plugin wrote /IT /PolyLineDimension). The value comes from its geometry and its own /Measure (Table 267, 268).
/// A plain value.
class PDF4QTLIBCORESHARED_EXPORT PDFMeasureAnnotationInfo
{
public:
    /// Longest \p text and \p author, which are kept (a longer one is cut): the texts of a document are untrusted,
    /// one long /Contents or /T shared by thousands of annotations would be copied into each of their rows
    static constexpr qsizetype MAXIMUM_TEXT_LENGTH = 1024;

    /// Most vertices of a measurement annotation, which is read (review RS7CCORE L6): the annotations of other
    /// producers and of the old DimensionsPlugin may have more vertices than the tool writes (200), so the reading
    /// limit is larger - bounded, a /Vertices array is untrusted
    static constexpr int MAXIMUM_READ_POINT_COUNT = 10000;

    /// Most vertices of all annotations of one readPages (16 MB of points): one long /Vertices array shared by
    /// thousands of annotations would be copied into each of them; the rest is truncated
    static constexpr size_t MAXIMUM_TOTAL_POINT_COUNT = 1000000;

    PDFObjectReference annotation;      ///< The annotation
    PDFInteger pageIndex = -1;          ///< Its page (from 0)
    PDFMeasureKind kind = PDFMeasureKind::Distance;
    std::vector<QPointF> points;        ///< Its points in the default user space of the page (normalized as PDFMeasureGeometry::normalizePoints)
    PDFReal value = 0.0;                ///< The value in \p unitLabel (by its /Measure; without one in points, square points or degrees)
    QString unitLabel;                  ///< The label of the unit (/U of the first number format; "pt", "sq. pt", "°" without /Measure)
    QByteArray unitId;                  ///< The id of PDFMeasureUnits of the label, empty when it is not known
    QString text;                       ///< The text of the value by the number formats of its /Measure (Table 268), else /Contents
    QString ratio;                      ///< /R of its /Measure
    PDFMeasureScale scale;              ///< The scale of its /Measure (PDFMeasureScale::createFromMeasure; invalid for an unknown unit, an anisotropic or no measure)
    QString author;                     ///< /T
    bool hasMeasure = false;            ///< It has a usable rectilinear /Measure

    /// Reads the measurement annotation; std::nullopt when the annotation is none (another type or intent, a
    /// damaged geometry). Untrusted input: the counts of the vertices are limited by \p maximumPointCount.
    static std::optional<PDFMeasureAnnotationInfo> read(const PDFDocument* document, PDFInteger pageIndex, PDFObjectReference annotation, int maximumPointCount);

    /// The measurement annotations of the pages (in the order of the pages and of /Annots), at most
    /// \p maximumCount and MAXIMUM_TOTAL_POINT_COUNT vertices together (\p isTruncated: the document has more).
    /// Runs in any thread on an immutable document; \p control (optional) stops it - a canceled read returns
    /// the annotations read until then, the caller asks \p control and drops them.
    static std::vector<PDFMeasureAnnotationInfo> readPages(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, size_t maximumCount, int maximumPointCount, bool* isTruncated,
                                                           const PDFOperationControl* control);
};

/// The units shown by a measurement (the panel Units, 473:8597): the ids of PDFMeasureUnits. An empty id = the
/// default unit of its quantity (PDFMeasureUnits::getDefaultUnit).
class PDF4QTLIBCORESHARED_EXPORT PDFMeasureDisplayUnits
{
public:
    QByteArray length;  ///< Unit of the distances and perimeters (/D)
    QByteArray area;    ///< Unit of the areas (/A)
    QByteArray angle;   ///< Unit of the angles (/T)
};

/// The look of a measurement annotation (the panel Labels, 471:5490: the font, its size, bold, italic, the color
/// of the text and of its background with its opacity) and its author. A plain value.
///
/// \p isOpaqueRequired (owner 2026-11-19, review RS7CCORE M3 "Neprůhledně"): the document declares PDF/A, whose
/// part 1 allows no transparency (ISO 19005-1, 6.4: /CA, /ca 1.0, no soft mask) - the colors of the labels and
/// of the lines are composited over white into opaque colors and an area is drawn as its outline only (no /IC,
/// no fill); nothing of the annotation is transparent and its dictionary has no /CA, /ca. The declaration of the
/// document stays true. PDFDocumentToolMeasure::addAnnotations sets it as the Signature tool decides
/// (PDFDocumentTool::getConformanceDeclarations contains "PDF/A"; the part is not read, so PDF/A-2 and -3 are
/// written opaque too).
class PDF4QTLIBCORESHARED_EXPORT PDFMeasureAnnotationStyle
{
public:
    QString fontFamily;                     ///< Family of the font of the label (empty = the default of the appearance generator)
    PDFReal fontSize = 10.0;                ///< Size of the font of the label in points
    bool isBold = false;
    bool isItalic = false;
    QColor textColor = Qt::black;           ///< Color of the text of the label; its alpha is the opacity
    QColor backgroundColor = QColor(0, 0, 0, 26);   ///< Color of the background of the label (black 10 % of 471:5490); its alpha is the opacity (0 = no background)
    QColor lineColor = Qt::black;           ///< Color of the lines of the measurement (/C)
    PDFReal lineWidth = 1.0;                ///< Width of the lines in points (/BS /W)
    QString author;                         ///< /T of the annotation (the author name of the application)
    bool isOpaqueRequired = false;          ///< No transparency (PDF/A-1, ISO 19005-1 6.4): opaque colors, an area as an outline

    /// The color \p color as written: composited over white into an opaque color when \p isOpaqueRequired
    /// (alpha a: c × a + 1 × (1 − a) per component), else unchanged
    static QColor getWrittenColor(const QColor& color, bool isOpaqueRequired);
};

/// Writes the measurement annotations (plan of step 7 of phase 2, 4.8, 5.1; the code of the old DimensionsPlugin,
/// dimensionsplugin.cpp:813-1080, moved to the core and corrected): the annotation of PDFMeasureKind on its page
/// with its /Measure dictionary of the subtype RL (Table 266, 267) - /R by PDFMeasureScale::getDocumentRatioText
/// (the C locale, B183), the required /R, /X, /D, /A, /T always (R7-12 (e)), /X with the effective /UserUnit of
/// the page (B209), the number formats with the defaults of Table 268 (/D 16 for /F /F, /SS a space - B208, R7-12
/// (d)), the unit labels of PDFMeasureDisplayUnits - and its appearance stream /AP (12.5.5; PDF/A) and /Contents
/// with the measured text (the alternate description of PDF/UA, ISO 14289-1 7.18). In a tagged document the
/// annotation gets its structure element /Annot with /OBJR as the annotations of the panel Annotate do (P7
/// checks them first, 5.1).
///
/// A measurement lies on one page (FM-9 cancelled by the owner on 17. 11.: the measurements of a document may lie
/// on different pages, each one on exactly one page). Reentrant; the caller owns the builder and uses it in one
/// thread.
///
/// Package P7 implements it; the skeleton has the interface (stub: no annotation, an error).
class PDF4QTLIBCORESHARED_EXPORT PDFMeasureWriter
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFMeasureWriter)

public:
    PDFMeasureWriter() = delete;

    /// Creates the annotation of one measurement and adds it to the annotations of \p page
    /// \param builder Builder of the changed document (one step of Undo of the caller)
    /// \param page The page of the measurement
    /// \param kind Kind of the measurement (the kind of the annotation, see PDFMeasureKind)
    /// \param points Points in the default user space of the page (the counts of PDFMeasureKind)
    /// \param scale Scale of the measurement (a valid scale; a measurement inside a viewport of the page has the scale of the viewport)
    /// \param units Units of the shown values
    /// \param style Look and author of the annotation
    /// \param errorMessage Translated reason of a failure (optional)
    /// \returns The reference of the new annotation, an invalid reference on a failure (nothing is added)
    static PDFObjectReference createAnnotation(PDFDocumentBuilder* builder,
                                               PDFObjectReference page,
                                               PDFMeasureKind kind,
                                               const std::vector<QPointF>& points,
                                               const PDFMeasureScale& scale,
                                               const PDFMeasureDisplayUnits& units,
                                               const PDFMeasureAnnotationStyle& style,
                                               QString* errorMessage);
};

}   // namespace pdf

#endif // PDFMEASUREWRITER_H
