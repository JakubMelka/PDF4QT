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

#ifndef PDFPAGECONTENTPROCESSOR_H
#define PDFPAGECONTENTPROCESSOR_H

#include "pdfrenderer.h"
#include "pdfparser.h"
#include "pdfcolorspaces.h"

#include <QMatrix>
#include <QPainterPath>
#include <QSharedPointer>

#include <stack>
#include <tuple>
#include <type_traits>

namespace pdf
{
static constexpr const char* PDF_RESOURCE_EXTGSTATE = "ExtGState";

class PDFRendererException : public std::exception
{
public:
    explicit PDFRendererException(RenderErrorType type, QString message) :
        m_error(type, std::move(message))
    {

    }

    const PDFRenderError& getError() const { return m_error; }

private:
    PDFRenderError m_error;
};

/// Process the contents of the page.
class PDFPageContentProcessor
{
public:
    explicit PDFPageContentProcessor(const PDFPage* page, const PDFDocument* document);
    virtual ~PDFPageContentProcessor();

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
        Rectangle,                          ///< re, adds rectangle

        // Path painting:               S, s, f, F, f*, B, B*, b, b*, n
        PathStroke,                         ///< S, Stroke
        PathCloseStroke,                    ///< s, Close, Stroke (equivalent of operators h S)
        PathFillWinding,                    ///< f, Fill, Winding
        PathFillWinding2,                   ///< F, same as previous, see PDF Reference 1.7, Table 4.10
        PathFillEvenOdd,                    ///< f*, Fill, Even-Odd
        PathFillStrokeWinding,              ///< B, Fill, Stroke, Winding
        PathFillStrokeEvenOdd,              ///< B*, Fill, Stroke, Even-Odd
        PathCloseFillStrokeWinding,         ///< b, Close, Fill, Stroke, Winding (equivalent of operators h B)
        PathCloseFillStrokeEvenOdd,         ///< b*, Close, Fill, Stroke, Even-Odd (equivalent of operators h B*)
        PathClear,                          ///< n, clear path (close current) path, "no-operation", used with clipping

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
        ColorSetStrokingColorN,             ///< SCN, same as SC, but also supports Pattern, Separation, DeviceN and ICCBased color spaces
        ColorSetFillingColor,               ///< sc, set current filling color
        ColorSetFillingColorN,              ///< scn, same as sc, but also supports Pattern, Separation, DeviceN and ICCBased color spaces
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

    class PDFLineDashPattern
    {
    public:
        explicit inline PDFLineDashPattern() = default;
        explicit inline PDFLineDashPattern(const std::vector<PDFReal>& dashArray, PDFReal dashOffset) :
            m_dashArray(dashArray),
            m_dashOffset(dashOffset)
        {

        }

        inline const std::vector<PDFReal>& getDashArray() const { return m_dashArray; }
        inline void setDashArray(const std::vector<PDFReal>& dashArray) { m_dashArray = dashArray; }

        inline PDFReal getDashOffset() const { return m_dashOffset; }
        inline void setDashOffset(PDFReal dashOffset) { m_dashOffset = dashOffset; }

        inline bool operator==(const PDFLineDashPattern& other) const { return m_dashArray == other.m_dashArray && m_dashOffset == other.m_dashOffset; }
        inline bool operator!=(const PDFLineDashPattern& other) const { return !(*this == other); }

        /// Is line solid? Function returns true, if yes.
        bool isSolid() const { return m_dashArray.empty(); }

    private:
        std::vector<PDFReal> m_dashArray;
        PDFReal m_dashOffset = 0.0;
    };

    /// Represents graphic state of the PDF (holding current graphic state parameters).
    /// Please see PDF Reference 1.7, Chapter 4.3 "Graphic State"
    class PDFPageContentProcessorState
    {
    public:
        explicit PDFPageContentProcessorState();
        ~PDFPageContentProcessorState();

        PDFPageContentProcessorState(const PDFPageContentProcessorState&) = default;
        PDFPageContentProcessorState(PDFPageContentProcessorState&&) = default;

        PDFPageContentProcessorState& operator=(PDFPageContentProcessorState&&) = delete;
        PDFPageContentProcessorState& operator=(const PDFPageContentProcessorState& other);

        enum StateFlag
        {
            StateUnchanged                      = 0x0000,
            StateCurrentTransformationMatrix    = 0x0001,
            StateStrokeColorSpace               = 0x0002,
            StateFillColorSpace                 = 0x0004,
            StateStrokeColor                    = 0x0008,
            StateFillColor                      = 0x0010,
            StateLineWidth                      = 0x0020,
            StateLineCapStyle                   = 0x0040,
            StateLineJoinStyle                  = 0x0080,
            StateMitterLimit                    = 0x0100,
            StateLineDashPattern                = 0x0200,
            StateRenderingIntent                = 0x0400,
            StateFlatness                       = 0x0800,
            StateSmoothness                     = 0x1000,
            StateAll                            = 0xFFFF
        };

