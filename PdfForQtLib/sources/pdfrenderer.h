//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFRENDERER_H
#define PDFRENDERER_H

#include "pdfpage.h"
#include "pdfparser.h"
#include "pdfcolorspaces.h"

#include <QMatrix>
#include <QSharedPointer>

#include <stack>

namespace pdf
{

enum RenderErrorType
{
    Error,
    NotImplemented
};

struct PDFRenderError
{
    explicit PDFRenderError() = default;
    explicit PDFRenderError(RenderErrorType type, QString message) :
        type(type),
        message(std::move(message))
    {

    }

    RenderErrorType type = RenderErrorType::Error;
    QString message;
};

/// Renders the PDF page on the painter, or onto an image.
class PDFRenderer
{
public:
    explicit PDFRenderer(const PDFDocument* document);

    enum Feature
    {
        Antialasing,        ///< Antialiasing for lines, shapes, etc.
        TextAntialiasing,   ///< Antialiasing for drawing text
        SmoothImages        ///< Adjust images to the device space using smooth transformation (slower, but better performance quality)
    };

    Q_DECLARE_FLAGS(Features, Feature)

    /// Paints desired page onto the painter. Page is painted in the rectangle using best-fit method.
    /// If the page doesn't exist, then error is returned. No exception is thrown. Rendering errors
    /// are reported and returned in the error list. If no error occured, empty list is returned.
    /// \param painter Painter
    /// \param rectangle Paint area for the page
    /// \param pageIndex Index of the page to be painted
    QList<PDFRenderError> render(QPainter* painter, const QRectF& rectangle, size_t pageIndex) const;

private:
    const PDFDocument* m_document;
    Features m_features;
};

/// Process the contents of the page.
class PDFPageContentProcessor
{
public:
    explicit PDFPageContentProcessor(const PDFPage* page, const PDFDocument* document);

    enum class Operator
    {
        // General graphic state        w, J, j, M, d, ri, i, gs
        SetLineWidth,                       ///< w, sets the line width
        SetLineCap,                         ///< J, sets the line cap
        SetLineJoin,                        ///< j, sets the line join
        SetMitterLimit,                     ///< M, sets the mitter limit
        SetLineDashPattern,                 ///< d, sets the line dash pattern
        SetRenderingIntent,                 ///< ri, sets the rendering intent
        SetFlatness,                        ///< i, sets the flattness (number in range from 0 to 100)
        SetGraphicState,                    ///< gs, sets the whole graphic state (stored in resource dictionary)

        // Special graphic state:       q, Q, cm
        SaveGraphicState,                   ///< q, saves the graphic state
        RestoreGraphicState,                ///< Q, restores the graphic state
        AdjustCurrentTransformationMatrix,  ///< cm, modify the current transformation matrix by matrix multiplication

        // Path construction:           m, l, c, v, y, h, re
        MoveCurrentPoint,                   ///< m, begin a new subpath by moving to the desired point
        LineTo,                             ///< l, appends a straight line segment to the subpath
        Bezier123To,                        ///< c, appends a Bézier curve with control points 1, 2, 3
        Bezier23To,                         ///< v, appends a Bézier curve with control points 2, 3
        Bezier13To,                         ///< y, appends a Bézier curve with control points 1, 3
        EndSubpath,                         ///< h, ends current subpath by adding straight line segment from the last point to the beginning

        // Path painting:               S, s, f, F, f*, B, B*, b, b*, n
        StrokePath,                         ///< S, stroke the path
        CloseAndStrokePath,                 ///< s, close the path and then stroke (equivalent of operators h S)
        FillPathWinding,                    ///< f, close the path, and then fill the path using "Non zero winding number rule"
        FillPathWinding2,                   ///< F, same as previous, see PDF Reference 1.7, Table 4.10
        FillPathEvenOdd,                    ///< f*, fill the path using "Even-odd rule"
        StrokeAndFillWinding,               ///< B, stroke and fill path, using "Non zero winding number rule"
        StrokeAndFillEvenOdd,               ///< B*, stroke and fill path, using "Even-odd rule"
        CloseAndStrokeAndFillWinding,       ///< b, close, stroke and fill path, using "Non zero winding number rule", equivalent of operators h B
        CloseAndStrokeAndFillEvenOdd,       ///< b*, close, stroke and fill path, using "Even-odd rule", equivalent of operators h B*
        ClearPath,                          ///< n, clear parh (close current) path, "no-operation", used with clipping

        // Clipping paths:             W, W*
        ClipWinding,                        ///< W, modify current clipping path by intersecting it with current path using "Non zero winding number rule"
        ClipEvenOdd,                        ///< W*, modify current clipping path by intersecting it with current path using "Even-odd rule"

        // Text object:                BT, ET
        TextBegin,                          ///< BT, begin text object, initialize text matrices, cannot be nested
        TextEnd,                            ///< ET, end text object, cannot be nested

        // Text state:                 Tc, Tw, Tz, TL, Tf, Tr, Ts
        TextSetCharacterSpacing,            ///< Tc, set text character spacing
        TextSetWordSpacing,                 ///< Tw, set text word spacing
        TextSetHorizontalScale,             ///< Tz, set text horizontal scaling (in percents, 100% = normal scaling)
        TextSetLeading,                     ///< TL, set text leading
        TextSetFontAndFontSize,             ///< Tf, set text font (name from dictionary) and its size
        TextSetRenderMode,                  ///< Tr, set text render mode
        TextSetRise,                        ///< Ts, set text rise

