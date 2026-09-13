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

#include "pdfpagecontenteditorprocessor.h"
#include "pdfcolorconvertor.h"
#include "pdfpattern.h"

#include <QPainter>

#include <algorithm>

namespace pdf
{

PDFPageContentEditorProcessor::PDFPageContentEditorProcessor(const PDFPage* page,
                                                             const PDFDocument* document,
                                                             const PDFFontCache* fontCache,
                                                             const PDFCMS* CMS,
                                                             const PDFOptionalContentActivity* optionalContentActivity,
                                                             QTransform pagePointToDevicePointMatrix,
                                                             const PDFMeshQualitySettings& meshQualitySettings) :
    BaseClass(page, document, fontCache, CMS, optionalContentActivity, pagePointToDevicePointMatrix, meshQualitySettings)
{
    m_clippingPaths.push(QPainterPath());

    if (auto fontDictionary = getFontDictionary())
    {
        m_content.setFontDictionary(*fontDictionary);
    }

    if (auto xObjectDictionary = getXObjectDictionary())
    {
        m_content.setXObjectDictionary(*xObjectDictionary);
    }

    if (auto graphicStateDictionary = getExtendedGraphicStateDictionary())
    {
        m_content.setGraphicStateDictionary(*graphicStateDictionary);
    }

    if (auto shadingDictionary = getShadingDictionary())
    {
        m_content.setShadingDictionary(*shadingDictionary);
    }
}

const PDFEditedPageContent& PDFPageContentEditorProcessor::getEditedPageContent() const
{
    return m_content;
}

PDFEditedPageContent PDFPageContentEditorProcessor::takeEditedPageContent()
{
    return std::move(m_content);
}

void PDFPageContentEditorProcessor::performInterceptInstruction(Operator currentOperator,
                                                                ProcessOrder processOrder,
                                                                const QByteArray& operatorAsText)
{
    BaseClass::performInterceptInstruction(currentOperator, processOrder, operatorAsText);

    if (processOrder == ProcessOrder::BeforeOperation)
    {
        if (currentOperator == Operator::TextBegin && !isTextProcessing())
        {
            // This intercept is called before the BT operator is performed, so the graphic
            // state still contains the text matrices of the previous text object. The operator
            // resets both matrices to the identity (and the reset is not recorded as an item,
            // because the text object is not yet started). The text element is written into
            // its own BT/ET block, so its initial state must contain the identity matrices.
            // Otherwise a text matrix equal to the stale one would not be serialized at all.
            PDFPageContentProcessorState state = getElementState();
            state.setTextMatrix(QTransform());
            state.setTextLineMatrix(QTransform());

            m_contentElementText.reset(new PDFEditedPageContentElementText(state, getGraphicState()->getCurrentTransformationMatrix()));
            m_contentElementText->setClipPath(getCurrentClipPathInElementSpace(m_contentElementText->getTransform()));
        }

        if (currentOperator == Operator::TextSetFontAndFontSize)
        {
            m_textFontBeforeOperator = getGraphicState()->getTextFont();
        }

        if (currentOperator == Operator::ShadingPaintShape)
        {
            // The shading object is needed to write the shading back into the content
            // stream. The graphic state must be taken before the operator is performed,
            // because the operator changes the fill color space to the shading pattern.
            m_shadingObject = PDFObject();
            const PDFPageContentProcessorState elementState = getElementState();
            m_shadingState = elementState;
            m_isShadingOperatorActive = true;

            const PDFFlatArray<PDFLexicalAnalyzer::Token, 33>& operands = getOperands();
            const PDFDictionary* shadingDictionary = getShadingDictionary();
            if (shadingDictionary && operands.size() == 1 && operands[0].type == PDFLexicalAnalyzer::TokenType::Name)
            {
                m_shadingObject = shadingDictionary->get(operands[0].data.toByteArray());
            }
        }
    }
    else
    {
        if (currentOperator == Operator::TextSetFontAndFontSize)
        {
            // Remember the font object from the current resource dictionary. The text
            // can be painted by a form XObject, whose font names can denote other fonts
            // than the same names in the page resources (or they can be missing there).
            const PDFFlatArray<PDFLexicalAnalyzer::Token, 33>& operands = getOperands();
            const PDFDictionary* fontDictionary = getFontDictionary();
            const PDFFontPointer& font = getGraphicState()->getTextFont();

            if (font && fontDictionary && operands.size() == 2 && operands[0].type == PDFLexicalAnalyzer::TokenType::Name)
            {
                const QByteArray fontName = operands[0].data.toByteArray();
                const PDFObject& fontObject = fontDictionary->get(fontName);

                // If the font can't be created, the operator fails and the previous
                // font stays selected, so it must be checked, that the font is really
                // the font from the resource dictionary. Fonts referenced by an object
                // reference are cached, the other ones are created each time.
                bool isFontFromResources = false;
                if (fontObject.isReference())
                {
                    try
                    {
                        isFontFromResources = getFontCache() && getFontCache()->getFont(fontObject, fontName) == font;
                    }
                    catch (const PDFException&)
                    {
                        isFontFromResources = false;
                    }
                }
                else if (!fontObject.isNull())
                {
                    isFontFromResources = font != m_textFontBeforeOperator;
                }

                if (isFontFromResources)
                {
                    m_fontResources[font.data()] = FontResourceInfo{ font, fontName, fontObject };
                }
            }

            m_textFontBeforeOperator.reset();
        }

        if (currentOperator == Operator::ShadingPaintShape)
        {
            m_isShadingOperatorActive = false;
            m_shadingObject = PDFObject();
        }

        if (currentOperator == Operator::TextEnd && !isTextProcessing())
        {
            if (m_contentElementText)
            {
                m_contentElementText->optimize();

                if (!m_contentElementText->isEmpty())
                {
                    PDFEditedPageContentElementText* textElement = m_contentElementText.get();
                    registerFontResources(textElement);

                    auto getFontKey = [textElement](const PDFFont* font) { return textElement->getFontResourceKey(font); };
                    textElement->setTextPath(std::move(m_textPath));
                    textElement->setItemsAsText(PDFEditedPageContentElementText::createItemsAsText(textElement->getState(), textElement->getItems(), getFontKey));
                    m_content.addContentElement(std::move(m_contentElementText));
                }
            }
            m_contentElementText.reset();
            m_textPath = QPainterPath();
        }
    }
}

void PDFPageContentEditorProcessor::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
    BaseClass::performPathPainting(path, stroke, fill, text, fillRule);

