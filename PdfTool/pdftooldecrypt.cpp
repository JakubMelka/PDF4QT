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
