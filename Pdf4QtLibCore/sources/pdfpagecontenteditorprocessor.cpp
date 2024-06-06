//    Copyright (C) 2023-2024 Jakub Melka
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
#include "pdfdocumentbuilder.h"
#include "pdfobject.h"
#include "pdfstreamfilters.h"

#include <QStringBuilder>
#include <QXmlStreamReader>

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

    m_content.setFontDictionary(*getFontDictionary());
    m_content.setXObjectDictionary(*getXObjectDictionary());
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
            m_contentElementText.reset(new PDFEditedPageContentElementText(*getGraphicState(), getGraphicState()->getCurrentTransformationMatrix()));
        }
    }
    else
    {
        if (currentOperator == Operator::TextEnd && !isTextProcessing())
        {
            if (m_contentElementText)
            {
                m_contentElementText->optimize();

                if (!m_contentElementText->isEmpty())
                {
                    m_contentElementText->setTextPath(std::move(m_textPath));
                    m_contentElementText->setItemsAsText(PDFEditedPageContentElementText::createItemsAsText(m_contentElementText->getState(), m_contentElementText->getItems()));
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
        m_content.addContentPath(*getGraphicState(), path, stroke, fill);
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
    BaseClass::performOriginalImagePainting(image, stream);

    PDFObject imageObject = PDFObject::createStream(std::make_shared<PDFStream>(*stream));
    m_content.addContentImage(*getGraphicState(), std::move(imageObject), QImage());

    return false;
}

void PDFPageContentEditorProcessor::performImagePainting(const QImage& image)
{
    BaseClass::performImagePainting(image);

    PDFEditedPageContentElement* backElement = m_content.getBackElement();
    Q_ASSERT(backElement);

    PDFEditedPageContentElementImage* imageElement = backElement->asImage();
    imageElement->setImage(image);
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

    if (order == ProcessOrder::AfterOperation)
    {
        m_clippingPaths.pop();
    }
}

void PDFPageContentEditorProcessor::performClipping(const QPainterPath& path, Qt::FillRule fillRule)
{
    BaseClass::performClipping(path, fillRule);

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
    m_contentElements.emplace_back(new PDFEditedPageContentElementPath(std::move(state), std::move(path), strokePath, fillPath, state.getCurrentTransformationMatrix()));
}

void PDFEditedPageContent::addContentImage(PDFPageContentProcessorState state, PDFObject imageObject, QImage image)
{
    m_contentElements.emplace_back(new PDFEditedPageContentElementImage(std::move(state), std::move(imageObject), std::move(image), state.getCurrentTransformationMatrix()));
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
    return new PDFEditedPageContentElementPath(getState(), getPath(), getStrokePath(), getFillPath(), getTransform());
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
    return new PDFEditedPageContentElementImage(getState(), getImageObject(), getImage(), getTransform());
}

QRectF PDFEditedPageContentElementImage::getBoundingBox() const
{
    return getTransform().mapRect(QRectF(0, 0, 1, 1));
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
    return new PDFEditedPageContentElementText(getState(), getItems(), getTextPath(), getTransform(), getItemsAsText());
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
    return getTransform().mapRect(m_textPath.boundingRect());
}

QPainterPath PDFEditedPageContentElementText::getTextPath() const
{
    return m_textPath;
}

void PDFEditedPageContentElementText::setTextPath(QPainterPath newTextPath)
{
    m_textPath = newTextPath;
}

QString PDFEditedPageContentElementText::createItemsAsText(const PDFPageContentProcessorState& initialState,
                                                           const std::vector<Item>& items)
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
                if (textItem.isCharacter())
                {
                    if (!textItem.character.isNull())
                    {
                        text += QString(textItem.character).toHtmlEscaped();
                    }
                    else if (textItem.isAdvance())
                    {
                        text += QString("<space advance=\"%1\"/>").arg(textItem.advance);
                    }
                    else if (textItem.cid != 0)
                    {
                        text += QString("<character cid=\"%1\"/>").arg(textItem.cid);
                    }
                }
            }
        }
        else if (item.isUpdateGraphicState)
        {
            PDFPageContentProcessorState newState = state;
            newState.setStateFlags(PDFPageContentProcessorState::StateFlags());

            newState.setState(item.state);
            PDFPageContentProcessorState::StateFlags flags = newState.getStateFlags();

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
                text += QString("<tf font=\"%1\" size=\"%2\"/>").arg(newState.getTextFont()->getFontId()).arg(newState.getTextFontSize());
            }

            if (flags.testFlag(PDFPageContentProcessorState::StateTextMatrix))
            {
                QTransform transform = newState.getTextMatrix();

                qreal x = transform.dx();
                qreal y = transform.dy();

                if (transform.isTranslating())
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

PDFPageContentEditorContentStreamBuilder::PDFPageContentEditorContentStreamBuilder(PDFDocument* document) :
    m_document(document)
{

}

void PDFPageContentEditorContentStreamBuilder::writeStateDifference(QTextStream& stream, const PDFPageContentProcessorState& state)
{
    PDFPageContentProcessorState oldState = m_currentState;
    m_currentState.setState(state);

    auto stateFlags = m_currentState.getStateFlags();

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateCurrentTransformationMatrix))
    {
        QTransform oldTransform = oldState.getCurrentTransformationMatrix();

        if (!oldTransform.isIdentity() && oldTransform.isInvertible())
        {
            oldTransform = oldTransform.inverted();
            PDFReal old_m11 = oldTransform.m11();
            PDFReal old_m12 = oldTransform.m12();
            PDFReal old_m21 = oldTransform.m21();
            PDFReal old_m22 = oldTransform.m22();
            PDFReal old_x = oldTransform.dx();
            PDFReal old_y = oldTransform.dy();

            stream << old_m11 << " " << old_m12 << " " << old_m21 << " " << old_m22 << " " << old_x << " " << old_y << " cm" << Qt::endl;
        }

        QTransform transform = m_currentState.getCurrentTransformationMatrix();

        PDFReal m11 = transform.m11();
        PDFReal m12 = transform.m12();
        PDFReal m21 = transform.m21();
        PDFReal m22 = transform.m22();
        PDFReal x = transform.dx();
        PDFReal y = transform.dy();

        stream << m11 << " " << m12 << " " << m21 << " " << m22 << " " << x << " " << y << " cm" << Qt::endl;
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateLineWidth))
    {
        stream << m_currentState.getLineWidth() << " w" << Qt::endl;
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateLineCapStyle))
    {
        stream << PDFPageContentProcessor::convertPenCapStyleToLineCap(m_currentState.getLineCapStyle()) << " J" << Qt::endl;
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateLineJoinStyle))
    {
        stream << PDFPageContentProcessor::convertPenJoinStyleToLineJoin(m_currentState.getLineJoinStyle()) << " j" << Qt::endl;
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateMitterLimit))
    {
        stream << m_currentState.getMitterLimit() << " M" << Qt::endl;
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateLineDashPattern))
    {
        const PDFLineDashPattern& dashPattern = m_currentState.getLineDashPattern();

        if (dashPattern.isSolid())
        {
            stream << "[] 0 d" << Qt::endl;
        }
        else
        {
            stream << "[ ";

            for (PDFReal arrayItem : dashPattern.getDashArray())
            {
                stream << arrayItem << " ";
            }

            stream << " ] " << dashPattern.getDashOffset() << " d" << Qt::endl;
        }
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateRenderingIntent))
    {
        switch (m_currentState.getRenderingIntent())
        {
        case pdf::RenderingIntent::Perceptual:
            stream << "/Perceptual ri" << Qt::endl;
            break;
        case pdf::RenderingIntent::AbsoluteColorimetric:
            stream << "/AbsoluteColorimetric ri" << Qt::endl;
            break;
        case pdf::RenderingIntent::RelativeColorimetric:
            stream << "/RelativeColorimetric ri" << Qt::endl;
            break;
        case pdf::RenderingIntent::Saturation:
            stream << "/Saturation ri" << Qt::endl;
            break;

        default:
            break;
        }
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateMitterLimit))
    {
        stream << m_currentState.getMitterLimit() << " M" << Qt::endl;
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateFlatness))
    {
        stream << m_currentState.getFlatness() << " i" << Qt::endl;
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateStrokeColor) ||
        stateFlags.testFlag(PDFPageContentProcessorState::StateStrokeColorSpace))
    {
        QColor color = m_currentState.getStrokeColor();
        const PDFAbstractColorSpace* strokeColorSpace = m_currentState.getStrokeColorSpace();
        if (strokeColorSpace && strokeColorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceGray)
        {
            stream << qGray(color.rgb()) / 255.0 << " G" << Qt::endl;
        }
        else if (strokeColorSpace && strokeColorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceCMYK)
        {
            const PDFColor& strokeColorOriginal = m_currentState.getStrokeColorOriginal();
            if (strokeColorOriginal.size() >= 4)
            {
                stream << strokeColorOriginal[0] << " " << strokeColorOriginal[1] << " " << strokeColorOriginal[2] << " " << strokeColorOriginal[3] << " K" << Qt::endl;
            }
        }
        else
        {
            stream << color.redF() << " " << color.greenF() << " " << color.blueF() << " RG" << Qt::endl;
        }
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateFillColor) ||
        stateFlags.testFlag(PDFPageContentProcessorState::StateFillColorSpace))
    {
        QColor color = m_currentState.getFillColor();
        const PDFAbstractColorSpace* fillColorSpace = m_currentState.getFillColorSpace();
        if (fillColorSpace && fillColorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceGray)
        {
            stream << qGray(color.rgb()) / 255.0 << " G" << Qt::endl;
        }
        else if (fillColorSpace && fillColorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceCMYK)
        {
            const PDFColor& fillColor = m_currentState.getFillColorOriginal();
            if (fillColor.size() >= 4)
            {
                stream << fillColor[0] << " " << fillColor[1] << " " << fillColor[2] << " " << fillColor[3] << " K" << Qt::endl;
            }
        }
        else
        {
            stream << color.redF() << " " << color.greenF() << " " << color.blueF() << " RG" << Qt::endl;
        }
    }

    m_currentState.setStateFlags(PDFPageContentProcessorState::StateFlags());


    PDFObjectFactory stateDictionary;
    stateDictionary.beginDictionary();

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateSmoothness))
    {
        stateDictionary.beginDictionaryItem("SM");
        stateDictionary << m_currentState.getSmoothness();
        stateDictionary.endDictionaryItem();
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateAlphaStroking))
    {
        stateDictionary.beginDictionaryItem("CA");
        stateDictionary << m_currentState.getAlphaStroking();
        stateDictionary.endDictionaryItem();
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateAlphaFilling))
    {
        stateDictionary.beginDictionaryItem("ca");
        stateDictionary << m_currentState.getAlphaFilling();
        stateDictionary.endDictionaryItem();
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateAlphaIsShape))
    {
        stateDictionary.beginDictionaryItem("AIS");
        stateDictionary << m_currentState.getAlphaIsShape();
        stateDictionary.endDictionaryItem();
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateTextKnockout))
    {
        stateDictionary.beginDictionaryItem("TK");
        stateDictionary << m_currentState.getTextKnockout();
        stateDictionary.endDictionaryItem();
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateStrokeAdjustment))
    {
        stateDictionary.beginDictionaryItem("SA");
        stateDictionary << m_currentState.getStrokeAdjustment();
        stateDictionary.endDictionaryItem();
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateBlendMode))
    {
        QString blendModeName = PDFBlendModeInfo::getBlendModeName(m_currentState.getBlendMode());

        stateDictionary.beginDictionaryItem("BM");
        stateDictionary << WrapName(blendModeName.toLatin1());
        stateDictionary.endDictionaryItem();
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateOverprint))
    {
        PDFOverprintMode overprintMode = m_currentState.getOverprintMode();

        stateDictionary.beginDictionaryItem("OPM");
        stateDictionary << overprintMode.overprintMode;
        stateDictionary.endDictionaryItem();

        stateDictionary.beginDictionaryItem("OP");
        stateDictionary << overprintMode.overprintStroking;
        stateDictionary.endDictionaryItem();

        stateDictionary.beginDictionaryItem("op");
        stateDictionary << overprintMode.overprintFilling;
        stateDictionary.endDictionaryItem();
    }

    stateDictionary.endDictionary();
    PDFObject stateObject = stateDictionary.takeObject();

    const PDFDictionary* dictionary = m_document->getDictionaryFromObject(stateObject);
    if (dictionary && dictionary->getCount() > 0)
    {
        // Apply state
        QByteArray key;

        for (size_t i = 0; i < m_graphicStateDictionary.getCount(); ++i)
        {
            const PDFDictionary* currentDictionary = m_document->getDictionaryFromObject(m_graphicStateDictionary.getValue(i));
            if (*currentDictionary == *dictionary)
            {
                key = m_graphicStateDictionary.getKey(i).getString();
                break;
            }
        }

        if (key.isEmpty())
        {
            int i = 0;
            while (true)
            {
                QByteArray currentKey = QString("s%1").arg(++i).toLatin1();
                if (!m_graphicStateDictionary.hasKey(currentKey))
                {
                    m_graphicStateDictionary.addEntry(PDFInplaceOrMemoryString(currentKey), std::move(stateObject));
                    key = currentKey;
                    break;
                }
            }
        }

        stream << "/" << key << " gs" << Qt::endl;
    }
}