    if (path.isEmpty())
    {
        return;
    }

    if (text)
    {
        m_textPath.addPath(path);
    }
    else
    {
        m_content.addContentPath(getElementState(), path, stroke, fill);
        if (PDFEditedPageContentElement* backElement = m_content.getBackElement())
        {
            backElement->setClipPath(getCurrentClipPathInElementSpace(backElement->getTransform()));
        }
    }
}

void PDFPageContentEditorProcessor::performUpdateGraphicsState(const PDFPageContentProcessorState& state)
{
    BaseClass::performUpdateGraphicsState(state);

    if (isTextProcessing() && m_contentElementText)
    {
        PDFEditedPageContentElementText::Item item;
        item.isUpdateGraphicState = true;
        item.state = state;

        m_contentElementText->addItem(item);
    }
}

void PDFPageContentEditorProcessor::performProcessTextSequence(const TextSequence& textSequence, ProcessOrder order)
{
    BaseClass::performProcessTextSequence(textSequence, order);

    if (order == ProcessOrder::BeforeOperation && m_contentElementText)
    {
        PDFEditedPageContentElementText::Item item;
        item.isText = true;
        item.textSequence = textSequence;

        m_contentElementText->addItem(item);
    }
}

bool PDFPageContentEditorProcessor::performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference)
{
    BaseClass::performOriginalImagePainting(image, stream, reference);

    PDFObject imageObject = PDFObject::createStream(std::make_shared<PDFStream>(*stream));
    m_content.addContentImage(getElementState(), std::move(imageObject), QImage());
    if (PDFEditedPageContentElement* backElement = m_content.getBackElement())
    {
        backElement->setClipPath(getCurrentClipPathInElementSpace(backElement->getTransform()));
    }

    return false;
}

void PDFPageContentEditorProcessor::performImagePainting(const QImage& image)
{
    BaseClass::performImagePainting(image);

    PDFEditedPageContentElement* backElement = m_content.getBackElement();
    if (!backElement)
    {
        return;
    }

    if (PDFEditedPageContentElementImage* imageElement = backElement->asImage())
    {
        imageElement->setImage(image);
    }
}

void PDFPageContentEditorProcessor::performSaveGraphicState(ProcessOrder order)
{
    BaseClass::performSaveGraphicState(order);

    if (order == ProcessOrder::BeforeOperation)
    {
        m_clippingPaths.push(m_clippingPaths.top());
    }
}

void PDFPageContentEditorProcessor::performRestoreGraphicState(ProcessOrder order)
{
    BaseClass::performRestoreGraphicState(order);

    if (order == ProcessOrder::AfterOperation && m_clippingPaths.size() > 1)
    {
        m_clippingPaths.pop();
    }
}

void PDFPageContentEditorProcessor::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
    BaseClass::performClipping(path, fillRule);

    // Clip paths are combined in the page coordinate space. Each clip path
    // is expressed in the user space active at the time of the clip operator,
    // so it must be mapped by the current transformation matrix before
    // it is intersected with the previous clip path.
    QPainterPath pageClipPath = path;
    pageClipPath.setFillRule(fillRule);
    pageClipPath = getGraphicState()->getCurrentTransformationMatrix().map(pageClipPath);

    QPainterPath& currentClipPath = m_clippingPaths.top();
    if (currentClipPath.isEmpty())
    {
        currentClipPath = pageClipPath;
    }
    else
    {
        currentClipPath = currentClipPath.intersected(pageClipPath);

        if (currentClipPath.isEmpty())
        {
            // The intersection has zero area, but an empty path means
            // "no clipping" in this container. Store a degenerate path
            // with zero fill area instead, which clips away all content.
            currentClipPath.moveTo(0, 0);
            currentClipPath.lineTo(1, 0);
        }
    }
}

QPainterPath PDFPageContentEditorProcessor::getCurrentClipPathInElementSpace(const QTransform& elementTransform) const
{
    const QPainterPath& clipPath = m_clippingPaths.top();
    if (clipPath.isEmpty())
    {
        return QPainterPath();
    }

    if (!elementTransform.isInvertible())
    {
        // Degenerate transformation matrix - the element is not visible anyway
        return QPainterPath();
    }

    return elementTransform.inverted().map(clipPath);
}

bool PDFPageContentEditorProcessor::isContentKindSuppressed(ContentKind kind) const
{
    // Jakub Melka: tiling patterns are not suppressed - their content is
    // decomposed into the standard edited content elements. Otherwise
    // everything, which is painted by a tiling pattern (images, paths, text),
    // would be silently lost, when the page content is written back.
    // Shadings are not suppressed either - the shading painted by the 'sh'
    // operator is converted to the shading element, see performPathPaintingUsingShading.
    Q_UNUSED(kind);
    return false;
}

