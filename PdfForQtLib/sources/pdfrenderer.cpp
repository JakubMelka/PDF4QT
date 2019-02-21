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
    { "S", PDFPageContentProcessor::Operator::PathStroke },
    { "s", PDFPageContentProcessor::Operator::PathCloseStroke },
    { "f", PDFPageContentProcessor::Operator::PathFillWinding },
    { "F", PDFPageContentProcessor::Operator::PathFillWinding2 },
    { "f*", PDFPageContentProcessor::Operator::PathFillEvenOdd },
    { "B", PDFPageContentProcessor::Operator::PathFillStrokeWinding },
    { "B*", PDFPageContentProcessor::Operator::PathFillStrokeEvenOdd },
    { "b", PDFPageContentProcessor::Operator::PathCloseFillStrokeWinding },
    { "b*", PDFPageContentProcessor::Operator::PathCloseFillStrokeEvenOdd },
    { "n", PDFPageContentProcessor::Operator::PathClear },

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
    m_features(Antialiasing | TextAntialiasing)
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
    m_document(document),
    m_colorSpaceDictionary(nullptr)
{
    Q_ASSERT(page);
    Q_ASSERT(document);

    const PDFObject& resources = m_document->getObject(m_page->getResources());
    if (resources.isDictionary() && resources.getDictionary()->hasKey(COLOR_SPACE_DICTIONARY))
    {
        const PDFObject& colorSpace = m_document->getObject(resources.getDictionary()->get(COLOR_SPACE_DICTIONARY));
        if (colorSpace.isDictionary())
        {
            m_colorSpaceDictionary = colorSpace.getDictionary();
        }
    }
}

QList<PDFRenderError> PDFPageContentProcessor::processContents()
{
    const PDFObject& contents = m_page->getContents();

    // Clear the old errors
    m_errorList.clear();

    // Initialize default color spaces (gray, RGB, CMYK)
    try
    {
        m_deviceGrayColorSpace = PDFAbstractColorSpace::createDeviceColorSpaceByName(m_colorSpaceDictionary, m_document, COLOR_SPACE_NAME_DEVICE_GRAY);
        m_deviceRGBColorSpace = PDFAbstractColorSpace::createDeviceColorSpaceByName(m_colorSpaceDictionary, m_document, COLOR_SPACE_NAME_DEVICE_RGB);
        m_deviceCMYKColorSpace = PDFAbstractColorSpace::createDeviceColorSpaceByName(m_colorSpaceDictionary, m_document, COLOR_SPACE_NAME_DEVICE_CMYK);
    }
    catch (PDFParserException exception)
    {
        m_errorList.append(PDFRenderError(RenderErrorType::Error, exception.getMessage()));

        // Create default color spaces anyway, but do not try to load them...
        m_deviceGrayColorSpace.reset(new PDFDeviceGrayColorSpace);
        m_deviceRGBColorSpace.reset(new PDFDeviceRGBColorSpace);
        m_deviceCMYKColorSpace.reset(new PDFDeviceCMYKColorSpace);
    }

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

    if (!m_stack.empty())
    {
        // Stack is not empty. There was more saves than restores. This is error.
        m_errorList.append(PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Graphic state stack was saved more times, than was restored.")));

        while (!m_stack.empty())
        {
            operatorRestoreGraphicState();
        }
    }

    return m_errorList;
}

void PDFPageContentProcessor::performPathPainting(const QPainterPath& path, bool stroke, bool fill, Qt::FillRule fillRule)
{
    Q_UNUSED(path);
    Q_UNUSED(stroke);
    Q_UNUSED(fill);
    Q_UNUSED(fillRule);
}

void PDFPageContentProcessor::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
    Q_UNUSED(path);
    Q_UNUSED(fillRule);
}

void PDFPageContentProcessor::performUpdateGraphicsState(const PDFPageContentProcessor::PDFPageContentProcessorState& state)
{
    Q_UNUSED(state);
}

void PDFPageContentProcessor::performSaveGraphicState(PDFPageContentProcessor::ProcessOrder order)
{
    Q_UNUSED(order);
}

