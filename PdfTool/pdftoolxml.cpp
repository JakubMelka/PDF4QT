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

#include "pdftoolxml.h"
#include "pdfdocumentxmlexport.h"

#include <QFile>

#include <cstdio>

namespace pdftool
{

static PDFToolXmlApplication s_xmlApplication;

QString PDFToolXmlApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "xml";

        case Name:
            return PDFToolTranslationContext::tr("XML export");

        case Description:
            return PDFToolTranslationContext::tr("Export internal data structure to xml.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolXmlApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    if (!readDocument(options, document, nullptr, false))
    {
        return ErrorDocumentReading;
    }

    // B166, Q7.2: the data of the streams (raw or as a text) are content extracted from the document
    // (ISO 32000-2, Table 22, bit 5) - the rule of review.report structure-xml; or --pswd is the owner password
    if ((options.xmlExportStreams || options.xmlExportStreamsAsText) && !isContentCopyAllowed(options, document))
    {
        return ErrorPermissions;
    }

    // The one implementation of the structure as XML (Document Report, R7-5): streamed into the
    // standard output in UTF-8, each stream decoded with the limit (Q7.18), every text well-formed
    pdf::PDFDocumentXmlExport::Options exportOptions;
    exportOptions.isStreamExported = options.xmlExportStreams;
    exportOptions.isStreamWrittenAsText = options.xmlExportStreamsAsText;
    exportOptions.isIndented = options.xmlUseIndent;
    exportOptions.isStringAlwaysBinary = options.xmlAlwaysBinaryStrings;

    std::fflush(stdout);
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("The standard output cannot be written."), options.outputCodec);
        return ErrorUnknown;
    }

    QString errorMessage;
    const bool isWritten = pdf::PDFDocumentXmlExport::write(&document, exportOptions, &output, nullptr, nullptr, &errorMessage);
    output.flush();
    if (!isWritten)
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorUnknown;
    }
    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolXmlApplication::getOptionsFlags() const
{
    return OpenDocument | XmlExport;
}

}   // namespace pdftool