bool PDFPageContentEditorProcessor::isTilingPatternProcessingAllowed(PDFInteger tileCount) const
{
    // Each tile is decomposed into the edited content elements, so a pattern
    // with a huge number of tiles would produce an unusable amount of elements.
    // Such patterns are not processed at all and an error is reported instead.
    return tileCount <= MAXIMUM_TILING_PATTERN_TILE_COUNT;
}

bool PDFPageContentEditorProcessor::performPathPaintingUsingShading(const QPainterPath& path, bool stroke, bool fill, const PDFShadingPattern* shadingPattern)
{
    BaseClass::performPathPaintingUsingShading(path, stroke, fill, shadingPattern);

    // Only shadings painted by the 'sh' operator are converted to the edited
    // content elements, because their shading object can be written back into
    // the content stream. Paths filled or stroked by a shading pattern are
    // skipped - the painting is reported as performed, so no mesh is created.
    if (!m_isShadingOperatorActive || m_shadingObject.isNull() || stroke || !fill)
    {
        return true;
    }

    const QTransform worldMatrix = getCurrentWorldMatrix();
    const QTransform transform = m_shadingState.getCurrentTransformationMatrix();
    if (!worldMatrix.isInvertible() || !transform.isInvertible())
    {
        // Degenerate transformation matrix - the shading is not visible
        return true;
    }

    PDFMeshQualitySettings settings;
    settings.deviceSpaceMeshingArea = getPageBoundingRectDeviceSpace();
    settings.userSpaceToDeviceSpaceMatrix = getPatternBaseMatrix();
    settings.initResolution();

    PDFMesh mesh = shadingPattern->createMesh(settings, getCMS(), m_shadingState.getRenderingIntent(), this, nullptr);

    // The mesh is created in the device space, but the element is displayed
    // in its own coordinate space, so it follows the element, when it is moved.
    mesh.transform(worldMatrix.inverted());

    // The 'sh' operator paints the whole page (the path is the page rectangle
    // in the user space), limited by the bounding box of the shading.
    QPainterPath area = path;
    if (!mesh.getBoundingPath().isEmpty())
    {
        area = area.intersected(mesh.getBoundingPath());
    }

    auto element = std::make_unique<PDFEditedPageContentElementShading>(m_shadingState, m_shadingObject, std::move(area), std::make_shared<const PDFMesh>(std::move(mesh)), transform);
    element->setClipPath(getCurrentClipPathInElementSpace(transform));
    m_content.addContentElement(std::move(element));

    return true;
}

void PDFPageContentEditorProcessor::performBeginTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup)
{
    BaseClass::performBeginTransparencyGroup(order, transparencyGroup);

    if (order == ProcessOrder::BeforeOperation)
    {
        // The graphic state still contains the blend mode and the constant alpha,
        // with which the transparency group is composed onto its backdrop (they
        // are reset in the graphic state of the group, when the group is started).
        const PDFPageContentProcessorState* state = getGraphicState();
        m_transparencyGroups.push_back(TransparencyGroupState{ state->getBlendMode(), state->getAlphaFilling(), state->getAlphaStroking() });
    }
}

void PDFPageContentEditorProcessor::performEndTransparencyGroup(ProcessOrder order, const PDFTransparencyGroup& transparencyGroup)
{
    BaseClass::performEndTransparencyGroup(order, transparencyGroup);

    if (order == ProcessOrder::AfterOperation && !m_transparencyGroups.empty())
    {
        m_transparencyGroups.pop_back();
    }
}

PDFPageContentProcessorState PDFPageContentEditorProcessor::getElementState() const
{
    PDFPageContentProcessorState state = *getGraphicState();

    if (!m_transparencyGroups.empty())
    {
        PDFReal alphaFilling = state.getAlphaFilling();
        PDFReal alphaStroking = state.getAlphaStroking();
        BlendMode blendMode = state.getBlendMode();

        // From the innermost transparency group to the outermost one
        for (auto it = m_transparencyGroups.crbegin(); it != m_transparencyGroups.crend(); ++it)
        {
            alphaFilling *= it->alphaFilling;
            alphaStroking *= it->alphaStroking;

            if (blendMode == BlendMode::Normal)
            {
                blendMode = it->blendMode;
            }
        }

        state.setAlphaFilling(alphaFilling);
        state.setAlphaStroking(alphaStroking);
        state.setBlendMode(blendMode);
    }

    return state;
}

void PDFPageContentEditorProcessor::registerFontResources(PDFEditedPageContentElementText* textElement) const
{
    auto registerFont = [this, textElement](const PDFFontPointer& font)
    {
        if (!font)
        {
            return;
        }

        auto it = m_fontResources.find(font.data());
        if (it != m_fontResources.cend())
        {
            textElement->addFontResource(font, it->second.fontObject, it->second.name);
        }
        else
        {
            // The font object is not known (for example, the font is selected by
            // the graphic state parameter dictionary), so the font identifier is used.
            textElement->addFontResource(font, PDFObject(), font->getFontId());
        }
    };

    registerFont(textElement->getState().getTextFont());

    for (const PDFEditedPageContentElementText::Item& item : textElement->getItems())
    {
        if (item.isUpdateGraphicState)
        {
            registerFont(item.state.getTextFont());
        }
    }
}

