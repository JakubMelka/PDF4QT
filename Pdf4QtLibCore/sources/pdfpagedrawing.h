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

#ifndef PDFPAGEDRAWING_H
#define PDFPAGEDRAWING_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QJsonArray>
#include <QMetaType>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFDocument;
class PDFDocumentBuilder;

/// A drawing on the pages of a document (plan of step 6 of phase 2, 4.1, 4.2, 5.5, 10.3; package P2
/// implements it): the visible appearance of Sign with Certificate (its form XObject) and the drawing
/// of Sign by Hand (written into the content of the pages). The application makes it of the scene of
/// PDFAppToolDrawingPart (toDrawing), MCP of the JSON of the parameter elements (fromJson), the old
/// Signature plugin of its page content elements.
///
/// A value: the elements are implicitly shared and immutable (std::shared_ptr of const data), a copy
/// copies the pointer, operator== compares the identity of the data (the parameter Value of a tool,
/// PDFDocumentToolParameterDefinition::createValue, RB7) - equal content of two drawings made apart
/// is not equal. Thread-safe to read; a worker gets a copy.
///
/// Geometry: the coordinates of an element are points of the default user space of its page (ISO
/// 32000-2, 8.3.2.3: the origin is the lower left corner of the media box, y grows upward, before
/// /Rotate and /UserUnit); a drawing of more pages has elements of more pages.
///
/// Limits (plan 7.2, untrusted input of MCP and of a stored scene): at most MAXIMUM_ELEMENT_COUNT
/// elements, MAXIMUM_POINT_COUNT points of all curves together, MAXIMUM_TEXT_LENGTH characters of a
/// text, an SVG image at most MAXIMUM_SVG_SIZE bytes without external references (xlink:href only
/// data:) and drawn with a limited size of its image.
///
/// Secrets: a drawing holds no secret (S6.2); its JSON (toJson) is safe in MCP, but a drawing of a
/// signature is a sensitive value (PDFDocumentToolParameterDefinition::setSensitive, R6-19): it is
/// never remembered by the application.
class PDF4QTLIBCORESHARED_EXPORT PDFPageDrawing
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFPageDrawing)