        Q_DECLARE_FLAGS(StateFlags, StateFlag)

        const QMatrix& getCurrentTransformationMatrix() const { return m_currentTransformationMatrix; }
        void setCurrentTransformationMatrix(const QMatrix& currentTransformationMatrix);

        const PDFAbstractColorSpace* getStrokeColorSpace() const { return m_strokeColorSpace.data(); }
        void setStrokeColorSpace(const QSharedPointer<PDFAbstractColorSpace>& strokeColorSpace);

        const PDFAbstractColorSpace* getFillColorSpace() const { return m_fillColorSpace.data(); }
        void setFillColorSpace(const QSharedPointer<PDFAbstractColorSpace>& fillColorSpace);

        const QColor& getStrokeColor() const { return m_strokeColor; }
        void setStrokeColor(const QColor& strokeColor);

        const QColor& getFillColor() const { return m_fillColor; }
        void setFillColor(const QColor& fillColor);

        PDFReal getLineWidth() const { return m_lineWidth; }
        void setLineWidth(PDFReal lineWidth);

        Qt::PenCapStyle getLineCapStyle() const { return m_lineCapStyle; }
        void setLineCapStyle(Qt::PenCapStyle lineCapStyle);

        Qt::PenJoinStyle getLineJoinStyle() const { return m_lineJoinStyle; }
        void setLineJoinStyle(Qt::PenJoinStyle lineJoinStyle);

        PDFReal getMitterLimit() const { return m_mitterLimit; }
        void setMitterLimit(const PDFReal& mitterLimit);

        const PDFLineDashPattern& getLineDashPattern() const { return m_lineDashPattern; }
        void setLineDashPattern(PDFLineDashPattern pattern);

        const QByteArray& getRenderingIntent() const { return m_renderingIntent; }
        void setRenderingIntent(const QByteArray& renderingIntent);

        PDFReal getFlatness() const { return m_flatness; }
        void setFlatness(PDFReal flatness);

        PDFReal getSmoothness() const { return m_smoothness; }
        void setSmoothness(PDFReal smoothness);

        StateFlags getStateFlags() const { return m_stateFlags; }
        void setStateFlags(StateFlags stateFlags) { m_stateFlags = stateFlags; }

    private:
        QMatrix m_currentTransformationMatrix;
        PDFColorSpacePointer m_strokeColorSpace;
        PDFColorSpacePointer m_fillColorSpace;
        QColor m_strokeColor;
        QColor m_fillColor;
        PDFReal m_lineWidth;
        Qt::PenCapStyle m_lineCapStyle;
        Qt::PenJoinStyle m_lineJoinStyle;
        PDFReal m_mitterLimit;
        PDFLineDashPattern m_lineDashPattern;
        QByteArray m_renderingIntent;
        PDFReal m_flatness;
        PDFReal m_smoothness;
        StateFlags m_stateFlags;
    };

    enum class ProcessOrder
    {
        BeforeOperation,
        AfterOperation
    };

    /// This function has to be implemented in the client drawing implementation, it should
    /// draw the path according to the parameters.
    /// \param path Path, which should be drawn (can be emtpy - in that case nothing happens)
    /// \param stroke Stroke the path
    /// \param fill Fill the path using given rule
    /// \param fillRule Fill rule used in the fill mode
    virtual void performPathPainting(const QPainterPath& path, bool stroke, bool fill, Qt::FillRule fillRule);

    /// This function has to be implemented in the client drawing implementation, it should
    /// clip along the path (intersect with current clipping path).
    virtual void performClipping(const QPainterPath& path, Qt::FillRule fillRule);

    /// This function has to be implemented in the client drawing implementation, it should
    /// update the device according to the graphic state change. The flags are set when
    /// the value differs from the previous graphic state.
    virtual void performUpdateGraphicsState(const PDFPageContentProcessorState& state);

    /// Implement to perform save of the graphic state. This function is called two times -
    /// before the operation and after the operation. Parameter \p order determines when
    /// this function is called.
    /// \param order If this function is called before the operation, or after the operation.
    virtual void performSaveGraphicState(ProcessOrder order);

    /// Implement to perform restore of the graphic state. This function is called two times -
    /// before the operation and after the operation. Parameter \p order determines when
    /// this function is called.
    /// \param order If this function is called before the operation, or after the operation.
    virtual void performRestoreGraphicState(ProcessOrder order);

