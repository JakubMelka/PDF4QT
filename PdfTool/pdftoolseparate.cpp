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

#include "pdftoolseparate.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumenttool_split.h"
#include "pdffilenametemplate.h"

#include <QDir>
#include <QFileInfo>

#include <limits>

namespace pdftool
{

static PDFToolSeparate s_toolSeparateApplication;

QString PDFToolSeparate::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "separate";

        case Name:
            return PDFToolTranslationContext::tr("Extract pages");

        case Description:
            return PDFToolTranslationContext::tr("Separate document into single page documents.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolSeparate::execute(const PDFToolOptions& options)
{
    // One implementation with the tools Split and Extract Pages of the new application (plan of
    // step 3 of phase 2, Q16): every page a new document of PDFDocumentSplitter (B37: no page of
    // another file reaches it through the fields, the destinations or the links; the page labels
    // follow the pages), the names by PDFFileNameTemplate ('%' of the pattern stays the page
    // number, it is the token '@'), the files by the folder sink
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    // B38, owner answer Q4: the files are new documents without encryption - the rule of Merge
    // (Assemble, and the owner password, when anything else is restricted; PdfTool unite)
    const bool isOwnerPasswordVerified = !options.password.isEmpty() && pdf::PDFDocumentMerger::verifyOwnerPassword(document.getStorage().getSecurityHandler(), options.password);
    const pdf::PDFDocumentMerger::Permission permission = pdf::PDFDocumentMerger::getPermission(&document, isOwnerPasswordVerified);
    if (permission != pdf::PDFDocumentMerger::Permission::Permitted)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("%1 Give the owner password of '%2' by --pswd.").arg(pdf::PDFDocumentMerger::getPermissionText(permission, QFileInfo(options.document).fileName()), options.document), options.outputCodec);
        return ErrorPermissions;
    }

    QString parseError;
    const pdf::PDFInteger pageCount = pdf::PDFInteger(document.getCatalog()->getPageCount());
    const std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(pageCount, parseError, true);
    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // B36: a page outside of the document is an error, never a read outside of the pages
    for (const pdf::PDFInteger pageIndex : pageIndices)
    {
        if (pageIndex < 0 || pageIndex >= pageCount)
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Page %1 does not exist, the document has %Ln pages.", nullptr, int(qMin<pdf::PDFInteger>(pageCount, std::numeric_limits<int>::max()))).arg(pageIndex + 1), options.outputCodec);
            return ErrorInvalidArguments;
        }
    }
    if (pageIndices.empty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("No page is selected."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (options.separatePagePattern.isEmpty())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("File template is empty."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    if (pageIndices.size() > 1 && !options.separatePagePattern.contains('%'))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("File template must contain character '%' for page number."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    // The pattern is a path: its folder, its file name the template ('%' -> '@'); an extension
    // ".pdf" of the pattern stays the extension of the files, another one is a part of the name
    const QFileInfo patternInfo(options.separatePagePattern);
    const QString folder = patternInfo.absolutePath();
    QString pattern = patternInfo.fileName();
    QString extension;
    if (pattern.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive) && pattern.size() > 4)
    {
        extension = pattern.right(4);
        pattern.chop(4);
    }
    pattern.replace(QLatin1Char('%'), QLatin1Char('@'));

    std::vector<pdf::PDFDocumentToolFileOutput::File> files;
    files.reserve(pageIndices.size());
    for (const pdf::PDFInteger pageIndex : pageIndices)
    {
        pdf::PDFDocumentToolFileOutput::File file;
        file.pages = { pageIndex };
        files.push_back(std::move(file));
    }

    QString nameError;
    if (!pdf::PDFDocumentToolFileOutput::createFileNames(&files, pdf::PDFFileNameTemplate(pattern, extension), QFileInfo(options.document).fileName(), QDate::currentDate(), &nameError))
    {
        PDFConsole::writeError(nameError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // An existing file is never overwritten (as before): it is reported and left out
    pdf::PDFDocumentToolFolderSink sink(folder, pdf::PDFDocumentToolFileSink::ExistingFiles::Skip);
    pdf::PDFDocumentToolInputDocument input = pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), sourceData, options.document, false);
    input.isOwnerPasswordVerified = isOwnerPasswordVerified;
    pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(std::move(input));
    context.fileSink = &sink;

    const pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolFileOutput::writeFiles(context, files, std::nullopt, nullptr, nullptr);
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (sink.getNotWrittenFiles().contains(files[i].fileName))
        {
            const QString filePath = QDir(folder).filePath(files[i].fileName);
            const QString reason = QFileInfo::exists(filePath) ? PDFToolTranslationContext::tr("File '%1' already exists.").arg(filePath)
                                                               : PDFToolTranslationContext::tr("File '%1' cannot be written.").arg(filePath);
            PDFConsole::writeError(PDFToolTranslationContext::tr("%1 Page %2 was not extracted.").arg(reason).arg(files[i].pages.front() + 1), options.outputCodec);
        }
    }

    if (result.status == pdf::PDFDocumentToolResultStatus::Failed)
    {
        PDFConsole::writeError(result.message, options.outputCodec);
        return ErrorUnknown;
    }

    // B38: a page, which was not extracted, is an error of the command
    return result.notWrittenFiles.isEmpty() ? ExitSuccess : ErrorFailedWriteToFile;
}

PDFToolAbstractApplication::Options PDFToolSeparate::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | Separate;
}


}   // namespace pdftool