public:
    /// Limits of a drawing (plan 7.2)
    static constexpr int MAXIMUM_ELEMENT_COUNT = 1000;
    static constexpr int MAXIMUM_POINT_COUNT = 100000;
    static constexpr int MAXIMUM_TEXT_LENGTH = 4096;
    static constexpr qint64 MAXIMUM_SVG_SIZE = 4 * 1024 * 1024;

    /// Kind of an element (the tools of the old Signature plugin and of Figma 434:4951, 435:8619:
    /// Text label, Freehand curve, SVG image, Accept mark, Reject mark, Dot; the shapes Rectangle,
    /// Rounded rectangle, Horizontal / Vertical / free Line)
    enum class Kind
    {
        Text,               ///< A text label: \p text in \p rectangle (its alignment, its angle)
        FreehandCurve,      ///< A curve through \p points
        Line,               ///< A line from points[0] to points[1] (horizontal, vertical, free)
        Rectangle,          ///< \p rectangle
        RoundedRectangle,   ///< \p rectangle with the radius \p cornerRadius
        Dot,                ///< A filled circle in \p rectangle
        AcceptMark,         ///< A check mark in \p rectangle
        RejectMark,         ///< A cross in \p rectangle
        SvgImage            ///< \p svgData drawn into \p rectangle (never through MCP, Q6.7)
    };

    /// Horizontal alignment of a text label
    enum class Alignment
    {
        Left,
        Center,
        Right
    };

    /// The style of an element (the right panel Style of 435:8619, the dock of the old plugin)
    struct Style
    {
        QColor penColor = Qt::black;        ///< Color of the strokes and of the text
        double penWidth = 1.0;              ///< Width of the strokes in points (0 = no stroke)
        QColor fillColor;                   ///< Fill of a closed shape, invalid = no fill
        QString fontFamily;                 ///< Font of a text, empty = the default font of the drawing
        double fontSize = 12.0;             ///< Size of the font in points
        Alignment alignment = Alignment::Left;
        double angle = 0.0;                 ///< Rotation of a text in degrees (counter clockwise)

        bool operator==(const Style&) const = default;
    };

    /// One element
    struct Element
    {
        Kind kind = Kind::Text;
        PDFInteger pageIndex = 0;           ///< The page of the element (index from 0)
        QRectF rectangle;                   ///< The box of the element (text, shapes, marks, image)
        std::vector<QPointF> points;        ///< Points of a curve or of a line
        double cornerRadius = 0.0;          ///< Radius of a rounded rectangle
        QString text;                       ///< The text of a text label
        QByteArray svgData;                 ///< The bytes of an SVG image
        Style style;

        bool operator==(const Element&) const = default;
    };

    /// An empty drawing
    PDFPageDrawing();

    /// A drawing of the elements (not checked: isValid checks the limits)
    explicit PDFPageDrawing(std::vector<Element> elements);

    /// The elements in their order (the order of painting)
    const std::vector<Element>& getElements() const;

    /// The drawing has no element
    bool isEmpty() const;

    /// The indices of the pages, which have an element, sorted, without duplicates
    std::vector<PDFInteger> getPageIndices() const;

    /// The first page with an element (Sign: the visible signature goes to the first page with
    /// graphics, 434:7784), -1 for an empty drawing
    PDFInteger getFirstPageIndex() const;

    /// The bounding box of the elements of the page in the user space of the page (the /BBox of the
    /// form XObject of a visible signature, the /Rect of its widget), an empty rectangle for none
    QRectF getBoundingBox(PDFInteger pageIndex) const;

    /// The drawing keeps the limits (plan 7.2), its elements are on pages of the document and are
    /// finite numbers; \p errorMessage gets the translated reason
    bool isValid(const PDFDocument* document, QString* errorMessage) const;

    /// Identity of the data (the parameter Value, RB7)
    bool operator==(const PDFPageDrawing& other) const { return m_data == other.m_data; }

    /// Writes the elements of a page as a form XObject (the appearance of a visible signature,
    /// /BBox = getBoundingBox, its matrix the identity): the content is wrapped in q … Q, the fonts are
    /// embedded, a PDF/A-1 target gets opaque colors (\p isOpaqueRequired; ISO 19005-1, 6.4)
    /// \param builder The builder of the document
    /// \param pageIndex The page, whose elements are written
    /// \param isOpaqueRequired No transparency is written (PDF/A-1)
    /// \returns the reference of the form XObject, an invalid reference on a failure
    PDFObjectReference createFormXObject(PDFDocumentBuilder* builder, PDFInteger pageIndex, bool isOpaqueRequired) const;

    /// Appends the elements of every page to the content of its page (Sign by Hand, the old Sign
    /// Electronically): PDFPageContentStreamBuilder::Mode::PlaceAfter, wrapped in q … Q, so the
    /// graphics state of the page does not change; an SVG image as a form XObject
    /// \param builder The builder of the document
    /// \param errorMessage Translated reason of a failure (a page without a valid media box, R6-17)
    /// \returns false on a failure (nothing is half written: the caller drops the builder)
    bool appendToPages(PDFDocumentBuilder* builder, QString* errorMessage) const;

    /// The JSON of the elements for MCP (the parameter elements of Sign by Hand, Q6.7): an array of
    /// objects { "kind", "page" (from 1), "rect" [x, y, width, height] or "points" [[x, y], ...],
    /// "text", "style" { "penColor" "#rrggbb", "penWidth", "fillColor", "fontSize", "alignment",
    /// "angle" } }; an SVG image is never in it
    QJsonArray toJson() const;

    /// A drawing of the JSON of toJson, read strictly (an unknown key, a wrong type, a value out of
    /// its range, a kind svg-image - Q6.7 -, more than the limits: std::nullopt and the translated
    /// reason). The pages are not checked against a document here (isValid).
    static std::optional<PDFPageDrawing> fromJson(const QJsonArray& json, QString* errorMessage);

private:
    struct Data
    {
        std::vector<Element> elements;
    };

    std::shared_ptr<const Data> m_data;
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFPageDrawing)

#endif // PDFPAGEDRAWING_H
