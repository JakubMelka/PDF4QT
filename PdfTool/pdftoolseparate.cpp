//    Copyright (C) 2020-2021 Jakub Melka
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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolseparate.h"
#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfoptimizer.h"
#include "pdfdocumentwriter.h"

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
        catch (pdf::PDFException exception)
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