QString PDFEditedPageContent::getOperatorToString(PDFPageContentProcessor::Operator operatorValue)
{
    switch (operatorValue)
    {
        case pdf::PDFPageContentProcessor::Operator::SetLineWidth:
            return "set_line_width";
        case pdf::PDFPageContentProcessor::Operator::SetLineCap:
            return "set_line_cap";
        case pdf::PDFPageContentProcessor::Operator::SetLineJoin:
            return "set_line_join";
        case pdf::PDFPageContentProcessor::Operator::SetMitterLimit:
            return "set_mitter_limit";
        case pdf::PDFPageContentProcessor::Operator::SetLineDashPattern:
            return "set_line_dash_pattern";
        case pdf::PDFPageContentProcessor::Operator::SetRenderingIntent:
            return "set_rendering_intent";
        case pdf::PDFPageContentProcessor::Operator::SetFlatness:
            return "set_flatness";
        case pdf::PDFPageContentProcessor::Operator::SetGraphicState:
            return "set_graphic_state";
        case pdf::PDFPageContentProcessor::Operator::SaveGraphicState:
            return "save";
        case pdf::PDFPageContentProcessor::Operator::RestoreGraphicState:
            return "restore";
        case pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix:
            return "set_cm";
        case pdf::PDFPageContentProcessor::Operator::MoveCurrentPoint:
            return "move_to";
        case pdf::PDFPageContentProcessor::Operator::LineTo:
            return "line_to";
        case pdf::PDFPageContentProcessor::Operator::Bezier123To:
            return "cubic123_to";
        case pdf::PDFPageContentProcessor::Operator::Bezier23To:
            return "cubic23_to";
        case pdf::PDFPageContentProcessor::Operator::Bezier13To:
            return "cubic13_to";
        case pdf::PDFPageContentProcessor::Operator::EndSubpath:
            return "close_path";
        case pdf::PDFPageContentProcessor::Operator::Rectangle:
            return "rect";
        case pdf::PDFPageContentProcessor::Operator::PathStroke:
            return "path_stroke";
        case pdf::PDFPageContentProcessor::Operator::PathCloseStroke:
            return "path_close_and_stroke";
        case pdf::PDFPageContentProcessor::Operator::PathFillWinding:
            return "path_fill_winding";
        case pdf::PDFPageContentProcessor::Operator::PathFillWinding2:
            return "path_fill_winding";
        case pdf::PDFPageContentProcessor::Operator::PathFillEvenOdd:
            return "path_fill_even_odd";
        case pdf::PDFPageContentProcessor::Operator::PathFillStrokeWinding:
            return "path_fill_stroke_winding";
        case pdf::PDFPageContentProcessor::Operator::PathFillStrokeEvenOdd:
            return "path_fill_stroke_even_odd";
        case pdf::PDFPageContentProcessor::Operator::PathCloseFillStrokeWinding:
            return "path_close_fill_stroke_winding";
        case pdf::PDFPageContentProcessor::Operator::PathCloseFillStrokeEvenOdd:
            return "path_close_fill_stroke_even_odd";
        case pdf::PDFPageContentProcessor::Operator::PathClear:
            return "path_clear";
        case pdf::PDFPageContentProcessor::Operator::ClipWinding:
            return "clip_winding";
        case pdf::PDFPageContentProcessor::Operator::ClipEvenOdd:
            return "clip_even_odd";
        case pdf::PDFPageContentProcessor::Operator::TextBegin:
            return "text_begin";
        case pdf::PDFPageContentProcessor::Operator::TextEnd:
            return "text_end";
        case pdf::PDFPageContentProcessor::Operator::TextSetCharacterSpacing:
            return "set_char_spacing";
        case pdf::PDFPageContentProcessor::Operator::TextSetWordSpacing:
            return "set_word_spacing";
        case pdf::PDFPageContentProcessor::Operator::TextSetHorizontalScale:
            return "set_hor_scale";
        case pdf::PDFPageContentProcessor::Operator::TextSetLeading:
            return "set_leading";
        case pdf::PDFPageContentProcessor::Operator::TextSetFontAndFontSize:
            return "set_font";
        case pdf::PDFPageContentProcessor::Operator::TextSetRenderMode:
            return "set_text_render_mode";
        case pdf::PDFPageContentProcessor::Operator::TextSetRise:
            return "set_text_rise";
        case pdf::PDFPageContentProcessor::Operator::TextMoveByOffset:
            return "text_move_by_offset";
        case pdf::PDFPageContentProcessor::Operator::TextSetLeadingAndMoveByOffset:
            return "text_set_leading_and_move_by_offset";
        case pdf::PDFPageContentProcessor::Operator::TextSetMatrix:
            return "text_set_matrix";
        case pdf::PDFPageContentProcessor::Operator::TextMoveByLeading:
            return "text_move_by_leading";
        case pdf::PDFPageContentProcessor::Operator::TextShowTextString:
            return "text_show_string";
        case pdf::PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing:
            return "text_show_string_with_spacing";
        case pdf::PDFPageContentProcessor::Operator::TextNextLineShowText:
            return "text_next_line_and_show_text";
        case pdf::PDFPageContentProcessor::Operator::TextSetSpacingAndShowText:
            return "text_set_spacing_and_show_text";
        case pdf::PDFPageContentProcessor::Operator::Type3FontSetOffset:
            return "text_t3_set_offset";
        case pdf::PDFPageContentProcessor::Operator::Type3FontSetOffsetAndBB:
            return "text_t3_set_offset_and_bb";
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColorSpace:
            return "set_stroke_color_space";
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColorSpace:
            return "set_filling_color_space";
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColor:
            return "set_stroke_color";
        case pdf::PDFPageContentProcessor::Operator::ColorSetStrokingColorN:
            return "set_stroke_color_n";
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColor:
            return "set_filling_color";
        case pdf::PDFPageContentProcessor::Operator::ColorSetFillingColorN:
            return "set_filling_color_n";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceGrayStroking:
            return "set_stroke_gray_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceGrayFilling:
            return "set_filling_gray_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceRGBStroking:
            return "set_stroke_rgb_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceRGBFilling:
            return "set_filling_rgb_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceCMYKStroking:
            return "set_stroke_cmyk_cs";
        case pdf::PDFPageContentProcessor::Operator::ColorSetDeviceCMYKFilling:
            return "set_filling_cmyk_cs";
        case pdf::PDFPageContentProcessor::Operator::ShadingPaintShape:
            return "shading_paint";
        case pdf::PDFPageContentProcessor::Operator::InlineImageBegin:
            return "ib";
        case pdf::PDFPageContentProcessor::Operator::InlineImageData:
            return "id";
        case pdf::PDFPageContentProcessor::Operator::InlineImageEnd:
            return "ie";
        case pdf::PDFPageContentProcessor::Operator::PaintXObject:
            return "paint_object";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentPoint:
            return "mc_point";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentPointWithProperties:
            return "mc_point_prop";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentBegin:
            return "mc_begin";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentBeginWithProperties:
            return "mc_begin_prop";
        case pdf::PDFPageContentProcessor::Operator::MarkedContentEnd:
            return "mc_end";
        case pdf::PDFPageContentProcessor::Operator::CompatibilityBegin:
            return "compat_begin";
        case pdf::PDFPageContentProcessor::Operator::CompatibilityEnd:
            return "compat_end";

        default:
            break;
    }

    return QString();
}