void PDFPageContentEditorContentStreamBuilder::writeElement(const PDFEditedPageContentElement* element)
{
    PDFPageContentProcessorState state = element->getState();
    state.setCurrentTransformationMatrix(element->getTransform());

    QTextStream stream(&m_outputContent, QDataStream::WriteOnly | QDataStream::Append);
    writeStateDifference(stream, state);

    if (const PDFEditedPageContentElementImage* imageElement = element->asImage())
    {
        QImage image = imageElement->getImage();

        writeImage(stream, image);
    }

    if (const PDFEditedPageContentElementPath* pathElement = element->asPath())
    {
        const bool isStroking = pathElement->getStrokePath();
        const bool isFilling = pathElement->getFillPath();

        writePainterPath(stream, pathElement->getPath(), isStroking, isFilling);
    }

    if (const PDFEditedPageContentElementText* textElement = element->asText())
    {
        QString text = textElement->getItemsAsText();

        if (!text.isEmpty())
        {
            writeText(stream, text);
        }
    }

    stream << Qt::endl;
}

const QByteArray& PDFPageContentEditorContentStreamBuilder::getOutputContent() const
{
    return m_outputContent;
}

void PDFPageContentEditorContentStreamBuilder::writePainterPath(QTextStream& stream,
                                                                const QPainterPath& path,
                                                                bool isStroking,
                                                                bool isFilling)
{
    const int elementCount = path.elementCount();

    for (int i = 0; i < elementCount; ++i)
    {
        QPainterPath::Element element = path.elementAt(i);

        switch (element.type)
        {
        case QPainterPath::MoveToElement:
            stream << element.x << " " << element.y << " m" << Qt::endl;
            break;
        case QPainterPath::LineToElement:
            stream << element.x << " " << element.y << " l" << Qt::endl;
            break;
        case QPainterPath::CurveToElement:
            stream << element.x << " " << element.y << " c" << Qt::endl;
            break;
        case QPainterPath::CurveToDataElement:
            stream << element.x << " " << element.y << " ";
            break;
        default:
            break;
        }
    }

    if (isStroking && !isFilling)
    {
        stream << "S" << Qt::endl;
    }
    else if (isStroking || isFilling)
    {
        switch (path.fillRule())
        {
        case Qt::OddEvenFill:
            if (isFilling && isStroking)
            {
                stream << "B*" << Qt::endl;
            }
            else
            {
                stream << "f*" << Qt::endl;
            }
            break;
        case Qt::WindingFill:
            if (isFilling && isStroking)
            {
                stream << "B" << Qt::endl;
            }
            else
            {
                stream << "f" << Qt::endl;
            }
            break;
        default:
            break;
        }
    }
    else
    {
        stream << "n" << Qt::endl;
    }
}

