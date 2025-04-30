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