void PDFPageContentProcessor::performRestoreGraphicState(PDFPageContentProcessor::ProcessOrder order)
{
    Q_UNUSED(order);
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
        case Operator::SetLineWidth:
        {
            invokeOperator(&PDFPageContentProcessor::operatorSetLineWidth);
            break;
        }

        case Operator::SetLineCap:
        {
            invokeOperator(&PDFPageContentProcessor::operatorSetLineCap);
            break;
        }

        case Operator::SetLineJoin:
        {
            invokeOperator(&PDFPageContentProcessor::operatorSetLineJoin);
            break;
        }

        case Operator::SetMitterLimit:
        {
            invokeOperator(&PDFPageContentProcessor::operatorSetMitterLimit);
            break;
        }

        case Operator::SetLineDashPattern:
        {
            invokeOperator(&PDFPageContentProcessor::operatorSetLineDashPattern);
            break;
        }

        case Operator::SetRenderingIntent:
        {
            invokeOperator(&PDFPageContentProcessor::operatorSetRenderingIntent);
            break;
        }

        case Operator::SetFlatness:
        {
            invokeOperator(&PDFPageContentProcessor::operatorSetFlatness);
            break;
        }

        case Operator::SetGraphicState:
        {
            invokeOperator(&PDFPageContentProcessor::operatorSetGraphicState);
            break;
        }

        case Operator::SaveGraphicState:
        {
            operatorSaveGraphicState();
            break;
        }

        case Operator::RestoreGraphicState:
        {
            operatorRestoreGraphicState();
            break;
        }

        case Operator::AdjustCurrentTransformationMatrix:
        {
            invokeOperator(&PDFPageContentProcessor::operatorAdjustCurrentTransformationMatrix);
            break;
        }

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

        case Operator::PathStroke:
        {
            operatorPathStroke();
            break;
        }

        case Operator::PathCloseStroke:
        {
            operatorPathCloseStroke();
            break;
        }

        case Operator::PathFillWinding:
        case Operator::PathFillWinding2:
        {
            operatorPathFillWinding();
            break;
        }

        case Operator::PathFillEvenOdd:
        {
            operatorPathFillEvenOdd();
            break;
        }

        case Operator::PathFillStrokeWinding:
        {
            operatorPathFillStrokeWinding();
            break;
        }

        case Operator::PathFillStrokeEvenOdd:
        {
            operatorPathFillStrokeEvenOdd();
            break;
        }

        case Operator::PathCloseFillStrokeWinding:
        {
            operatorPathCloseFillStrokeWinding();
            break;
        }

        case Operator::PathCloseFillStrokeEvenOdd:
        {
            operatorPathCloseFillStrokeEvenOdd();
            break;
        }

        case Operator::PathClear:
        {
            operatorPathClear();
            break;
        }

        case Operator::ClipEvenOdd:
        {
            operatorClipEvenOdd();
            break;
        }

        case Operator::ClipWinding:
        {
            operatorClipWinding();
            break;
        }

        case Operator::ColorSetStrokingColorSpace:
        {
            // CS, set current color space for stroking operations
            invokeOperator(&PDFPageContentProcessor::operatorColorSetStrokingColorSpace);
            break;
        }

        case Operator::ColorSetFillingColorSpace:
        {
            // cs, set current color space for filling operations
            invokeOperator(&PDFPageContentProcessor::operatorColorSetFillingColorSpace);
            break;
        }

        case Operator::ColorSetStrokingColor:
        {
            // SC, set current stroking color
            operatorColorSetStrokingColor();
            break;
        }

        case Operator::ColorSetStrokingColorN:
        {
            // SCN, same as SC, but also supports Pattern, Separation, DeviceN and ICCBased color spaces
            operatorColorSetStrokingColorN();
            break;
        }

        case Operator::ColorSetFillingColor:
        {
            // sc, set current filling color
            operatorColorSetFillingColor();
            break;
        }

        case Operator::ColorSetFillingColorN:
        {
            // scn, same as sc, but also supports Pattern, Separation, DeviceN and ICCBased color spaces
            operatorColorSetFillingColorN();
            break;
        }

        case Operator::ColorSetDeviceGrayStroking:
        {
            // G, set DeviceGray color space for stroking color and set color
            invokeOperator(&PDFPageContentProcessor::operatorColorSetDeviceGrayStroking);
            break;
        }

        case Operator::ColorSetDeviceGrayFilling:
        {
            // g, set DeviceGray color space for filling color and set color
            invokeOperator(&PDFPageContentProcessor::operatorColorSetDeviceGrayFilling);
            break;
        }

        case Operator::ColorSetDeviceRGBStroking:
        {
            // RG, set DeviceRGB color space for stroking color and set color
            invokeOperator(&PDFPageContentProcessor::operatorColorSetDeviceRGBStroking);
            break;
        }

        case Operator::ColorSetDeviceRGBFilling:
        {
            // rg, set DeviceRGB color space for filling color and set color
            invokeOperator(&PDFPageContentProcessor::operatorColorSetDeviceRGBFilling);
            break;
        }

        case Operator::ColorSetDeviceCMYKStroking:
        {
            // K, set DeviceCMYK color space for stroking color and set color
            invokeOperator(&PDFPageContentProcessor::operatorColorSetDeviceCMYKStroking);
            break;
        }

        case Operator::ColorSetDeviceCMYKFilling:
        {
            // k, set DeviceCMYK color space for filling color and set color
            invokeOperator(&PDFPageContentProcessor::operatorColorSetDeviceCMYKFilling);
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

void PDFPageContentProcessor::updateGraphicState()
{
    if (m_graphicState.getStateFlags())
    {
        performUpdateGraphicsState(m_graphicState);
        m_graphicState.setStateFlags(PDFPageContentProcessorState::StateUnchanged);
    }
}

void PDFPageContentProcessor::operatorSetLineWidth(PDFReal lineWidth)
{
    lineWidth = qMax(0.0, lineWidth);
    m_graphicState.setLineWidth(lineWidth);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorSetLineCap(PDFInteger lineCap)
{
    lineCap = qBound<PDFInteger>(0, lineCap, 2);

    Qt::PenCapStyle penCapStyle = Qt::FlatCap;
    switch (penCapStyle)
    {
        case 0:
        {
            penCapStyle = Qt::FlatCap;
            break;
        }

        case 1:
        {
            penCapStyle = Qt::RoundCap;
            break;
        }

        case 2:
        {
            penCapStyle = Qt::SquareCap;
            break;
        }

        default:
        {
            // This case can't occur, because we are correcting invalid values above.
            Q_ASSERT(false);
            break;
        }
    }

    m_graphicState.setLineCapStyle(penCapStyle);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorSetLineJoin(PDFInteger lineJoin)
{
    lineJoin = qBound<PDFInteger>(0, lineJoin, 2);

    Qt::PenJoinStyle penJoinStyle = Qt::MiterJoin;
    switch (penJoinStyle)
    {
        case 0:
        {
            penJoinStyle = Qt::MiterJoin;
            break;
        }

        case 1:
        {
            penJoinStyle = Qt::RoundJoin;
            break;
        }

        case 2:
        {
            penJoinStyle = Qt::BevelJoin;
            break;
        }

        default:
        {
            // This case can't occur, because we are correcting invalid values above.
            Q_ASSERT(false);
            break;
        }
    }

    m_graphicState.setLineJoinStyle(penJoinStyle);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorSetMitterLimit(PDFReal mitterLimit)
{
    mitterLimit = qMax(0.0, mitterLimit);
    m_graphicState.setMitterLimit(mitterLimit);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorSetLineDashPattern()
{
    // Operand stack must be of this form [ ... numbers ... ] offset. We check it.
    // Minimal number of operands is [] 0.

    if (m_operands.size() < 3)
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid line dash pattern."));
    }

    // Now, we have at least 3 arguments. Check we have an array
    if (m_operands[0].type != PDFLexicalAnalyzer::TokenType::ArrayStart ||
        m_operands[m_operands.size() - 2].type != PDFLexicalAnalyzer::TokenType::ArrayEnd)
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid line dash pattern."));
    }

    const size_t dashArrayStartIndex = 1;
    const size_t dashArrayEndIndex = m_operands.size() - 2;
    const size_t dashOffsetIndex = m_operands.size() - 1;

    std::vector<PDFReal> dashArray;
    dashArray.reserve(dashArrayEndIndex - dashArrayStartIndex);
    for (size_t i = dashArrayStartIndex; i < dashArrayEndIndex; ++i)
    {
        dashArray.push_back(readOperand<PDFReal>(i));
    }

    const PDFReal dashOffset = readOperand<PDFReal>(dashOffsetIndex);
    PDFLineDashPattern pattern(std::move(dashArray), dashOffset);
    m_graphicState.setLineDashPattern(std::move(pattern));
    updateGraphicState();
}

void PDFPageContentProcessor::operatorSetRenderingIntent(PDFName intent)
{
    m_graphicState.setRenderingIntent(intent.name);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorSetFlatness(PDFReal flatness)
{
    flatness = qBound(0.0, flatness, 100.0);
    m_graphicState.setFlatness(flatness);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorSaveGraphicState()
{
    performSaveGraphicState(ProcessOrder::BeforeOperation);
    m_stack.push(m_graphicState);
    m_stack.top().setStateFlags(PDFPageContentProcessorState::StateUnchanged);
    performSaveGraphicState(ProcessOrder::AfterOperation);
}

void PDFPageContentProcessor::operatorRestoreGraphicState()
{
    if (m_stack.empty())
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Trying to restore graphic state more times than it was saved."));
    }

    performRestoreGraphicState(ProcessOrder::BeforeOperation);
    m_graphicState = m_stack.top();
    m_stack.pop();
    updateGraphicState();
    performRestoreGraphicState(ProcessOrder::AfterOperation);
}

void PDFPageContentProcessor::operatorAdjustCurrentTransformationMatrix(PDFReal a, PDFReal b, PDFReal c, PDFReal d, PDFReal e, PDFReal f)
{
    // We will comment following equation:
    //  Adobe PDF Reference 1.7 says, that we have this transformation using coefficient a, b, c, d, e and f:
    //                             [ a, b, 0 ]
    //  [x', y', 1] = [ x, y, 1] * [ c, d, 0 ]
    //                             [ e, f, 1 ]
    // If we transpose this equation (we want this, because Qt uses transposed matrices (QMatrix).
    // So, we will get following result:
    //
    // [ x' ]   [ a, c, e]    [ x ]
    // [ y' ] = [ b, d, f] *  [ y ]
    // [ 1  ]   [ 0, 0, 1]    [ 1 ]
    //
    // So, it is obvious, than we will have following coefficients:
    //  m_11 = a, m_21 = c, dx = e
    //  m_12 = b, m_22 = d, dy = f
    //
    // We must also check, that matrix is invertible. If it is not, then we will throw exception
    // to avoid errors later (for some operations, we assume matrix is invertible).

    QMatrix matrix(a, b, c, d, e, f);
    QMatrix transformMatrix = m_graphicState.getCurrentTransformationMatrix() * matrix;

    if (!transformMatrix.isInvertible())
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Transformation matrix is not invertible."));
    }

    m_graphicState.setCurrentTransformationMatrix(transformMatrix);
    updateGraphicState();
}

template<>
PDFReal PDFPageContentProcessor::readOperand<PDFReal>(size_t index) const
{
    if (index < m_operands.size())
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

template<>
PDFInteger PDFPageContentProcessor::readOperand<PDFInteger>(size_t index) const
{
    if (index < m_operands.size())
    {
        const PDFLexicalAnalyzer::Token& token = m_operands[index];

        switch (token.type)
        {
            case PDFLexicalAnalyzer::TokenType::Integer:
                return token.data.value<PDFInteger>();

            default:
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (integer) on index %1. Operand is of type '%2'.").arg(index + 1).arg(PDFLexicalAnalyzer::getStringFromOperandType(token.type)));
        }
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (integer) on index %1. Only %2 operands provided.").arg(index + 1).arg(m_operands.size()));
    }

    return 0;
}

template<>
PDFPageContentProcessor::PDFName PDFPageContentProcessor::readOperand<PDFPageContentProcessor::PDFName>(size_t index) const
{
    if (index < m_operands.size())
    {
        const PDFLexicalAnalyzer::Token& token = m_operands[index];

        switch (token.type)
        {
            case PDFLexicalAnalyzer::TokenType::Name:
                return PDFName{ token.data.toByteArray() };

            default:
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (name) on index %1. Operand is of type '%2'.").arg(index + 1).arg(PDFLexicalAnalyzer::getStringFromOperandType(token.type)));
        }
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (name) on index %1. Only %2 operands provided.").arg(index + 1).arg(m_operands.size()));
    }

    return PDFName();
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

void PDFPageContentProcessor::operatorPathStroke()
{
    // Do not close the path
    if (!m_currentPath.isEmpty())
    {
        performPathPainting(m_currentPath, true, false, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathCloseStroke()
{
    // Close the path
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.closeSubpath();
        performPathPainting(m_currentPath, true, false, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathFillWinding()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::WindingFill);
        performPathPainting(m_currentPath, false, true, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathFillEvenOdd()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::OddEvenFill);
        performPathPainting(m_currentPath, false, true, Qt::OddEvenFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathFillStrokeWinding()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::WindingFill);
        performPathPainting(m_currentPath, true, true, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathFillStrokeEvenOdd()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::OddEvenFill);
        performPathPainting(m_currentPath, true, true, Qt::OddEvenFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathCloseFillStrokeWinding()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.closeSubpath();
        m_currentPath.setFillRule(Qt::WindingFill);
        performPathPainting(m_currentPath, true, true, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathCloseFillStrokeEvenOdd()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.closeSubpath();
        m_currentPath.setFillRule(Qt::OddEvenFill);
        performPathPainting(m_currentPath, true, true, Qt::OddEvenFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathClear()
{
    m_currentPath = QPainterPath();
}

void PDFPageContentProcessor::operatorClipWinding()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::WindingFill);
        performClipping(m_currentPath, Qt::WindingFill);
    }
}

void PDFPageContentProcessor::operatorClipEvenOdd()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::OddEvenFill);
        performClipping(m_currentPath, Qt::OddEvenFill);
    }
}

void PDFPageContentProcessor::operatorColorSetStrokingColorSpace(PDFPageContentProcessor::PDFName name)
{
    PDFColorSpacePointer colorSpace = PDFAbstractColorSpace::createColorSpace(m_colorSpaceDictionary, m_document, PDFObject::createName(std::make_shared<PDFString>(QByteArray(name.name))));
    if (colorSpace)
    {
        // We must also set default color (it can depend on the color space)
        m_graphicState.setStrokeColorSpace(colorSpace);
        m_graphicState.setStrokeColor(colorSpace->getDefaultColor());
        updateGraphicState();
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid color space."));
    }
}

void PDFPageContentProcessor::operatorColorSetFillingColorSpace(PDFName name)
{
    PDFColorSpacePointer colorSpace = PDFAbstractColorSpace::createColorSpace(m_colorSpaceDictionary, m_document, PDFObject::createName(std::make_shared<PDFString>(QByteArray(name.name))));
    if (colorSpace)
    {
        // We must also set default color (it can depend on the color space)
        m_graphicState.setFillColorSpace(colorSpace);
        m_graphicState.setFillColor(colorSpace->getDefaultColor());
        updateGraphicState();
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid color space."));
    }
}

void PDFPageContentProcessor::operatorColorSetStrokingColor()
{
    const PDFAbstractColorSpace* colorSpace = m_graphicState.getStrokeColorSpace();
    const size_t colorSpaceComponentCount = colorSpace->getColorComponentCount();
    const size_t operandCount = m_operands.size();

    if (operandCount == colorSpaceComponentCount)
    {
        PDFColor color;
        for (size_t i = 0; i < operandCount; ++i)
        {
            color.push_back(readOperand<PDFReal>(i));
        }
        m_graphicState.setStrokeColor(colorSpace->getColor(color));
        updateGraphicState();
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid color component count. Provided %1, required %2.").arg(operandCount).arg(colorSpaceComponentCount));
    }
}

void PDFPageContentProcessor::operatorColorSetStrokingColorN()
{
    // TODO: Implement operator SCN
    throw PDFRendererException(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Not implemented!"));
}

void PDFPageContentProcessor::operatorColorSetFillingColor()
{
    const PDFAbstractColorSpace* colorSpace = m_graphicState.getFillColorSpace();
    const size_t colorSpaceComponentCount = colorSpace->getColorComponentCount();
    const size_t operandCount = m_operands.size();

    if (operandCount == colorSpaceComponentCount)
    {
        PDFColor color;
        for (size_t i = 0; i < operandCount; ++i)
        {
            color.push_back(readOperand<PDFReal>(i));
        }
        m_graphicState.setFillColor(colorSpace->getColor(color));
        updateGraphicState();
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid color component count. Provided %1, required %2.").arg(operandCount).arg(colorSpaceComponentCount));
    }
}

void PDFPageContentProcessor::operatorColorSetFillingColorN()
{
    // TODO: Implement operator scn
    throw PDFRendererException(RenderErrorType::NotImplemented, PDFTranslationContext::tr("Not implemented!"));
}

void PDFPageContentProcessor::operatorColorSetDeviceGrayStroking(PDFReal gray)
{
    m_graphicState.setStrokeColorSpace(m_deviceGrayColorSpace);
    m_graphicState.setStrokeColor(getColorFromColorSpace(m_graphicState.getStrokeColorSpace(), gray));
    updateGraphicState();
}

void PDFPageContentProcessor::operatorColorSetDeviceGrayFilling(PDFReal gray)
{
    m_graphicState.setFillColorSpace(m_deviceGrayColorSpace);
    m_graphicState.setFillColor(getColorFromColorSpace(m_graphicState.getFillColorSpace(), gray));
    updateGraphicState();
}

void PDFPageContentProcessor::operatorColorSetDeviceRGBStroking(PDFReal r, PDFReal g, PDFReal b)
{
    m_graphicState.setStrokeColorSpace(m_deviceRGBColorSpace);
    m_graphicState.setStrokeColor(getColorFromColorSpace(m_graphicState.getStrokeColorSpace(), r, g, b));
    updateGraphicState();
}

void PDFPageContentProcessor::operatorColorSetDeviceRGBFilling(PDFReal r, PDFReal g, PDFReal b)
{
    m_graphicState.setFillColorSpace(m_deviceRGBColorSpace);
    m_graphicState.setFillColor(getColorFromColorSpace(m_graphicState.getFillColorSpace(), r, g, b));
    updateGraphicState();
}

void PDFPageContentProcessor::operatorColorSetDeviceCMYKStroking(PDFReal c, PDFReal m, PDFReal y, PDFReal k)
{
    m_graphicState.setStrokeColorSpace(m_deviceCMYKColorSpace);
    m_graphicState.setStrokeColor(getColorFromColorSpace(m_graphicState.getStrokeColorSpace(), c, m, y, k));
    updateGraphicState();
}

void PDFPageContentProcessor::operatorColorSetDeviceCMYKFilling(PDFReal c, PDFReal m, PDFReal y, PDFReal k)
{
    m_graphicState.setFillColorSpace(m_deviceCMYKColorSpace);
    m_graphicState.setFillColor(getColorFromColorSpace(m_graphicState.getFillColorSpace(), c, m, y, k));
    updateGraphicState();
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
    m_smoothness(0.01),
    m_stateFlags(StateUnchanged)
{
    m_fillColorSpace.reset(new PDFDeviceGrayColorSpace);
    m_strokeColorSpace = m_fillColorSpace;
}

PDFPageContentProcessor::PDFPageContentProcessorState::~PDFPageContentProcessorState()
{

}

PDFPageContentProcessor::PDFPageContentProcessorState& PDFPageContentProcessor::PDFPageContentProcessorState::operator=(const PDFPageContentProcessor::PDFPageContentProcessorState& other)
{
    setCurrentTransformationMatrix(other.getCurrentTransformationMatrix());
    setStrokeColorSpace(other.m_strokeColorSpace);
    setFillColorSpace(other.m_fillColorSpace);
    setStrokeColor(other.getStrokeColor());
    setFillColor(other.getFillColor());
    setLineWidth(other.getLineWidth());
    setLineCapStyle(other.getLineCapStyle());
    setLineJoinStyle(other.getLineJoinStyle());
    setMitterLimit(other.getMitterLimit());
    setLineDashPattern(other.getLineDashPattern());
    setRenderingIntent(other.getRenderingIntent());
    setFlatness(other.getFlatness());
    setSmoothness(other.getSmoothness());
    return *this;
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setCurrentTransformationMatrix(const QMatrix& currentTransformationMatrix)
{
    if (m_currentTransformationMatrix != currentTransformationMatrix)
    {
        m_currentTransformationMatrix = currentTransformationMatrix;
        m_stateFlags |= StateCurrentTransformationMatrix;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setStrokeColorSpace(const QSharedPointer<PDFAbstractColorSpace>& strokeColorSpace)
{
    if (m_strokeColorSpace != strokeColorSpace)
    {
        m_strokeColorSpace = strokeColorSpace;
        m_stateFlags |= StateStrokeColorSpace;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setFillColorSpace(const QSharedPointer<PDFAbstractColorSpace>& fillColorSpace)
{
    if (m_fillColorSpace != fillColorSpace)
    {
        m_fillColorSpace = fillColorSpace;
        m_stateFlags |= StateFillColorSpace;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setStrokeColor(const QColor& strokeColor)
{
    if (m_strokeColor != strokeColor)
    {
        m_strokeColor = strokeColor;
        m_stateFlags |= StateStrokeColor;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setFillColor(const QColor& fillColor)
{
    if (m_fillColor != fillColor)
    {
        m_fillColor = fillColor;
        m_stateFlags |= StateFillColor;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setLineWidth(PDFReal lineWidth)
{
    if (m_lineWidth != lineWidth)
    {
        m_lineWidth = lineWidth;
        m_stateFlags |= StateLineWidth;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setLineCapStyle(Qt::PenCapStyle lineCapStyle)
{
    if (m_lineCapStyle != lineCapStyle)
    {
        m_lineCapStyle = lineCapStyle;
        m_stateFlags |= StateLineCapStyle;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setLineJoinStyle(Qt::PenJoinStyle lineJoinStyle)
{
    if (m_lineJoinStyle != lineJoinStyle)
    {
        m_lineJoinStyle = lineJoinStyle;
        m_stateFlags |= StateLineJoinStyle;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setMitterLimit(const PDFReal& mitterLimit)
{
    if (m_mitterLimit != mitterLimit)
    {
        m_mitterLimit = mitterLimit;
        m_stateFlags |= StateMitterLimit;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setLineDashPattern(PDFLineDashPattern pattern)
{
    if (m_lineDashPattern != pattern)
    {
        m_lineDashPattern = std::move(pattern);
        m_stateFlags |= StateLineDashPattern;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setRenderingIntent(const QByteArray& renderingIntent)
{
    if (m_renderingIntent != renderingIntent)
    {
        m_renderingIntent = renderingIntent;
        m_stateFlags |= StateRenderingIntent;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setFlatness(PDFReal flatness)
{
    if (m_flatness != flatness)
    {
        m_flatness = flatness;
        m_stateFlags |= StateFlatness;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setSmoothness(PDFReal smoothness)
{
    if (m_smoothness != smoothness)
    {
        m_smoothness = smoothness;
        m_stateFlags |= StateSmoothness;
    }
}

}   // namespace pdf
