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

#include "pdfocrapplyprocessor.h"
#include "pdfocrconfiguration.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrengine.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"
#include "pdfsecurityhandler.h"
#include "pdfexception.h"
#include "pdfcatalog.h"
#include "pdfform.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace pdf
{

std::vector<PDFInteger> PDFOCRApplyProcessor::Plan::getPages() const
{
    std::vector<PDFInteger> pages;
    pages.reserve(requests.size());
    for (const PDFOCRTextLayerWriter::PageRequest& request : requests)
    {
        pages.push_back(request.pageIndex);
    }
    return pages;
}

PDFOCRApplyProcessor::Context PDFOCRApplyProcessor::createContext(const PDFDocument* document, const QString& fileName)
{
    Context context;
    context.document = document;
    context.fileName = fileName;

    if (!document)
    {
        return context;
    }

    if (const PDFSecurityHandler* securityHandler = document->getStorage().getSecurityHandler())
    {
        context.canModify = securityHandler->isAllowed(PDFSecurityHandler::Permission::Modify);
        context.canCopyContent = securityHandler->isAllowed(PDFSecurityHandler::Permission::CopyContent);
        context.isEncrypted = securityHandler->getMode() != EncryptionMode::None;
    }

    context.certificationPermissions = getCertificationPermissions(document);
    context.hasSignatures = hasSignatureFields(document);
    context.isTagged = PDFOCRPagePreparer::isTaggedDocument(document);
    PDFOCRPagePreparer::hasConformanceDeclaration(document, &context.conformanceDeclarations);
    return context;
}

int PDFOCRApplyProcessor::getCertificationPermissions(const PDFDocument* document)
{
    if (!document)
    {
        return 0;
    }

    const PDFDictionary* trailer = document->getTrailerDictionary();
    const PDFDictionary* catalog = trailer ? document->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    const PDFDictionary* permissions = catalog ? document->getDictionaryFromObject(catalog->get("Perms")) : nullptr;
    if (!permissions || !permissions->hasKey("DocMDP") || document->getObject(permissions->get("DocMDP")).isNull())
    {
        return 0;
    }

    // The permissions are in the transform parameters of the DocMDP signature reference;
    // the default value of /P is 2 (ISO 32000-2, 12.8.2.2)
    PDFInteger value = 2;
    PDFDocumentDataLoaderDecorator loader(document);
    if (const PDFDictionary* signature = document->getDictionaryFromObject(permissions->get("DocMDP")))
    {
        const PDFObject& referencesObject = document->getObject(signature->get("Reference"));
        if (referencesObject.isArray() && referencesObject.getArray()->getCount() > 0)
        {
            const PDFDictionary* reference = document->getDictionaryFromObject(referencesObject.getArray()->getItem(0));
            const PDFDictionary* transformParameters = reference ? document->getDictionaryFromObject(reference->get("TransformParams")) : nullptr;
            if (transformParameters)
            {
                value = loader.readIntegerFromDictionary(transformParameters, "P", 2);
            }
        }
    }

    return int(std::clamp(value, PDFInteger(1), PDFInteger(3)));
}

bool PDFOCRApplyProcessor::hasSignatureFields(const PDFDocument* document)
{
    if (!document)
    {
        return false;
    }

    bool hasSignature = false;
    try
    {
        const PDFForm form = PDFForm::parse(document, document->getCatalog()->getFormObject());
        form.apply([&](const PDFFormField* field)
        {
            if (field->getFieldType() == PDFFormField::FieldType::Signature && !document->getObject(field->getValue()).isNull())
            {
                hasSignature = true;
            }
        });
    }
    catch (const PDFException&)
    {
        // A damaged form is not a signature
    }

    return hasSignature;
}

QString PDFOCRApplyProcessor::checkPermissions(const Context& context, OutputMode outputMode)
{
    if (!context.canModify)
    {
        // A copy with the text layer is a modified document as well (PDF-12)
        return PDFTranslationContext::tr("The permissions of the document do not allow its modification, so the text layer cannot be written into the document nor into its copy. The recognized text can be exported, if the permissions allow copying of the content.");
    }

    // The certification signature (DocMDP) is enforced, not only reported (PDF-12)
    if (context.certificationPermissions == 1)
    {
        return PDFTranslationContext::tr("The document is certified and its certification does not allow any change. The text layer cannot be written into the document nor into its copy; the recognized text can only be exported.");
    }

    if (outputMode == OutputMode::ModifyDocument)
    {
        if (context.certificationPermissions > 0)
        {
            return PDFTranslationContext::tr("The document is certified and its certification allows only filling of forms, signing and annotating. Writing the text layer would invalidate the certification, so the current document cannot be modified. "
                                             "Use the output mode 'Create a copy of the document with OCR'; the certification of the copy will not be valid.");
        }

        if (context.hasConformanceDeclaration())
        {
            // A false conformance declaration must not be kept (PDF-15)
            return PDFTranslationContext::tr("The document declares the conformance with %1. The conformance of the result cannot be validated, so the text layer cannot be written into this document. "
                                             "Use the output mode 'Create a copy of the document with OCR'; the copy will be an ordinary PDF without the unverified conformance declaration.")
                    .arg(context.conformanceDeclarations.join(QStringLiteral(", ")));
        }
    }

    return QString();
}

PDFOCRApplyProcessor::Plan PDFOCRApplyProcessor::createPlan(const Context& context, const Request& request)
{
    Plan plan;
    plan.outputMode = request.outputMode;
    plan.writerOptions = request.writerOptions;
    plan.writerOptions.markAsArtifact = context.isTagged;
    plan.compression = request.compression;
    plan.memoryBudget = request.memoryBudget;
    plan.removeConformance = request.outputMode == OutputMode::CreateCopy && context.hasConformanceDeclaration();
    plan.usedAllResults = request.usedAllResults;

    const PDFInteger pageCount = context.document ? PDFInteger(context.document->getCatalog()->getPageCount()) : 0;

    auto exclude = [&plan](PDFInteger pageIndex, const QString& reason)
    {
        plan.excludedPages.push_back(pageIndex);
        plan.excluded << PDFTranslationContext::tr("Page %1: %2").arg(pageIndex + 1).arg(reason);
    };

    for (const PDFOCRPageResult& result : request.results)
    {
        const PDFInteger pageIndex = result.pageIndex;

        if (pageIndex < 0 || pageIndex >= pageCount)
        {
            exclude(pageIndex, PDFTranslationContext::tr("the page is not a part of the document."));
            continue;
        }

        if (!result.hasResult())
        {
            exclude(pageIndex, PDFTranslationContext::tr("the page has no result."));
            continue;
        }

        if (result.reviewOnly)
        {
            exclude(pageIndex, PDFTranslationContext::tr("recognized for review/export only."));
            continue;
        }

        std::shared_ptr<PDFOCREngineFactory> factory = result.provenance.engineId.isEmpty() ? nullptr : PDFOCREngineRegistry::getInstance()->getFactory(result.provenance.engineId);
        if (factory && factory->getCapabilities().isExportOnly)
        {
            exclude(pageIndex, PDFTranslationContext::tr("recognized by an engine, whose results can only be exported."));
            continue;
        }

        auto analysisIt = request.analysis.find(pageIndex);
        const PDFOCRPageAnalysis* analysis = analysisIt != request.analysis.end() ? &analysisIt->second : nullptr;

        // An unknown page (not analyzed yet) is decided by the writer: an obsolete own layer is removed
        if (!result.hasUsableText() && analysis && !analysis->hasOwnOCRLayer)
        {
            exclude(pageIndex, PDFTranslationContext::tr("no text to write."));
            continue;
        }

        // The revision of the document is verified again (JOB-02, EXPORT-04)
        auto fingerprintIt = request.fingerprints.find(pageIndex);
        if (fingerprintIt != request.fingerprints.end() && result.pageFingerprint != fingerprintIt->second)
        {
            exclude(pageIndex, PDFTranslationContext::tr("the page content differs from the content, which was recognized."));
            continue;
        }

        if (!PDFOCRValidator::validate(result).isEmpty())
        {
            exclude(pageIndex, PDFTranslationContext::tr("the result has invalid geometry."));
            continue;
        }

        const PDFOCRConfidenceStatistics statistics = PDFOCRConfidenceStatistics::compute(result, request.reviewCriteria);
        plan.unreviewedWords += statistics.unreviewedCount;
        plan.uncertainWords += statistics.reviewRequiredCount;
        plan.outsideDictionaryWords += statistics.outsideDictionaryCount;

        PDFOCRTextLayerWriter::PageRequest pageRequest;
        pageRequest.pageIndex = pageIndex;
        pageRequest.result = result;

        // The writer checks the collisions with the existing text of the current
        // document, not of the document of the recognition (PDF-02)
        if (analysis)
        {
            pageRequest.result.analysis = *analysis;
            if (analysis->hasOwnOCRLayer)
            {
                ++plan.replacedLayers;
            }
        }

        if (fingerprintIt != request.fingerprints.end())
        {
            plan.knownFingerprints[pageIndex] = fingerprintIt->second;
        }

        plan.requests.push_back(std::move(pageRequest));
    }

    return plan;
}

QString PDFOCRApplyProcessor::setCopyFileName(Plan& plan, const Context& context, const QString& copyFileName)
{
    if (copyFileName.isEmpty())
    {
        return PDFTranslationContext::tr("The file name of the copy is empty.");
    }

    // Canonical paths of files, which do not exist, are empty and would compare equal
    if (!context.fileName.isEmpty())
    {
        const QFileInfo copyInfo(copyFileName);
        const QFileInfo sourceInfo(context.fileName);
        const bool isSameFile = (copyInfo.exists() && sourceInfo.exists()) ? copyInfo == sourceInfo
                                                                          : QDir::cleanPath(copyInfo.absoluteFilePath()) == QDir::cleanPath(sourceInfo.absoluteFilePath());
        if (isSameFile)
        {
            return PDFTranslationContext::tr("The copy cannot overwrite the opened document.");
        }
    }

    plan.copyFileName = copyFileName;
    return QString();
}

QStringList PDFOCRApplyProcessor::getSummary(const Plan& plan)
{
    QStringList summary;
    summary << (plan.outputMode == OutputMode::CreateCopy ? PDFTranslationContext::tr("Target: copy of the document, %1").arg(QDir::toNativeSeparators(plan.copyFileName))
                                                          : PDFTranslationContext::tr("Target: current document (saved later by the standard Save command)"));
    summary << PDFTranslationContext::tr("Pages: %1%2").arg(PDFOCRPageSelection::describe(plan.getPages()), plan.usedAllResults ? PDFTranslationContext::tr(" (all pages with a result, because no checked page has a result)") : QString());
    summary << PDFTranslationContext::tr("Own OCR layers to be replaced: %1").arg(plan.replacedLayers);
    summary << PDFTranslationContext::tr("Words requiring review: %1, unreviewed words: %2").arg(plan.uncertainWords).arg(plan.unreviewedWords);
    if (plan.outsideDictionaryWords > 0)
    {
        summary << PDFTranslationContext::tr("Unreviewed words not found in the dictionary: %1").arg(plan.outsideDictionaryWords);
    }
    if (plan.compression.isEnabled())
    {
        summary << PDFTranslationContext::tr("Compression of the scanned images: %1").arg(PDFOCRCompressionSettings::getModeName(plan.compression.mode));
    }
    return summary;
}

QStringList PDFOCRApplyProcessor::getWarnings(const Plan& plan, const Context& context)
{
    QStringList warnings;
    if (plan.writerOptions.onlyReviewed)
    {
        warnings << PDFTranslationContext::tr("Only reviewed words will be written: the text layer will be INCOMPLETE.");
    }
    else if (plan.uncertainWords > 0)
    {
        warnings << PDFTranslationContext::tr("Uncertain words are written as well; the uncertainty is an information for the review, not a filter of the text.");
    }
    if (plan.outputMode == OutputMode::CreateCopy && context.certificationPermissions > 0)
    {
        warnings << PDFTranslationContext::tr("The document is certified. The certification of the copy is not valid, because the copy contains the added text layer.");
    }
    else if (context.hasSignatures)
    {
        // No claim, that the signatures stay valid (PDF-12)
        warnings << PDFTranslationContext::tr("The document is signed. Writing the text layer changes the content of the document; the state of the signatures or of the certification may stop to be valid, depending on the signature type and on the way of saving. Consider creating a copy.");
    }
    if (context.isTagged)
    {
        warnings << PDFTranslationContext::tr("The document is tagged. The existing structure tree is preserved and the text layer is written as an artifact; the result is not a complete accessible (PDF/UA) document.");
    }
    if (plan.outputMode == OutputMode::CreateCopy && context.hasConformanceDeclaration())
    {
        warnings << PDFTranslationContext::tr("The copy will be an ordinary PDF: the unverified declaration of conformance (%1) is removed from its metadata.").arg(context.conformanceDeclarations.join(QStringLiteral(", ")));
    }
    if (plan.compression.isEnabled())
    {
        if (plan.compression.isLossy())
        {
            warnings << PDFTranslationContext::tr("The compression of the scanned images is LOSSY: the images of the written pages are changed (%1).").arg(PDFOCRCompressionSettings::getModeName(plan.compression.mode));
        }
        warnings << PDFTranslationContext::tr("The images are compressed: an OCR project saved before does not match the changed pages anymore; save the project again after writing.");
    }
    warnings << PDFTranslationContext::tr("Correcting or deleting the text of the layer is not a redaction of the scanned image.");
    return warnings;
}

PDFOCRApplyProcessor::Result PDFOCRApplyProcessor::execute(const Context& context, const Plan& plan, const PDFOperationControl* operationControl)
{
    Result result;
    result.copyFileName = plan.copyFileName;

    const PDFDocument* document = context.document;
    if (!document)
    {
        result.errorMessage = PDFTranslationContext::tr("No document.");
        return result;
    }

    const QString permissionError = checkPermissions(context, plan.outputMode);
    if (!permissionError.isEmpty())
    {
        result.errorMessage = permissionError;
        return result;
    }

    try
    {
        // The missing fingerprints are computed here, outside of the GUI thread (R12, JOB-02, EXPORT-04)
        std::vector<PDFOCRTextLayerWriter::PageRequest> pageRequests;
        QStringList changedPages;
        for (PDFOCRTextLayerWriter::PageRequest request : plan.requests)
        {
            auto it = plan.knownFingerprints.find(request.pageIndex);
            const QByteArray fingerprint = it != plan.knownFingerprints.end() ? it->second : PDFOCRPagePreparer::computePageFingerprint(document, request.pageIndex);
            if (request.result.pageFingerprint != fingerprint)
            {
                changedPages << PDFTranslationContext::tr("Page %1: the page content differs from the content, which was recognized.").arg(request.pageIndex + 1);
                continue;
            }
            pageRequests.push_back(std::move(request));
        }

        if (pageRequests.empty())
        {
            result.report.messages << changedPages;
            result.errorMessage = changedPages.isEmpty() ? PDFTranslationContext::tr("There is no result, which can be written into the PDF.") : changedPages.join(QChar('\n'));
            return result;
        }

        std::vector<PDFInteger> pages;
        for (const PDFOCRTextLayerWriter::PageRequest& request : pageRequests)
        {
            pages.push_back(request.pageIndex);
        }

        // Compression of the scanned images before the text layer, so the layer is bound
        // to the compressed page (its fingerprint covers the image streams)
        PDFDocumentPointer compressedDocument;
        const PDFDocument* baseDocument = document;
        if (plan.compression.isEnabled())
        {
            PDFDocument compressed = PDFOCRImageCompressor::compress(document, pages, plan.compression, plan.memoryBudget, operationControl, &result.compressionReport);
            if (PDFOperationControl::isOperationCancelled(operationControl))
            {
                result.errorMessage = PDFTranslationContext::tr("Operation was cancelled.");
                return result;
            }

            if (result.compressionReport.isChanged() || result.compressionReport.versionRaised)
            {
                compressedDocument.reset(new PDFDocument(std::move(compressed)));
                baseDocument = compressedDocument.data();
            }
        }

        // Own layers of the pages are replaced by the new layer
        for (PDFOCRTextLayerWriter::PageRequest& request : pageRequests)
        {
            const PDFOCRTextLayerWriter::LayerInfo layerInfo = PDFOCRTextLayerWriter::readLayerInfo(baseDocument, request.pageIndex);
            if (layerInfo.isPresent)
            {
                request.layerId = layerInfo.layerId;
            }
        }

        PDFDocumentModifier modifier(baseDocument);
        result.report = PDFOCRTextLayerWriter::apply(modifier.getBuilder(), baseDocument, pageRequests, plan.writerOptions);
        result.report.messages << changedPages;

        if (result.report.error)
        {
            result.errorMessage = result.report.error.message;
            return result;
        }

        if (plan.removeConformance && !PDFOCRTextLayerWriter::removeConformanceDeclaration(modifier.getBuilder(), baseDocument))
        {
            // An unverified declaration of conformance must not stay in the copy (PDF-15, R07)
            result.errorMessage = PDFTranslationContext::tr("The conformance declaration could not be removed from the metadata, the copy was not written.");
            return result;
        }

        if (PDFOperationControl::isOperationCancelled(operationControl))
        {
            result.errorMessage = PDFTranslationContext::tr("Operation was cancelled.");
            return result;
        }

        result.conformanceRemoved = plan.removeConformance;

        if (result.report.isModified() || result.conformanceRemoved || compressedDocument || !plan.copyFileName.isEmpty())
        {
            modifier.markPageContentsChanged();
            if (modifier.finalize())
            {
                result.document = modifier.getDocument();
            }
            else if (compressedDocument)
            {
                // The text layer is identical, only the images were compressed
                result.document = compressedDocument;
            }
            else if (!plan.copyFileName.isEmpty())
            {
                result.document = PDFDocumentPointer(new PDFDocument(*document));
            }
        }

        // Validation of the result: every written layer must be readable and bound to its page
        if (result.document)
        {
            for (PDFInteger pageIndex : result.report.writtenPages)
            {
                const PDFOCRTextLayerWriter::LayerInfo info = PDFOCRTextLayerWriter::readLayerInfo(result.document.data(), pageIndex);
                if (!info.isPresent || !info.fingerprintMatches)
                {
                    result.errorMessage = PDFTranslationContext::tr("Validation of the text layer of the page %1 failed.").arg(pageIndex + 1);
                    result.document.reset();
                    return result;
                }

                // Nesting of the whole content of the page is validated by the parser (PDF-05)
                QString validationError;
                if (!PDFOCRTextLayerWriter::validatePageContent(result.document.data(), pageIndex, &validationError))
                {
                    result.errorMessage = PDFTranslationContext::tr("The content of the page %1 is not valid after writing the text layer: %2").arg(pageIndex + 1).arg(validationError);
                    result.document.reset();
                    return result;
                }
            }

            // New fingerprints of the pages (they change, when the images were compressed)
            for (PDFInteger pageIndex : pages)
            {
                result.fingerprints[pageIndex] = PDFOCRPagePreparer::computePageFingerprint(result.document.data(), pageIndex);
            }
        }

        if (result.document && !plan.copyFileName.isEmpty() && !PDFOperationControl::isOperationCancelled(operationControl))
        {
            PDFDocumentWriter writer(nullptr);
            const PDFOperationResult writeResult = writer.write(plan.copyFileName, result.document.data(), true);
            if (!writeResult)
            {
                result.errorMessage = writeResult.getErrorMessage();
            }
        }
    }
    catch (const PDFException& exception)
    {
        result.errorMessage = exception.getMessage();
        result.document.reset();
    }
    catch (const std::exception& exception)
    {
        result.errorMessage = QString::fromLocal8Bit(exception.what());
        result.document.reset();
    }
    catch (...)
    {
        result.errorMessage = PDFTranslationContext::tr("Unexpected error.");
        result.document.reset();
    }

    return result;
}

PDFOCRApplyProcessor::Result PDFOCRApplyProcessor::removeLayers(const Context& context, const std::vector<PDFInteger>& pages, const PDFOperationControl* operationControl)
{
    Result result;

    if (!context.document)
    {
        result.errorMessage = PDFTranslationContext::tr("No document.");
        return result;
    }

    if (!context.canModify)
    {
        result.errorMessage = PDFTranslationContext::tr("The permissions of the document do not allow its modification.");
        return result;
    }

    if (context.certificationPermissions > 0)
    {
        // The removal changes the content of the certified document (PDF-12)
        result.errorMessage = PDFTranslationContext::tr("The document is certified; removing the text layer would invalidate the certification.");
        return result;
    }

    try
    {
        PDFDocumentModifier modifier(context.document);
        for (PDFInteger page : pages)
        {
            if (PDFOperationControl::isOperationCancelled(operationControl))
            {
                result.errorMessage = PDFTranslationContext::tr("Operation was cancelled.");
                return result;
            }

            if (PDFOCRTextLayerWriter::removeLayer(modifier.getBuilder(), context.document, page))
            {
                result.report.writtenPages.push_back(page);
            }
        }

        modifier.markPageContentsChanged();
        if (!result.report.writtenPages.empty() && modifier.finalize())
        {
            result.document = modifier.getDocument();
        }
    }
    catch (const PDFException& exception)
    {
        result.errorMessage = exception.getMessage();
        result.document.reset();
    }
    catch (const std::exception& exception)
    {
        result.errorMessage = QString::fromLocal8Bit(exception.what());
        result.document.reset();
    }
    catch (...)
    {
        result.errorMessage = PDFTranslationContext::tr("Unexpected error.");
        result.document.reset();
    }

    return result;
}

}   // namespace pdf