void PDFPageContentEditorContentStreamBuilder::writeText(QTextStream& stream, const QString& text)
{
    stream << "q BT" << Qt::endl;

    QString xml = QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?><doc>%1</doc>").arg(text);

    QXmlStreamReader reader(xml);
    m_textFont = m_currentState.getTextFont();

    while (!reader.atEnd() && !reader.hasError())
    {
        reader.readNext();

        switch (reader.tokenType())
        {
            case QXmlStreamReader::NoToken:
                break;

            case QXmlStreamReader::Invalid:
                addError(PDFTranslationContext::tr("Invalid XML text."));
                break;

            case QXmlStreamReader::StartDocument:
            case QXmlStreamReader::EndDocument:
            case QXmlStreamReader::EndElement:
            case QXmlStreamReader::Comment:
            case QXmlStreamReader::DTD:
            case QXmlStreamReader::ProcessingInstruction:
            case QXmlStreamReader::EntityReference:
                break;

            case QXmlStreamReader::StartElement:
                writeTextCommand(stream, reader);
                break;

            case QXmlStreamReader::Characters:
            {
                QString characters = reader.text().toString();

                if (m_textFont)
                {
                    PDFEncodedText encodedText = m_textFont->encodeText(characters);

                    if (!encodedText.encodedText.isEmpty())
                    {
                        stream << "<" << encodedText.encodedText.toHex() << "> Tj" << Qt::endl;
                    }

                    if (!encodedText.isValid)
                    {
                        addError(PDFTranslationContext::tr("Error during converting text to font encoding. Some characters were not converted: '%1'.").arg(encodedText.errorString));
                    }
                }
                else
                {
                    addError(PDFTranslationContext::tr("Text font not defined!"));
                }
                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }
    }

    stream << "ET Q" << Qt::endl;
}

void PDFPageContentEditorContentStreamBuilder::writeTextCommand(QTextStream& stream, const QXmlStreamReader& reader)
{
    QXmlStreamAttributes attributes = reader.attributes();

    auto isCommand = [&reader](const char* tag) -> bool
    {
        QString tagString = reader.name().toString();
        QXmlStreamAttributes attributes = reader.attributes();
        return tagString == QLatin1String(tag) && attributes.size() == 1 && attributes.hasAttribute("v");
    };

    if (reader.name().toString() == "doc")
    {
        return;
    }

    if (isCommand("tr"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const int textRenderingMode = attribute.value().toInt(&ok);
        if (!ok || textRenderingMode < 0 || textRenderingMode > 7)
        {
            addError(PDFTranslationContext::tr("Invalid rendering mode '%1'. Valid values are 0-7.").arg(textRenderingMode));
        }
        else
        {
            stream << textRenderingMode << " Tr" << Qt::endl;
        }
    }
    else if (isCommand("ts"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const double textRise = attribute.value().toDouble(&ok);

        if (!ok)
        {
            addError(PDFTranslationContext::tr("Cannot convert text '%1' to number.").arg(attribute.value().toString()));
        }
        else
        {
            stream << textRise << " Ts" << Qt::endl;
        }
    }
    else if (isCommand("tc"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const double textCharacterSpacing = attribute.value().toDouble(&ok);

        if (!ok)
        {
            addError(PDFTranslationContext::tr("Cannot convert text '%1' to number.").arg(attribute.value().toString()));
        }
        else
        {
            stream << textCharacterSpacing << " Tc" << Qt::endl;
        }
    }
    else if (isCommand("tw"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const double textWordSpacing = attribute.value().toDouble(&ok);

        if (!ok)
        {
            addError(PDFTranslationContext::tr("Cannot convert text '%1' to number.").arg(attribute.value().toString()));
        }
        else
        {
            stream << textWordSpacing << " Tw" << Qt::endl;
        }
    }
    else if (isCommand("tl"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const double textLeading = attribute.value().toDouble(&ok);

        if (!ok)
        {
            addError(PDFTranslationContext::tr("Cannot convert text '%1' to number.").arg(attribute.value().toString()));
        }
        else
        {
            stream << textLeading << " TL" << Qt::endl;
        }
    }
    else if (isCommand("tz"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const PDFReal textScaling = attribute.value().toDouble(&ok);

        if (!ok)
        {
            addError(PDFTranslationContext::tr("Cannot convert text '%1' to number.").arg(attribute.value().toString()));
        }
        else
        {
            stream << textScaling << " Tz" << Qt::endl;
        }
    }
    else if (reader.name().toString() == "tf")
    {
        if (attributes.hasAttribute("font") && attributes.hasAttribute("size"))
        {
            bool ok = false;
            QByteArray v1 = attributes.value("font").toString().toLatin1();
            PDFReal v2 = attributes.value("size").toDouble(&ok);

            if (!ok)
            {
                addError(PDFTranslationContext::tr("Cannot convert text '%1' to number.").arg(attributes.value("size").toString()));
            }
            else
            {
                v1 = selectFont(v1);
                stream << "/" << v1 << " " << v2 << " Tf" << Qt::endl;
            }
        }
        else
        {
            addError(PDFTranslationContext::tr("Text font command requires two attributes - font and size."));
        }
    }
    else if (reader.name().toString() == "tpos")
    {
        if (attributes.hasAttribute("x") && attributes.hasAttribute("y"))
        {
            bool ok1 = false;
            bool ok2 = false;
            PDFReal v1 = attributes.value("x").toDouble(&ok1);
            PDFReal v2 = attributes.value("y").toDouble(&ok2);

            if (!ok1)
            {
                addError(PDFTranslationContext::tr("Cannot convert text '%1' to number.").arg(attributes.value("x").toString()));
            }
            else if (!ok2)
            {
                addError(PDFTranslationContext::tr("Cannot convert text '%1' to number.").arg(attributes.value("y").toString()));
            }
            else
            {
                stream << v1 << " " << v2 << " Td" << Qt::endl;
            }
        }
        else
        {
            addError(PDFTranslationContext::tr("Text translation command requires two attributes - x and y."));
        }
    }
    else if (reader.name().toString() == "tmatrix")
    {
        if (attributes.hasAttribute("m11") && attributes.hasAttribute("m12") &&
            attributes.hasAttribute("m21") && attributes.hasAttribute("m22") &&
            attributes.hasAttribute("x") && attributes.hasAttribute("y"))
        {
            bool ok1 = false;
            bool ok2 = false;
            bool ok3 = false;
            bool ok4 = false;
            bool ok5 = false;
            bool ok6 = false;
            PDFReal m11 = attributes.value("m11").toDouble(&ok1);
            PDFReal m12 = attributes.value("m12").toDouble(&ok2);
            PDFReal m21 = attributes.value("m21").toDouble(&ok3);
            PDFReal m22 = attributes.value("m22").toDouble(&ok4);
            PDFReal x = attributes.value("x").toDouble(&ok5);
            PDFReal y = attributes.value("y").toDouble(&ok6);

            if (!ok1 || !ok2 || !ok3 || !ok4 || !ok5 | !ok6)
            {
                addError(PDFTranslationContext::tr("Invalid text matrix parameters."));
            }

            else
            {
                stream << m11 << " " << m12 << " " << m21 << " " << m22 << " " << x << " " << y << " Tm" << Qt::endl;
            }
        }
        else
        {
            addError(PDFTranslationContext::tr("Set text matrix command requires six elements - m11, m12, m21, m22, x, y."));
        }
    }
    else
    {
        addError(PDFTranslationContext::tr("Invalid command '%1'.").arg(reader.name().toString()));
    }
}

void PDFPageContentEditorContentStreamBuilder::writeImage(QTextStream& stream, const QImage& image)
{
    QByteArray key;

    int i = 0;
    while (true)
    {
        QByteArray currentKey = QString("Im%1").arg(++i).toLatin1();
        if (!m_xobjectDictionary.hasKey(currentKey))
        {
            PDFArray array;
            array.appendItem(PDFObject::createName("FlateDecode"));

            QImage codedImage = image;
            codedImage = codedImage.convertToFormat(QImage::Format_ARGB32);

            QByteArray decodedStream(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes());

            // Compress the content stream
            QByteArray compressedData = PDFFlateDecodeFilter::compress(decodedStream);
            PDFDictionary imageDictionary;
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Subtitle"), PDFObject::createName("Image"));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(image.width()));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(image.height()));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceRGB"));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedData.size()));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createArray(std::make_shared<PDFArray>(qMove(array))));
            PDFObject imageObject = PDFObject::createStream(std::make_shared<PDFStream>(qMove(imageDictionary), qMove(compressedData)));

            m_xobjectDictionary.addEntry(PDFInplaceOrMemoryString(currentKey), std::move(imageObject));
            key = currentKey;
            break;
        }
    }

    stream << "/" << key << " Do" << Qt::endl;
}

QByteArray PDFPageContentEditorContentStreamBuilder::selectFont(const QByteArray& font)
{
    m_textFont = nullptr;

    PDFObject fontObject = m_fontDictionary.get(font);
    if (!fontObject.isNull())
    {
        try
        {
            m_textFont = PDFFont::createFont(fontObject, font, m_document);
        }
        catch (const PDFException&)
        {
            addError(PDFTranslationContext::tr("Font '%1' is invalid.").arg(QString::fromLatin1(font)));
        }
    }

    if (!m_textFont)
    {
        QByteArray defaultFontKey = "PDF4QT_DefFnt";
        if (!m_fontDictionary.hasKey(defaultFontKey))
        {
            PDFObjectFactory defaultFontFactory;

            defaultFontFactory.beginDictionary();
            defaultFontFactory.beginDictionaryItem("Type");
            defaultFontFactory << WrapName("Font");
            defaultFontFactory.endDictionaryItem();
            defaultFontFactory.beginDictionaryItem("Subtype");
            defaultFontFactory << WrapName("Type1");
            defaultFontFactory.endDictionaryItem();
            defaultFontFactory.beginDictionaryItem("BaseFont");
            defaultFontFactory << WrapName("Helvetica");
            defaultFontFactory.endDictionaryItem();
            defaultFontFactory.beginDictionaryItem("Encoding");
            defaultFontFactory << WrapName("WinAnsiEncoding");
            defaultFontFactory.endDictionaryItem();
            defaultFontFactory.endDictionary();

            m_fontDictionary.setEntry(PDFInplaceOrMemoryString(defaultFontKey), defaultFontFactory.takeObject());
        }

        m_textFont = PDFFont::createFont(fontObject, font, m_document);
        return defaultFontKey;
    }

    return font;
}

void PDFPageContentEditorContentStreamBuilder::addError(const QString& error)
{
    m_errors << error;
}

void PDFPageContentEditorContentStreamBuilder::setFontDictionary(const PDFDictionary& newFontDictionary)
{
    m_fontDictionary = newFontDictionary;
}

}   // namespace pdf
