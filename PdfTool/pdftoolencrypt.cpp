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

#include "pdftoolencrypt.h"
#include "pdfdocumenttool_encryption.h"
#include "pdfocrexport.h"

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

    // R5-23: the rules of the factory (an empty owner password is the open password, restrictions
    // need an owner password of their own, the characters of revision 4)
    QString errorMessage;
    if (!pdf::PDFSecurityHandlerFactory::validate(settings, &errorMessage))
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorEncryptionSettings;
    }

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

    // One implementation with the tools of the new application (plan of step 5 of phase 2, 4.7):
    // /ID, no old crypt filters, the attachments, the declaration of PDF/A, the check by reading
    // the result back; B101 - a failure is an error, never a file without the encryption
    pdf::PDFDocumentToolEncryption::Change change = pdf::PDFDocumentToolEncryption::changeEncryption(&document, std::move(settings), nullptr, nullptr);
    if (!change.error.isEmpty())
    {
        PDFConsole::writeError(change.error, options.outputCodec);
        return ErrorEncryptionSettings;
    }

    if (!change.conformanceRemoved.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("The declaration of %1 was removed: the standard does not allow the encryption.").arg(change.conformanceRemoved.join(QStringLiteral(", "))), options.outputCodec);
    }

    // The bytes, which were read back by the new password, are the file (another write would have
    // other random vectors of AES and would not be the verified file); written whole or not at all
    QString writeError;
    if (!pdf::PDFOCRStructuredExporter::writeFile(options.document, change.writtenData, &writeError))
    {
        PDFConsole::writeError(writeError, options.outputCodec);
        return ErrorDocumentWriting;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolEncryptApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | Encrypt;
}

}   // namespace pdftool
