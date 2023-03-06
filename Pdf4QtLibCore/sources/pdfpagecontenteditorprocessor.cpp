//    Copyright (C) 2023 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "pdfpagecontenteditorprocessor.h"

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
    Q_UNUSED(operatorAsText);

    if (processOrder == ProcessOrder::BeforeOperation)
    {
        if (currentOperator == Operator::TextBegin && !isTextProcessing())
        {
            m_contentElementText.reset(new PDFEditedPageContentElementText(*getGraphicState()));
        }
    }
    else
    {
        if (currentOperator == Operator::TextEnd && !isTextProcessing())
        {
            if (m_contentElementText && !m_contentElementText->isEmpty())
            {
                m_content.addContentElement(std::move(m_contentElementText));
            }
            m_contentElementText.reset();

            m_textBoundingRect = QRectF();
        }
    }
}

void PDFPageContentEditorProcessor::performPathPainting(const QPainterPath& path, bool stroke, bool fill, bool text, Qt::FillRule fillRule)
{
    if (path.isEmpty())
    {
        return;
    }

    Q_UNUSED(fillRule);
    Q_UNUSED(stroke);
    Q_UNUSED(fill);
    Q_UNUSED(text);

    if (text)
    {
        QPainterPath mappedPath = getCurrentWorldMatrix().map(path);
        QRectF boundingRect = mappedPath.boundingRect();
        m_textBoundingRect = m_textBoundingRect.united(boundingRect);
    }
    else
    {
        m_content.addContentPath(*getGraphicState(), path, stroke, fill);
    }
}

void PDFPageContentEditorProcessor::performUpdateGraphicsState(const PDFPageContentProcessorState& state)
{
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
    if (order == ProcessOrder::BeforeOperation)
    {
        PDFEditedPageContentElementText::Item item;
        item.isText = true;
        item.textSequence = textSequence;

        m_contentElementText->addItem(item);
    }
}

bool PDFPageContentEditorProcessor::performOriginalImagePainting(const PDFImage& image, const PDFStream* stream)
{
    Q_UNUSED(image);

    PDFObject imageObject = PDFObject::createStream(std::make_shared<PDFStream>(*stream));
    m_content.addContentImage(*getGraphicState(), std::move(imageObject), QImage());

    return false;
}

void PDFPageContentEditorProcessor::performImagePainting(const QImage& image)
{
    PDFEditedPageContentElement* backElement = m_content.getBackElement();
    Q_ASSERT(backElement);

    PDFEditedPageContentElementImage* imageElement = backElement->asImage();
    imageElement->setImage(image);
}

void PDFPageContentEditorProcessor::performSaveGraphicState(ProcessOrder order)
{
    if (order == ProcessOrder::BeforeOperation)
    {
        m_clippingPaths.push(m_clippingPaths.top());
    }
}

void PDFPageContentEditorProcessor::performRestoreGraphicState(ProcessOrder order)
{
    if (order == ProcessOrder::AfterOperation)
    {
        m_clippingPaths.pop();
    }
}

void PDFPageContentEditorProcessor::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
    Q_UNUSED(fillRule);

    if (m_clippingPaths.top().isEmpty())
    {
        m_clippingPaths.top() = path;
    }
    else
    {
        m_clippingPaths.top() = m_clippingPaths.top().intersected(path);
    }
}

bool PDFPageContentEditorProcessor::isContentKindSuppressed(ContentKind kind) const
{
    switch (kind)
    {
        case ContentKind::Shading:
        case ContentKind::Tiling:
            return true;

        default:
            break;
    }

    return false;
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
    m_contentElements.emplace_back(new PDFEditedPageContentElementPath(std::move(state), std::move(path), strokePath, fillPath));
}

void PDFEditedPageContent::addContentImage(PDFPageContentProcessorState state, PDFObject imageObject, QImage image)
{
    m_contentElements.emplace_back(new PDFEditedPageContentElementImage(std::move(state), std::move(imageObject), std::move(image)));
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

PDFEditedPageContentElement::PDFEditedPageContentElement(PDFPageContentProcessorState state) :
    m_state(std::move(state))
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

PDFEditedPageContentElementPath::PDFEditedPageContentElementPath(PDFPageContentProcessorState state, QPainterPath path, bool strokePath, bool fillPath) :
    PDFEditedPageContentElement(std::move(state)),
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
    return new PDFEditedPageContentElementPath(getState(), getPath(), getStrokePath(), getFillPath());
}

QRectF PDFEditedPageContentElementPath::getBoundingBox() const
{
    QPainterPath mappedPath = getState().getCurrentTransformationMatrix().map(m_path);
    return mappedPath.boundingRect();
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

PDFEditedPageContentElementImage::PDFEditedPageContentElementImage(PDFPageContentProcessorState state, PDFObject imageObject, QImage image) :
    PDFEditedPageContentElement(std::move(state)),
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
    return new PDFEditedPageContentElementImage(getState(), getImageObject(), getImage());
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

PDFEditedPageContentElementText::PDFEditedPageContentElementText(PDFPageContentProcessorState state) :
    PDFEditedPageContentElement(state)
{

}

PDFEditedPageContentElementText::PDFEditedPageContentElementText(PDFPageContentProcessorState state, std::vector<Item> items) :
    PDFEditedPageContentElement(state),
    m_items(std::move(items))
{

}

PDFEditedPageContentElement::Type PDFEditedPageContentElementText::getType() const
{
    return Type::Text;
}

PDFEditedPageContentElementText* PDFEditedPageContentElementText::clone() const
{
    return new PDFEditedPageContentElementText(getState(), getItems());
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

}   // namespace pdf
