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

#include "pdfpagecontenteditorcontentstreambuilder.h"
#include "pdfdocumentbuilder.h"
#include "pdfobject.h"
#include "pdfstreamfilters.h"
#include "pdfpainterutils.h"

#include <algorithm>
#include <exception>
#include <QBuffer>
#include <QPainter>
#include <QStringBuilder>
#include <QXmlStreamReader>
#include <QPaintEngine>

namespace pdf
{

namespace pagecontenteditorcontentstreambuilder
{

/// Formats the real number, so it can be written into the content stream.
/// Numbers in the content stream must not use the exponential notation
/// (see PDF 32000-1, chapter 7.3.3), but the default QTextStream formatting
/// produces it for very small or very large values (for example '1e-05').
/// Such a number is rejected by the parser and the whole operator, in which
/// it appears, is skipped.
QByteArray formatNumber(PDFReal value)
{
    return PDFDocumentBuilder::formatPDFReal(value);
}

/// Returns the image samples as a continuous byte array, without the padding
/// which the image can have at the end of each scanline.
/// \param image Image
/// \param bytesPerPixel Number of bytes of a single pixel
QByteArray getImageSamples(const QImage& image, int bytesPerPixel)
{
    QByteArray samples;
    QBuffer buffer(&samples);

    if (buffer.open(QIODevice::WriteOnly))
    {
        const int bytesPerScanLine = qMin(bytesPerPixel * image.width(), int(image.bytesPerLine()));

        for (int scanLineIndex = 0; scanLineIndex < image.height(); ++scanLineIndex)
        {
            buffer.write((const char*)image.constScanLine(scanLineIndex), bytesPerScanLine);
        }

        buffer.close();
    }

    return samples;
}

/// Creates the soft mask image stream from the alpha channel of an image.
/// The mask is a grayscale image, where the sample value is the opacity
/// of the corresponding pixel (see PDF 32000-1, chapter 11.6.5.3).
/// \param alphaImage Alpha channel of the image (format \p Format_Alpha8)
PDFObject createSoftMaskObject(const QImage& alphaImage)
{
    Q_ASSERT(alphaImage.format() == QImage::Format_Alpha8);

    PDFArrayBuilder filter;
    filter.appendItem(PDFObject::createName("FlateDecode"));

    QByteArray compressedData = PDFFlateDecodeFilter::compress(getImageSamples(alphaImage, 1));

    PDFDictionaryBuilder softMaskDictionary;
    softMaskDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
    softMaskDictionary.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(alphaImage.width()));
    softMaskDictionary.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(alphaImage.height()));
    softMaskDictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceGray"));
    softMaskDictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
    softMaskDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedData.size()));
    softMaskDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createArray(qMove(filter)));

    return PDFObject::createStream(PDFStream(qMove(softMaskDictionary), qMove(compressedData)));
}

}   // namespace pagecontenteditorcontentstreambuilder

using namespace pagecontenteditorcontentstreambuilder;

class PDFContentEditorPaintEngine : public QPaintEngine
{
public:
    PDFContentEditorPaintEngine(PDFPageContentEditorContentStreamBuilder* builder) :
        QPaintEngine(PrimitiveTransform | AlphaBlend | PorterDuff | PainterPaths | ConstantOpacity | BlendModes | PaintOutsidePaintEvent),
        m_builder(builder)
    {

    }

    virtual Type type() const override { return User; }

    virtual bool begin(QPaintDevice*) override;
    virtual bool end() override;

    virtual void updateState(const QPaintEngineState& state) override;
    virtual void drawPixmap(const QRectF& r, const QPixmap& pm, const QRectF& sr) override;

    virtual void drawPath(const QPainterPath& path) override;
    virtual void drawPolygon(const QPointF* points, int pointCount, PolygonDrawMode mode) override;

private:
    /// Applies clip operation with the given path. The path is expressed
    /// in the painter logical coordinates and is mapped to the page space
    /// by the current transformation matrix.
    void applyClip(const QPainterPath& path, Qt::ClipOperation operation);

    /// Returns the active clip path in the page coordinate space,
    /// or an empty path if clipping is not active.
    QPainterPath getEffectiveClipPath() const;

    PDFPageContentProcessorState m_state;
    PDFPageContentEditorContentStreamBuilder* m_builder = nullptr;
    QPainterPath m_clipPath; ///< Clip path in the page coordinate space
    bool m_hasClip = false;
    bool m_isClipEnabled = true;
    bool m_isFillActive = false;
    bool m_isStrokeActive = false;
};

bool PDFContentEditorPaintEngine::begin(QPaintDevice*)
{
    return !isActive();
}

bool PDFContentEditorPaintEngine::end()
{
    return true;
}

void PDFContentEditorPaintEngine::updateState(const QPaintEngineState& newState)
{
    QPaintEngine::DirtyFlags stateFlags = newState.state();

    if (stateFlags.testFlag(QPaintEngine::DirtyPen))
    {
        PDFPainterHelper::applyPenToGraphicState(&m_state, newState.pen());
        m_isStrokeActive = newState.pen().style() != Qt::NoPen;
    }

    if (stateFlags.testFlag(QPaintEngine::DirtyBrush))
    {
        PDFPainterHelper::applyBrushToGraphicState(&m_state, newState.brush());
        m_isFillActive = newState.brush().style() != Qt::NoBrush;
    }

    if (stateFlags.testFlag(QPaintEngine::DirtyTransform))
    {
        m_state.setCurrentTransformationMatrix(newState.transform());
    }

    if (stateFlags.testFlag(QPaintEngine::DirtyCompositionMode))
    {
        m_state.setBlendMode(PDFBlendModeInfo::getBlendModeFromCompositionMode(newState.compositionMode()));
    }

    if (stateFlags.testFlag(QPaintEngine::DirtyOpacity))
    {
        m_state.setAlphaFilling(newState.opacity());
        m_state.setAlphaStroking(newState.opacity());
    }

    // Clip handling must be performed after the transform handling above,
    // because the clip path is mapped to the page space by the current
    // transformation matrix.
    if (stateFlags.testFlag(QPaintEngine::DirtyClipEnabled))
    {
        m_isClipEnabled = newState.isClipEnabled();
    }

    if (stateFlags.testFlag(QPaintEngine::DirtyClipRegion))
    {
        QPainterPath clipPath;
        for (const QRect& rect : newState.clipRegion())
        {
            clipPath.addRect(rect);
        }
        applyClip(clipPath, newState.clipOperation());
    }

    if (stateFlags.testFlag(QPaintEngine::DirtyClipPath))
    {
        applyClip(newState.clipPath(), newState.clipOperation());
    }
}

void PDFContentEditorPaintEngine::applyClip(const QPainterPath& path, Qt::ClipOperation operation)
{
    switch (operation)
    {
    case Qt::NoClip:
        m_hasClip = false;
        m_clipPath = QPainterPath();
        break;

    case Qt::ReplaceClip:
        m_clipPath = m_state.getCurrentTransformationMatrix().map(path);
        m_hasClip = true;
        break;

    case Qt::IntersectClip:
    {
        QPainterPath mappedPath = m_state.getCurrentTransformationMatrix().map(path);
        m_clipPath = m_hasClip ? m_clipPath.intersected(mappedPath) : mappedPath;
        m_hasClip = true;

        if (m_clipPath.isEmpty())
        {
            // The intersection has zero area, but an empty path means
            // "no clipping". Store a degenerate path with zero fill area
            // instead, which clips away all content.
            m_clipPath.moveTo(0, 0);
            m_clipPath.lineTo(1, 0);
        }
        break;
    }

    default:
        break;
    }
}

