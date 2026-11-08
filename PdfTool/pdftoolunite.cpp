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

#include "pdftoolunite.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentreader.h"
#include "pdfdocumentwriter.h"

#include <QFileInfo>

namespace pdftool
{

static PDFToolUnite s_toolUniteApplication;

QString PDFToolUnite::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "unite";

        case Name:
            return PDFToolTranslationContext::tr("Merge documents");

        case Description:
            return PDFToolTranslationContext::tr("Merge multiple documents to a single document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolUnite::execute(const PDFToolOptions& options)
{
    if (options.uniteFiles.size() < 3)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("At least two documents and target (merged) document must be specified."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    QStringList files = options.uniteFiles;
    QString targetFile = files.back();
    files.pop_back();

    if (QFileInfo::exists(targetFile))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Target file '%1' already exists. Document merging not performed.").arg(targetFile), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    // One implementation with the tool Merge of the new application (PDFDocumentMerger, plan of
    // step 2 of phase 2, 5.1): every page of every document, an outline item and a document
    // part per document (the default of Merge)
    std::vector<pdf::PDFDocumentMerger::Source> sources;
    std::vector<pdf::PDFDocumentMerger::Item> items;
    for (const QString& fileName : files)
    {
        // The password of --pswd (B15): one password, tried once for each encrypted document
        bool isFirstPasswordAttempt = true;
        auto passwordCallback = [&options, &isFirstPasswordAttempt](bool* ok) -> QString
        {
            *ok = isFirstPasswordAttempt && !options.password.isEmpty();
            isFirstPasswordAttempt = false;
            return options.password;
        };

        pdf::PDFDocumentReader reader(nullptr, passwordCallback, options.permissiveReading, false);
        pdf::PDFDocument document = reader.readFromFile(fileName);
        if (reader.getReadingResult() == pdf::PDFDocumentReader::Result::Cancelled)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Document '%1' is encrypted: give its password by --pswd.").arg(fileName), options.outputCodec);
            return ErrorDocumentReading;
        }
        if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot open document '%1'.").arg(fileName), options.outputCodec);
            return ErrorDocumentReading;
        }

        // ISO 32000-2, Table 22 (owner answer Q2): the merged document is not encrypted. A document
        // with an empty user password is opened by it without asking for --pswd (7.6.4.4), so the
        // password is verified as its owner password (most restricted files are such)
        const bool isOwnerPasswordVerified = !options.password.isEmpty() && pdf::PDFDocumentMerger::verifyOwnerPassword(document.getStorage().getSecurityHandler(), options.password);
        const pdf::PDFDocumentMerger::Permission permission = pdf::PDFDocumentMerger::getPermission(&document, isOwnerPasswordVerified);
        if (permission != pdf::PDFDocumentMerger::Permission::Permitted)
        {
            // The owner password is given by --pswd (the core says to unlock it by the owner password)
            PDFConsole::writeError(PDFToolTranslationContext::tr("%1 Give the owner password of '%2' by --pswd.").arg(pdf::PDFDocumentMerger::getPermissionText(permission, QFileInfo(fileName).fileName()), fileName), options.outputCodec);
            return ErrorPermissions;
        }

        const size_t sourceIndex = sources.size();
        const size_t pageCount = document.getCatalog()->getPageCount();
        sources.push_back(pdf::PDFDocumentMerger::Source::createDocument(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), QFileInfo(fileName).fileName()));
        sources.back().isOwnerPasswordVerified = isOwnerPasswordVerified;
        for (size_t i = 0; i < pageCount; ++i)
        {
            pdf::PDFDocumentMerger::Item item;
            item.source = sourceIndex;
            item.pageIndex = pdf::PDFInteger(i);
            items.push_back(item);
        }
    }

    pdf::PDFDocumentMerger::Settings settings;
    settings.outline = pdf::PDFDocumentMerger::OutlineMode::DocumentParts;
    settings.checkPermissions = true;
    pdf::PDFDocumentMerger::Result result = pdf::PDFDocumentMerger::merge(sources, items, settings, nullptr, nullptr);
    if (!result.isSuccess)
    {
        PDFConsole::writeError(result.errorMessage, options.outputCodec);
        return ErrorUnknown;
    }
    if (!result.leftOut.empty())
    {
        PDFConsole::writeError(result.leftOut.front().reason, options.outputCodec);
        return ErrorDocumentReading;
    }

    pdf::PDFDocumentWriter writer(nullptr);
    pdf::PDFOperationResult writeResult = writer.write(targetFile, &result.document, false);
    if (!writeResult)
    {
        PDFConsole::writeError(writeResult.getErrorMessage(), options.outputCodec);
        return ErrorFailedWriteToFile;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolUnite::getOptionsFlags() const
{
    return ConsoleFormat | Unite;
}

}   // namespace pdftool