QString PDFEditedPageContent::getOperandName(PDFPageContentProcessor::Operator operatorValue, int operandIndex)
{
    static const std::map<std::pair<PDFPageContentProcessor::Operator, int>, QString> operands =
    {
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetLineWidth, 0), "lineWidth" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetLineCap, 0), "lineCap" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetLineJoin, 0), "lineJoin" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetMitterLimit, 0), "mitterLimit" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetRenderingIntent, 0), "renderingIntent" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetFlatness, 0), "flatness" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::SetGraphicState, 0), "graphicState" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 0), "a" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 1), "b" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 2), "c" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 3), "d" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 4), "e" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::AdjustCurrentTransformationMatrix, 5), "f" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::MoveCurrentPoint, 0), "x" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::MoveCurrentPoint, 1), "y" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::LineTo, 0), "x" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::LineTo, 1), "y" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 0), "x1" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 1), "y1" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 2), "x2" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 3), "y2" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 4), "x3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier123To, 5), "y3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier23To, 0), "x2" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier23To, 1), "y2" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier23To, 2), "x3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier23To, 3), "y3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier13To, 0), "x1" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier13To, 1), "y1" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier13To, 2), "x3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Bezier13To, 3), "y3" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Rectangle, 0), "x" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Rectangle, 1), "y" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Rectangle, 2), "width" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::Rectangle, 3), "height" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetCharacterSpacing, 0), "charSpacing" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetWordSpacing, 0), "wordSpacing" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetHorizontalScale, 0), "scale" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetLeading, 0), "leading" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetFontAndFontSize, 0), "font" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetFontAndFontSize, 1), "fontSize" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetRenderMode, 0), "renderMode" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetRise, 0), "rise" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextMoveByOffset, 0), "tx" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextMoveByOffset, 1), "ty" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetLeadingAndMoveByOffset, 0), "tx" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetLeadingAndMoveByOffset, 1), "ty" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 0), "a" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 1), "b" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 2), "c" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 3), "d" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 4), "e" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetMatrix, 5), "f" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextShowTextString, 0), "string" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextNextLineShowText, 0), "string" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing, 0), "wSpacing" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing, 1), "chSpacing" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextShowTextIndividualSpacing, 2), "string" },
        { std::make_pair(pdf::PDFPageContentProcessor::Operator::TextSetSpacingAndShowText, 0), "string" },
    };

    auto it = operands.find(std::make_pair(operatorValue, operandIndex));
    if (it != operands.cend())
    {
        return it->second;
    }

    return QString("op%1").arg(operandIndex);
}

void PDFEditedPageContent::addContentPath(PDFPageContentProcessorState state, QPainterPath path, bool strokePath, bool fillPath)
{
    QTransform transform = state.getCurrentTransformationMatrix();
    m_contentElements.emplace_back(new PDFEditedPageContentElementPath(std::move(state), std::move(path), strokePath, fillPath, transform));
}

void PDFEditedPageContent::addContentImage(PDFPageContentProcessorState state, PDFObject imageObject, QImage image)
{
    QTransform transform = state.getCurrentTransformationMatrix();
    m_contentElements.emplace_back(new PDFEditedPageContentElementImage(std::move(state), std::move(imageObject), std::move(image), transform));
}

void PDFEditedPageContent::addContentElement(std::unique_ptr<PDFEditedPageContentElement> element)
{
    m_contentElements.emplace_back(std::move(element));
}

PDFEditedPageContentElement* PDFEditedPageContent::getBackElement() const
{
    if (m_contentElements.empty())
    {
        return nullptr;
    }

    return m_contentElements.back().get();
}

PDFDictionary PDFEditedPageContent::getFontDictionary() const
{
    return m_fontDictionary;
}

void PDFEditedPageContent::setFontDictionary(const PDFDictionary& newFontDictionary)
{
    m_fontDictionary = newFontDictionary;
}