QPainterPath PDFContentEditorPaintEngine::getEffectiveClipPath() const
{
    if (m_hasClip && m_isClipEnabled)
    {
        return m_clipPath;
    }

    return QPainterPath();
}

void PDFContentEditorPaintEngine::drawPixmap(const QRectF& r, const QPixmap& pm, const QRectF& sr)
{
    QPixmap pixmap = pm.copy(sr.toRect());
    m_builder->writeImage(pixmap.toImage(), m_state.getCurrentTransformationMatrix(), r, getEffectiveClipPath());
}

void PDFContentEditorPaintEngine::drawPath(const QPainterPath& path)
{
    m_builder->writeStyledPath(path, m_state, m_isStrokeActive, m_isFillActive, getEffectiveClipPath());
}

void PDFContentEditorPaintEngine::drawPolygon(const QPointF* points,
                                              int pointCount,
                                              PolygonDrawMode mode)
{
    bool isStroking = m_isStrokeActive;
    bool isFilling = m_isFillActive && mode != PolylineMode;

    QPolygonF polygon;
    for (int i = 0; i < pointCount; ++i)
    {
        polygon << points[i];
    }

    QPainterPath path;
    path.addPolygon(polygon);

    Qt::FillRule fillRule = Qt::OddEvenFill;
    switch (mode)
    {
    case QPaintEngine::OddEvenMode:
        fillRule = Qt::OddEvenFill;
        break;
    case QPaintEngine::WindingMode:
        fillRule = Qt::WindingFill;
        break;
    case QPaintEngine::ConvexMode:
        break;
    case QPaintEngine::PolylineMode:
        break;
    }

    path.setFillRule(fillRule);

    m_builder->writeStyledPath(path, m_state, isStroking, isFilling, getEffectiveClipPath());
}

PDFContentEditorPaintDevice::PDFContentEditorPaintDevice(PDFPageContentEditorContentStreamBuilder* builder, QRectF mediaRect, QRectF mediaRectMM) :
    m_paintEngine(new PDFContentEditorPaintEngine(builder)),
    m_mediaRect(mediaRect),
    m_mediaRectMM(mediaRectMM)
{

}

int PDFContentEditorPaintDevice::metric(PaintDeviceMetric metric) const
{
    switch (metric)
    {
    case QPaintDevice::PdmWidth:
        return m_mediaRect.width();
    case QPaintDevice::PdmHeight:
        return m_mediaRect.height();
    case QPaintDevice::PdmWidthMM:
        return m_mediaRectMM.width();
    case QPaintDevice::PdmHeightMM:
        return m_mediaRectMM.height();
    case QPaintDevice::PdmNumColors:
        return INT_MAX;
    case QPaintDevice::PdmDepth:
        return 8;
    case QPaintDevice::PdmDpiX:
    case QPaintDevice::PdmPhysicalDpiX:
        return m_mediaRect.width() * 25.4 / m_mediaRectMM.width();
    case QPaintDevice::PdmDpiY:
    case QPaintDevice::PdmPhysicalDpiY:
        return m_mediaRect.height() * 25.4 / m_mediaRectMM.height();
    case QPaintDevice::PdmDevicePixelRatio:
        return 1;
    case QPaintDevice::PdmDevicePixelRatioScaled:
        return int(1.0 * QPaintDevice::devicePixelRatioFScale());
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    case QPaintDevice::PdmDevicePixelRatioF_EncodedA:
    case QPaintDevice::PdmDevicePixelRatioF_EncodedB:
        return QPaintDevice::encodeMetricF(metric, 1.0);
#endif
    default:
        Q_ASSERT(false);
        break;
    }

    return 0;
}

PDFContentEditorPaintDevice::~PDFContentEditorPaintDevice()
{
    delete m_paintEngine;
}

int PDFContentEditorPaintDevice::devType() const
{
    return QInternal::Picture;
}

QPaintEngine* PDFContentEditorPaintDevice::paintEngine() const
{
    return m_paintEngine;
}

PDFPageContentEditorContentStreamBuilder::PDFPageContentEditorContentStreamBuilder(PDFDocument* document) :
    m_document(document)
{

}

