//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfdocumentmanipulator.h"
#include "pdfdocumentbuilder.h"
#include "pdfoptimizer.h"

namespace pdf
{

PDFOperationResult PDFDocumentManipulator::assemble(const AssembledPages& pages)
{
    if (pages.empty())
    {
        return tr("Empty page list.");
    }

    try
    {
        classify(pages);

        pdf::PDFDocumentBuilder documentBuilder;
        if (m_flags.testFlag(SingleDocument))
        {
            PDFInteger documentIndex = -1;

            for (const AssembledPage& assembledPage : pages)
            {
                if (assembledPage.isDocumentPage())
                {
                    documentIndex = assembledPage.documentIndex;
                }
            }

            if (documentIndex == -1 || !m_documents.count(documentIndex))
            {
                throw PDFException(tr("Invalid document."));
            }

            documentBuilder.setDocument(m_documents.at(documentIndex));
        }
        else
        {
            documentBuilder.createDocument();
        }

        initializeMergedObjects(documentBuilder);

        ProcessedPages processedPages = processPages(documentBuilder, pages);
        std::vector<PDFObjectReference> adjustedPages;
        std::transform(processedPages.cbegin(), processedPages.cend(), std::back_inserter(adjustedPages), [](const auto& page) { return page.targetPageReference; });
        documentBuilder.setPages(adjustedPages);

        // Correct page tree (invalid parents are present)
        documentBuilder.flattenPageTree();
        if (!m_flags.testFlag(SingleDocument) || m_flags.testFlag(RemovedPages))
        {
            documentBuilder.removeOutline();
            documentBuilder.removeThreads();
            documentBuilder.removeDocumentActions();
            documentBuilder.removeStructureTree();
        }

        // Jakub Melka: we also create document parts for each document part (if we aren't
        // manipulating a single document).
        if (!m_flags.testFlag(SingleDocument))
        {

            std::vector<size_t> documentPartPageCounts;
            documentBuilder.createDocumentParts(documentPartPageCounts);
        }

        pdf::PDFDocument mergedDocument = documentBuilder.build();

        // Optimize document - remove unused objects and shrink object storage
        finalizeDocument(&mergedDocument);
    }
    catch (PDFException exception)
    {
        return exception.getMessage();
    }

    return true;
}

PDFDocumentManipulator::ProcessedPages PDFDocumentManipulator::processPages(PDFDocumentBuilder& documentBuilder, const AssembledPages& pages)
{
    ProcessedPages processedPages;

    // First, decide, if we are manipulating a single document, or
    // an array of documents. If the former is the case, then we do not
    // want to copy objects, it is just unnecessary. If the latter is the case,
    // then we must

    if (m_flags.testFlag(SingleDocument))
    {
        documentBuilder.flattenPageTree();
        std::vector<PDFObjectReference> pageReferences = documentBuilder.getPages();
        std::set<PDFObjectReference> usedPages;

        processedPages.reserve(pageReferences.size());
        for (const AssembledPage& assembledPage : pages)
        {
            ProcessedPage processedPage;
            processedPage.assembledPage = assembledPage;

            if (assembledPage.isDocumentPage())
            {
                const PDFInteger pageIndex = assembledPage.pageIndex;

                if (pageIndex < 0 || pageIndex >= PDFInteger(pageReferences.size()))
                {
                    throw PDFException(tr("Missing page (%1) in a document.").arg(pageIndex));
                }

                PDFObjectReference pageReference = pageReferences[pageIndex];
                if (!usedPages.count(pageReference))
                {
                    processedPage.targetPageReference = pageReference;
                    usedPages.insert(pageReference);
                }
                else
                {
                    // Page is being cloned. So we must clone it...
                    std::vector<pdf::PDFObjectReference> references = pdf::PDFDocumentBuilder::createReferencesFromObjects(documentBuilder.copyFrom(pdf::PDFDocumentBuilder::createObjectsFromReferences({ pageReference }), *documentBuilder.getStorage(), true));
                    Q_ASSERT(references.size() == 1);
                    processedPage.targetPageReference = references.front();
                    usedPages.insert(references.front());
                }
            }

            processedPages.push_back(processedPage);
        }
    }
    else
    {
        processedPages = collectObjectsAndCopyPages(documentBuilder, pages);
    }

    // Now, create "special" pages, such as image pages or blank pages, and rotate
    // final pages (we must check, that page object exists).
    for (ProcessedPage& processedPage : processedPages)
    {
        if (processedPage.assembledPage.isBlankPage() || processedPage.assembledPage.isImagePage())
        {
            QImage image;

            if (processedPage.assembledPage.isImagePage())
            {
                const PDFInteger imageIndex = processedPage.assembledPage.imageIndex;

                if (!m_images.count(imageIndex))
                {
                    throw PDFException(tr("Missing image."));
                }

                image = m_images.at(imageIndex);

                if (image.isNull())
                {
                    throw PDFException(tr("Missing image."));
                }
            }

            QRectF pageRect = QRectF(QPointF(0, 0), processedPage.assembledPage.pageSize * PDF_MM_TO_POINT);
            processedPage.targetPageReference = documentBuilder.appendPage(pageRect);
            PDFPageContentStreamBuilder contentStreamBuilder(&documentBuilder);

            QPainter* painter = contentStreamBuilder.begin(processedPage.targetPageReference);

            if (processedPage.assembledPage.isImagePage())
            {
                // Just paint the image
                painter->drawImage(pageRect, image);
            }

            contentStreamBuilder.end(painter);
        }

        if (!processedPage.targetPageReference.isValid())
        {
            throw PDFException(tr("Error occured during page creation."));
        }

        documentBuilder.setPageRotation(processedPage.targetPageReference, processedPage.assembledPage.pageRotation);
    }

    return processedPages;
}

PDFDocumentManipulator::ProcessedPages PDFDocumentManipulator::collectObjectsAndCopyPages(PDFDocumentBuilder& documentBuilder, const AssembledPages& pages)
{
    ProcessedPages processedPages;
    processedPages.reserve(pages.size());

    std::map<std::pair<int, int>, PDFObjectReference> documentPages;

    for (const AssembledPage& assembledPage : pages)
    {
        ProcessedPage processedPage;
        processedPage.assembledPage = assembledPage;
        processedPages.push_back(processedPage);

        if (assembledPage.isDocumentPage())
        {
            documentPages[std::make_pair(assembledPage.documentIndex, assembledPage.pageIndex)] = PDFObjectReference();
        }
    }

    for (auto it = documentPages.begin(); it != documentPages.end();)
    {
        const int documentIndex = it->first.first;

        // Jakub Melka: we will find end of a single document page range
        auto itEnd = it;
        while (itEnd != documentPages.end() && itEnd->first.first == documentIndex)
        {
            ++itEnd;
        }

        if (documentIndex != -1)
        {
            // Check we have the document
            if (!m_documents.count(documentIndex))
            {
                throw PDFException(tr("Invalid document."));
            }
            const PDFDocument* document = m_documents.at(documentIndex);

            // Copy the pages into the target document builder
            std::vector<PDFInteger> pageIndices;
            for (auto currentIt = it; currentIt != itEnd; ++currentIt)
            {
                pageIndices.push_back(currentIt->first.second);
            }

            pdf::PDFDocumentBuilder temporaryBuilder(document);
            temporaryBuilder.flattenPageTree();

            std::vector<pdf::PDFObjectReference> currentPages = temporaryBuilder.getPages();
            std::vector<pdf::PDFObjectReference> objectsToMerge;
            objectsToMerge.reserve(std::distance(it, itEnd));

            for (int pageIndex : pageIndices)
            {
                if (pageIndex < 0 || pageIndex >= currentPages.size())
                {
                    throw PDFException(tr("Missing page (%1) in a document.").arg(pageIndex));
                }

                objectsToMerge.push_back(currentPages[pageIndex]);
            }

            pdf::PDFObjectReference acroFormReference;
            pdf::PDFObjectReference namesReference;
            pdf::PDFObjectReference ocPropertiesReference;

            pdf::PDFObject formObject = document->getCatalog()->getFormObject();
            if (formObject.isReference())
            {
                acroFormReference = formObject.getReference();
            }
            else
            {
                acroFormReference = temporaryBuilder.addObject(formObject);
            }

            if (const pdf::PDFDictionary* catalogDictionary = temporaryBuilder.getDictionaryFromObject(temporaryBuilder.getObjectByReference(temporaryBuilder.getCatalogReference())))
            {
                pdf::PDFObject namesObject = catalogDictionary->get("Names");
                if (namesObject.isReference())
                {
                    namesReference = namesObject.getReference();
                }

                pdf::PDFObject ocPropertiesObject = catalogDictionary->get("OCProperties");
                if (ocPropertiesObject.isReference())
                {
                    ocPropertiesReference = ocPropertiesObject.getReference();
                }
            }

            if (!namesReference.isValid())
            {
                namesReference = temporaryBuilder.addObject(pdf::PDFObject());
            }

            if (!ocPropertiesReference.isValid())
            {
                ocPropertiesReference = temporaryBuilder.addObject(pdf::PDFObject());
            }

            objectsToMerge.insert(objectsToMerge.end(), { acroFormReference, namesReference, ocPropertiesReference });

            // Now, we are ready to merge objects into target document builder
            std::vector<pdf::PDFObjectReference> references = pdf::PDFDocumentBuilder::createReferencesFromObjects(documentBuilder.copyFrom(pdf::PDFDocumentBuilder::createObjectsFromReferences(objectsToMerge), *temporaryBuilder.getStorage(), true));

            ocPropertiesReference = references.back();
            references.pop_back();
            namesReference = references.back();
            references.pop_back();
            acroFormReference = references.back();
            references.pop_back();

            documentBuilder.appendTo(m_mergedObjects[MOT_OCProperties], documentBuilder.getObjectByReference(ocPropertiesReference));
            documentBuilder.appendTo(m_mergedObjects[MOT_Form], documentBuilder.getObjectByReference(acroFormReference));
            documentBuilder.mergeNames(m_mergedObjects[MOT_Names], namesReference);

            Q_ASSERT(references.size() == std::distance(it, itEnd));

            auto referenceIt = references.begin();
            for (auto currentIt = it; currentIt != itEnd; ++currentIt, ++referenceIt)
            {
                it->second = *referenceIt;
            }
        }

        // Advance the index
        it = itEnd;
    }

    std::set<PDFObjectReference> usedReferences;
    for (ProcessedPage& processedPage : processedPages)
    {
        if (processedPage.assembledPage.isDocumentPage())
        {
            auto key = std::make_pair(processedPage.assembledPage.documentIndex, processedPage.assembledPage.pageIndex);
            Q_ASSERT(documentPages.count(key));

            PDFObjectReference pageReference = documentPages.at(key);
            if (!usedReferences.count(pageReference))
            {
                processedPage.targetPageReference = pageReference;
                usedReferences.insert(pageReference);
            }
            else
            {
                // Page is being cloned. So we must clone it...
                std::vector<pdf::PDFObjectReference> references = pdf::PDFDocumentBuilder::createReferencesFromObjects(documentBuilder.copyFrom(pdf::PDFDocumentBuilder::createObjectsFromReferences({ pageReference }), *documentBuilder.getStorage(), true));
                Q_ASSERT(references.size() == 1);
                processedPage.targetPageReference = references.front();
                usedReferences.insert(references.front());
            }
        }
    }

    return processedPages;
}

void PDFDocumentManipulator::classify(const AssembledPages& pages)
{
    m_flags = None;

    std::set<PDFInteger> documentIndices;
    std::set<PDFInteger> pageIndices;
    for (const AssembledPage& assembledPage : pages)
    {
        documentIndices.insert(assembledPage.documentIndex);
        pageIndices.insert(assembledPage.pageIndex);
    }

    documentIndices.erase(-1);
    pageIndices.erase(-1);

    m_flags.setFlag(SingleDocument, documentIndices.size() == 1);

    if (m_flags.testFlag(SingleDocument) && m_documents.count(*documentIndices.begin()))
    {
        const PDFDocument* document = m_documents.at(*documentIndices.begin());
        const bool pagesRemoved = pageIndices.size() < document->getCatalog()->getPageCount();
        m_flags.setFlag(RemovedPages, pagesRemoved);
    }
}

void PDFDocumentManipulator::initializeMergedObjects(PDFDocumentBuilder& documentBuilder)
{
    m_mergedObjects[MOT_OCProperties] = documentBuilder.addObject(PDFObject());
    m_mergedObjects[MOT_Form] = documentBuilder.addObject(PDFObject());
    m_mergedObjects[MOT_Names] = documentBuilder.addObject(PDFObject());
}

void PDFDocumentManipulator::finalizeMergedObjects(PDFDocumentBuilder& documentBuilder)
{
    if (!m_flags.testFlag(SingleDocument))
    {
        if (!documentBuilder.getObjectByReference(m_mergedObjects[MOT_OCProperties]).isNull())
        {
            documentBuilder.setCatalogOptionalContentProperties(m_mergedObjects[MOT_OCProperties]);
        }

        if (!documentBuilder.getObjectByReference(m_mergedObjects[MOT_Names]).isNull())
        {
            documentBuilder.setCatalogNames(m_mergedObjects[MOT_Names]);
        }

        if (!documentBuilder.getObjectByReference(m_mergedObjects[MOT_Form]).isNull())
        {
            documentBuilder.setCatalogAcroForm(m_mergedObjects[MOT_Form]);
        }
    }
}

void PDFDocumentManipulator::finalizeDocument(PDFDocument* document)
{
    auto optimizationFlags = pdf::PDFOptimizer::OptimizationFlags(PDFOptimizer::RemoveUnusedObjects |
                                                                  PDFOptimizer::ShrinkObjectStorage |
                                                                  PDFOptimizer::DereferenceSimpleObjects |
                                                                  PDFOptimizer::MergeIdenticalObjects);
    PDFOptimizer optimizer(optimizationFlags, nullptr);
    optimizer.setDocument(document);
    optimizer.optimize();
    PDFDocument mergedDocument = optimizer.takeOptimizedDocument();

    // We must adjust some objects - they can have merged objects
    pdf::PDFDocumentBuilder finalBuilder(&mergedDocument);
    if (const pdf::PDFDictionary* dictionary = finalBuilder.getDictionaryFromObject(finalBuilder.getObjectByReference(finalBuilder.getCatalogReference())))
    {
        pdf::PDFDocumentDataLoaderDecorator loader(finalBuilder.getStorage());
        pdf::PDFObjectReference ocPropertiesReference = loader.readReferenceFromDictionary(dictionary, "OCProperties");
        if (ocPropertiesReference.isValid())
        {
            finalBuilder.setObject(ocPropertiesReference, pdf::PDFObjectManipulator::removeDuplicitReferencesInArrays(finalBuilder.getObjectByReference(ocPropertiesReference)));
        }
        pdf::PDFObjectReference acroFormReference = loader.readReferenceFromDictionary(dictionary, "AcroForm");
        if (acroFormReference.isValid())
        {
            finalBuilder.setObject(acroFormReference, pdf::PDFObjectManipulator::removeDuplicitReferencesInArrays(finalBuilder.getObjectByReference(acroFormReference)));
        }
    }
    m_assembledDocument = finalBuilder.build();
}

}   // namespace pdf
