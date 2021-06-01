//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdftoolencrypt.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentwriter.h"

namespace pdftool
{

static PDFToolEncryptApplication s_encryptApplication;

QString PDFToolEncryptApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "encrypt";

        case Name:
            return PDFToolTranslationContext::tr("Encrypt");

        case Description:
            return PDFToolTranslationContext::tr("Encrypt the document (with only owner access only).");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolEncryptApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
    settings.algorithm = options.encryptionAlgorithm;
    settings.encryptContents = options.encryptionContents;
    settings.userPassword = options.encryptionUserPassword;
    settings.ownerPassword = options.encryptionOwnerPassword;
    settings.permissions = options.encryptionPermissions;

    QString errorMessage;
    if (!pdf::PDFSecurityHandlerFactory::validate(settings, &errorMessage))
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorEncryptionSettings;
    }

    pdf::PDFSecurityHandlerPointer securityHandler = pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings);

    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, true))
    {
        if (readDocument(options, document, &sourceData, false))
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Authorization as owner failed. Encryption change is not permitted if authorized as user only."), options.outputCodec);
        }
        return ErrorDocumentReading;
    }

    pdf::PDFDocumentBuilder builder(&document);
    builder.setSecurityHandler(qMove(securityHandler));
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

PDFToolAbstractApplication::Options PDFToolEncryptApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | Encrypt;
}

}   // namespace pdftool