    /// Returns current graphic state
    const PDFPageContentProcessorState* getGraphicState() const { return &m_graphicState; }

    /// Adds error to the error list
    /// \param error Error message
    void addError(const QString& error) { m_errorList.append(PDFRenderError(RenderErrorType::Error, error)); }

private:
    /// Process the content stream
    void processContentStream(const PDFStream* stream);

    /// Processes single command
    void processCommand(const QByteArray& command);

    /// Wrapper for PDF Name
    struct PDFName
    {
        QByteArray name;
    };

    template<typename T>
    T readOperand(size_t index) const;

    template<>
    PDFReal readOperand<PDFReal>(size_t index) const;

    template<>
    PDFInteger readOperand<PDFInteger>(size_t index) const;

    template<>
    PDFName readOperand<PDFName>(size_t index) const;

    template<size_t index, typename T>
    inline T readOperand() const { return readOperand<T>(index); }

    template<typename Tuple, class F, std::size_t... I>
    inline void invokeOperatorImpl(F function, std::index_sequence<I...>)
    {
        (this->*function)(readOperand<I, typename std::tuple_element<I, Tuple>::type>()...);
    }

    /// Function invokes operator (function) with given arguments. For that reason, variadic
    /// templates are used. Basically, for each argument of the function, we need type of the argument,
    /// and its index. To retrieve it, we use std::tuple, variadic template and functionality
    /// analogic to std::apply implementation.
    template<typename... Operands>
    inline void invokeOperator(void(PDFPageContentProcessor::* function)(Operands...))
    {
        invokeOperatorImpl<std::tuple<Operands...>>(function, std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<std::tuple<Operands...>>>>{});
    }

    /// Returns the current poin in the path. If path doesn't exist, then
    /// exception is thrown.
    QPointF getCurrentPoint() const;

    /// Notifies the updated graphic state. If nothing changed in graphic state, then nothing happens.
    void updateGraphicState();

    template<typename... Operands>
    inline QColor getColorFromColorSpace(const PDFAbstractColorSpace* colorSpace, Operands... operands)
    {


        constexpr const size_t operandCount = sizeof...(Operands);
        const size_t colorSpaceComponentCount = colorSpace->getColorComponentCount();
        if (operandCount == colorSpaceComponentCount)
        {
            return colorSpace->getColor(PDFColor(static_cast<PDFColorComponent>(operands)...));
        }
        else
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid color component count. Provided %1, required %2.").arg(operandCount).arg(colorSpaceComponentCount));
        }

        return QColor();
    }

    /// Converts PDF line cap to Qt's pen cap style. Function always succeeds,
    /// if invalid \p lineCap occurs, then some valid pen cap style is returned.
    /// \param lineCap PDF Line cap style (see PDF Reference 1.7, values can be 0, 1, and 2)
    static Qt::PenCapStyle convertLineCapToPenCapStyle(PDFInteger lineCap);

    /// Convers Qt's pen cap style to PDF's line cap style (defined in the PDF Reference)
    /// \param penCapStyle Qt's pen cap style to be converted
    static PDFInteger convertPenCapStyleToLineCap(Qt::PenCapStyle penCapStyle);

    /// Converts PDF line join to Qt's pen join style. Function always succeeds,
    /// if invalid \p lineJoin occurs, then some valid pen join style is returned.
    /// \param lineJoin PDF Line join style (see PDF Reference 1.7, values can be 0, 1, and 2)
    static Qt::PenJoinStyle convertLineJoinToPenJoinStyle(PDFInteger lineJoin);

    /// Convers Qt's pen join style to PDF's line join style (defined in the PDF Reference)
    /// \param penJoinStyle Qt's pen join style to be converted
    static PDFInteger convertPenJoinStyleToLineJoin(Qt::PenJoinStyle penJoinStyle);

    // General graphic state        w, J, j, M, d, ri, i, gs
    void operatorSetLineWidth(PDFReal lineWidth);           ///< w, sets the line width
    void operatorSetLineCap(PDFInteger lineCap);            ///< J, sets the line cap
    void operatorSetLineJoin(PDFInteger lineJoin);          ///< j, sets the line join
    void operatorSetMitterLimit(PDFReal mitterLimit);       ///< M, sets the mitter limit
    void operatorSetLineDashPattern();                      ///< d, sets the line dash pattern
    void operatorSetRenderingIntent(PDFName intent);        ///< ri, sets the rendering intent
    void operatorSetFlatness(PDFReal flatness);             ///< i, sets the flattness (number in range from 0 to 100)
    void operatorSetGraphicState(PDFName dictionaryName);   ///< gs, sets the whole graphic state (stored in resource dictionary)

