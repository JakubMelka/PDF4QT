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

#include "pdftoolinfometadata.h"
#include "pdfdocumentreport.h"

namespace pdftool
{

static PDFToolInfoMetadataApplication s_infoMetadataApplication;

QString PDFToolInfoMetadataApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info-metadata";

        case Name:
            return PDFToolTranslationContext::tr("Extract document metadata");

        case Description:
            return PDFToolTranslationContext::tr("Extract document metadata (embedded xml stream).");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInfoMetadataApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    // B166, Q7.2: the XMP packet is content extracted from the document (ISO 32000-2, Table 22, bit 5), or
    // --pswd is the owner password
    if (!isContentCopyAllowed(options, document))
    {
        return ErrorPermissions;
    }

    // The one implementation of the report (R7-5): the packet decoded with the limit (Q7.18), UTF-8
    bool hasMetadata = false;
    QString errorMessage;
    const QString metadata = pdf::PDFDocumentReport::getMetadata(&document, &hasMetadata, &errorMessage);
    if (!hasMetadata)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Metadata not found in document."), options.outputCodec);
    }
    else if (!errorMessage.isEmpty())
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
    }
    else
    {
        PDFConsole::writeData(metadata.toUtf8());
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoMetadataApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument;
}

}   // namespace pdftool
