//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfredact.h"
#include "pdfpainter.h"
#include "pdfdocumentbuilder.h"
#include "pdfoptimizer.h"

namespace pdf
{

PDFRedact::PDFRedact(const PDFDocument* document,
                     const PDFFontCache* fontCache,
                     const PDFCMS* cms,
                     const PDFOptionalContentActivity* optionalContentActivity,
                     const PDFMeshQualitySettings* meshQualitySettings,
                     QColor redactFillColor) :
    m_document(document),
    m_fontCache(fontCache),
    m_cms(cms),
    m_optionalContentActivity(optionalContentActivity),
    m_meshQualitySettings(meshQualitySettings),
    m_redactFillColor(redactFillColor)
{

}

PDFDocument PDFRedact::perform(Options options)
{
    PDFDocumentBuilder builder;
    builder.createDocument();

    PDFRenderer renderer(m_document,
                         m_fontCache,
                         m_cms,
                         m_optionalContentActivity,
                         PDFRenderer::None,
                         *m_meshQualitySettings);

    for (size_t i = 0; i < m_document->getCatalog()->getPageCount(); ++i)
    {
        const PDFPage* page = m_document->getCatalog()->getPage(i);

        PDFPrecompiledPage compiledPage;
        renderer.compile(&compiledPage, i);

        PDFObjectReference newPageReference = builder.appendPage(page->getMediaBox());

        if (!page->getCropBox().isEmpty())
        {
            builder.setPageCropBox(newPageReference, page->getCropBox());
        }
        if (!page->getBleedBox().isEmpty())
        {
            builder.setPageBleedBox(newPageReference, page->getBleedBox());
        }
        if (!page->getTrimBox().isEmpty())
        {
            builder.setPageTrimBox(newPageReference, page->getTrimBox());
        }
        if (!page->getArtBox().isEmpty())
        {
            builder.setPageArtBox(newPageReference, page->getArtBox());
        }
        builder.setPageRotation(newPageReference, page->getPageRotation());

        PDFPageContentStreamBuilder contentStreamBuilder(&builder);

        QPainterPath redactPath;

        for (const PDFObjectReference& annotationReference : page->getAnnotations())
        {
            PDFAnnotationPtr annotation = PDFAnnotation::parse(&m_document->getStorage(), annotationReference);
            if (!annotation || annotation->getType() != AnnotationType::Redact)
            {
                continue;
            }

            // We have redact annotation here
            const PDFRedactAnnotation* redactAnnotation = dynamic_cast<const PDFRedactAnnotation*>(annotation.get());
            Q_ASSERT(redactAnnotation);

            redactPath.addPath(redactAnnotation->getRedactionRegion().getPath());
        }

        QMatrix matrix;
        matrix.translate(0, page->getMediaBox().height());
        matrix.scale(1.0, -1.0);

        QPainter* painter = contentStreamBuilder.begin(newPageReference);
        compiledPage.redact(redactPath, matrix, m_redactFillColor);
        compiledPage.draw(painter, QRectF(), matrix, PDFRenderer::None);
        contentStreamBuilder.end(painter);
    }

    if (options.testFlag(CopyTitle))
    {
        builder.setDocumentTitle(m_document->getInfo()->title);
    }

    if (options.testFlag(CopyMetadata))
    {
        PDFObject info = m_document->getTrailerDictionary()->get("Info");
        if (!info.isNull())
        {
            std::vector<PDFObject> copiedObjects = builder.copyFrom({ info }, m_document->getStorage(), true);
            if (copiedObjects.size() == 1 && copiedObjects.front().isReference())
            {
                builder.setDocumentInfo(copiedObjects.front().getReference());
            }
        }
    }

    if (options.testFlag(CopyOutline))
    {
        const PDFOutlineItem* outlineItem = m_document->getCatalog()->getOutlineRootPtr().data();

        if (outlineItem)
        {
            builder.setOutline(outlineItem);
        }
    }

    PDFDocument redactedDocument = builder.build();
    PDFOptimizer optimizer(PDFOptimizer::All, nullptr);
    optimizer.setDocument(&redactedDocument);
    optimizer.optimize();
    return optimizer.takeOptimizedDocument();
}

}   // namespace pdf
