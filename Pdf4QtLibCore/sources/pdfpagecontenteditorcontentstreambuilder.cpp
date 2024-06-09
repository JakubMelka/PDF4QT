//    Copyright (C) 2024 Jakub Melka
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

#include "pdfpagecontenteditorcontentstreambuilder.h"
#include "pdfdocumentbuilder.h"
#include "pdfobject.h"
#include "pdfstreamfilters.h"
#include "pdfpainterutils.h"

#include <QStringBuilder>
#include <QXmlStreamReader>

namespace pdf
{

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
            stream << qGray(color.rgb()) / 255.0 << " g" << Qt::endl;
        }
        else if (fillColorSpace && fillColorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceCMYK)
        {
            const PDFColor& fillColor = m_currentState.getFillColorOriginal();
            if (fillColor.size() >= 4)
            {
                stream << fillColor[0] << " " << fillColor[1] << " " << fillColor[2] << " " << fillColor[3] << " k" << Qt::endl;
            }
        }
        else
        {
            stream << color.redF() << " " << color.greenF() << " " << color.blueF() << " rg" << Qt::endl;
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

void PDFPageContentEditorContentStreamBuilder::writeEditedElement(const PDFEditedPageContentElement* element)
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

void PDFPageContentEditorContentStreamBuilder::writeStyledPath(const QPainterPath& path,
                                                               const QPen& pen,
                                                               const QBrush& brush,
                                                               bool isStroking,
                                                               bool isFilling)
{
    PDFPageContentProcessorState newState = m_currentState;
    newState.setCurrentTransformationMatrix(QTransform());

    PDFPainterHelper::applyPenToGraphicState(&newState, pen);
    PDFPainterHelper::applyBrushToGraphicState(&newState, brush);

    QTextStream stream(&m_outputContent, QDataStream::WriteOnly | QDataStream::Append);
    writeStateDifference(stream, newState);

    writePainterPath(stream, path, isStroking, isFilling);
}

}   // namespace pdf
