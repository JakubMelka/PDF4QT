//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

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
    if (!readDocument(options, document, &sourceData))
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
        catch (pdf::PDFException e)
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