        // Text positioning:           Td, TD, Tm, T*
        TextMoveByOffset,                   ///< Td, move by offset
        TextSetLeadingAndMoveByOffset,      ///< TD, sets thext leading and moves by offset, x y TD is equivalent to sequence -y TL x y Td
        TextSetMatrix,                      ///< Tm, set text matrix
        TextMoveByLeading,                  ///< T*, moves text by leading, equivalent to 0 leading Td

        // Text showing:               Tj, TJ, ', "
        TextShowTextString,                 ///< Tj, show text string
        TextShowTextIndividualSpacing,      ///< TJ, show text, allow individual text spacing
        TextNextLineShowText,               ///< ', move to the next line and show text ("string '" is equivalent to "T* string Tj")
        TextSetSpacingAndShowText,          ///< ", move to the next line, set spacing and show text (equivalent to sequence "w1 Tw w2 Tc string '")

        // Type 3 font:                d0, d1
        Type3FontSetOffset,                 ///< d0, set width information, see PDF 1.7 Reference, Table 5.10
        Type3FontSetOffsetAndBB,            ///< d1, set offset and glyph bounding box

        // Color:                      CS, cs, SC, SCN, sc, scn, G, g, RG, rg, K, k
        ColorSetStrokingColorSpace,         ///< CS, set current color space for stroking operations
        ColorSetFillingColorSpace,          ///< cs, set current color space for filling operations
        ColorSetStrokingColor,              ///< SC, set current stroking color
        ColorSetStrokingColorN,             ///< SCN, same as SC, but also supports Pattern, Separtion, DeviceN and ICCBased color spaces
        ColorSetFillingColor,               ///< sc, set current filling color
        ColorSetFillingColorN,              ///< scn, same as sc, but also supports Pattern, Separtion, DeviceN and ICCBased color spaces
        ColorSetDeviceGrayStroking,         ///< G, set DeviceGray color space for stroking color and set color
        ColorSetDeviceGrayFilling,          ///< g, set DeviceGray color space for filling color and set color
        ColorSetDeviceRGBStroking,          ///< RG, set DeviceRGB color space for stroking color and set color
        ColorSetDeviceRGBFilling,           ///< rg, set DeviceRGB color space for filling color and set color
        ColorSetDeviceCMYKStroking,         ///< K, set DeviceCMYK color space for stroking color and set color
        ColorSetDeviceCMYKFilling,          ///< k, set DeviceCMYK color space for filling color and set color

        // Shading pattern:            sh
        ShadingPaintShape,                  ///< sh, paint shape

        // Inline images:              BI, ID, EI
        InlineImageBegin,                   ///< BI, begin inline image
        InlineImageData,                    ///< ID, inline image data
        InlineImageEnd,                     ///< EI, end of inline image

        // XObject:                    Do
        PaintXObject,                       ///< Do, paint the X Object (image, form, ...)

        // Marked content:             MP, DP, BMC, BDC, EMC
        MarkedContentPoint,                 ///< MP, marked content point
        MarkedContentPointWithProperties,   ///< DP, marked content point with properties
        MarkedContentBegin,                 ///< BMC, begin of sequence of marked content
        MarkedContentBeginWithProperties,   ///< BDC, begin of sequence of marked content with properties
        MarkedContentEnd,                   ///< EMC, end of marked content sequence

        // Compatibility:              BX, EX
        CompatibilityBegin,                 ///< BX, Compatibility mode begin (unrecognized operators are ignored)
        CompatibilityEnd,                   ///< EX, Compatibility mode end
        Invalid                             ///< Invalid operator, use for error reporting
    };

    /// Process the contents of the page
    QList<PDFRenderError> processContents();

protected:
    /// Process the content stream
    QList<PDFRenderError> processContentStream(const PDFStream* stream);

    /// Represents graphic state of the PDF (holding current graphic state parameters).
    /// Please see PDF Reference 1.7, Chapter 4.3 "Graphic State"
    class PDFPageContentProcessorState
    {
    public:
        explicit PDFPageContentProcessorState();
        ~PDFPageContentProcessorState();

    private:
        QMatrix m_currentTransformationMatrix;
        QSharedPointer<PDFAbstractColorSpace> m_fillColorSpace;
        QSharedPointer<PDFAbstractColorSpace> m_strokeColorSpace;
        QColor m_fillColor;
        QColor m_strokeColor;
        PDFReal m_lineWidth;
        Qt::PenCapStyle m_lineCapStyle;
        Qt::PenJoinStyle m_lineJoinStyle;
        PDFReal m_mitterLimit;
        QByteArray m_renderingIntent;
        PDFReal m_flatness;
        PDFReal m_smoothness;
    };

private:

    const PDFPage* m_page;
    const PDFDocument* m_document;

    /// Array with current operand arguments
    PDFFlatArray<PDFLexicalAnalyzer::Token, 33> m_operands;

    /// Stack with current graphic states
    std::stack<PDFPageContentProcessorState> m_stack;
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFRenderer::Features)

#endif // PDFRENDERER_H
