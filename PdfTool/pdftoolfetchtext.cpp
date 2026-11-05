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

#include "pdftoolfetchtext.h"
#include "pdfdocumenttextexporter.h"
#include "pdfdocumenttextflow.h"

namespace pdftool
{

static PDFToolFetchTextApplication s_fetchTextApplication;

QString PDFToolFetchTextApplication::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "fetch-text";

        case Name:
            return PDFToolTranslationContext::tr("Fetch text");

        case Description:
            return PDFToolTranslationContext::tr("Fetch text content from document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolFetchTextApplication::execute(const PDFToolOptions& options)
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
    std::vector<pdf::PDFInteger> pages = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // The owner 2026-11-05: the titles of the structure are printed, the order of the content
    // stream gives also the text outside of a marked content
    pdf::PDFDocumentTextFlowFactory factory;
    factory.setStructureTitleText(true);
    factory.setUnmarkedText(true);
    pdf::PDFDocumentTextFlow documentTextFlow = factory.create(&document, pages, options.textAnalysisAlgorithm);

    pdf::PDFDocumentTextExporter::Options exportOptions;
    exportOptions.showPageNumbers = options.textShowPageNumbers;
    exportOptions.showStructureTitles = options.textShowStructTitles;
    exportOptions.showStructureLanguage = options.textShowStructLanguage;
    exportOptions.showStructureAlternativeDescription = options.textShowStructAlternativeDescription;
    exportOptions.showStructureExpandedForm = options.textShowStructExpandedForm;
    exportOptions.showStructureActualText = options.textShowStructActualText;
    exportOptions.showStructurePhoneme = options.textShowStructPhoneme;

    PDFOutputFormatter formatter(options.outputStyle);
    pdf::PDFDocumentTextExporter::write(documentTextFlow, exportOptions, formatter);

    for (const pdf::PDFRenderError& error : factory.getErrors())
    {
        PDFConsole::writeError(error.message, options.outputCodec);
    }

    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolFetchTextApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | TextAnalysis | TextShow;
}

}   // namespace pdftool
