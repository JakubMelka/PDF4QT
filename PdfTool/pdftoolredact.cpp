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

#include "pdftoolredact.h"
#include "pdfdocumentwriter.h"
#include "pdfconstants.h"
#include "pdfredact.h"
#include "pdffont.h"

namespace pdftool
{

static PDFToolRedact s_redactApplication;

QString PDFToolRedact::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
    case Command:
        return "redact";

    case Name:
        return PDFToolTranslationContext::tr("Redact");

    case Description:
        return PDFToolTranslationContext::tr("Create a redacted document from the original document.");

    default:
        Q_ASSERT(false);
        break;
    }

    return QString();
}

int PDFToolRedact::execute(const PDFToolOptions& options)
{
    if (options.redactedDocument.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Redacted document file name is not set."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    // We are ready to redact the document
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Export, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(options.cmsSettings);
    pdf::PDFCMSPointer cms = cmsManager.getCurrentCMS();
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFModifiedDocument md(&document, &optionalContentActivity);
    fontCache.setDocument(md);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    pdf::PDFRedact redactor(&document, &fontCache, cms.get(), &optionalContentActivity, &meshQualitySettings, Qt::black);
    pdf::PDFDocument redactedDocument = redactor.perform(options.redactOptions);

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult result = writer.write(options.redactedDocument, &redactedDocument, true);
    if (!result)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write redacted document. %1").arg(result.getErrorMessage()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolRedact::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | ColorManagementSystem | Redact;
}

}   // namespace pdftool
