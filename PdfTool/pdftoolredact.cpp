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

#include "pdftoolredact.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumenttool_redact.h"
#include "pdfredact.h"

#include <QColor>
#include <QSaveFile>

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

    // --pswd, --no-permissive-reading (R6-24)
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    // One implementation with Redact Content of the new application (plan of step 6 of phase 2,
    // 4.7): the marks of the document (its Redact annotations), the black fill, the parts kept by
    // the options, the layers by OCUsage::Export, the color management of the options --cms*
    // (R6-24). The copy is a new document without encryption: a restricted document needs its owner
    // password by --pswd (B139, R6-9 - the core tool checks it), and a finding of the check of the
    // copy means no file (owner answer Q6.9).
    using Tool = pdf::PDFDocumentToolRedact;
    const bool isOwnerPasswordVerified = !options.password.isEmpty() && pdf::PDFDocumentMerger::verifyOwnerPassword(document.getStorage().getSecurityHandler(), options.password);

    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_USE_MARKS), true);
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_FILL_COLOR), QColor(Qt::black));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_NO_FILL), false);
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_KEEP_TITLE), options.redactOptions.testFlag(pdf::PDFRedact::CopyTitle));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_KEEP_METADATA), options.redactOptions.testFlag(pdf::PDFRedact::CopyMetadata));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_KEEP_OUTLINE), options.redactOptions.testFlag(pdf::PDFRedact::CopyOutline));

    pdf::PDFDocumentToolInputDocument input = pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), sourceData, options.document, false);
    input.isOwnerPasswordVerified = isOwnerPasswordVerified;
    pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(std::move(input));
    context.rendering.cmsSettings = options.cmsSettings;

    const Tool tool;
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    if (result.status == pdf::PDFDocumentToolResultStatus::NothingToDo)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("The document has no redaction marks."), options.outputCodec);
        return ErrorInvalidArguments;
    }
    if (!result.isSuccess() || result.documents.empty() || result.documents.front().writtenData.isEmpty())
    {
        const pdf::PDFDocument* inputDocument = context.getDocument();
        const bool isRestricted = inputDocument && pdf::PDFDocumentMerger::getPermission(inputDocument, isOwnerPasswordVerified) != pdf::PDFDocumentMerger::Permission::Permitted;
        PDFConsole::writeError(PDFToolTranslationContext::tr("The redacted document has not been created. %1").arg(result.message), options.outputCodec);
        if (isRestricted)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Give the owner password of '%1' by --pswd.").arg(options.document), options.outputCodec);
        }
        return isRestricted ? ErrorPermissions : ErrorUnknown;
    }

    // The warnings of the check (owner answers Q6.10, Q6.19) and the problems of the pages
    for (const pdf::PDFDocumentToolStep& step : result.steps)
    {
        if (step.state == pdf::PDFDocumentToolStep::State::Warning)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: %1").arg(step.text), options.outputCodec);
        }
    }
    for (const pdf::PDFDocumentToolPageProblem& problem : result.renderingIssues)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: %1").arg(problem.message), options.outputCodec);
    }

    // The verified bytes are the file, never a new write of the document
    QSaveFile file(options.redactedDocument);
    const QByteArray& data = result.documents.front().writtenData;
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to write redacted document. %1").arg(file.errorString()), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolRedact::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | ColorManagementSystem | Redact;
}

}   // namespace pdftool
