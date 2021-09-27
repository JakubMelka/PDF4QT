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

    PDFOutputFormatter formatter(options.outputStyle, options.outputCodec);
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
