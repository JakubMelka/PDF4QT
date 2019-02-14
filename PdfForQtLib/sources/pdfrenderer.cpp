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

#include "pdfrenderer.h"
#include "pdfrenderer_impl.h"
#include "pdfdocument.h"

namespace pdf
{

// Graphic state operators - mapping from PDF name to the enum, splitted into groups.
// Please see Table 4.1 in PDF Reference 1.7, chapter 4.1 - Graphic Objects.
//
//  General graphic state:      w, J, j, M, d, ri, i, gs
//  Special graphic state:      q, Q, cm
//  Path construction:          m, l, c, v, y, h, re
//  Path painting:              S, s, F, f, f*, B, B*, b, b*, n
//  Clipping paths:             W, W*
//  Text object:                BT, ET
//  Text state:                 Tc, Tw, Tz, TL, Tf, Tr, Ts
//  Text positioning:           Td, TD, Tm, T*
//  Text showing:               Tj, TJ, ', "
//  Type 3 font:                d0, d1
//  Color:                      CS, cs, SC, SCN, sc, scn, G, g, RG, rg, K, k
//  Shading pattern:            sh
//  Inline images:              BI, ID, EI
//  XObject:                    Do
//  Marked content:             MP, DP, BMC, BDC, EMC
//  Compatibility:              BX, EX

static constexpr const std::pair<const char*, PDFPageContentProcessor::Operator> operators[] =
{
    // General graphic state        w, J, j, M, d, ri, i, gs
    { "w", PDFPageContentProcessor::Operator::SetLineWidth },
    { "J", PDFPageContentProcessor::Operator::SetLineCap },
    { "j", PDFPageContentProcessor::Operator::SetLineJoin },
    { "M", PDFPageContentProcessor::Operator::SetMitterLimit },
    { "d", PDFPageContentProcessor::Operator::SetLineDashPattern },
    { "ri", PDFPageContentProcessor::Operator::SetRenderingIntent },
    { "i", PDFPageContentProcessor::Operator::SetFlatness },
    { "gs", PDFPageContentProcessor::Operator::SetGraphicState },

    // Special graphic state:       q, Q, cm
    { "q", PDFPageContentProcessor::Operator::SaveGraphicState },
    { "Q", PDFPageContentProcessor::Operator::RestoreGraphicState },
    { "cm", PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix },

    // Path construction:           m, l, c, v, y, h, re
    { "m", PDFPageContentProcessor::Operator::MoveCurrentPoint },
    { "l", PDFPageContentProcessor::Operator::LineTo },
    { "c", PDFPageContentProcessor::Operator::Bezier123To },
    { "v", PDFPageContentProcessor::Operator::Bezier23To },
    { "y", PDFPageContentProcessor::Operator::Bezier13To },
    { "h", PDFPageContentProcessor::Operator::EndSubpath },
    { "re", PDFPageContentProcessor::Operator::Rectangle },

    // Path painting:               S, s, f, F, f*, B, B*, b, b*, n
    { "S", PDFPageContentProcessor::Operator::StrokePath },
    { "s", PDFPageContentProcessor::Operator::CloseAndStrokePath },
    { "f", PDFPageContentProcessor::Operator::FillPathWinding },
    { "F", PDFPageContentProcessor::Operator::FillPathWinding2 },
    { "f*", PDFPageContentProcessor::Operator::FillPathEvenOdd },
    { "B", PDFPageContentProcessor::Operator::StrokeAndFillWinding },
    { "B*", PDFPageContentProcessor::Operator::StrokeAndFillEvenOdd },
    { "b", PDFPageContentProcessor::Operator::CloseAndStrokeAndFillWinding },
    { "b*", PDFPageContentProcessor::Operator::CloseAndStrokeAndFillEvenOdd },
    { "n", PDFPageContentProcessor::Operator::ClearPath },

    // Clipping paths:             W, W*
    { "W", PDFPageContentProcessor::Operator::ClipWinding },
    { "W*", PDFPageContentProcessor::Operator::ClipEvenOdd },

    // Text object:                BT, ET
    { "BT", PDFPageContentProcessor::Operator::TextBegin },
    { "ET", PDFPageContentProcessor::Operator::TextEnd },

    // Text state:                 Tc, Tw, Tz, TL, Tf, Tr, Ts
    { "Tc", PDFPageContentProcessor::Operator::TextSetCharacterSpacing },
    { "Tw", PDFPageContentProcessor::Operator::TextSetWordSpacing },
    { "Tz", PDFPageContentProcessor::Operator::TextSetHorizontalScale },
    { "TL", PDFPageContentProcessor::Operator::TextSetLeading },
    { "Tf", PDFPageContentProcessor::Operator::TextSetFontAndFontSize },
    { "Tr", PDFPageContentProcessor::Operator::TextSetRenderMode },
    { "Ts", PDFPageContentProcessor::Operator::TextSetRise },

    // Text positioning:           Td, TD, Tm, T*
    { "Td", PDFPageContentProcessor::Operator::TextMoveByOffset },
    { "TD", PDFPageContentProcessor::Operator::TextSetLeadingAndMoveByOffset },
    { "Tm", PDFPageContentProcessor::Operator::TextSetMatrix },
    { "T*", PDFPageContentProcessor::Operator::TextMoveByLeading },

    // Text showing:               Tj, TJ, ', "
    { "Tj", PDFPageContentProcessor::Operator::TextShowTextString },
    { "TJ", PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing },
    { "'", PDFPageContentProcessor::Operator::TextNextLineShowText },
    { "\"", PDFPageContentProcessor::Operator::TextSetSpacingAndShowText },

    // Type 3 font:                d0, d1
    { "d0", PDFPageContentProcessor::Operator::Type3FontSetOffset },
    { "d1", PDFPageContentProcessor::Operator::Type3FontSetOffsetAndBB },

    // Color:                      CS, cs, SC, SCN, sc, scn, G, g, RG, rg, K, k
    { "CS", PDFPageContentProcessor::Operator::ColorSetStrokingColorSpace },
    { "cs", PDFPageContentProcessor::Operator::ColorSetFillingColorSpace },
    { "SC", PDFPageContentProcessor::Operator::ColorSetStrokingColor },
    { "SCN", PDFPageContentProcessor::Operator::ColorSetStrokingColorN },
    { "sc", PDFPageContentProcessor::Operator::ColorSetFillingColor },
    { "scn", PDFPageContentProcessor::Operator::ColorSetFillingColorN },
    { "G", PDFPageContentProcessor::Operator::ColorSetDeviceGrayStroking },
    { "g", PDFPageContentProcessor::Operator::ColorSetDeviceGrayFilling },
    { "RG", PDFPageContentProcessor::Operator::ColorSetDeviceRGBStroking },
    { "rg", PDFPageContentProcessor::Operator::ColorSetDeviceRGBFilling },
    { "K", PDFPageContentProcessor::Operator::ColorSetDeviceCMYKStroking },
    { "k", PDFPageContentProcessor::Operator::ColorSetDeviceCMYKFilling },

    // Shading pattern:            sh
    { "sh", PDFPageContentProcessor::Operator::ShadingPaintShape },

    // Inline images:              BI, ID, EI
    { "BI", PDFPageContentProcessor::Operator::InlineImageBegin },
    { "ID", PDFPageContentProcessor::Operator::InlineImageData },
    { "EI", PDFPageContentProcessor::Operator::InlineImageEnd },

    // XObject:                    Do
    { "Do", PDFPageContentProcessor::Operator::PaintXObject },

    // Marked content:             MP, DP, BMC, BDC, EMC
    { "MP", PDFPageContentProcessor::Operator::MarkedContentPoint },
    { "DP", PDFPageContentProcessor::Operator::MarkedContentPointWithProperties },
    { "BMC", PDFPageContentProcessor::Operator::MarkedContentBegin },
    { "BDC", PDFPageContentProcessor::Operator::MarkedContentBeginWithProperties },
    { "EMC", PDFPageContentProcessor::Operator::MarkedContentEnd },

    // Compatibility:              BX, EX
    { "BX", PDFPageContentProcessor::Operator::CompatibilityBegin },
    { "EX", PDFPageContentProcessor::Operator::CompatibilityEnd }
};

PDFRenderer::PDFRenderer(const PDFDocument* document) :
    m_document(document),
    m_features(Antialasing | TextAntialiasing)
{
    Q_ASSERT(document);
}

QList<PDFRenderError> PDFRenderer::render(QPainter* painter, const QRectF& rectangle, size_t pageIndex) const
{
    Q_UNUSED(painter);
    Q_UNUSED(rectangle);

    const PDFCatalog* catalog = m_document->getCatalog();
    if (pageIndex >= catalog->getPageCount() || !catalog->getPage(pageIndex))
    {
        // Invalid page index
        return { PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Page %1 doesn't exist.").arg(pageIndex + 1)) };
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    Q_ASSERT(page);

    PDFPageContentProcessor processor(page, m_document);
    return processor.processContents();
}

PDFPageContentProcessor::PDFPageContentProcessor(const PDFPage* page, const PDFDocument* document) :
    m_page(page),
    m_document(document)
{
    Q_ASSERT(page);
    Q_ASSERT(document);
}

QList<PDFRenderError> PDFPageContentProcessor::processContents()
{
    const PDFObject& contents = m_page->getContents();

    // Clear the old errors
    m_errorList.clear();

    if (contents.isArray())
    {
        const PDFArray* array = contents.getArray();
        const size_t count = array->getCount();

        for (size_t i = 0; i < count; ++i)
        {
            const PDFObject& streamObject = m_document->getObject(array->getItem(i));
            if (streamObject.isStream())
            {
                processContentStream(streamObject.getStream());
            }
            else
            {
                m_errorList.append(PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Invalid page contents.")));
            }
        }
    }
    else if (contents.isStream())
    {
        processContentStream(contents.getStream());
    }
    else
    {
        m_errorList.append(PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Invalid page contents.")));
    }

    return m_errorList;
}

void PDFPageContentProcessor::processContentStream(const PDFStream* stream)
{
    QByteArray content = m_document->getDecodedStream(stream);

    PDFLexicalAnalyzer parser(content.constBegin(), content.constEnd());

    while (!parser.isAtEnd())
    {
        try
        {
            PDFLexicalAnalyzer::Token token = parser.fetch();
            switch (token.type)
            {
                case PDFLexicalAnalyzer::TokenType::Command:
                {
                    // Process the command, then clear the operand stack
                    processCommand(token.data.toByteArray());
                    m_operands.clear();
                    break;
                }

                case PDFLexicalAnalyzer::TokenType::EndOfFile:
                {
                    // Do nothing, just break, we are at the end
                    break;
                }

                default:
                {
                    // Push the operand onto the operand stack
                    m_operands.push_back(std::move(token));
                    break;
                }
            }
        }
        catch (PDFParserException exception)
        {
            m_errorList.append(PDFRenderError(RenderErrorType::Error, exception.getMessage()));
        }
        catch (PDFRendererException exception)
        {
            m_errorList.append(exception.getError());
        }
    }
}

void PDFPageContentProcessor::processCommand(const QByteArray& command)
{
    Operator op = Operator::Invalid;

    // Find the command in the command array
    for (const std::pair<const char*, PDFPageContentProcessor::Operator>& operatorDescriptor : operators)
    {
        if (command == operatorDescriptor.first)
        {
            op = operatorDescriptor.second;
            break;
        }
    }

    switch (op)
    {
        case Operator::MoveCurrentPoint:
        {
            invokeOperator(&PDFPageContentProcessor::operatorMoveCurrentPoint);
            break;
        }

        case Operator::LineTo:
        {
            invokeOperator(&PDFPageContentProcessor::operatorLineTo);
            break;
        }

        case Operator::Bezier123To:
        {
            invokeOperator(&PDFPageContentProcessor::operatorBezier123To);
            break;
        }

        case Operator::Bezier23To:
        {
            invokeOperator(&PDFPageContentProcessor::operatorBezier23To);
            break;
        }

        case Operator::Bezier13To:
        {
            invokeOperator(&PDFPageContentProcessor::operatorBezier13To);
            break;
        }
        case Operator::EndSubpath:
        {
            // Call directly, no parameters involved
            operatorEndSubpath();
            break;
        }

        case Operator::Rectangle:
        {
            invokeOperator(&PDFPageContentProcessor::operatorRectangle);
            break;
        }

        case Operator::Invalid:
        {
            m_errorList.append(PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Unknown operator '%1'.").arg(QString::fromLatin1(command))));
            break;
        }

        default:
        {
            m_errorList.append(PDFRenderError(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Not implemented operator '%1'.").arg(QString::fromLatin1(command))));
            break;
        }
    }
}

QPointF PDFPageContentProcessor::getCurrentPoint() const
{
    const int elementCount = m_currentPath.elementCount();
    if (elementCount > 0)
    {
        QPainterPath::Element element = m_currentPath.elementAt(elementCount - 1);
        return QPointF(element.x, element.y);
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Current point of path is not set. Path is empty."));
    }

    return QPointF();
}

template<>
PDFReal PDFPageContentProcessor::readOperand<PDFReal>(size_t index) const
{
    if (m_operands.size() < index)
    {
        const PDFLexicalAnalyzer::Token& token = m_operands[index];

        switch (token.type)
        {
            case PDFLexicalAnalyzer::TokenType::Real:
            case PDFLexicalAnalyzer::TokenType::Integer:
                return token.data.value<PDFReal>();

            default:
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (real number) on index %1. Operand is of type '%2'.").arg(index + 1).arg(PDFLexicalAnalyzer::getStringFromOperandType(token.type)));
        }
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (real number) on index %1. Only %2 operands provided.").arg(index + 1).arg(m_operands.size()));
    }

    return 0.0;
}

void PDFPageContentProcessor::operatorMoveCurrentPoint(PDFReal x, PDFReal y)
{
    m_currentPath.moveTo(x, y);;
}

void PDFPageContentProcessor::operatorLineTo(PDFReal x, PDFReal y)
{
    m_currentPath.lineTo(x, y);
}

void PDFPageContentProcessor::operatorBezier123To(PDFReal x1, PDFReal y1, PDFReal x2, PDFReal y2, PDFReal x3, PDFReal y3)
{
    m_currentPath.cubicTo(x1, y1, x2, y2, x3, y3);
}

void PDFPageContentProcessor::operatorBezier23To(PDFReal x2, PDFReal y2, PDFReal x3, PDFReal y3)
{
    QPointF currentPoint = getCurrentPoint();
    m_currentPath.cubicTo(currentPoint.x(), currentPoint.y(), x2, y2, x3, y3);
}

void PDFPageContentProcessor::operatorBezier13To(PDFReal x1, PDFReal y1, PDFReal x3, PDFReal y3)
{
    m_currentPath.cubicTo(x1, y1, x3, y3, x3, y3);
}

void PDFPageContentProcessor::operatorEndSubpath()
{
    m_currentPath.closeSubpath();
}

void PDFPageContentProcessor::operatorRectangle(PDFReal x, PDFReal y, PDFReal width, PDFReal height)
{
    if (width < 0 || height < 0)
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid size of rectangle (%1 x %2).").arg(width).arg(height));
    }

    m_currentPath.addRect(QRectF(x, y, width, height));
}

PDFPageContentProcessor::PDFPageContentProcessorState::PDFPageContentProcessorState() :
    m_currentTransformationMatrix(),
    m_fillColorSpace(),
    m_strokeColorSpace(),
    m_fillColor(Qt::black),
    m_strokeColor(Qt::black),
    m_lineWidth(1.0),
    m_lineCapStyle(Qt::FlatCap),
    m_lineJoinStyle(Qt::MiterJoin),
    m_mitterLimit(10.0),
    m_renderingIntent(),
    m_flatness(1.0),
    m_smoothness(0.01)
{
    m_fillColorSpace.reset(new PDFDeviceGrayColorSpace);
    m_strokeColorSpace = m_fillColorSpace;
}

PDFPageContentProcessor::PDFPageContentProcessorState::~PDFPageContentProcessorState()
{

}

}   // namespace pdf