    // Special graphic state:       q, Q, cm
    void operatorSaveGraphicState();                   ///< q, saves the graphic state
    void operatorRestoreGraphicState();                ///< Q, restores the graphic state
    void operatorAdjustCurrentTransformationMatrix(PDFReal a, PDFReal b, PDFReal c, PDFReal d, PDFReal e, PDFReal f);  ///< cm, modify the current transformation matrix by matrix multiplication

    // Path construction operators
    void operatorMoveCurrentPoint(PDFReal x, PDFReal y);
    void operatorLineTo(PDFReal x, PDFReal y);
    void operatorBezier123To(PDFReal x1, PDFReal y1, PDFReal x2, PDFReal y2, PDFReal x3, PDFReal y3);
    void operatorBezier23To(PDFReal x2, PDFReal y2, PDFReal x3, PDFReal y3);
    void operatorBezier13To(PDFReal x1, PDFReal y1, PDFReal x3, PDFReal y3);
    void operatorEndSubpath();
    void operatorRectangle(PDFReal x, PDFReal y, PDFReal width, PDFReal height);

    // Path painting operators
    void operatorPathStroke();
    void operatorPathCloseStroke();
    void operatorPathFillWinding();
    void operatorPathFillEvenOdd();
    void operatorPathFillStrokeWinding();
    void operatorPathFillStrokeEvenOdd();
    void operatorPathCloseFillStrokeWinding();
    void operatorPathCloseFillStrokeEvenOdd();
    void operatorPathClear();

    // Clipping paths:             W, W*
    void operatorClipWinding(); ///< W, modify current clipping path by intersecting it with current path using "Non zero winding number rule"
    void operatorClipEvenOdd(); ///< W*, modify current clipping path by intersecting it with current path using "Even-odd rule"

    // Color:                      CS, cs, SC, SCN, sc, scn, G, g, RG, rg, K, k
    void operatorColorSetStrokingColorSpace(PDFName name);                           ///< CS, set current color space for stroking operations
    void operatorColorSetFillingColorSpace(PDFName name);                            ///< cs, set current color space for filling operations
    void operatorColorSetStrokingColor();                                                   ///< SC, set current stroking color
    void operatorColorSetStrokingColorN();                                                  ///< SCN, same as SC, but also supports Pattern, Separation, DeviceN and ICCBased color spaces
    void operatorColorSetFillingColor();                                                    ///< sc, set current filling color
    void operatorColorSetFillingColorN();                                                   ///< scn, same as sc, but also supports Pattern, Separation, DeviceN and ICCBased color spaces
    void operatorColorSetDeviceGrayStroking(PDFReal gray);                                  ///< G, set DeviceGray color space for stroking color and set color
    void operatorColorSetDeviceGrayFilling(PDFReal gray);                                   ///< g, set DeviceGray color space for filling color and set color
    void operatorColorSetDeviceRGBStroking(PDFReal r, PDFReal g, PDFReal b);                ///< RG, set DeviceRGB color space for stroking color and set color
    void operatorColorSetDeviceRGBFilling(PDFReal r, PDFReal g, PDFReal b);                 ///< rg, set DeviceRGB color space for filling color and set color
    void operatorColorSetDeviceCMYKStroking(PDFReal c, PDFReal m, PDFReal y, PDFReal k);    ///< K, set DeviceCMYK color space for stroking color and set color
    void operatorColorSetDeviceCMYKFilling(PDFReal c, PDFReal m, PDFReal y, PDFReal k);     ///< k, set DeviceCMYK color space for filling color and set color

    const PDFPage* m_page;
    const PDFDocument* m_document;
    const PDFDictionary* m_colorSpaceDictionary;

    // Default color spaces
    PDFColorSpacePointer m_deviceGrayColorSpace;
    PDFColorSpacePointer m_deviceRGBColorSpace;
    PDFColorSpacePointer m_deviceCMYKColorSpace;

    /// Array with current operand arguments
    PDFFlatArray<PDFLexicalAnalyzer::Token, 33> m_operands;

    /// Stack with saved graphic states
    std::stack<PDFPageContentProcessorState> m_stack;

    /// Current graphic state
    PDFPageContentProcessorState m_graphicState;

    /// List of errors
    QList<PDFRenderError> m_errorList;

    /// Current painter path
    QPainterPath m_currentPath;
};

}   // namespace pdf

#endif // PDFPAGECONTENTPROCESSOR_H
