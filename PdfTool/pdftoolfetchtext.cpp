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
            return PDFToolTranslationContext::tr("Fetch text content from a document.");

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
    if (!readDocument(options, document, &sourceData))
    {
        return ErrorDocumentReading;
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

    for (const pdf::PDFRenderError& error : factory.getErrors())
    {
        PDFConsole::writeError(error.message, options.outputCodec);
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolFetchTextApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | TextAnalysis;
}

}   // namespace pdftool