void PDFPageContentEditorContentStreamBuilder::writeStateDifference(QTextStream& stream, const PDFPageContentProcessorState& state)
{
    m_currentState.setState(state);

    auto stateFlags = m_currentState.getStateFlags();

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateLineWidth))
    {
        stream << formatNumber(m_currentState.getLineWidth()) << " w" << Qt::endl;
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
        stream << formatNumber(m_currentState.getMitterLimit()) << " M" << Qt::endl;
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
                stream << formatNumber(arrayItem) << " ";
            }

            stream << " ] " << formatNumber(dashPattern.getDashOffset()) << " d" << Qt::endl;
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

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateFlatness))
    {
        stream << formatNumber(m_currentState.getFlatness()) << " i" << Qt::endl;
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateStrokeColor) ||
        stateFlags.testFlag(PDFPageContentProcessorState::StateStrokeColorSpace))
    {
        QColor color = m_currentState.getStrokeColor();
        const PDFAbstractColorSpace* strokeColorSpace = m_currentState.getStrokeColorSpace();
        if (strokeColorSpace && strokeColorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceGray)
        {
            stream << formatNumber(qGray(color.rgb()) / 255.0) << " G" << Qt::endl;
        }
        else if (strokeColorSpace && strokeColorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceCMYK)
        {
            const PDFColor& strokeColorOriginal = m_currentState.getStrokeColorOriginal();
            if (strokeColorOriginal.size() >= 4)
            {
                stream << formatNumber(strokeColorOriginal[0]) << " " << formatNumber(strokeColorOriginal[1]) << " " << formatNumber(strokeColorOriginal[2]) << " " << formatNumber(strokeColorOriginal[3]) << " K" << Qt::endl;
            }
        }
        else
        {
            stream << formatNumber(color.redF()) << " " << formatNumber(color.greenF()) << " " << formatNumber(color.blueF()) << " RG" << Qt::endl;
        }
    }

    if (stateFlags.testFlag(PDFPageContentProcessorState::StateFillColor) ||
        stateFlags.testFlag(PDFPageContentProcessorState::StateFillColorSpace))
    {
        QColor color = m_currentState.getFillColor();
        const PDFAbstractColorSpace* fillColorSpace = m_currentState.getFillColorSpace();
        if (fillColorSpace && fillColorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceGray)
        {
            stream << formatNumber(qGray(color.rgb()) / 255.0) << " g" << Qt::endl;
        }
        else if (fillColorSpace && fillColorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::DeviceCMYK)
        {
            const PDFColor& fillColor = m_currentState.getFillColorOriginal();
            if (fillColor.size() >= 4)
            {
                stream << formatNumber(fillColor[0]) << " " << formatNumber(fillColor[1]) << " " << formatNumber(fillColor[2]) << " " << formatNumber(fillColor[3]) << " k" << Qt::endl;
            }
        }
        else
        {
            stream << formatNumber(color.redF()) << " " << formatNumber(color.greenF()) << " " << formatNumber(color.blueF()) << " rg" << Qt::endl;
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
    updateTransparencyGroups(element);

    PDFPageContentProcessorState state = element->getState();
    state.setCurrentTransformationMatrix(element->getTransform());

    QTextStream stream(&m_outputContent, QDataStream::WriteOnly | QDataStream::Append);
    writeStateDifference(stream, state);

    const QPainterPath& clipPath = element->getClipPath();
    const bool isNeededToWriteCurrentTransformationMatrix = this->isNeededToWriteCurrentTransformationMatrix();
    const bool isNeededGraphicStateSave = isNeededToWriteCurrentTransformationMatrix || !clipPath.isEmpty();

    if (isNeededGraphicStateSave)
    {
        stream << "q" << Qt::endl;

        if (isNeededToWriteCurrentTransformationMatrix)
        {
            writeCurrentTransformationMatrix(stream);
        }

        // The element clip path is expressed in the element coordinate
        // space, so it must be written after the transformation matrix.
        if (!clipPath.isEmpty())
        {
            writeClipPath(stream, clipPath);
        }
    }

    if (const PDFEditedPageContentElementImage* imageElement = element->asImage())
    {
        const PDFObject imageObject = imageElement->getImageObject();
        const PDFDictionary* imageDictionary = m_document->getDictionaryFromObject(imageObject);
        const bool isReusableImageXObject = imageDictionary &&
                                            imageDictionary->hasKey("Subtype") &&
                                            m_document->getObject(imageDictionary->get("Subtype")).isName() &&
                                            m_document->getObject(imageDictionary->get("Subtype")).getString() == "Image";

        if (isReusableImageXObject)
        {
            writeImageObject(stream, imageObject);
        }
        else
        {
            writeImage(stream, imageElement->getImage());
        }
    }

    if (const PDFEditedPageContentElementPath* pathElement = element->asPath())
    {
        const bool isStroking = pathElement->getStrokePath();
        const bool isFilling = pathElement->getFillPath();

        writePainterPath(stream, pathElement->getPath(), isStroking, isFilling);
    }

    if (const PDFEditedPageContentElementShading* shadingElement = element->asShading())
    {
        writeShadingObject(stream, shadingElement->getShadingObject());
    }

    if (const PDFEditedPageContentElementText* textElement = element->asText())
    {
        QString text = textElement->getItemsAsText();

        if (!text.isEmpty())
        {
            auto previousOverrides = m_fontOverrides;
            auto previousFontResourceObjects = m_fontResourceObjects;
            m_fontOverrides.clear();
            m_fontResourceObjects.clear();

            // Fonts are identified by the keys of the text element. The same key can
            // denote a different font in the page resources (the text can be painted
            // by a form XObject, which has its own resources), so the font object is
            // used to find (or to create) the right entry of the font dictionary.
            for (const PDFEditedPageContentElementText::FontResource& fontResource : textElement->getFontResources())
            {
                if (fontResource.font)
                {
                    m_fontOverrides.insert(fontResource.key, fontResource.font);
                }

                if (!fontResource.fontObject.isNull())
                {
                    m_fontResourceObjects.insert(fontResource.key, fontResource.fontObject);
                }
            }

            auto addFontOverride = [this](const PDFFontPointer& font)
            {
                if (font && !font->getFontId().isEmpty() && !m_fontOverrides.contains(font->getFontId()))
                {
                    m_fontOverrides.insert(font->getFontId(), font);
                }
            };

            PDFPageContentProcessorState textState = element->getState();
            textState.setStateFlags(PDFPageContentProcessorState::StateFlags());
            addFontOverride(textState.getTextFont());

            for (const PDFEditedPageContentElementText::Item& item : textElement->getItems())
            {
                if (!item.isUpdateGraphicState)
                {
                    continue;
                }

                PDFPageContentProcessorState updatedState = textState;
                updatedState.setState(item.state);
                PDFPageContentProcessorState::StateFlags flags = updatedState.getStateFlags();
                textState = updatedState;
                textState.setStateFlags(PDFPageContentProcessorState::StateFlags());

                if ((flags.testFlag(PDFPageContentProcessorState::StateTextFont) ||
                     flags.testFlag(PDFPageContentProcessorState::StateTextFontSize)))
                {
                    addFontOverride(textState.getTextFont());
                }
            }

            writeText(stream, text, textElement->getFontResourceKey(m_currentState.getTextFont().data()));
            m_fontOverrides = std::move(previousOverrides);
            m_fontResourceObjects = std::move(previousFontResourceObjects);
        }
    }

    if (isNeededGraphicStateSave)
    {
        stream << "Q" << Qt::endl;
    }
}

const QByteArray& PDFPageContentEditorContentStreamBuilder::getOutputContent()
{
    finishTransparencyGroups();
    return m_outputContent;
}

void PDFPageContentEditorContentStreamBuilder::finishTransparencyGroups()
{
    while (!m_transparencyGroups.empty())
    {
        endTransparencyGroup();
    }
}

void PDFPageContentEditorContentStreamBuilder::updateTransparencyGroups(const PDFEditedPageContentElement* element)
{
    std::vector<PDFEditedPageContentTransparencyGroupPointer> groups;
    for (PDFEditedPageContentTransparencyGroupPointer group = element->getTransparencyGroup(); group; group = group->parent)
    {
        groups.push_back(group);
    }
    std::reverse(groups.begin(), groups.end());

    size_t commonGroupCount = 0;
    while (commonGroupCount < groups.size() &&
           commonGroupCount < m_transparencyGroups.size() &&
           m_transparencyGroups[commonGroupCount].group == groups[commonGroupCount])
    {
        ++commonGroupCount;
    }

    while (m_transparencyGroups.size() > commonGroupCount)
    {
        endTransparencyGroup();
    }

    for (size_t i = commonGroupCount; i < groups.size(); ++i)
    {
        beginTransparencyGroup(groups[i]);
    }

    if (!m_transparencyGroups.empty())
    {
        const QRectF boundingBox = getPaintedAreaBoundingBox(element);
        if (!boundingBox.isEmpty())
        {
            for (OpenTransparencyGroup& openGroup : m_transparencyGroups)
            {
                openGroup.boundingBox = openGroup.boundingBox.united(boundingBox);
            }
        }
    }
}

QRectF PDFPageContentEditorContentStreamBuilder::getPaintedAreaBoundingBox(const PDFEditedPageContentElement* element)
{
    const QTransform transform = element->getTransform();

    // Bounding box of the element geometry, which is not limited by the clip path
    QRectF boundingBox;
    if (const PDFEditedPageContentElementPath* pathElement = element->asPath())
    {
        boundingBox = transform.map(pathElement->getPath()).boundingRect();
    }
    else if (const PDFEditedPageContentElementText* textElement = element->asText())
    {
        boundingBox = transform.mapRect(textElement->getTextPath().boundingRect());
    }
    else if (const PDFEditedPageContentElementImage* imageElement = element->asImage())
    {
        Q_UNUSED(imageElement);
        boundingBox = transform.mapRect(QRectF(0.0, 0.0, 1.0, 1.0));
    }
    else if (const PDFEditedPageContentElementShading* shadingElement = element->asShading())
    {
        boundingBox = transform.mapRect(shadingElement->getArea().boundingRect());
    }
    else
    {
        boundingBox = element->getBoundingBox();
    }

    // The geometry doesn't contain the stroke of the path (or of the text). A miter join
    // exceeds the geometry at most by the half of the line width multiplied by the miter
    // limit (the painter uses the whole line width as the unit of the miter limit, so it
    // is used here to be safe). Line caps exceed the geometry by less than the line width.
    const PDFPageContentProcessorState& state = element->getState();
    const PDFReal scale = qMax(qAbs(transform.m11()) + qAbs(transform.m21()), qAbs(transform.m12()) + qAbs(transform.m22()));
    const PDFReal margin = state.getLineWidth() * qMax<PDFReal>(state.getMitterLimit(), 1.0) * scale + 1.0;
    boundingBox.adjust(-margin, -margin, margin, margin);

    // The element can't paint anything outside of its clip path
    const QPainterPath& clipPath = element->getClipPath();
    if (!clipPath.isEmpty())
    {
        boundingBox = boundingBox.intersected(transform.map(clipPath).boundingRect());
    }

    return boundingBox;
}

void PDFPageContentEditorContentStreamBuilder::beginTransparencyGroup(const PDFEditedPageContentTransparencyGroupPointer& group)
{
    OpenTransparencyGroup openGroup;
    openGroup.group = group;
    openGroup.outputContent = std::move(m_outputContent);
    openGroup.state = m_currentState;
    openGroup.state.setStateFlags(PDFPageContentProcessorState::StateFlags());
    m_transparencyGroups.push_back(std::move(openGroup));

    m_outputContent.clear();

    // The blend mode, the constant alpha and the soft mask are reset to the default
    // values at the beginning of the transparency group (they are used to compose
    // the group onto its backdrop).
    m_currentState.setBlendMode(BlendMode::Normal);
    m_currentState.setAlphaFilling(1.0);
    m_currentState.setAlphaStroking(1.0);
    m_currentState.setSoftMask(nullptr);
    m_currentState.setStateFlags(PDFPageContentProcessorState::StateFlags());
}

void PDFPageContentEditorContentStreamBuilder::endTransparencyGroup()
{
    Q_ASSERT(!m_transparencyGroups.empty());

    OpenTransparencyGroup openGroup = std::move(m_transparencyGroups.back());
    m_transparencyGroups.pop_back();

    QByteArray groupContent = std::move(m_outputContent);
    m_outputContent = std::move(openGroup.outputContent);
    m_currentState = openGroup.state;

    const PDFEditedPageContentTransparencyGroup* group = openGroup.group.get();

    // The elements are written in the page coordinate space and the form XObject
    // is painted with the identity transformation matrix, so the bounding box
    // of the elements is the bounding box of the form XObject.
    QRectF boundingBox = openGroup.boundingBox;
    if (boundingBox.isNull())
    {
        boundingBox = QRectF(0.0, 0.0, 1.0, 1.0);
    }

    PDFArrayBuilder boundingBoxArray;
    boundingBoxArray.appendItem(PDFObject::createReal(boundingBox.left()));
    boundingBoxArray.appendItem(PDFObject::createReal(boundingBox.top()));
    boundingBoxArray.appendItem(PDFObject::createReal(boundingBox.right()));
    boundingBoxArray.appendItem(PDFObject::createReal(boundingBox.bottom()));

    PDFDictionaryBuilder groupDictionary;
    groupDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("Group"));
    groupDictionary.setEntry(PDFInplaceOrMemoryString("S"), PDFObject::createName("Transparency"));
    groupDictionary.setEntry(PDFInplaceOrMemoryString("I"), PDFObject::createBool(group->isolated));
    groupDictionary.setEntry(PDFInplaceOrMemoryString("K"), PDFObject::createBool(group->knockout));
    if (!group->colorSpaceObject.isNull())
    {
        groupDictionary.setEntry(PDFInplaceOrMemoryString("CS"), PDFObject(group->colorSpaceObject));
    }

    PDFArrayBuilder filter;
    filter.appendItem(PDFObject::createName("FlateDecode"));

    // The form XObject has no resource dictionary, so it uses the resources of the page,
    // into which all resources used by the elements of the group are written.
    QByteArray compressedData = PDFFlateDecodeFilter::compress(groupContent);
    PDFDictionaryBuilder formDictionary;
    formDictionary.setEntry(PDFInplaceOrMemoryString("Type"), PDFObject::createName("XObject"));
    formDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Form"));
    formDictionary.setEntry(PDFInplaceOrMemoryString("BBox"), PDFObject::createArray(qMove(boundingBoxArray)));
    formDictionary.setEntry(PDFInplaceOrMemoryString("Group"), PDFObject::createDictionary(qMove(groupDictionary)));
    formDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedData.size()));
    formDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createArray(qMove(filter)));
    PDFObject formObject = PDFObject::createStream(PDFStream(qMove(formDictionary), qMove(compressedData)));

    QByteArray key;
    for (int i = 1; key.isEmpty() || m_xobjectDictionary.hasKey(key); ++i)
    {
        key = "Fm" + QByteArray::number(i);
    }
    m_xobjectDictionary.addEntry(PDFInplaceOrMemoryString(key), qMove(formObject));

    QTextStream stream(&m_outputContent, QDataStream::WriteOnly | QDataStream::Append);
    stream << "q" << Qt::endl;

    PDFPageContentProcessorState groupState = m_currentState;
    groupState.setBlendMode(group->blendMode);
    groupState.setAlphaFilling(group->alphaFilling);
    groupState.setAlphaStroking(group->alphaStroking);
    writeStateDifference(stream, groupState);

    stream << "/" << key << " Do" << Qt::endl;
    stream << "Q" << Qt::endl;

    // The graphic state is restored by the 'Q' operator
    m_currentState = openGroup.state;
}

