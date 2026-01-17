// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdftoolremoveexternallinks.h"
#include "pdfannotation.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

namespace pdftool
{

namespace
{
struct RemovalResult
{
    pdf::PDFDocumentPointer document;
    pdf::PDFModifiedDocument::ModificationFlags flags;
    int removed = 0;
};

RemovalResult removeExternalLinkAnnotations(const pdf::PDFDocument* document)
{
    RemovalResult result;
    if (!document)
    {
        return result;
    }

    pdf::PDFDocumentModifier modifier(document);
    pdf::PDFDocumentBuilder* builder = modifier.getBuilder();
    builder->flattenPageTree();

    const pdf::PDFObjectStorage* storage = builder->getStorage();
    pdf::PDFDocumentDataLoaderDecorator loader(storage);

    std::vector<std::pair<pdf::PDFObjectReference, pdf::PDFObjectReference>> annotationsToRemove;
    std::vector<pdf::PDFObjectReference> pageReferences = builder->getPages();

    for (const pdf::PDFObjectReference pageReference : pageReferences)
    {
        const pdf::PDFObject& pageObject = storage->getObjectByReference(pageReference);
        const pdf::PDFDictionary* pageDictionary = storage->getDictionaryFromObject(pageObject);
        if (!pageDictionary)
        {
            continue;
        }

        std::vector<pdf::PDFObjectReference> annotationReferences = loader.readReferenceArrayFromDictionary(pageDictionary, "Annots");
        for (const pdf::PDFObjectReference& annotationReference : annotationReferences)
        {
            pdf::PDFAnnotationPtr annotation = pdf::PDFAnnotation::parse(storage, annotationReference);
            if (pdf::PDFAnnotation::isExternalLinkAnnotation(annotation.data()))
            {
                annotationsToRemove.emplace_back(pageReference, annotationReference);
            }
        }
    }

    result.removed = static_cast<int>(annotationsToRemove.size());
    if (annotationsToRemove.empty())
    {
        return result;
    }

    for (const auto& item : annotationsToRemove)
    {
        builder->removeAnnotation(item.first, item.second);
    }

    modifier.markAnnotationsChanged();
    if (modifier.finalize())
    {
        result.document = modifier.getDocument();
        result.flags = modifier.getFlags();
    }

    return result;
}
}

static PDFToolRemoveExternalLinks s_removeExternalLinksApplication;

QString PDFToolRemoveExternalLinks::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "remove-external-links";

        case Name:
            return PDFToolTranslationContext::tr("Remove external links");

        case Description:
            return PDFToolTranslationContext::tr("Remove all external link annotations from a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolRemoveExternalLinks::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    RemovalResult result = removeExternalLinkAnnotations(&document);

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("remove-external-links", PDFToolTranslationContext::tr("Remove External Links"));

    if (result.removed == 0)
    {
        formatter.writeText("result", PDFToolTranslationContext::tr("No external link annotations found."));
        formatter.endDocument();
        PDFConsole::writeText(formatter.getString(), options.outputCodec);
        return ExitSuccess;
    }

    formatter.writeText("removed-count", PDFToolTranslationContext::tr("External link annotations removed: %1.").arg(result.removed));
    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    if (!result.document)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to create modified document."), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult writeResult = writer.write(options.document, result.document.data(), true);
    if (!writeResult)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write document. %1").arg(writeResult.getErrorMessage()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolRemoveExternalLinks::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument;
}

}   // namespace pdftool
