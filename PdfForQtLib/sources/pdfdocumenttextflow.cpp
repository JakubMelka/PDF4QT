//    Copyright (C) 2020 Jakub Melka
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

#include "pdfdocumenttextflow.h"
#include "pdfdocument.h"
#include "pdfstructuretree.h"
#include "pdfcompiler.h"
#include "pdfexecutionpolicy.h"
#include "pdfconstants.h"
#include "pdfcms.h"

namespace pdf
{

PDFDocumentTextFlow PDFDocumentTextFlowFactory::create(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, Algorithm algorithm)
{
    PDFDocumentTextFlow result;
    PDFStructureTree structureTree;

    const PDFCatalog* catalog = document->getCatalog();
    if (algorithm == Algorithm::Auto || algorithm == Algorithm::Structure)
    {
        structureTree = PDFStructureTree::parse(&document->getStorage(), catalog->getStructureTreeRoot());
    }

    if (algorithm == Algorithm::Auto)
    {
        // Determine algorithm
        if (catalog->isLogicalStructureMarked() && structureTree.isValid())
        {
            algorithm = Algorithm::Structure;
        }
        else
        {
            algorithm = Algorithm::Layout;
        }
    }

    Q_ASSERT(algorithm != Algorithm::Auto);

    QMutex mutex;

    // Perform algorithm to retrieve document text
    switch (algorithm)
    {
        case Algorithm::Layout:
        {
            PDFFontCache fontCache(DEFAULT_FONT_CACHE_LIMIT, DEFAULT_REALIZED_FONT_CACHE_LIMIT);

            std::map<PDFInteger, PDFDocumentTextFlow::Items> items;

            PDFCMSGeneric cms;
            PDFMeshQualitySettings mqs;
            PDFOptionalContentActivity oca(document, OCUsage::Export, nullptr);
            pdf::PDFModifiedDocument md(const_cast<PDFDocument*>(document), &oca);
            fontCache.setDocument(md);
            fontCache.setCacheShrinkEnabled(nullptr, false);

            auto generateTextLayout = [this, &items, &mutex, &fontCache, &cms, &mqs, &oca, document, catalog](PDFInteger pageIndex)
            {
                if (!catalog->getPage(pageIndex))
                {
                    // Invalid page index
                    return;
                }

                const PDFPage* page = catalog->getPage(pageIndex);
                Q_ASSERT(page);

                PDFTextLayoutGenerator generator(PDFRenderer::IgnoreOptionalContent, page, document, &fontCache, &cms, &oca, QMatrix(), mqs);
                QList<PDFRenderError> errors = generator.processContents();
                PDFTextLayout textLayout = generator.createTextLayout();
                PDFTextFlows textFlows = PDFTextFlow::createTextFlows(textLayout, PDFTextFlow::FlowFlags(PDFTextFlow::SeparateBlocks) | PDFTextFlow::RemoveSoftHyphen, pageIndex);

                PDFDocumentTextFlow::Items flowItems;
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ PDFDocumentTextFlow::PageStart, pageIndex, PDFTranslationContext::tr("Page %1").arg(pageIndex + 1) });
                for (const PDFTextFlow& textFlow : textFlows)
                {
                    flowItems.emplace_back(PDFDocumentTextFlow::Item{ PDFDocumentTextFlow::Text, pageIndex, textFlow.getText() });
                }
                flowItems.emplace_back(PDFDocumentTextFlow::Item{ PDFDocumentTextFlow::PageEnd, pageIndex, QString() });

                QMutexLocker lock(&mutex);
                items[pageIndex] = qMove(flowItems);
                m_errors.append(qMove(errors));
            };

            PDFExecutionPolicy::execute(PDFExecutionPolicy::Scope::Page, pageIndices.begin(), pageIndices.end(), generateTextLayout);

            fontCache.setCacheShrinkEnabled(nullptr, true);

            PDFDocumentTextFlow::Items flowItems;
            for (const auto& item : items)
            {
                flowItems.insert(flowItems.end(), std::make_move_iterator(item.second.begin()), std::make_move_iterator(item.second.end()));
            }

            result = PDFDocumentTextFlow(qMove(flowItems));
            break;
        }

        case Algorithm::Structure:
        {
            if (!structureTree.isValid())
            {
                m_errors << PDFRenderError(RenderErrorType::Error, PDFTranslationContext::tr("Valid tagged document required."));
                break;
            }

            PDFStructureTreeTextExtractor extractor(document, &structureTree);
            extractor.perform(pageIndices);

            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    return result;
}

}   // namespace pdf
