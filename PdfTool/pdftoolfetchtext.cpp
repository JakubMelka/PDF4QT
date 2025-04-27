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

#include "pdftoolfetchtext.h"
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

    pdf::PDFDocumentTextFlowFactory factory;
    pdf::PDFDocumentTextFlow documentTextFlow = factory.create(&document, pages, options.textAnalysisAlgorithm);

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("text-extraction", QString());
    formatter.endl();

    for (const pdf::PDFDocumentTextFlow::Item& item : documentTextFlow.getItems())
    {
        if (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureItemStart))
        {
            formatter.beginHeader("item", item.text);
        }

        if (!item.text.isEmpty())
        {
            bool showText = (item.flags.testFlag(pdf::PDFDocumentTextFlow::Text)) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::PageStart) && options.textShowPageNumbers) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::PageEnd) && options.textShowPageNumbers) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureTitle) && options.textShowStructTitles) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureLanguage) && options.textShowStructLanguage) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureAlternativeDescription) && options.textShowStructAlternativeDescription) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureExpandedForm) && options.textShowStructExpandedForm) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureActualText) && options.textShowStructActualText) ||
                            (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructurePhoneme) && options.textShowStructPhoneme);

            if (showText)
            {
                formatter.writeText("text", item.text);
            }
        }

        if (item.flags.testFlag(pdf::PDFDocumentTextFlow::StructureItemEnd))
        {
            formatter.endHeader();
        }

        if (item.flags.testFlag(pdf::PDFDocumentTextFlow::PageEnd))
        {
            formatter.endl();
        }
    }

    formatter.endDocument();

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
