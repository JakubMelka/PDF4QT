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
#include "pdfdocument.h"

namespace pdf
{

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

    if (contents.isArray())
    {
        const PDFArray* array = contents.getArray();
        const size_t count = array->getCount();

        QList<PDFRenderError> errors;
        for (size_t i = 0; i < count; ++i)
        {
            const PDFObject& streamObject = m_document->getObject(array->getItem(i));
            if (streamObject.isStream())
            {
                errors.append(processContentStream(streamObject.getStream()));
            }
            else
            {
                errors.append(PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Invalid page contents.")));
            }
        }

        return std::move(errors);
    }
    else if (contents.isStream())
    {
        return processContentStream(contents.getStream());
    }
    else
    {
        return { PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Invalid page contents.")) };
    }
}

QList<PDFRenderError> PDFPageContentProcessor::processContentStream(const PDFStream* stream)
{
    QByteArray content = m_document->getDecodedStream(stream);

    return QList<PDFRenderError>();
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