PDFDictionary PDFEditedPageContent::getXObjectDictionary() const
{
    return m_xobjectDictionary;
}

void PDFEditedPageContent::setXObjectDictionary(const PDFDictionary& newXobjectDictionary)
{
    m_xobjectDictionary = newXobjectDictionary;
}

PDFDictionary PDFEditedPageContent::getGraphicStateDictionary() const
{
    return m_graphicStateDictionary;
}

void PDFEditedPageContent::setGraphicStateDictionary(const PDFDictionary& newGraphicStateDictionary)
{
    m_graphicStateDictionary = newGraphicStateDictionary;
}

PDFDictionary PDFEditedPageContent::getShadingDictionary() const
{
    return m_shadingDictionary;
}

void PDFEditedPageContent::setShadingDictionary(const PDFDictionary& newShadingDictionary)
{
    m_shadingDictionary = newShadingDictionary;
}

PDFEditedPageContentElement::PDFEditedPageContentElement(PDFPageContentProcessorState state, QTransform transform) :
    m_state(std::move(state)),
    m_transform(transform)
{

}

const PDFPageContentProcessorState& PDFEditedPageContentElement::getState() const
{
    return m_state;
}

void PDFEditedPageContentElement::setState(const PDFPageContentProcessorState& newState)
{
    m_state = newState;
}

QTransform PDFEditedPageContentElement::getTransform() const
{
    return m_transform;
}

void PDFEditedPageContentElement::setTransform(const QTransform& newTransform)
{
    m_transform = newTransform;
}

const QPainterPath& PDFEditedPageContentElement::getClipPath() const
{
    return m_clipPath;
}

void PDFEditedPageContentElement::setClipPath(const QPainterPath& clipPath)
{
    m_clipPath = clipPath;
}

PDFEditedPageContentElementPath::PDFEditedPageContentElementPath(PDFPageContentProcessorState state, QPainterPath path, bool strokePath, bool fillPath, QTransform transform) :
    PDFEditedPageContentElement(std::move(state), transform),
    m_path(std::move(path)),
    m_strokePath(strokePath),
    m_fillPath(fillPath)
{

}

PDFEditedPageContentElement::Type PDFEditedPageContentElementPath::getType() const
{
    return Type::Path;
}

PDFEditedPageContentElementPath* PDFEditedPageContentElementPath::clone() const
{
    PDFEditedPageContentElementPath* copy = new PDFEditedPageContentElementPath(getState(), getPath(), getStrokePath(), getFillPath(), getTransform());
    copy->setClipPath(getClipPath());
    return copy;
}

QRectF PDFEditedPageContentElementPath::getBoundingBox() const
{
    QPainterPath mappedPath = getTransform().map(m_path);
    QRectF boundingBox = mappedPath.boundingRect();

    if (!m_clipPath.isEmpty())
    {
        boundingBox = boundingBox.intersected(getTransform().mapRect(m_clipPath.boundingRect()));
    }

    return boundingBox;
}

QPainterPath PDFEditedPageContentElementPath::getPath() const
{
    return m_path;
}

void PDFEditedPageContentElementPath::setPath(QPainterPath newPath)
{
    m_path = newPath;
}

bool PDFEditedPageContentElementPath::getStrokePath() const
{
    return m_strokePath;
}

void PDFEditedPageContentElementPath::setStrokePath(bool newStrokePath)
{
    m_strokePath = newStrokePath;
}

bool PDFEditedPageContentElementPath::getFillPath() const
{
    return m_fillPath;
}

void PDFEditedPageContentElementPath::setFillPath(bool newFillPath)
{
    m_fillPath = newFillPath;
}

PDFEditedPageContentElementImage::PDFEditedPageContentElementImage(PDFPageContentProcessorState state, PDFObject imageObject, QImage image, QTransform transform) :
    PDFEditedPageContentElement(std::move(state), transform),
    m_imageObject(std::move(imageObject)),
    m_image(std::move(image))
{

}

PDFEditedPageContentElement::Type PDFEditedPageContentElementImage::getType() const
{
    return PDFEditedPageContentElement::Type::Image;
}

PDFEditedPageContentElementImage* PDFEditedPageContentElementImage::clone() const
{
    PDFEditedPageContentElementImage* copy = new PDFEditedPageContentElementImage(getState(), getImageObject(), getImage(), getTransform());
    copy->setClipPath(getClipPath());
    return copy;
}

QRectF PDFEditedPageContentElementImage::getBoundingBox() const
{
    QRectF boundingBox = getTransform().mapRect(QRectF(0, 0, 1, 1));

    if (!m_clipPath.isEmpty())
    {
        boundingBox = boundingBox.intersected(getTransform().mapRect(m_clipPath.boundingRect()));
    }

    return boundingBox;
}

PDFObject PDFEditedPageContentElementImage::getImageObject() const
{
    return m_imageObject;
}

void PDFEditedPageContentElementImage::setImageObject(const PDFObject& newImageObject)
{
    m_imageObject = newImageObject;
}

QImage PDFEditedPageContentElementImage::getImage() const
{
    return m_image;
}

void PDFEditedPageContentElementImage::setImage(const QImage& newImage)
{
    m_image = newImage;
}

PDFEditedPageContentElementShading::PDFEditedPageContentElementShading(PDFPageContentProcessorState state,
                                                                       PDFObject shadingObject,
                                                                       QPainterPath area,
                                                                       std::shared_ptr<const PDFMesh> mesh,
                                                                       QTransform transform) :
    PDFEditedPageContentElement(std::move(state), transform),
    m_shadingObject(std::move(shadingObject)),
    m_area(std::move(area)),
    m_mesh(std::move(mesh))
{

}