void PDFPageContentEditorContentStreamBuilder::writePathGeometry(QTextStream& stream, const QPainterPath& path)
{
    const int elementCount = path.elementCount();

    for (int i = 0; i < elementCount; ++i)
    {
        QPainterPath::Element element = path.elementAt(i);

        switch (element.type)
        {
        case QPainterPath::MoveToElement:
            stream << formatNumber(element.x) << " " << formatNumber(element.y) << " m" << Qt::endl;
            break;

        case QPainterPath::LineToElement:
            stream << formatNumber(element.x) << " " << formatNumber(element.y) << " l" << Qt::endl;
            break;

        case QPainterPath::CurveToElement:
            stream << formatNumber(element.x) << " " << formatNumber(element.y) << " ";
            ++i;

            while (i < elementCount)
            {
                QPainterPath::Element currentElement = path.elementAt(i);

                if (currentElement.type == QPainterPath::CurveToDataElement)
                {
                    ++i;
                    stream << formatNumber(currentElement.x) << " " << formatNumber(currentElement.y) << " ";
                }
                else
                {
                    --i;
                    break;
                }
            }
            stream << " c" << Qt::endl;
            break;

        case QPainterPath::CurveToDataElement:
            stream << formatNumber(element.x) << " " << formatNumber(element.y) << " ";
            break;

        default:
            break;
        }
    }
}

