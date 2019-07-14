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

#include "pdfpagecontentprocessor.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfimage.h"

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

void PDFPageContentProcessor::initDictionaries(const PDFObject& resourcesObject)
{
    auto getDictionary = [this, &resourcesObject](const char* resourceName) -> const pdf::PDFDictionary*
    {
        const PDFObject& resources = m_document->getObject(resourcesObject);
        if (resources.isDictionary() && resources.getDictionary()->hasKey(resourceName))
        {
            const PDFObject& resourceDictionary = m_document->getObject(resources.getDictionary()->get(resourceName));
            if (resourceDictionary.isDictionary())
            {
                return resourceDictionary.getDictionary();
            }
        }

        return nullptr;
    };

    m_colorSpaceDictionary = getDictionary(COLOR_SPACE_DICTIONARY);
    m_fontDictionary = getDictionary("Font");
    m_xobjectDictionary = getDictionary("XObject");
    m_extendedGraphicStateDictionary = getDictionary(PDF_RESOURCE_EXTGSTATE);
    m_propertiesDictionary = getDictionary("Properties");
}

PDFPageContentProcessor::PDFPageContentProcessor(const PDFPage* page,
                                                 const PDFDocument* document,
                                                 const PDFFontCache* fontCache,
                                                 const PDFOptionalContentActivity* optionalContentActivity) :
    m_page(page),
    m_document(document),
    m_fontCache(fontCache),
    m_optionalContentActivity(optionalContentActivity),
    m_colorSpaceDictionary(nullptr),
    m_fontDictionary(nullptr),
    m_xobjectDictionary(nullptr),
    m_extendedGraphicStateDictionary(nullptr),
    m_propertiesDictionary(nullptr),
    m_textBeginEndState(0),
    m_compatibilityBeginEndState(0)
{
    Q_ASSERT(page);
    Q_ASSERT(document);

    initDictionaries(m_page->getResources());
}

PDFPageContentProcessor::~PDFPageContentProcessor()
{

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

    m_graphicState.setStrokeColorSpace(m_deviceGrayColorSpace);
    m_graphicState.setStrokeColor(m_deviceGrayColorSpace->getDefaultColor());
    m_graphicState.setFillColorSpace(m_deviceGrayColorSpace);
    m_graphicState.setFillColor(m_deviceGrayColorSpace->getDefaultColor());
    m_graphicState.setStateFlags(PDFPageContentProcessorState::StateAll);
    updateGraphicState();

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

void PDFPageContentProcessor::reportRenderError(RenderErrorType type, QString message)
{
    m_errorList.append(PDFRenderError(type, qMove(message)));
}

void PDFPageContentProcessor::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
    Q_UNUSED(path);
    Q_UNUSED(stroke);
    Q_UNUSED(fill);
    Q_UNUSED(text);
    Q_UNUSED(fillRule);
}

void PDFPageContentProcessor::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
    Q_UNUSED(path);
    Q_UNUSED(fillRule);
}

void PDFPageContentProcessor::performImagePainting(const QImage& image)
{
    Q_UNUSED(image);
}

void PDFPageContentProcessor::performUpdateGraphicsState(const PDFPageContentProcessorState& state)
{
    if (state.getStateFlags().testFlag(PDFPageContentProcessorState::StateTextFont) ||
        state.getStateFlags().testFlag(PDFPageContentProcessorState::StateTextFontSize))
    {
        m_realizedFont.dirty();
    }
}

void PDFPageContentProcessor::performSaveGraphicState(ProcessOrder order)
{
    Q_UNUSED(order);
}

void PDFPageContentProcessor::performRestoreGraphicState(ProcessOrder order)
{
    Q_UNUSED(order);
}

void PDFPageContentProcessor::performMarkedContentPoint(const QByteArray& tag, const PDFObject& properties)
{
    Q_UNUSED(tag);
    Q_UNUSED(properties);
}

void PDFPageContentProcessor::performMarkedContentBegin(const QByteArray& tag, const PDFObject& properties)
{
    Q_UNUSED(tag);
    Q_UNUSED(properties);
}

void PDFPageContentProcessor::performMarkedContentEnd()
{

}

bool PDFPageContentProcessor::isContentSuppressed() const
{
    return std::any_of(m_markedContentStack.cbegin(), m_markedContentStack.cend(), [](const MarkedContentState& state) { return state.contentSuppressed; });
}

void PDFPageContentProcessor::processContent(const QByteArray& content)
{
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
            m_operands.clear();
            m_errorList.append(PDFRenderError(RenderErrorType::Error, exception.getMessage()));
        }
        catch (PDFRendererException exception)
        {
            m_operands.clear();
            m_errorList.append(exception.getError());
        }
    }
}

void PDFPageContentProcessor::processContentStream(const PDFStream* stream)
{
    QByteArray content = m_document->getDecodedStream(stream);

    processContent(content);
}

