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

#include "pdftoolseparate.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfoptimizer.h"
#include "pdfdocumentwriter.h"

#include <QFileInfo>

namespace pdftool
{

static PDFToolSeparate s_toolSeparateApplication;

QString PDFToolSeparate::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "separate";

        case Name:
            return PDFToolTranslationContext::tr("Extract pages");

        case Description:
            return PDFToolTranslationContext::tr("Separate document into single page documents.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolSeparate::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    if (!document.getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Document doesn't allow to copy content."), options.outputCodec);
        return ErrorPermissions;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.separatePagePattern.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("File template is empty."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (!options.separatePagePattern.contains("%"))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("File template must contain character '%' for page number."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    for (pdf::PDFInteger pageIndex : pageIndices)
    {
        try
        {
            pdf::PDFDocumentBuilder documentBuilder(&document);
            documentBuilder.flattenPageTree();
            std::vector<pdf::PDFObjectReference> pageReferences = documentBuilder.getPages();
            std::vector<pdf::PDFObjectReference> singlePageRef = { pageReferences[pageIndex] };
            documentBuilder.setPages(singlePageRef);
            documentBuilder.removeOutline();
            documentBuilder.removeThreads();
            documentBuilder.removeDocumentActions();
            documentBuilder.removeStructureTree();

            pdf::PDFDocument singlePageDocument = documentBuilder.build();

            // Optimize document - remove unused objects and shrink object storage
            pdf::PDFOptimizer optimizer(pdf::PDFOptimizer::RemoveUnusedObjects | pdf::PDFOptimizer::ShrinkObjectStorage, nullptr);
            optimizer.setDocument(&singlePageDocument);
            optimizer.optimize();
            singlePageDocument = optimizer.takeOptimizedDocument();

            QString fileName = options.separatePagePattern;
            fileName.replace('%', QString::number(pageIndex + 1));

            if (QFileInfo::exists(fileName))
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("File '%1' already exists. Page %2 was not extracted.").arg(fileName).arg(pageIndex + 1), options.outputCodec);
            }
            else
            {
                pdf::PDFDocumentWriter writer(nullptr);
                pdf::PDFOperationResult result = writer.write(fileName, &singlePageDocument, false);
                if (!result)
                {
                    PDFConsole::writeError(result.getErrorMessage(), options.outputCodec);
                }
            }
        }
        catch (const pdf::PDFException &exception)
        {
            PDFConsole::writeError(exception.getMessage(), options.outputCodec);
        }
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolSeparate::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | Separate;
}


}   // namespace pdftool
