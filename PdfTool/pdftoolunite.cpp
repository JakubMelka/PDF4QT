//    Copyright (C) 2020-2021 Jakub Melka
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

#include "pdftoolunite.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfoptimizer.h"
#include "pdfdocumentwriter.h"

namespace pdftool
{

static PDFToolUnite s_toolUniteApplication;

QString PDFToolUnite::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "unite";

        case Name:
            return PDFToolTranslationContext::tr("Merge documents");

        case Description:
            return PDFToolTranslationContext::tr("Merge multiple documents to a single document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolUnite::execute(const PDFToolOptions& options)
{
    if (options.uniteFiles.size() < 3)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("At least two documents and target (merged) document must be specified."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    QStringList files = options.uniteFiles;
    QString targetFile = files.back();
    files.pop_back();

    if (QFileInfo::exists(targetFile))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Target file '%1' already exists. Document merging not performed.").arg(targetFile), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    try
    {
        pdf::PDFDocumentBuilder documentBuilder;
        documentBuilder.createDocument();

        std::vector<size_t> documentPartPageCounts;

        pdf::PDFObjectReference ocPropertiesMerged = documentBuilder.addObject(pdf::PDFObject());
        pdf::PDFObjectReference formMerged = documentBuilder.addObject(pdf::PDFObject());
        pdf::PDFObjectReference namesMerged = documentBuilder.addObject(pdf::PDFObject());

        std::vector<pdf::PDFObjectReference> pages;
        for (const QString& fileName : files)
        {
            pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, options.permissiveReading, false);
            pdf::PDFDocument document = reader.readFromFile(fileName);
            if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot open document '%1'.").arg(fileName), options.outputCodec);
                return ErrorDocumentReading;
            }

            if (!document.getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::Assemble))
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Document doesn't allow to assemble pages."), options.outputCodec);
                return ErrorPermissions;
            }

            pdf::PDFDocumentBuilder temporaryBuilder(&document);
            temporaryBuilder.flattenPageTree();

            std::vector<pdf::PDFObjectReference> currentPages = temporaryBuilder.getPages();
            std::vector<pdf::PDFObjectReference> objectsToMerge = currentPages;

            pdf::PDFObjectReference acroFormReference;
            pdf::PDFObjectReference namesReference;
            pdf::PDFObjectReference ocPropertiesReference;

            pdf::PDFObject formObject = document.getCatalog()->getFormObject();
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

            documentPartPageCounts.push_back(references.size());

            documentBuilder.appendTo(ocPropertiesMerged, documentBuilder.getObjectByReference(ocPropertiesReference));
            documentBuilder.appendTo(formMerged, documentBuilder.getObjectByReference(acroFormReference));
            documentBuilder.mergeNames(namesMerged, namesReference);
            pages.insert(pages.end(), references.cbegin(), references.cend());
        }

        documentBuilder.setPages(pages);
        if (!documentBuilder.getObjectByReference(ocPropertiesMerged).isNull())
        {
            documentBuilder.setCatalogOptionalContentProperties(ocPropertiesMerged);
        }

        if (!documentBuilder.getObjectByReference(namesMerged).isNull())
        {
            documentBuilder.setCatalogNames(namesMerged);
        }

        if (!documentBuilder.getObjectByReference(formMerged).isNull())
        {
            documentBuilder.setCatalogAcroForm(formMerged);
        }

        // Correct page tree (invalid parents are present)
        documentBuilder.flattenPageTree();
        documentBuilder.createDocumentParts(documentPartPageCounts);
        pdf::PDFDocument mergedDocument = documentBuilder.build();

        // Optimize document - remove unused objects and shrink object storage
        pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::RemoveUnusedObjects | pdf::PDFOptimizer::ShrinkObjectStorage | pdf::PDFOptimizer::DereferenceSimpleObjects | pdf::PDFOptimizer::MergeIdenticalObjects, nullptr);
        optimizer.setDocument(&mergedDocument);
        optimizer.optimize();
        mergedDocument = optimizer.takeOptimizedDocument();

        // We must adjust some objects - they can be merged
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
        mergedDocument = finalBuilder.build();

        pdf::PDFDocumentWriter writer(nullptr);
        pdf::PDFOperationResult result = writer.write(targetFile, &mergedDocument, false);
        if (!result)
        {
            PDFConsole::writeError(result.getErrorMessage(), options.outputCodec);
            return ErrorFailedWriteToFile;
        }
    }
    catch (pdf::PDFException exception)
    {
        PDFConsole::writeError(exception.getMessage(), options.outputCodec);
        return ErrorUnknown;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolUnite::getOptionsFlags() const
{
    return ConsoleFormat | Unite;
}

}   // namespace pdftool