PDFEditedPageContentElement::Type PDFEditedPageContentElementShading::getType() const
{
    return Type::Shading;
}

PDFEditedPageContentElementShading* PDFEditedPageContentElementShading::clone() const
{
    PDFEditedPageContentElementShading* copy = new PDFEditedPageContentElementShading(getState(), getShadingObject(), getArea(), m_mesh, getTransform());
    copy->setClipPath(getClipPath());
    return copy;
}

QRectF PDFEditedPageContentElementShading::getBoundingBox() const
{
    QRectF boundingBox = getTransform().mapRect(m_area.boundingRect());

    if (!m_clipPath.isEmpty())
    {
        boundingBox = boundingBox.intersected(getTransform().mapRect(m_clipPath.boundingRect()));
    }

    return boundingBox;
}

const PDFObject& PDFEditedPageContentElementShading::getShadingObject() const
{
    return m_shadingObject;
}

const QPainterPath& PDFEditedPageContentElementShading::getArea() const
{
    return m_area;
}

void PDFEditedPageContentElementShading::paint(QPainter* painter, const PDFColorConvertor& convertor) const
{
    if (!m_mesh)
    {
        return;
    }

    if (convertor.isActive())
    {
        PDFMesh convertedMesh = *m_mesh;
        convertedMesh.convertColors(convertor);
        convertedMesh.paint(painter, getState().getAlphaFilling());
    }
    else
    {
        m_mesh->paint(painter, getState().getAlphaFilling());
    }
}

PDFEditedPageContentElementText::PDFEditedPageContentElementText(PDFPageContentProcessorState state, QTransform transform) :
    PDFEditedPageContentElement(state, transform)
{

}

PDFEditedPageContentElementText::PDFEditedPageContentElementText(PDFPageContentProcessorState state,
                                                                 std::vector<Item> items,
                                                                 QPainterPath textPath,
                                                                 QTransform transform,
                                                                 QString itemsAsText) :
    PDFEditedPageContentElement(state, transform),
    m_items(std::move(items)),
    m_textPath(std::move(textPath)),
    m_itemsAsText(itemsAsText)
{

}

PDFEditedPageContentElement::Type PDFEditedPageContentElementText::getType() const
{
    return Type::Text;
}

PDFEditedPageContentElementText* PDFEditedPageContentElementText::clone() const
{
    PDFEditedPageContentElementText* copy = new PDFEditedPageContentElementText(getState(), getItems(), getTextPath(), getTransform(), getItemsAsText());
    copy->setClipPath(getClipPath());
    copy->setFontResources(getFontResources());
    return copy;
}

void PDFEditedPageContentElementText::addItem(Item item)
{
    m_items.emplace_back(std::move(item));
}

const std::vector<PDFEditedPageContentElementText::Item>& PDFEditedPageContentElementText::getItems() const
{
    return m_items;
}

void PDFEditedPageContentElementText::setItems(const std::vector<Item>& newItems)
{
    m_items = newItems;
}

QRectF PDFEditedPageContentElementText::getBoundingBox() const
{
    QRectF boundingBox = getTransform().mapRect(m_textPath.boundingRect());

    if (!m_clipPath.isEmpty())
    {
        boundingBox = boundingBox.intersected(getTransform().mapRect(m_clipPath.boundingRect()));
    }

    return boundingBox;
}

QPainterPath PDFEditedPageContentElementText::getTextPath() const
{
    return m_textPath;
}

void PDFEditedPageContentElementText::setTextPath(QPainterPath newTextPath)
{
    m_textPath = newTextPath;
}

QByteArray PDFEditedPageContentElementText::addFontResource(const PDFFontPointer& font, const PDFObject& fontObject, const QByteArray& key)
{
    for (const FontResource& fontResource : m_fontResources)
    {
        if (fontResource.font == font)
        {
            return fontResource.key;
        }
    }

    auto isKeyUsed = [this](const QByteArray& currentKey)
    {
        return std::any_of(m_fontResources.cbegin(), m_fontResources.cend(), [&currentKey](const FontResource& fontResource) { return fontResource.key == currentKey; });
    };

    // The same name can denote different fonts (the fonts can be selected
    // in different content streams), but the key must identify the font.
    QByteArray uniqueKey = key;
    for (int i = 1; uniqueKey.isEmpty() || isKeyUsed(uniqueKey); ++i)
    {
        uniqueKey = key + "_" + QByteArray::number(i);
    }

    m_fontResources.push_back(FontResource{ uniqueKey, font, fontObject });
    return uniqueKey;
}

const std::vector<PDFEditedPageContentElementText::FontResource>& PDFEditedPageContentElementText::getFontResources() const
{
    return m_fontResources;
}

void PDFEditedPageContentElementText::setFontResources(const std::vector<FontResource>& fontResources)
{
    m_fontResources = fontResources;
}

QByteArray PDFEditedPageContentElementText::getFontResourceKey(const PDFFont* font) const
{
    for (const FontResource& fontResource : m_fontResources)
    {
        if (fontResource.font.data() == font)
        {
            return fontResource.key;
        }
    }

    return font ? font->getFontId() : QByteArray();
}