void PDFPageContentProcessor::processForm(const QMatrix& matrix, const QRectF& boundingBox, const PDFObject& resources, const QByteArray& content)
{
    PDFPageContentProcessorStateGuard guard(this);

    QMatrix formMatrix = matrix * m_graphicState.getCurrentTransformationMatrix();
    m_graphicState.setCurrentTransformationMatrix(formMatrix);
    updateGraphicState();

    // If the clipping box is valid, then use clipping. Clipping box is in the form coordinate system
    if (boundingBox.isValid())
    {
        QPainterPath path;
        path.addRect(boundingBox);
        performClipping(path, path.fillRule());
    }

    // Initialize the resources, if we have them
    if (!resources.isNull())
    {
        initDictionaries(resources);
    }

    processContent(content);
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

        case Operator::Type3FontSetOffset:
        {
            // d0, set width information, see PDF 1.7 Reference, Table 5.10
            invokeOperator(&PDFPageContentProcessor::operatorType3FontSetOffset);
            break;
        }

        case Operator::Type3FontSetOffsetAndBB:
        {
            // d1, set offset and glyph bounding box
            invokeOperator(&PDFPageContentProcessor::operatorType3FontSetOffsetAndBB);
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

        case Operator::TextBegin:
        {
            // BT, begin text object, initialize text matrices, cannot be nested
            operatorTextBegin();
            break;
        }

        case Operator::TextEnd:
        {
            // ET, end text object, cannot be nested
            operatorTextEnd();
            break;
        }

        case Operator::TextSetCharacterSpacing:
        {
            // Tc, set text character spacing
            invokeOperator(&PDFPageContentProcessor::operatorTextSetCharacterSpacing);
            break;
        }

        case Operator::TextSetWordSpacing:
        {
            // Tw, set text word spacing
            invokeOperator(&PDFPageContentProcessor::operatorTextSetWordSpacing);
            break;
        }

        case Operator::TextSetHorizontalScale:
        {
            // Tz, set text horizontal scaling (in percents, 100% = normal scaling)
            invokeOperator(&PDFPageContentProcessor::operatorTextSetHorizontalScale);
            break;
        }

        case Operator::TextSetLeading:
        {
            // TL, set text leading
            invokeOperator(&PDFPageContentProcessor::operatorTextSetLeading);
            break;
        }

        case Operator::TextSetFontAndFontSize:
        {
            // Tf, set text font (name from dictionary) and its size
            invokeOperator(&PDFPageContentProcessor::operatorTextSetFontAndFontSize);
            break;
        }

        case Operator::TextSetRenderMode:
        {
            // Tr, set text render mode
            invokeOperator(&PDFPageContentProcessor::operatorTextSetRenderMode);
            break;
        }

        case Operator::TextSetRise:
        {
            // Ts, set text rise
            invokeOperator(&PDFPageContentProcessor::operatorTextSetRise);
            break;
        }

        case Operator::TextMoveByOffset:
        {
            // Td, move by offset
            invokeOperator(&PDFPageContentProcessor::operatorTextMoveByOffset);
            break;
        }

        case Operator::TextSetLeadingAndMoveByOffset:
        {
            // TD, sets text leading and moves by offset, x y TD is equivalent to sequence -y TL x y Td
            invokeOperator(&PDFPageContentProcessor::operatorTextSetLeadingAndMoveByOffset);
            break;
        }

        case Operator::TextSetMatrix:
        {
            // Tm, set text matrix
            invokeOperator(&PDFPageContentProcessor::operatorTextSetMatrix);
            break;
        }

        case Operator::TextMoveByLeading:
        {
            // T*, moves text by leading, equivalent to 0 leading Td
            operatorTextMoveByLeading();
            break;
        }

        case Operator::TextShowTextString:
        {
            // Tj, show text string
            invokeOperator(&PDFPageContentProcessor::operatorTextShowTextString);
            break;
        }

        case Operator::TextShowTextIndividualSpacing:
        {
            // TJ, show text, allow individual text spacing
            invokeOperator(&PDFPageContentProcessor::operatorTextShowTextIndividualSpacing);
            break;
        }

        case Operator::TextNextLineShowText:
        {
            // ', move to the next line and show text ("string '" is equivalent to "T* string Tj")
            invokeOperator(&PDFPageContentProcessor::operatorTextNextLineShowText);
            break;
        }

        case Operator::TextSetSpacingAndShowText:
        {
            // ", move to the next line, set spacing and show text (equivalent to sequence "w1 Tw w2 Tc string '")
            invokeOperator(&PDFPageContentProcessor::operatorTextSetSpacingAndShowText);
            break;
        }

        case Operator::PaintXObject:
        {
            // Do, paint the X Object (image, form, ...)
            invokeOperator(&PDFPageContentProcessor::operatorPaintXObject);
            break;
        }

        case Operator::MarkedContentPoint:
        {
            // MP, marked content point
            invokeOperator(&PDFPageContentProcessor::operatorMarkedContentPoint);
            break;
        }

        case Operator::MarkedContentPointWithProperties:
        {
            // DP, marked content point with properties
            operatorMarkedContentPointWithProperties(readOperand<PDFOperandName>(0), readObjectFromOperandStack(1));
            break;
        }

        case Operator::MarkedContentBegin:
        {
            // BMC, begin of sequence of marked content
            invokeOperator(&PDFPageContentProcessor::operatorMarkedContentBegin);
            break;
        }

        case Operator::MarkedContentBeginWithProperties:
        {
            // BDC, begin of sequence of marked content with properties
            operatorMarkedContentBeginWithProperties(readOperand<PDFOperandName>(0), readObjectFromOperandStack(1));
            break;
        }

        case Operator::MarkedContentEnd:
        {
            // EMC, end of marked content sequence
            operatorMarkedContentEnd();
            break;
        }

        case Operator::CompatibilityBegin:
        {
            operatorCompatibilityBegin();
            break;
        }

        case Operator::CompatibilityEnd:
        {
            operatorCompatibilityEnd();
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

Qt::PenCapStyle PDFPageContentProcessor::convertLineCapToPenCapStyle(PDFInteger lineCap)
{
    lineCap = qBound<PDFInteger>(0, lineCap, 2);

    Qt::PenCapStyle penCapStyle = Qt::FlatCap;
    switch (lineCap)
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

    return penCapStyle;
}

PDFInteger PDFPageContentProcessor::convertPenCapStyleToLineCap(Qt::PenCapStyle penCapStyle)
{
    switch (penCapStyle)
    {
        case Qt::FlatCap:
            return 0;
        case Qt::SquareCap:
            return 2;
        case Qt::RoundCap:
            return 1;

        default:
            break;
    }

    // Invalid pen cap style occured
    Q_ASSERT(false);
    return 0;
}

void PDFPageContentProcessor::operatorSetLineCap(PDFInteger lineCap)
{
    const Qt::PenCapStyle penCapStyle = convertLineCapToPenCapStyle(lineCap);
    m_graphicState.setLineCapStyle(penCapStyle);
    updateGraphicState();
}

Qt::PenJoinStyle PDFPageContentProcessor::convertLineJoinToPenJoinStyle(PDFInteger lineJoin)
{
    lineJoin = qBound<PDFInteger>(0, lineJoin, 2);

    Qt::PenJoinStyle penJoinStyle = Qt::MiterJoin;
    switch (lineJoin)
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

    return penJoinStyle;
}

PDFInteger PDFPageContentProcessor::convertPenJoinStyleToLineJoin(Qt::PenJoinStyle penJoinStyle)
{
    switch (penJoinStyle)
    {
        case Qt::MiterJoin:
            return 0;
        case Qt::BevelJoin:
            return 2;
        case Qt::RoundJoin:
            return 1;

        default:
            break;
    }

    // Invalid pen join style occured
    Q_ASSERT(false);
    return 0;
}

void PDFPageContentProcessor::operatorSetLineJoin(PDFInteger lineJoin)
{
    const Qt::PenJoinStyle penJoinStyle = convertLineJoinToPenJoinStyle(lineJoin);
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

void PDFPageContentProcessor::operatorSetRenderingIntent(PDFOperandName intent)
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

void PDFPageContentProcessor::operatorSetGraphicState(PDFOperandName dictionaryName)
{
    if (m_extendedGraphicStateDictionary)
    {
        if (m_extendedGraphicStateDictionary->hasKey(dictionaryName.name))
        {
            const PDFObject& graphicStateObject = m_document->getObject(m_extendedGraphicStateDictionary->get(dictionaryName.name));
            if (graphicStateObject.isDictionary())
            {
                const PDFDictionary* graphicStateDictionary = graphicStateObject.getDictionary();

                PDFDocumentDataLoaderDecorator loader(m_document);
                const PDFReal lineWidth = loader.readNumberFromDictionary(graphicStateDictionary, "LW", m_graphicState.getLineWidth());
                const Qt::PenCapStyle penCapStyle = convertLineCapToPenCapStyle(loader.readNumberFromDictionary(graphicStateDictionary, "LC", convertPenCapStyleToLineCap(m_graphicState.getLineCapStyle())));
                const Qt::PenJoinStyle penJoinStyle = convertLineJoinToPenJoinStyle(loader.readNumberFromDictionary(graphicStateDictionary, "LJ", convertPenJoinStyleToLineJoin(m_graphicState.getLineJoinStyle())));
                const PDFReal mitterLimit = loader.readNumberFromDictionary(graphicStateDictionary, "MT", m_graphicState.getMitterLimit());

                const PDFObject& lineDashPatternObject = m_document->getObject(graphicStateDictionary->get("D"));
                if (lineDashPatternObject.isArray())
                {
                    const PDFArray* lineDashPatternDefinitionArray = lineDashPatternObject.getArray();
                    if (lineDashPatternDefinitionArray->getCount() == 2)
                    {
                        PDFLineDashPattern pattern(loader.readNumberArray(lineDashPatternDefinitionArray->getItem(0)), loader.readNumber(lineDashPatternDefinitionArray->getItem(1), 0.0));
                        m_graphicState.setLineDashPattern(pattern);
                    }
                }

                const PDFObject& renderingIntentObject = m_document->getObject(graphicStateDictionary->get("RI"));
                if (renderingIntentObject.isName())
                {
                    m_graphicState.setRenderingIntent(renderingIntentObject.getString());
                }

                const PDFReal flatness = loader.readNumberFromDictionary(graphicStateDictionary, "FL", m_graphicState.getFlatness());
                const PDFReal smoothness = loader.readNumberFromDictionary(graphicStateDictionary, "SM", m_graphicState.getSmoothness());
                const bool textKnockout = loader.readBooleanFromDictionary(graphicStateDictionary, "TK", m_graphicState.getTextKnockout());

                m_graphicState.setLineWidth(lineWidth);
                m_graphicState.setLineCapStyle(penCapStyle);
                m_graphicState.setLineJoinStyle(penJoinStyle);
                m_graphicState.setMitterLimit(mitterLimit);
                m_graphicState.setFlatness(flatness);
                m_graphicState.setSmoothness(smoothness);
                m_graphicState.setTextKnockout(textKnockout);
                updateGraphicState();
            }
            else
            {
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Graphic state '%1' found, but invalid in resource dictionary.").arg(QString::fromLatin1(dictionaryName.name)));
            }
        }
        else
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Graphic state '%1' not found in resource dictionary.").arg(QString::fromLatin1(dictionaryName.name)));
        }
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid graphic state resource dictionary."));
    }
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
    QMatrix transformMatrix = matrix * m_graphicState.getCurrentTransformationMatrix();

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
PDFPageContentProcessor::PDFOperandName PDFPageContentProcessor::readOperand<PDFPageContentProcessor::PDFOperandName>(size_t index) const
{
    if (index < m_operands.size())
    {
        const PDFLexicalAnalyzer::Token& token = m_operands[index];

        switch (token.type)
        {
            case PDFLexicalAnalyzer::TokenType::Name:
                return PDFOperandName{ token.data.toByteArray() };

            default:
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (name) on index %1. Operand is of type '%2'.").arg(index + 1).arg(PDFLexicalAnalyzer::getStringFromOperandType(token.type)));
        }
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (name) on index %1. Only %2 operands provided.").arg(index + 1).arg(m_operands.size()));
    }

    return PDFOperandName();
}


template<>
PDFPageContentProcessor::PDFOperandString PDFPageContentProcessor::readOperand<PDFPageContentProcessor::PDFOperandString>(size_t index) const
{
    if (index < m_operands.size())
    {
        const PDFLexicalAnalyzer::Token& token = m_operands[index];

        switch (token.type)
        {
            case PDFLexicalAnalyzer::TokenType::String:
                return PDFOperandString{ token.data.toByteArray() };

            default:
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (string) on index %1. Operand is of type '%2'.").arg(index + 1).arg(PDFLexicalAnalyzer::getStringFromOperandType(token.type)));
        }
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't read operand (string) on index %1. Only %2 operands provided.").arg(index + 1).arg(m_operands.size()));
    }

    return PDFOperandString();
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
    const PDFReal xMin = qMin(x, x + width);
    const PDFReal xMax = qMax(x, x + width);
    const PDFReal yMin = qMin(y, y + height);
    const PDFReal yMax = qMax(y, y + height);
    const PDFReal correctedWidth = xMax - xMin;
    const PDFReal correctedHeight = yMax - yMin;

    m_currentPath.addRect(QRectF(xMin, yMin, correctedWidth, correctedHeight));
}

void PDFPageContentProcessor::operatorPathStroke()
{
    // Do not close the path
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::WindingFill);
        performPathPainting(m_currentPath, true, false, false, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathCloseStroke()
{
    // Close the path
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.closeSubpath();
        m_currentPath.setFillRule(Qt::WindingFill);
        performPathPainting(m_currentPath, true, false, false, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathFillWinding()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::WindingFill);
        performPathPainting(m_currentPath, false, true, false, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathFillEvenOdd()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::OddEvenFill);
        performPathPainting(m_currentPath, false, true, false, Qt::OddEvenFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathFillStrokeWinding()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::WindingFill);
        performPathPainting(m_currentPath, true, true, false, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathFillStrokeEvenOdd()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.setFillRule(Qt::OddEvenFill);
        performPathPainting(m_currentPath, true, true, false, Qt::OddEvenFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathCloseFillStrokeWinding()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.closeSubpath();
        m_currentPath.setFillRule(Qt::WindingFill);
        performPathPainting(m_currentPath, true, true, false, Qt::WindingFill);
        m_currentPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorPathCloseFillStrokeEvenOdd()
{
    if (!m_currentPath.isEmpty())
    {
        m_currentPath.closeSubpath();
        m_currentPath.setFillRule(Qt::OddEvenFill);
        performPathPainting(m_currentPath, true, true, false, Qt::OddEvenFill);
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

void PDFPageContentProcessor::operatorType3FontSetOffset(PDFReal wx, PDFReal wy)
{
    performSetCharWidth(wx, wy);
}

void PDFPageContentProcessor::operatorType3FontSetOffsetAndBB(PDFReal wx, PDFReal wy, PDFReal llx, PDFReal lly, PDFReal urx, PDFReal ury)
{
    performSetCacheDevice(wx, wy, llx, lly, urx, ury);
}

void PDFPageContentProcessor::operatorColorSetStrokingColorSpace(PDFPageContentProcessor::PDFOperandName name)
{
    PDFColorSpacePointer colorSpace = PDFAbstractColorSpace::createColorSpace(m_colorSpaceDictionary, m_document, PDFObject::createName(std::make_shared<PDFString>(QByteArray(name.name))));
    if (colorSpace)
    {
        // We must also set default color (it can depend on the color space)
        m_graphicState.setStrokeColorSpace(colorSpace);
        m_graphicState.setStrokeColor(colorSpace->getDefaultColor());
        updateGraphicState();
        checkStrokingColor();
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid color space."));
    }
}

void PDFPageContentProcessor::operatorColorSetFillingColorSpace(PDFOperandName name)
{
    PDFColorSpacePointer colorSpace = PDFAbstractColorSpace::createColorSpace(m_colorSpaceDictionary, m_document, PDFObject::createName(std::make_shared<PDFString>(QByteArray(name.name))));
    if (colorSpace)
    {
        // We must also set default color (it can depend on the color space)
        m_graphicState.setFillColorSpace(colorSpace);
        m_graphicState.setFillColor(colorSpace->getDefaultColor());
        updateGraphicState();
        checkFillingColor();
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
        checkStrokingColor();
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid color component count. Provided %1, required %2.").arg(operandCount).arg(colorSpaceComponentCount));
    }
}

void PDFPageContentProcessor::operatorColorSetStrokingColorN()
{
    // In our implementation, operator 'SC' can also set color using all color spaces
    // PDF reference 1.7 allows here Pattern, Separation, DeviceN and ICCBased color spaces here,
    // but default operator can use them (with exception of Pattern color space).
    operatorColorSetStrokingColor();
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
        checkFillingColor();
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid color component count. Provided %1, required %2.").arg(operandCount).arg(colorSpaceComponentCount));
    }
}

void PDFPageContentProcessor::operatorColorSetFillingColorN()
{
    // In our implementation, operator 'sc' can also set color using all color spaces
    // PDF reference 1.7 allows here Pattern, Separation, DeviceN and ICCBased color spaces here,
    // but default operator can use them (with exception of Pattern color space).
    operatorColorSetFillingColor();
}

void PDFPageContentProcessor::operatorColorSetDeviceGrayStroking(PDFReal gray)
{
    m_graphicState.setStrokeColorSpace(m_deviceGrayColorSpace);
    m_graphicState.setStrokeColor(getColorFromColorSpace(m_graphicState.getStrokeColorSpace(), gray));
    updateGraphicState();
    checkStrokingColor();
}

void PDFPageContentProcessor::operatorColorSetDeviceGrayFilling(PDFReal gray)
{
    m_graphicState.setFillColorSpace(m_deviceGrayColorSpace);
    m_graphicState.setFillColor(getColorFromColorSpace(m_graphicState.getFillColorSpace(), gray));
    updateGraphicState();
    checkFillingColor();
}

void PDFPageContentProcessor::operatorColorSetDeviceRGBStroking(PDFReal r, PDFReal g, PDFReal b)
{
    m_graphicState.setStrokeColorSpace(m_deviceRGBColorSpace);
    m_graphicState.setStrokeColor(getColorFromColorSpace(m_graphicState.getStrokeColorSpace(), r, g, b));
    updateGraphicState();
    checkStrokingColor();
}

void PDFPageContentProcessor::operatorColorSetDeviceRGBFilling(PDFReal r, PDFReal g, PDFReal b)
{
    m_graphicState.setFillColorSpace(m_deviceRGBColorSpace);
    m_graphicState.setFillColor(getColorFromColorSpace(m_graphicState.getFillColorSpace(), r, g, b));
    updateGraphicState();
    checkFillingColor();
}

void PDFPageContentProcessor::operatorColorSetDeviceCMYKStroking(PDFReal c, PDFReal m, PDFReal y, PDFReal k)
{
    m_graphicState.setStrokeColorSpace(m_deviceCMYKColorSpace);
    m_graphicState.setStrokeColor(getColorFromColorSpace(m_graphicState.getStrokeColorSpace(), c, m, y, k));
    updateGraphicState();
    checkStrokingColor();
}

void PDFPageContentProcessor::operatorColorSetDeviceCMYKFilling(PDFReal c, PDFReal m, PDFReal y, PDFReal k)
{
    m_graphicState.setFillColorSpace(m_deviceCMYKColorSpace);
    m_graphicState.setFillColor(getColorFromColorSpace(m_graphicState.getFillColorSpace(), c, m, y, k));
    updateGraphicState();
    checkFillingColor();
}

void PDFPageContentProcessor::operatorTextBegin()
{
    m_graphicState.setTextMatrix(QMatrix());
    m_graphicState.setTextLineMatrix(QMatrix());
    updateGraphicState();

    ++m_textBeginEndState;

    if (m_textBeginEndState > 1)
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Text object already started."));
    }
}

void PDFPageContentProcessor::operatorTextEnd()
{
    if (--m_textBeginEndState < 0)
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Text object ended more than once."));
    }

    if (!m_textClippingPath.isEmpty())
    {
        QPainterPath clippingPath = m_graphicState.getCurrentTransformationMatrix().inverted().map(m_textClippingPath);
        performClipping(clippingPath, clippingPath.fillRule());
        m_textClippingPath = QPainterPath();
    }
}

void PDFPageContentProcessor::operatorTextSetCharacterSpacing(PDFReal charSpacing)
{
    m_graphicState.setTextCharacterSpacing(charSpacing);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorTextSetWordSpacing(PDFReal wordSpacing)
{
    m_graphicState.setTextWordSpacing(wordSpacing);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorTextSetHorizontalScale(PDFReal horizontalScaling)
{
    // We disable horizontal scaling to less than 1%
    horizontalScaling = qMax(horizontalScaling, 1.0);

    m_graphicState.setTextHorizontalScaling(horizontalScaling / 100.0);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorTextSetLeading(PDFReal leading)
{
    m_graphicState.setTextLeading(leading);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorTextSetFontAndFontSize(PDFOperandName fontName, PDFReal fontSize)
{
    if (m_fontDictionary)
    {
        if (m_fontDictionary->hasKey(fontName.name))
        {
            try
            {
                PDFFontPointer font = m_fontCache->getFont(m_fontDictionary->get(fontName.name));

                m_graphicState.setTextFont(qMove(font));
                m_graphicState.setTextFontSize(fontSize);
                updateGraphicState();
            }
            catch (PDFParserException)
            {
                m_graphicState.setTextFont(nullptr);
                m_graphicState.setTextFontSize(fontSize);
                updateGraphicState();
                throw;
            }
        }
        else
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Font '%1' not found in font dictionary.").arg(QString::fromLatin1(fontName.name)));
        }
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid font dictionary."));
    }
}

void PDFPageContentProcessor::operatorTextSetRenderMode(PDFInteger mode)
{
    mode = qBound<PDFInteger>(0, mode, 7);
    m_graphicState.setTextRenderingMode(static_cast<TextRenderingMode>(mode));
    updateGraphicState();
}

void PDFPageContentProcessor::operatorTextSetRise(PDFReal rise)
{
    m_graphicState.setTextRise(rise);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorTextMoveByOffset(PDFReal t_x, PDFReal t_y)
{
    const QMatrix& textLineMatrix = m_graphicState.getTextLineMatrix();
    QMatrix transformedMatrix = QMatrix(1, 0, 0, 1, t_x, t_y) * textLineMatrix;

    m_graphicState.setTextMatrix(transformedMatrix);
    m_graphicState.setTextLineMatrix(transformedMatrix);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorTextSetLeadingAndMoveByOffset(PDFReal t_x, PDFReal t_y)
{
    // Update of graphic state is
    m_graphicState.setTextLeading(-t_y);
    operatorTextMoveByOffset(t_x, t_y);
}

void PDFPageContentProcessor::operatorTextSetMatrix(PDFReal a, PDFReal b, PDFReal c, PDFReal d, PDFReal e, PDFReal f)
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

    m_graphicState.setTextMatrix(matrix);
    m_graphicState.setTextLineMatrix(matrix);
    updateGraphicState();
}

void PDFPageContentProcessor::operatorTextMoveByLeading()
{
    operatorTextMoveByOffset(0.0, -m_graphicState.getTextLeading());
}

void PDFPageContentProcessor::operatorTextShowTextString(PDFOperandString text)
{
    if (m_graphicState.getTextFont())
    {
        // Get the realized font
        const PDFRealizedFontPointer& realizedFont = getRealizedFont();
        if (!realizedFont)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid font, text can't be printed."));
        }

        TextSequence textSequence;

        // We use simple heuristic to ensure reallocation doesn't occur too often
        textSequence.items.reserve(m_operands.size());
        realizedFont->fillTextSequence(text.string, textSequence, this);
        drawText(textSequence);
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid font, text can't be printed."));
    }
}

void PDFPageContentProcessor::operatorTextShowTextIndividualSpacing()
{
    // Operand stack must be of this form [ ... text, number, text, number, number, text ... ]. We check it.

    if (m_operands.size() < 2)
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid parameters of text operator with individual character spacing."));
    }

    // Now, we have at least 2 arguments. Check we have an array
    if (m_operands[0].type != PDFLexicalAnalyzer::TokenType::ArrayStart ||
        m_operands[m_operands.size() - 1].type != PDFLexicalAnalyzer::TokenType::ArrayEnd)
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid line dash pattern."));
    }

    if (m_graphicState.getTextFont())
    {
        TextSequence textSequence;

        // We use simple heuristic to ensure reallocation doesn't occur too often
        textSequence.items.reserve(m_operands.size() * 4);

        // Get the realized font
        const PDFRealizedFontPointer& realizedFont = getRealizedFont();
        if (!realizedFont)
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid font, text can't be printed."));
        }

        for (size_t i = 1, lastIndex = m_operands.size() - 1; i < lastIndex; ++i)
        {
            switch (m_operands[i].type)
            {
                case PDFLexicalAnalyzer::TokenType::Integer:
                {
                    textSequence.items.push_back(TextSequenceItem(m_operands[i].data.value<PDFInteger>()));
                    break;
                }

                case PDFLexicalAnalyzer::TokenType::Real:
                {
                    textSequence.items.push_back(TextSequenceItem(m_operands[i].data.value<PDFReal>()));
                    break;
                }

                case PDFLexicalAnalyzer::TokenType::String:
                {
                    realizedFont->fillTextSequence(m_operands[i].data.toByteArray(), textSequence, this);
                    break;
                }

                default:
                {
                    // Error - we have operand of different type
                    throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid operand of text show operator."));
                }
            }
        }

        drawText(textSequence);
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid font, text can't be printed."));
    }
}

void PDFPageContentProcessor::operatorTextNextLineShowText(PDFOperandString text)
{
    operatorTextMoveByLeading();
    operatorTextShowTextString(qMove(text));
}

void PDFPageContentProcessor::operatorTextSetSpacingAndShowText(PDFReal t_w, PDFReal t_c, PDFOperandString text)
{
    m_graphicState.setTextWordSpacing(t_w);
    m_graphicState.setTextCharacterSpacing(t_c);
    updateGraphicState();

    operatorTextNextLineShowText(qMove(text));
}

void PDFPageContentProcessor::operatorPaintXObject(PDFPageContentProcessor::PDFOperandName name)
{
    if (m_xobjectDictionary)
    {
        // According to the specification, XObjects are skipped entirely, as no operator was invoked.
        if (m_xobjectDictionary->hasKey("OC"))
        {
            const PDFObject& object = m_xobjectDictionary->get("OC");
            if (object.isReference())
            {
                if (isContentSuppressedByOC(object.getReference()))
                {
                    return;
                }
            }
            else
            {
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Reference to optional content expected."));
            }
        }

        const PDFObject& object = m_document->getObject(m_xobjectDictionary->get(name.name));
        if (object.isStream())
        {
            const PDFStream* stream = object.getStream();
            const PDFDictionary* streamDictionary = stream->getDictionary();

            PDFDocumentDataLoaderDecorator loader(m_document);
            QByteArray subtype = loader.readNameFromDictionary(streamDictionary, "Subtype");
            if (subtype == "Image")
            {
                PDFColorSpacePointer colorSpace;

                if (streamDictionary->hasKey("ColorSpace"))
                {
                    const PDFObject& colorSpaceObject = m_document->getObject(streamDictionary->get("ColorSpace"));
                    if (colorSpaceObject.isName() || colorSpaceObject.isArray())
                    {
                        colorSpace = PDFAbstractColorSpace::createColorSpace(m_colorSpaceDictionary, m_document, colorSpaceObject);
                    }
                    else if (!colorSpaceObject.isNull())
                    {
                        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid color space of the image."));
                    }
                }

                PDFImage pdfImage = PDFImage::createImage(m_document, stream, qMove(colorSpace), this);
                QImage image = pdfImage.getImage();

                if (!image.isNull())
                {
                    performImagePainting(image);
                }
                else
                {
                    throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Can't decode the image."));
                }
            }
			else if (subtype == "Form")
			{
                PDFInteger formType = loader.readIntegerFromDictionary(streamDictionary, "FormType", 1);
                if (formType != 1)
                {
                    throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Form of type %1 not supported.").arg(formType));
                }

                // Read the bounding rectangle, if it is present
                QRectF boundingBox = loader.readRectangle(streamDictionary->get("BBox"), QRectF());

                // Read the transformation matrix, if it is present
                QMatrix transformationMatrix;

                if (streamDictionary->hasKey("Matrix"))
                {
                    std::vector<PDFReal> matrixNumbers = loader.readNumberArrayFromDictionary(streamDictionary, "Matrix");
                    if (matrixNumbers.size() != 6)
                    {
                        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid number of matrix elements. Expected 6, actual %1.").arg(matrixNumbers.size()));
                    }

                    transformationMatrix = QMatrix(matrixNumbers[0], matrixNumbers[1], matrixNumbers[2], matrixNumbers[3], matrixNumbers[4], matrixNumbers[5]);
                }

                // Read the dictionary content
                QByteArray content = m_document->getDecodedStream(stream);

                // Read resources
                PDFObject resources = m_document->getObject(streamDictionary->get("Resources"));

                processForm(transformationMatrix, boundingBox, resources, content);
			}
            else
            {
                throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Unknown XObject type '%1'.").arg(QString::fromLatin1(subtype)));
            }
        }
        else
        {
            throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid format of XObject. Dictionary expected."));
        }
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("XObject resource dictionary not found."));
    }
}

void PDFPageContentProcessor::operatorMarkedContentPoint(PDFOperandName name)
{
    performMarkedContentPoint(name.name, PDFObject());
}

void PDFPageContentProcessor::operatorMarkedContentPointWithProperties(PDFOperandName name, PDFObject properties)
{
    performMarkedContentPoint(name.name, properties);
}

void PDFPageContentProcessor::operatorMarkedContentBegin(PDFOperandName name)
{
    operatorMarkedContentBeginWithProperties(qMove(name), PDFObject());
}

void PDFPageContentProcessor::operatorMarkedContentBeginWithProperties(PDFOperandName name, PDFObject properties)
{
    // Handle the optional content
    if (name.name == "OC")
    {
        PDFObjectReference ocg;
        if (m_propertiesDictionary && properties.isName())
        {
            const PDFObject& ocgObject = m_propertiesDictionary->get(properties.getString());
            if (ocgObject.isReference())
            {
                ocg = ocgObject.getReference();
            }
        }

        m_markedContentStack.emplace_back(name.name, MarkedContentKind::OptionalContent, isContentSuppressedByOC(ocg));
    }
    else
    {
        m_markedContentStack.emplace_back(name.name, MarkedContentKind::Other, false);
    }

    performMarkedContentBegin(name.name, properties);
}

void PDFPageContentProcessor::operatorMarkedContentEnd()
{
    if (m_markedContentStack.empty())
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Mismatched begin/end of marked content."));
    }

    m_markedContentStack.pop_back();
    performMarkedContentEnd();
}

void PDFPageContentProcessor::operatorCompatibilityBegin()
{
    ++m_compatibilityBeginEndState;
}

void PDFPageContentProcessor::operatorCompatibilityEnd()
{
    if (--m_compatibilityBeginEndState < 0)
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Compatibility operator begin/end mismatch."));
    }
}

void PDFPageContentProcessor::drawText(const TextSequence& textSequence)
{
    if (textSequence.items.empty())
    {
        // Do not display empty text
        return;
    }

    const PDFRealizedFontPointer& font = getRealizedFont();
    if (font)
    {
        const bool isType3Font = m_graphicState.getTextFont()->getFontType() == FontType::Type3;
        const PDFReal fontSize = m_graphicState.getTextFontSize();
        const PDFReal horizontalScaling = m_graphicState.getTextHorizontalScaling() * 0.01; // Horizontal scaling is in percents
        const PDFReal characterSpacing = m_graphicState.getTextCharacterSpacing();
        const PDFReal wordSpacing = m_graphicState.getTextWordSpacing();
        const PDFReal textRise = m_graphicState.getTextRise();
        const TextRenderingMode textRenderingMode = m_graphicState.getTextRenderingMode();
        const bool fill = isTextRenderingModeFilled(textRenderingMode);
        const bool stroke = isTextRenderingModeStroked(textRenderingMode);
        const bool clipped = isTextRenderingModeClipped(textRenderingMode);

        // Detect horizontal writing system
        const bool isHorizontalWritingSystem = font->isHorizontalWritingSystem();

        // Calculate text rendering matrix
        QMatrix adjustMatrix(horizontalScaling, 0.0, 0.0, 1.0, 0.0, textRise);
        QMatrix textMatrix = m_graphicState.getTextMatrix();

        if (!isType3Font)
        {
            for (const TextSequenceItem& item : textSequence.items)
            {
                PDFReal displacementX = 0.0;
                PDFReal displacementY = 0.0;

                if (item.isCharacter())
                {
                    QChar character = item.character;
                    QPointF advance = isHorizontalWritingSystem ? QPointF(item.advance, 0) : QPointF(0, item.advance);

                    // First, compute the advance
                    const PDFReal additionalAdvance = (character == QChar(QChar::Space)) ? wordSpacing + characterSpacing : characterSpacing;
                    if (isHorizontalWritingSystem)
                    {
                        advance.rx() += additionalAdvance;
                    }
                    else
                    {
                        advance.ry() += additionalAdvance;
                    }
                    advance.rx() *= horizontalScaling;

                    // Then get the glyph path and paint it
                    if (item.glyph)
                    {
                        const QPainterPath& glyphPath = *item.glyph;
                        if (!glyphPath.isEmpty())
                        {
                            QMatrix textRenderingMatrix = adjustMatrix * textMatrix;
                            QPainterPath transformedGlyph = textRenderingMatrix.map(glyphPath);
                            performPathPainting(transformedGlyph, stroke, fill, true, transformedGlyph.fillRule());

                            if (clipped)
                            {
                                // Clipping is enabled, we must transform to the device coordinates
                                QMatrix toDeviceSpaceTransform = textRenderingMatrix * m_graphicState.getCurrentTransformationMatrix();
                                m_textClippingPath = m_textClippingPath.united(toDeviceSpaceTransform.map(glyphPath));
                            }
                        }
                    }

                    displacementX = advance.x();
                    displacementY = advance.y();
                }
                else if (item.isAdvance())
                {
                    if (horizontalScaling)
                    {
                        displacementX = -item.advance * 0.001 * fontSize * horizontalScaling;
                    }
                    else
                    {
                        displacementY = -item.advance * 0.001 * fontSize;
                    }
                }

                textMatrix.translate(displacementX, displacementY);
            }
        }
        else
        {
            // Type 3 Font

            PDFPageContentProcessorStateGuard guard(this);

            Q_ASSERT(dynamic_cast<const PDFType3Font*>(m_graphicState.getTextFont().get()));
            const PDFType3Font* parentFont = static_cast<const PDFType3Font*>(m_graphicState.getTextFont().get());

            QMatrix fontMatrix = parentFont->getFontMatrix();
            PDFObject resources = parentFont->getResources();
            if (!resources.isNull())
            {
                initDictionaries(resources);
            }

            QMatrix fontAdjustedMatrix = fontMatrix * adjustMatrix;

            for (const TextSequenceItem& item : textSequence.items)
            {
                // First, compute horizontal advance

                qreal displacementX = 0.0;
                if (item.advance != 0.0)
                {
                    qreal ry = 0.0;
                    fontAdjustedMatrix.map(item.advance, 0.0, &displacementX, &ry);
                }

                if (item.characterContentStream)
                {
                    PDFPageContentProcessorStateGuard guard(this);

                    // We must clear operands, because we are processing a new content stream
                    m_operands.clear();

                    QMatrix worldMatrix = fontAdjustedMatrix * textMatrix * m_graphicState.getCurrentTransformationMatrix();
                    m_graphicState.setCurrentTransformationMatrix(worldMatrix);
                    updateGraphicState();

                    processContent(*item.characterContentStream);
                }

                textMatrix.translate(displacementX, 0.0);
            }
        }

        m_graphicState.setTextMatrix(textMatrix);
        updateGraphicState();
    }
    else
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid font, text can't be printed."));
    }
}

PDFRealizedFontPointer PDFPageContentProcessor::getRealizedFontImpl() const
{
    if (m_graphicState.getTextFont())
    {
        return m_fontCache->getRealizedFont(m_graphicState.getTextFont(), m_graphicState.getTextFontSize());
    }

    return PDFRealizedFontPointer();
}

void PDFPageContentProcessor::checkStrokingColor()
{
    if (!m_graphicState.getStrokeColor().isValid())
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid stroking color."));
    }
}

void PDFPageContentProcessor::checkFillingColor()
{
    if (!m_graphicState.getFillColor().isValid())
    {
        throw PDFRendererException(RenderErrorType::Error, PDFTranslationContext::tr("Invalid filling color."));
    }
}

PDFObject PDFPageContentProcessor::readObjectFromOperandStack(size_t startPosition) const
{
    auto tokenFetcher = [this, &startPosition]()
    {
        if (startPosition < m_operands.size())
        {
            return m_operands[startPosition++];
        }
        return PDFLexicalAnalyzer::Token();
    };

    PDFParser parser(tokenFetcher);
    return parser.getObject();
}

bool PDFPageContentProcessor::isContentSuppressedByOC(PDFObjectReference ocgOrOcmd)
{
    if (!m_optionalContentActivity)
    {
        // Optional content activity control is suppressed, treat all content as not suppressed
        return false;
    }

    if (m_optionalContentActivity->getProperties()->hasOptionalContentGroup(ocgOrOcmd))
    {
        // Simplest case - we have single optional content group
        return m_optionalContentActivity->getState(ocgOrOcmd) == OCState::OFF;
    }

    PDFOptionalContentMembershipObject ocmd;
    try
    {
        ocmd = PDFOptionalContentMembershipObject::create(m_document, PDFObject::createReference(ocgOrOcmd));
    }
    catch (PDFParserException e)
    {
        m_errorList.push_back(PDFRenderError(RenderErrorType::Error, e.getMessage()));
    }

    if (ocmd.isValid())
    {
        return ocmd.evaluate(m_optionalContentActivity) == OCState::OFF;
    }

    return false;
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
    m_textCharacterSpacing(0.0),
    m_textWordSpacing(0.0),
    m_textHorizontalScaling(100.0),
    m_textLeading(0.0),
    m_textFontSize(0.0),
    m_textRenderingMode(TextRenderingMode::Fill),
    m_textRise(0.0),
    m_textKnockout(true),
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
    setTextCharacterSpacing(other.getTextCharacterSpacing());
    setTextWordSpacing(other.getTextWordSpacing());
    setTextHorizontalScaling(other.getTextHorizontalScaling());
    setTextLeading(other.getTextLeading());
    setTextFont(other.getTextFont());
    setTextFontSize(other.getTextFontSize());
    setTextRenderingMode(other.getTextRenderingMode());
    setTextRise(other.getTextRise());
    setTextKnockout(other.getTextKnockout());
    setTextMatrix(other.getTextMatrix());
    setTextLineMatrix(other.getTextLineMatrix());
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

void PDFPageContentProcessor::PDFPageContentProcessorState::setMitterLimit(PDFReal mitterLimit)
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

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextLeading(PDFReal textLeading)
{
    if (m_textLeading != textLeading)
    {
        m_textLeading = textLeading;
        m_stateFlags |= StateTextLeading;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextFontSize(PDFReal textFontSize)
{
    if (m_textFontSize != textFontSize)
    {
        m_textFontSize = textFontSize;
        m_stateFlags |= StateTextFontSize;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextKnockout(bool textKnockout)
{
    if (m_textKnockout != textKnockout)
    {
        m_textKnockout = textKnockout;
        m_stateFlags |= StateTextKnockout;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextLineMatrix(const QMatrix& textLineMatrix)
{
    if (m_textLineMatrix != textLineMatrix)
    {
        m_textLineMatrix = textLineMatrix;
        m_stateFlags |= StateTextLineMatrix;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextMatrix(const QMatrix& textMatrix)
{
    if (m_textMatrix != textMatrix)
    {
        m_textMatrix = textMatrix;
        m_stateFlags |= StateTextMatrix;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextRise(PDFReal textRise)
{
    if (m_textRise != textRise)
    {
        m_textRise = textRise;
        m_stateFlags |= StateTextRise;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextRenderingMode(TextRenderingMode textRenderingMode)
{
    if (m_textRenderingMode != textRenderingMode)
    {
        m_textRenderingMode = textRenderingMode;
        m_stateFlags |= StateTextRenderingMode;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextFont(const PDFFontPointer& textFont)
{
    if (m_textFont != textFont)
    {
        m_textFont = textFont;
        m_stateFlags |= StateTextFont;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextHorizontalScaling(PDFReal textHorizontalScaling)
{
    if (m_textHorizontalScaling != textHorizontalScaling)
    {
        m_textHorizontalScaling = textHorizontalScaling;
        m_stateFlags |= StateTextHorizontalScaling;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextWordSpacing(PDFReal textWordSpacing)
{
    if (m_textWordSpacing != textWordSpacing)
    {
        m_textWordSpacing = textWordSpacing;
        m_stateFlags |= StateTextWordSpacing;
    }
}

void PDFPageContentProcessor::PDFPageContentProcessorState::setTextCharacterSpacing(PDFReal textCharacterSpacing)
{
    if (m_textCharacterSpacing != textCharacterSpacing)
    {
        m_textCharacterSpacing = textCharacterSpacing;
        m_stateFlags |= StateTextCharacterSpacing;
    }
}

PDFPageContentProcessor::PDFPageContentProcessorStateGuard::PDFPageContentProcessorStateGuard(PDFPageContentProcessor* processor) :
    m_processor(processor),
    m_colorSpaceDictionary(processor->m_colorSpaceDictionary),
    m_fontDictionary(processor->m_fontDictionary),
    m_xobjectDictionary(processor->m_xobjectDictionary),
    m_extendedGraphicStateDictionary(processor->m_extendedGraphicStateDictionary),
    m_propertiesDictionary(processor->m_propertiesDictionary)
{
    m_processor->operatorSaveGraphicState();
}

PDFPageContentProcessor::PDFPageContentProcessorStateGuard::~PDFPageContentProcessorStateGuard()
{
    // Restore dictionaries
    m_processor->m_colorSpaceDictionary = m_colorSpaceDictionary;
    m_processor->m_fontDictionary = m_fontDictionary;
    m_processor->m_xobjectDictionary = m_xobjectDictionary;
    m_processor->m_extendedGraphicStateDictionary = m_extendedGraphicStateDictionary;
    m_processor->m_propertiesDictionary = m_propertiesDictionary;

    m_processor->operatorRestoreGraphicState();
}

}   // namespace pdf
