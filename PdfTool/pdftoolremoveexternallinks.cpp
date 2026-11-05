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

#include "pdftoolremoveexternallinks.h"
#include "pdfdocumentlinkoperations.h"
#include "pdfdocumentwriter.h"

namespace pdftool
{

static PDFToolRemoveExternalLinks s_removeExternalLinksApplication;

QString PDFToolRemoveExternalLinks::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "remove-external-links";

        case Name:
            return PDFToolTranslationContext::tr("Remove external links");

        case Description:
            return PDFToolTranslationContext::tr("Remove all external link annotations from a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolRemoveExternalLinks::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    const pdf::PDFDocumentLinkOperations::RemovalResult result = pdf::PDFDocumentLinkOperations::removeExternalLinkAnnotations(&document);

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("remove-external-links", PDFToolTranslationContext::tr("Remove External Links"));

    if (result.removed == 0)
    {
        formatter.writeText("result", PDFToolTranslationContext::tr("No external link annotations found."));
        formatter.endDocument();
        PDFConsole::writeText(formatter.getString(), options.outputCodec);
        return ExitSuccess;
    }

    formatter.writeText("removed-count", PDFToolTranslationContext::tr("External link annotations removed: %1.").arg(result.removed));
    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    if (!result.document)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to create modified document."), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult writeResult = writer.write(options.document, result.document.data(), true);
    if (!writeResult)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write document. %1").arg(writeResult.getErrorMessage()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolRemoveExternalLinks::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument;
}

}   // namespace pdftool
