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

#include "pdftoolinfojavascript.h"
#include "pdfjavascriptscanner.h"

namespace pdftool
{

static PDFToolInfoJavaScriptApplication s_infoJavaScriptApplication;

QString PDFToolInfoJavaScriptApplication::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info-javascript";

        case Name:
            return PDFToolTranslationContext::tr("Info (JavaScript code)");

        case Description:
            return PDFToolTranslationContext::tr("Retrieve informations about JavaScript usage in a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInfoJavaScriptApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData))
    {
        return ErrorDocumentReading;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pages = options.getPageRange(document.getCatalog()->getPageCount(), parseError);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFJavaScriptScanner scanner(&document);
    pdf::PDFJavaScriptScanner::Entries javascripts = scanner.scan(pages, pdf::PDFJavaScriptScanner::ScanMask);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoJavaScriptApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector;
}

}   // namespace pdftool
