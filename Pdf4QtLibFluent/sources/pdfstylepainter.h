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

#ifndef PDFSTYLEPAINTER_H
#define PDFSTYLEPAINTER_H

#include "pdfthemecolors.h"
#include "pdfthemefonts.h"

#include <QFont>
#include <QPainterPath>
#include <QRectF>
#include <QString>

class QPainter;

namespace pdf
{

/// Drawing primitives of the design shared by the style and by the widgets of
/// the library: flat rounded shapes, strokes lying inside of a shape, soft
/// shadows, icons and texts. All coordinates are logical pixels.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFStylePainter
{
public:
    PDFStylePainter() = delete;

    /// Radii of the corners of a shape
    struct Radii
    {
        constexpr Radii() = default;
        constexpr Radii(qreal radius) : topLeft(radius), topRight(radius), bottomRight(radius), bottomLeft(radius) { }
        constexpr Radii(qreal topLeft, qreal topRight, qreal bottomRight, qreal bottomLeft) :
            topLeft(topLeft), topRight(topRight), bottomRight(bottomRight), bottomLeft(bottomLeft) { }

        /// Returns radii of a shape, which is smaller (positive) or larger (negative) by the delta
        Radii adjusted(qreal delta) const;

        /// Returns radii mirrored horizontally (right-to-left layout)
        Radii mirrored() const { return Radii(topRight, topLeft, bottomLeft, bottomRight); }

        qreal topLeft = 0.0;
        qreal topRight = 0.0;
        qreal bottomRight = 0.0;
        qreal bottomLeft = 0.0;
    };

    /// Returns the color of the active theme
    static QColor color(PDFThemeColors::Color color);

    /// Returns the path of the rounded rectangle
    static QPainterPath roundedPath(const QRectF& rect, const Radii& radii);

    /// Fills the rounded rectangle
    static void fill(QPainter* painter, const QRectF& rect, const Radii& radii, const QColor& color);

    /// Fills the rounded rectangle by a color of the active theme
    static void fill(QPainter* painter, const QRectF& rect, const Radii& radii, PDFThemeColors::Color color);

    /// Draws a stroke, which lies whole inside of the rounded rectangle
    static void strokeInside(QPainter* painter, const QRectF& rect, const Radii& radii, const QColor& color, qreal width = 1.0);
    static void strokeInside(QPainter* painter, const QRectF& rect, const Radii& radii, PDFThemeColors::Color color, qreal width = 1.0);

    /// Draws a stroke, which lies whole outside of the rounded rectangle and touches it
    static void strokeOutside(QPainter* painter, const QRectF& rect, const Radii& radii, const QColor& color, qreal width);

    /// Draws the focus ring of 2 px around the shape of a control
    /// \param painter Painter
    /// \param shape Shape of the control
    /// \param radii Radii of the shape
    /// \param isAccentShape The shape is filled with the accent color. When the
    ///        ring has the same color, they are one shape in the design: the
    ///        ring reaches 1 px into the shape, so no lighter seam of two
    ///        antialiased edges is left between them.
    static void focusRing(QPainter* painter, const QRectF& shape, const Radii& radii, bool isAccentShape);

    /// Fills a rectangle without antialiasing (dividers, separators, grooves)
    static void fillRect(QPainter* painter, const QRectF& rect, const QColor& color);
    static void fillRect(QPainter* painter, const QRectF& rect, PDFThemeColors::Color color);

    /// Fills an ellipse
    static void fillEllipse(QPainter* painter, const QRectF& rect, const QColor& color);

    /// Draws a stroke, which lies whole inside of the ellipse
    static void strokeEllipseInside(QPainter* painter, const QRectF& rect, const QColor& color, qreal width);

    /// Draws a soft shadow of the rounded rectangle, as the drop shadow of the design
    /// \param painter Painter
    /// \param rect Rectangle, which casts the shadow
    /// \param radii Radii of the rectangle
    /// \param offset Offset of the shadow
    /// \param blur Blur of the shadow
    /// \param color Color of the shadow
    static void shadow(QPainter* painter, const QRectF& rect, const Radii& radii, const QPointF& offset, qreal blur, const QColor& color);

    /// Draws an icon of the design
    static void icon(QPainter* painter, const QRectF& rect, const QString& name, const QColor& color);
    static void icon(QPainter* painter, const QRectF& rect, const QString& name, PDFThemeColors::Color color);

    /// Draws a text of a single line
    /// \param painter Painter
    /// \param rect Rectangle of the text
    /// \param flags Alignment and text flags (Qt::AlignmentFlag, Qt::TextFlag)
    /// \param text Text
    /// \param font Font
    /// \param color Color
    static void text(QPainter* painter, const QRectF& rect, int flags, const QString& text, const QFont& font, const QColor& color);

    /// Returns the width of the text of a single line
    static qreal textWidth(const QString& text, const QFont& font, int flags = 0);

    /// Returns the rectangle of the given size centered in the outer rectangle
    static QRectF centered(const QRectF& outer, const QSizeF& size);
};

}   // namespace pdf

#endif // PDFSTYLEPAINTER_H
