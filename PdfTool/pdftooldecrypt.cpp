//    Copyright (C) 2021 Jakub Melka
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

#include "pdftooldecrypt.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

namespace pdftool
{

static PDFToolDecryptApplication s_decryptApplication;

QString PDFToolDecryptApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "decrypt";

        case Name:
            return PDFToolTranslationContext::tr("Decrypt");

        case Description:
            return PDFToolTranslationContext::tr("Remove encryption from a document (with only owner access only).");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolDecryptApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, true))
    {
        if (readDocument(options, document, &sourceData, false))
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Authorization as owner failed. Encryption removal is not permitted if authorized as user only."), options.outputCodec);
        }
        return ErrorDocumentReading;
    }

    if (document.getStorage().getSecurityHandler()->getMode() == pdf::EncryptionMode::None)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Document is not encrypted."), options.outputCodec);
        return ExitSuccess;
    }

    pdf::PDFDocumentBuilder builder(&document);
    builder.removeEncryption();
    builder.setSecurityHandler(pdf::PDFSecurityHandlerPointer(new pdf::PDFNoneSecurityHandler()));
    document = builder.build();

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult result = writer.write(options.document, &document, true);

    if (!result)
    {
        PDFConsole::writeError(result.getErrorMessage(), options.outputCodec);
        return ErrorDocumentWriting;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolDecryptApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument;
}

}   // namespace pdftool
