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

#include "pdftoolinfometadata.h"
#include "pdfexception.h"

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

    pdf::PDFObject metadata = document.getObject(document.getCatalog()->getMetadata());
    if (metadata.isStream())
    {
        try
        {
            QByteArray rawData = document.getDecodedStream(metadata.getStream());
            PDFConsole::writeData(rawData);
        }
        catch (const pdf::PDFException &e)
        {
            PDFConsole::writeError(e.getMessage(), options.outputCodec);
        }
    }
    else
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Metadata not found in document."), options.outputCodec);
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoMetadataApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument;
}

}   // namespace pdftool