void PDFPageContentEditorContentStreamBuilder::writeClipPath(QTextStream& stream, const QPainterPath& clipPath)
{
    writePathGeometry(stream, clipPath);

    if (clipPath.fillRule() == Qt::WindingFill)
    {
        stream << "W n" << Qt::endl;
    }
    else
    {
        stream << "W* n" << Qt::endl;
    }
}

void PDFPageContentEditorContentStreamBuilder::writePainterPath(QTextStream& stream,
                                                                const QPainterPath& path,
                                                                bool isStroking,
                                                                bool isFilling)
{
    writePathGeometry(stream, path);

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

void PDFPageContentEditorContentStreamBuilder::writeText(QTextStream& stream, const QString& text, const QByteArray& fontKey)
{
    // The text object is enclosed in the q/Q operators, so the graphic state,
    // which is changed inside the text object, is restored at its end.
    const PDFPageContentProcessorState savedState = m_currentState;

    stream << "q BT" << Qt::endl;

    QString xml = QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?><doc>%1</doc>").arg(text);

    QXmlStreamReader reader(xml);
    m_textFont = m_currentState.getTextFont();

    // Write the initial text state. The text state can be set outside
    // of the text object (for example, font can be selected before the BT
    // operator) and then the serialized items contain no corresponding
    // command. Without an explicit Tf operator, the content stream
    // would be invalid and the text would not be displayed at all.
    if (m_textFont)
    {
        QByteArray currentFontKey = selectFont(!fontKey.isEmpty() ? fontKey : m_textFont->getFontId());
        m_currentTextFontKey = currentFontKey;
        m_currentTextFontSize = m_currentState.getTextFontSize();
        stream << "/" << currentFontKey << " " << formatNumber(m_currentState.getTextFontSize()) << " Tf" << Qt::endl;
    }

    if (!qFuzzyIsNull(m_currentState.getTextCharacterSpacing()))
    {
        stream << formatNumber(m_currentState.getTextCharacterSpacing()) << " Tc" << Qt::endl;
    }

    if (!qFuzzyIsNull(m_currentState.getTextWordSpacing()))
    {
        stream << formatNumber(m_currentState.getTextWordSpacing()) << " Tw" << Qt::endl;
    }

    if (!qFuzzyCompare(m_currentState.getTextHorizontalScaling(), 100.0))
    {
        stream << formatNumber(m_currentState.getTextHorizontalScaling()) << " Tz" << Qt::endl;
    }

    if (!qFuzzyIsNull(m_currentState.getTextLeading()))
    {
        stream << formatNumber(m_currentState.getTextLeading()) << " TL" << Qt::endl;
    }

    if (!qFuzzyIsNull(m_currentState.getTextRise()))
    {
        stream << formatNumber(m_currentState.getTextRise()) << " Ts" << Qt::endl;
    }

    if (m_currentState.getTextRenderingMode() != TextRenderingMode::Fill)
    {
        stream << int(m_currentState.getTextRenderingMode()) << " Tr" << Qt::endl;
    }

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
                writeTextWithFallback(stream, characters);
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

    m_currentState = savedState;
}

void PDFPageContentEditorContentStreamBuilder::writeTextCommand(QTextStream& stream, const QXmlStreamReader& reader)
{
    const QXmlStreamAttributes attributes = reader.attributes();
    const QString tag = reader.name().toString();

    auto isCommand = [&reader](const char* tag) -> bool
    {
        QString tagString = reader.name().toString();
        QXmlStreamAttributes attributes = reader.attributes();
        return tagString == QLatin1String(tag) && attributes.size() == 1 && attributes.hasAttribute("v");
    };

    auto reportInvalidNumber = [this](const QString& value)
    {
        addError(PDFTranslationContext::tr("Cannot convert text '%1' to number.").arg(value));
    };

    if (tag == "doc")
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
            reportInvalidNumber(attribute.value().toString());
        }
        else
        {
            stream << formatNumber(textRise) << " Ts" << Qt::endl;
        }
    }
    else if (isCommand("tc"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const double textCharacterSpacing = attribute.value().toDouble(&ok);

        if (!ok)
        {
            reportInvalidNumber(attribute.value().toString());
        }
        else
        {
            stream << formatNumber(textCharacterSpacing) << " Tc" << Qt::endl;
        }
    }
    else if (isCommand("tw"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const double textWordSpacing = attribute.value().toDouble(&ok);

        if (!ok)
        {
            reportInvalidNumber(attribute.value().toString());
        }
        else
        {
            stream << formatNumber(textWordSpacing) << " Tw" << Qt::endl;
        }
    }
    else if (isCommand("tl"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const double textLeading = attribute.value().toDouble(&ok);

        if (!ok)
        {
            reportInvalidNumber(attribute.value().toString());
        }
        else
        {
            stream << formatNumber(textLeading) << " TL" << Qt::endl;
        }
    }
    else if (isCommand("tz"))
    {
        const QXmlStreamAttribute& attribute = attributes.front();
        bool ok = false;
        const PDFReal textScaling = attribute.value().toDouble(&ok);

        if (!ok)
        {
            reportInvalidNumber(attribute.value().toString());
        }
        else
        {
            stream << formatNumber(textScaling) << " Tz" << Qt::endl;
        }
    }
    else if (isCommand("tk"))
    {
        const QString value = attributes.front().value().toString().trimmed();
        const bool isTrue = value == "1" || value.compare("true", Qt::CaseInsensitive) == 0;
        const bool isFalse = value == "0" || value.compare("false", Qt::CaseInsensitive) == 0;

        if (!isTrue && !isFalse)
        {
            addError(PDFTranslationContext::tr("Invalid boolean value '%1'. Valid values are 0, 1, true and false.").arg(value));
        }
        else
        {
            PDFPageContentProcessorState state = m_currentState;
            state.setTextKnockout(isTrue);
            writeStateDifference(stream, state);
        }
    }
    else if (tag == "fill" || tag == "stroke")
    {
        const bool isFilling = tag == "fill";

        QByteArray colorOperator;
        QStringList componentNames;
        if (attributes.size() == 1 && attributes.hasAttribute("gray"))
        {
            componentNames = QStringList{ "gray" };
            colorOperator = isFilling ? "g" : "G";
        }
        else if (attributes.size() == 3 && attributes.hasAttribute("r") && attributes.hasAttribute("g") && attributes.hasAttribute("b"))
        {
            componentNames = QStringList{ "r", "g", "b" };
            colorOperator = isFilling ? "rg" : "RG";
        }
        else if (attributes.size() == 4 && attributes.hasAttribute("c") && attributes.hasAttribute("m") && attributes.hasAttribute("y") && attributes.hasAttribute("k"))
        {
            componentNames = QStringList{ "c", "m", "y", "k" };
            colorOperator = isFilling ? "k" : "K";
        }

        if (colorOperator.isEmpty())
        {
            addError(PDFTranslationContext::tr("Color command requires attribute gray, attributes r, g, b, or attributes c, m, y, k."));
            return;
        }

        QByteArray colorCommand;
        for (const QString& componentName : componentNames)
        {
            bool ok = false;
            const QString value = attributes.value(componentName).toString();
            const PDFReal component = value.toDouble(&ok);

            if (!ok)
            {
                reportInvalidNumber(value);
                return;
            }

            colorCommand += formatNumber(component) + " ";
        }

        stream << colorCommand << colorOperator << Qt::endl;
    }
    else if (tag == "space")
    {
        if (attributes.size() == 1 && attributes.hasAttribute("advance"))
        {
            bool ok = false;
            const PDFReal advance = attributes.value("advance").toDouble(&ok);

            if (!ok)
            {
                reportInvalidNumber(attributes.value("advance").toString());
            }
            else
            {
                stream << "[ " << formatNumber(advance) << " ] TJ" << Qt::endl;
            }
        }
        else
        {
            addError(PDFTranslationContext::tr("Space command requires one attribute - advance."));
        }
    }
    else if (tag == "character")
    {
        if (attributes.size() == 1 && attributes.hasAttribute("cid"))
        {
            if (!m_textFont)
            {
                addError(PDFTranslationContext::tr("Text font not defined!"));
                return;
            }

            bool ok = false;
            const uint cid = attributes.value("cid").toUInt(&ok);
            if (!ok)
            {
                reportInvalidNumber(attributes.value("cid").toString());
                return;
            }

            QByteArray encodedText;
            if (const PDFFontCMap* cmap = m_textFont->getCMap())
            {
                encodedText = cmap->encode(cid);
            }
            else if (cid <= 0xFFu)
            {
                encodedText.append(static_cast<char>(cid));
            }

            if (encodedText.isEmpty())
            {
                addError(PDFTranslationContext::tr("Cannot encode character with cid '%1' using the current font.").arg(cid));
            }
            else
            {
                writeTextHexString(stream, encodedText);
            }
        }
        else
        {
            addError(PDFTranslationContext::tr("Character command requires one attribute - cid."));
        }
    }
    else if (tag == "tf")
    {
        if (attributes.hasAttribute("font") && attributes.hasAttribute("size"))
        {
            bool ok = false;
            QByteArray v1 = attributes.value("font").toString().toLatin1();
            PDFReal v2 = attributes.value("size").toDouble(&ok);

            if (!ok)
            {
                reportInvalidNumber(attributes.value("size").toString());
            }
            else
            {
                v1 = selectFont(v1);
                m_currentTextFontKey = v1;
                m_currentTextFontSize = v2;
                stream << "/" << v1 << " " << formatNumber(v2) << " Tf" << Qt::endl;
            }
        }
        else
        {
            addError(PDFTranslationContext::tr("Text font command requires two attributes - font and size."));
        }
    }
    else if (tag == "tpos")
    {
        if (attributes.hasAttribute("x") && attributes.hasAttribute("y"))
        {
            bool ok1 = false;
            bool ok2 = false;
            PDFReal v1 = attributes.value("x").toDouble(&ok1);
            PDFReal v2 = attributes.value("y").toDouble(&ok2);

            if (!ok1)
            {
                reportInvalidNumber(attributes.value("x").toString());
            }
            else if (!ok2)
            {
                reportInvalidNumber(attributes.value("y").toString());
            }
            else
            {
                // The recorded position is the absolute text matrix translation.
                // Operator Td is relative to the current text line matrix, so
                // the absolute position must be set with the Tm operator.
                stream << "1 0 0 1 " << formatNumber(v1) << " " << formatNumber(v2) << " Tm" << Qt::endl;
            }
        }
        else
        {
            addError(PDFTranslationContext::tr("Text translation command requires two attributes - x and y."));
        }
    }
    else if (tag == "tmatrix")
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

            if (!ok1 || !ok2 || !ok3 || !ok4 || !ok5 || !ok6)
            {
                addError(PDFTranslationContext::tr("Invalid text matrix parameters."));
            }
            else
            {
                stream << formatNumber(m11) << " " << formatNumber(m12) << " " << formatNumber(m21) << " " << formatNumber(m22) << " " << formatNumber(x) << " " << formatNumber(y) << " Tm" << Qt::endl;
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

void PDFPageContentEditorContentStreamBuilder::writeTextWithFallback(QTextStream& stream, const QString& characters)
{
    Q_ASSERT(m_textFont);

    // Split the text into maximal runs of code points encodable by the current
    // font, and runs of code points which must be written with a fallback font.
    struct CodePointRun
    {
        std::u32string codePoints;
        bool isEncodable = false;
    };
    std::vector<CodePointRun> runs;

    for (qsizetype i = 0, size = characters.size(); i < size; ++i)
    {
        const QChar character = characters[i];
        char32_t codePoint = character.unicode();

        if (character.isHighSurrogate() && i + 1 < size && characters[i + 1].isLowSurrogate())
        {
            codePoint = QChar::surrogateToUcs4(character, characters[i + 1]);
            ++i;
        }

        const bool isEncodable = !m_textFont->encodeCharacter(codePoint).isEmpty();
        if (runs.empty() || runs.back().isEncodable != isEncodable)
        {
            runs.push_back(CodePointRun{ std::u32string(), isEncodable });
        }
        runs.back().codePoints.push_back(codePoint);
    }

    for (const CodePointRun& run : runs)
    {
        if (run.isEncodable)
        {
            PDFEncodedText encodedText = m_textFont->encodeText(QString::fromUcs4(run.codePoints.data(), int(run.codePoints.size())));

            if (!encodedText.encodedText.isEmpty())
            {
                writeTextHexString(stream, encodedText.encodedText);
            }

            if (!encodedText.isValid)
            {
                // Cannot happen for characters positively checked by encodeCharacter,
                // this is a safety net only.
                addError(PDFTranslationContext::tr("Error during converting text to font encoding. Some characters were not converted: '%1'.").arg(encodedText.errorString));
            }
        }
        else
        {
            std::vector<PDFEditorFallbackFontManager::Run> fallbackRuns = m_fallbackFontManager.encode(run.codePoints, m_textFont, m_fontDictionary, [this](const QString& error) { addError(error); });

            if (fallbackRuns.empty())
            {
                addError(PDFTranslationContext::tr("Error during converting text to font encoding. Some characters were not converted: '%1'.").arg(QString::fromUcs4(run.codePoints.data(), int(run.codePoints.size()))));
                continue;
            }

            for (const PDFEditorFallbackFontManager::Run& fallbackRun : fallbackRuns)
            {
                stream << "/" << fallbackRun.fontResourceKey << " " << formatNumber(m_currentTextFontSize) << " Tf" << Qt::endl;
                writeTextHexString(stream, fallbackRun.encodedBytes);
            }

            // Restore the original font
            stream << "/" << m_currentTextFontKey << " " << formatNumber(m_currentTextFontSize) << " Tf" << Qt::endl;
        }
    }
}

void PDFPageContentEditorContentStreamBuilder::writeTextHexString(QTextStream& stream, const QByteArray& encodedText)
{
    stream << "<" << encodedText.toHex() << "> Tj" << Qt::endl;
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
            PDFArrayBuilder array;
            array.appendItem(PDFObject::createName("FlateDecode"));

            // The alpha channel cannot be stored in the image samples, it must be
            // written as a separate soft mask image. Convert to the non-premultiplied
            // format first - a direct conversion of a premultiplied image to RGB888
            // would darken the semi-transparent pixels.
            const bool hasAlphaChannel = image.hasAlphaChannel();
            QImage codedImage = hasAlphaChannel ? image.convertToFormat(QImage::Format_ARGB32) : image;
            QImage softMaskImage = hasAlphaChannel ? codedImage.convertToFormat(QImage::Format_Alpha8) : QImage();
            codedImage = codedImage.convertToFormat(QImage::Format_RGB888);

            QByteArray decodedStream = getImageSamples(codedImage, 3);

            // Compress the content stream
            QByteArray compressedData = PDFFlateDecodeFilter::compress(decodedStream);
            PDFDictionaryBuilder imageDictionary;
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Subtype"), PDFObject::createName("Image"));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Width"), PDFObject::createInteger(image.width()));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Height"), PDFObject::createInteger(image.height()));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Predictor"), PDFObject::createInteger(1));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("ColorSpace"), PDFObject::createName("DeviceRGB"));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("BitsPerComponent"), PDFObject::createInteger(8));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(compressedData.size()));
            imageDictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createArray(qMove(array)));

            if (!softMaskImage.isNull())
            {
                imageDictionary.setEntry(PDFInplaceOrMemoryString("SMask"), createSoftMaskObject(softMaskImage));
            }

            PDFObject imageObject = PDFObject::createStream(PDFStream(qMove(imageDictionary), qMove(compressedData)));

            m_xobjectDictionary.addEntry(PDFInplaceOrMemoryString(currentKey), std::move(imageObject));
            key = currentKey;
            break;
        }
    }

    stream << "/" << key << " Do" << Qt::endl;
}