/// Creates the color command of the text items. Colors are represented in the same
/// way, as the content stream builder writes the colors of the graphic state -
/// DeviceGray and DeviceCMYK colors are kept, other colors are converted to RGB.
/// \param tag Tag of the command ("fill" or "stroke")
/// \param colorSpace Color space
/// \param color Color converted to RGB
/// \param originalColor Color in the color space
static QString createColorCommand(const char* tag, const PDFAbstractColorSpace* colorSpace, const QColor& color, const PDFColor& originalColor)
{
    if (colorSpace && colorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceGray)
    {
        return QString("<%1 gray=\"%2\"/>").arg(QLatin1String(tag)).arg(qGray(color.rgb()) / 255.0);
    }

    if (colorSpace && colorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceCMYK && originalColor.size() >= 4)
    {
        return QString("<%1 c=\"%2\" m=\"%3\" y=\"%4\" k=\"%5\"/>").arg(QLatin1String(tag)).arg(originalColor[0]).arg(originalColor[1]).arg(originalColor[2]).arg(originalColor[3]);
    }

    return QString("<%1 r=\"%2\" g=\"%3\" b=\"%4\"/>").arg(QLatin1String(tag)).arg(color.redF()).arg(color.greenF()).arg(color.blueF());
}

QString PDFEditedPageContentElementText::createItemsAsText(const PDFPageContentProcessorState& initialState,
                                                           const std::vector<Item>& items,
                                                           const std::function<QByteArray(const PDFFont*)>& getFontKey)
{
    QString text;

    PDFPageContentProcessorState state = initialState;
    state.setStateFlags(PDFPageContentProcessorState::StateFlags());

    for (const Item& item : items)
    {
        if (item.isText)
        {
            for (const TextSequenceItem& textItem : item.textSequence.items)
            {
                if (textItem.isCharacter() || textItem.isContentStream())
                {
                    if (!textItem.character.isNull())
                    {
                        text += QString(textItem.character).toHtmlEscaped();
                    }
                    else if (textItem.cid != 0)
                    {
                        text += QString("<character cid=\"%1\"/>").arg(textItem.cid);
                    }
                    else if (textItem.isAdvance())
                    {
                        text += QString("<space advance=\"%1\"/>").arg(textItem.advance);
                    }
                }
                else if (textItem.isAdvance())
                {
                    text += QString("<space advance=\"%1\"/>").arg(textItem.advance);
                }
            }
        }
        else if (item.isUpdateGraphicState)
        {
            PDFPageContentProcessorState newState = state;
            newState.setStateFlags(PDFPageContentProcessorState::StateFlags());

            newState.setState(item.state);
            PDFPageContentProcessorState::StateFlags flags = newState.getStateFlags();

            if (flags.testFlag(PDFPageContentProcessorState::StateFillColor) ||
                flags.testFlag(PDFPageContentProcessorState::StateFillColorSpace))
            {
                text += createColorCommand("fill", newState.getFillColorSpace(), newState.getFillColor(), newState.getFillColorOriginal());
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateStrokeColor) ||
                flags.testFlag(PDFPageContentProcessorState::StateStrokeColorSpace))
            {
                text += createColorCommand("stroke", newState.getStrokeColorSpace(), newState.getStrokeColor(), newState.getStrokeColorOriginal());
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextRenderingMode))
            {
                text += QString("<tr v=\"%1\"/>").arg(int(newState.getTextRenderingMode()));
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextRise))
            {
                text += QString("<ts v=\"%1\"/>").arg(newState.getTextRise());
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextCharacterSpacing))
            {
                text += QString("<tc v=\"%1\"/>").arg(newState.getTextCharacterSpacing());
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextWordSpacing))
            {
                text += QString("<tw v=\"%1\"/>").arg(newState.getTextWordSpacing());
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextLeading))
            {
                text += QString("<tl v=\"%1\"/>").arg(newState.getTextLeading());
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextHorizontalScaling))
            {
                text += QString("<tz v=\"%1\"/>").arg(newState.getTextHorizontalScaling());
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextKnockout))
            {
                text += QString("<tk v=\"%1\"/>").arg(newState.getTextKnockout());
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextFont) ||
                flags.testFlag(PDFPageContentProcessorState::StateTextFontSize))
            {
                if (const PDFFontPointer& font = newState.getTextFont())
                {
                    const QByteArray fontKey = getFontKey ? getFontKey(font.data()) : font->getFontId();
                    text += QString("<tf font=\"%1\" size=\"%2\"/>").arg(QString::fromLatin1(fontKey).toHtmlEscaped()).arg(newState.getTextFontSize());
                }
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextMatrix))
            {
                QTransform transform = newState.getTextMatrix();

                qreal x = transform.dx();
                qreal y = transform.dy();

                // Position can be serialized alone only for a pure translation matrix.
                // QTransform::isTranslating() returns true also for rotated/scaled
                // matrices, which would discard rotation, scale or mirroring of the text.
                if (transform.type() <= QTransform::TxTranslate)
                {
                    text += QString("<tpos x=\"%1\" y=\"%2\"/>").arg(x).arg(y);
                }
                else
                {
                    text += QString("<tmatrix m11=\"%1\" m12=\"%2\" m21=\"%3\" m22=\"%4\" x=\"%5\" y=\"%6\"/>").arg(transform.m11()).arg(transform.m12()).arg(transform.m21()).arg(transform.m22()).arg(x).arg(y);
                }
            }

            state = newState;
            state.setStateFlags(PDFPageContentProcessorState::StateFlags());
        }
    }

    return text;
}

QString PDFEditedPageContentElementText::getItemsAsText() const
{
    return m_itemsAsText;
}

void PDFEditedPageContentElementText::setItemsAsText(const QString& newItemsAsText)
{
    m_itemsAsText = newItemsAsText;
}

void PDFEditedPageContentElementText::optimize()
{
    while (!m_items.empty() && !m_items.back().isText)
    {
        m_items.pop_back();
    }
}

}   // namespace pdf