void PDFPageContentEditorContentStreamBuilder::writeImageObject(QTextStream& stream, const PDFObject& imageObject)
{
    QByteArray key;

    int i = 0;
    while (true)
    {
        QByteArray currentKey = QString("Im%1").arg(++i).toLatin1();
        if (!m_xobjectDictionary.hasKey(currentKey))
        {
            m_xobjectDictionary.addEntry(PDFInplaceOrMemoryString(currentKey), PDFObject(imageObject));
            key = currentKey;
            break;
        }
    }

    stream << "/" << key << " Do" << Qt::endl;
}

QByteArray PDFPageContentEditorContentStreamBuilder::selectFont(const QByteArray& font)
{
    m_textFont = nullptr;
    QByteArray fontKey = font;

    if (auto overrideIt = m_fontOverrides.constFind(font); overrideIt != m_fontOverrides.cend() && !overrideIt.value().isNull())
    {
        m_textFont = overrideIt.value();

        // The font object must be valid in the document, into which the content
        // is written (the text element can come from another document).
        if (auto objectIt = m_fontResourceObjects.constFind(font); objectIt != m_fontResourceObjects.cend() && m_document->getDictionaryFromObject(objectIt.value()))
        {
            fontKey = getFontResourceKey(font, objectIt.value());
        }

        if (!m_fontDictionary.hasKey(fontKey))
        {
            // The font can't be selected by the key, which is missing in the font
            // dictionary, so the fallback font is used instead.
            m_textFont = nullptr;
        }
    }

    PDFObject fontObject = m_fontDictionary.get(fontKey);
    if (!m_textFont && !fontObject.isNull())
    {
        try
        {
            m_textFont = PDFFont::createFont(fontObject, fontKey, m_document);
        }
        catch (const PDFException& exception)
        {
            addError(exception.getMessage());
        }
        catch (const std::exception& exception)
        {
            addError(PDFTranslationContext::tr("Font '%1' is invalid: %2")
                     .arg(QString::fromLatin1(font))
                     .arg(QString::fromUtf8(exception.what())));
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

        fontObject = m_fontDictionary.get(defaultFontKey);
        try
        {
            if (auto overrideDefault = m_fontOverrides.constFind(defaultFontKey); overrideDefault != m_fontOverrides.cend() && !overrideDefault.value().isNull())
            {
                m_textFont = overrideDefault.value();
            }
            else
            {
                m_textFont = PDFFont::createFont(fontObject, defaultFontKey, m_document);
            }
        }
        catch (const PDFException& exception)
        {
            addError(exception.getMessage());
        }
        catch (const std::exception& exception)
        {
            addError(PDFTranslationContext::tr("Failed to create fallback font '%1': %2")
                     .arg(QString::fromLatin1(defaultFontKey))
                     .arg(QString::fromUtf8(exception.what())));
        }

        if (m_textFont)
        {
            return defaultFontKey;
        }

        return font;
    }

    return fontKey;
}

QByteArray PDFPageContentEditorContentStreamBuilder::getFontResourceKey(const QByteArray& key, const PDFObject& fontObject)
{
    // The key is the name of the font in the resource dictionary of the content
    // stream, in which the font was selected. It can be a form XObject, whose
    // resources differ from the page resources - the same name can denote another
    // font, or it can be missing in the page resources. So the font is looked up
    // by its object and if it is not found, it is added under an unused key.
    if (m_fontDictionary.get(key) == fontObject)
    {
        return key;
    }

    for (size_t i = 0; i < m_fontDictionary.getCount(); ++i)
    {
        if (m_fontDictionary.getValue(i) == fontObject)
        {
            return m_fontDictionary.getKey(i).getString();
        }
    }

    QByteArray uniqueKey = key;
    for (int i = 1; uniqueKey.isEmpty() || m_fontDictionary.hasKey(uniqueKey); ++i)
    {
        uniqueKey = key + "_" + QByteArray::number(i);
    }

    m_fontDictionary.addEntry(PDFInplaceOrMemoryString(uniqueKey), PDFObject(fontObject));
    return uniqueKey;
}

void PDFPageContentEditorContentStreamBuilder::writeShadingObject(QTextStream& stream, const PDFObject& shadingObject)
{
    QByteArray key;

    for (size_t i = 0; i < m_shadingDictionary.getCount(); ++i)
    {
        if (m_shadingDictionary.getValue(i) == shadingObject)
        {
            key = m_shadingDictionary.getKey(i).getString();
            break;
        }
    }

    if (key.isEmpty())
    {
        int i = 0;
        while (true)
        {
            QByteArray currentKey = QString("Sh%1").arg(++i).toLatin1();
            if (!m_shadingDictionary.hasKey(currentKey))
            {
                m_shadingDictionary.addEntry(PDFInplaceOrMemoryString(currentKey), PDFObject(shadingObject));
                key = currentKey;
                break;
            }
        }
    }

    stream << "/" << key << " sh" << Qt::endl;
}

void PDFPageContentEditorContentStreamBuilder::addError(const QString& error)
{
    m_errors << error;
}

void PDFPageContentEditorContentStreamBuilder::setFontDictionary(PDFDictionaryBuilder newFontDictionary)
{
    m_fontDictionary = std::move(newFontDictionary);
}

void PDFPageContentEditorContentStreamBuilder::setXObjectDictionary(PDFDictionaryBuilder newXObjectDictionary)
{
    m_xobjectDictionary = std::move(newXObjectDictionary);
}

void PDFPageContentEditorContentStreamBuilder::setGraphicStateDictionary(PDFDictionaryBuilder newGraphicStateDictionary)
{
    m_graphicStateDictionary = std::move(newGraphicStateDictionary);
}

void PDFPageContentEditorContentStreamBuilder::setShadingDictionary(PDFDictionaryBuilder newShadingDictionary)
{
    m_shadingDictionary = std::move(newShadingDictionary);
}

void PDFPageContentEditorContentStreamBuilder::writeStyledPath(const QPainterPath& path,
                                                               const QPen& pen,
                                                               const QBrush& brush,
                                                               bool isStroking,
                                                               bool isFilling)
{
    finishTransparencyGroups();

    PDFPageContentProcessorState newState = m_currentState;
    newState.setCurrentTransformationMatrix(QTransform());

    PDFPainterHelper::applyPenToGraphicState(&newState, pen);
    PDFPainterHelper::applyBrushToGraphicState(&newState, brush);

    QTextStream stream(&m_outputContent, QDataStream::WriteOnly | QDataStream::Append);
    writeStateDifference(stream, newState);

    bool isNeededToWriteCurrentTransformationMatrix = this->isNeededToWriteCurrentTransformationMatrix();
    if (isNeededToWriteCurrentTransformationMatrix)
    {
        stream << "q" << Qt::endl;
        writeCurrentTransformationMatrix(stream);
    }

    writePainterPath(stream, path, isStroking, isFilling);

    if (isNeededToWriteCurrentTransformationMatrix)
    {
        stream << "Q" << Qt::endl;
    }
}

void PDFPageContentEditorContentStreamBuilder::writeStyledPath(const QPainterPath& path,
                                                               const PDFPageContentProcessorState& state,
                                                               bool isStroking,
                                                               bool isFilling,
                                                               const QPainterPath& clipPath)
{
    finishTransparencyGroups();

    QTextStream stream(&m_outputContent, QDataStream::WriteOnly | QDataStream::Append);
    writeStateDifference(stream, state);

    const bool isNeededToWriteCurrentTransformationMatrix = this->isNeededToWriteCurrentTransformationMatrix();
    const bool isNeededGraphicStateSave = isNeededToWriteCurrentTransformationMatrix || !clipPath.isEmpty();

    if (isNeededGraphicStateSave)
    {
        stream << "q" << Qt::endl;

        // The clip path is expressed in the page coordinate space,
        // so it must be written before the transformation matrix.
        if (!clipPath.isEmpty())
        {
            writeClipPath(stream, clipPath);
        }

        if (isNeededToWriteCurrentTransformationMatrix)
        {
            writeCurrentTransformationMatrix(stream);
        }
    }

    writePainterPath(stream, path, isStroking, isFilling);

    if (isNeededGraphicStateSave)
    {
        stream << "Q" << Qt::endl;
    }
}

void PDFPageContentEditorContentStreamBuilder::writeImage(const QImage& image,
                                                          const QRectF& rectangle)
{
    finishTransparencyGroups();

    QTextStream stream(&m_outputContent, QDataStream::WriteOnly | QDataStream::Append);

    // This overload places a newly created image into the page coordinate space,
    // so the state left by the previously written element must not be applied to
    // it. The transformation matrix of the last written element would move the
    // image out of the page (or mirror it), and the graphic state parameters,
    // which take part in painting an image, would change the way the image is
    // composed onto the page - the alpha source flag even decides, whether the
    // soft mask of the image is interpreted as shape, or as opacity (see
    // PDF 32000-1, chapter 11.6.4.4). All of them are reset to the default
    // values. The parameters, which cannot affect an image (for example the
    // line style, or the color), are left as they are.
    PDFPageContentProcessorState newState = m_currentState;
    newState.setCurrentTransformationMatrix(QTransform());
    newState.setAlphaStroking(1.0);
    newState.setAlphaFilling(1.0);
    newState.setAlphaIsShape(false);
    newState.setBlendMode(BlendMode::Normal);
    newState.setRenderingIntent(RenderingIntent::Perceptual);
    newState.setOverprintMode(PDFOverprintMode());
    writeStateDifference(stream, newState);

    stream << "q" << Qt::endl;

    QSizeF rectangleSize = QSizeF(image.size()).scaled(rectangle.size(), Qt::KeepAspectRatio);
    QRectF transformedRectangle(QPointF(), rectangleSize);
    transformedRectangle.moveCenter(rectangle.center());

    QTransform imageTransform(transformedRectangle.width(), 0, 0, transformedRectangle.height(), transformedRectangle.left(), transformedRectangle.top());

    PDFReal m11 = imageTransform.m11();
    PDFReal m12 = imageTransform.m12();
    PDFReal m21 = imageTransform.m21();
    PDFReal m22 = imageTransform.m22();
    PDFReal x = imageTransform.dx();
    PDFReal y = imageTransform.dy();

    stream << formatNumber(m11) << " " << formatNumber(m12) << " " << formatNumber(m21) << " " << formatNumber(m22) << " " << formatNumber(x) << " " << formatNumber(y) << " cm" << Qt::endl;

    writeImage(stream, image);

    stream << "Q" << Qt::endl;
}

void PDFPageContentEditorContentStreamBuilder::writeImage(const QImage& image, QTransform transform, const QRectF& rectangle, const QPainterPath& clipPath)
{
    finishTransparencyGroups();

    QTransform oldTransform = m_currentState.getCurrentTransformationMatrix();
    m_currentState.setCurrentTransformationMatrix(transform);

    QTextStream stream(&m_outputContent, QDataStream::WriteOnly | QDataStream::Append);

    stream << "q" << Qt::endl;

    // The clip path is expressed in the page coordinate space,
    // so it must be written before the transformation matrix.
    if (!clipPath.isEmpty())
    {
        writeClipPath(stream, clipPath);
    }

    if (isNeededToWriteCurrentTransformationMatrix())
    {
        writeCurrentTransformationMatrix(stream);
    }

    // This overload is used by the paint engine. The rectangle is expressed
    // in the painter's logical coordinates, where the y axis points down and
    // the image should fill the whole rectangle with the first image row at
    // the rectangle's top edge. The image unit square has the first row at
    // v = 1, so the y axis must be flipped here.
    stream << formatNumber(rectangle.width()) << " 0 0 " << formatNumber(-rectangle.height()) << " " << formatNumber(rectangle.left()) << " " << formatNumber(rectangle.bottom()) << " cm" << Qt::endl;

    writeImage(stream, image);

    stream << "Q" << Qt::endl;

    m_currentState.setCurrentTransformationMatrix(oldTransform);
}

bool PDFPageContentEditorContentStreamBuilder::isNeededToWriteCurrentTransformationMatrix() const
{
    return !m_currentState.getCurrentTransformationMatrix().isIdentity();
}

void PDFPageContentEditorContentStreamBuilder::writeCurrentTransformationMatrix(QTextStream& stream)
{
    QTransform transform = m_currentState.getCurrentTransformationMatrix();

    PDFReal m11 = transform.m11();
    PDFReal m12 = transform.m12();
    PDFReal m21 = transform.m21();
    PDFReal m22 = transform.m22();
    PDFReal x = transform.dx();
    PDFReal y = transform.dy();

    stream << formatNumber(m11) << " " << formatNumber(m12) << " " << formatNumber(m21) << " " << formatNumber(m22) << " " << formatNumber(x) << " " << formatNumber(y) << " cm" << Qt::endl;
}

}   // namespace pdf
