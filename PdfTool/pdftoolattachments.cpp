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

#include "pdftoolattachments.h"
#include "pdfexception.h"

#include <QFile>
#include <QMimeDatabase>

namespace pdftool
{

static PDFToolAttachmentsApplication s_attachmentsApplication;

QString PDFToolAttachmentsApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "attachments";

        case Name:
            return PDFToolTranslationContext::tr("Attachments");

        case Description:
            return PDFToolTranslationContext::tr("Show list or save attached files.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolAttachmentsApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    if (!readDocument(options, document, nullptr, false))
    {
        return ErrorDocumentReading;
    }

    struct FileInfo
    {
        QString no;
        QString fileName;
        QString mimeType;
        QString mimeTypeDescription;
        QString description;
        bool isSaved = false;
        int packedSize = 0;
        const pdf::PDFFileSpecification* specification = nullptr;
    };

    QMimeDatabase mimeDatabase;

    size_t savedFileCount = 0;
    size_t no = 1;
    std::vector<FileInfo> embeddedFiles;
    for (const auto& item : document.getCatalog()->getEmbeddedFiles())
    {
        const pdf::PDFFileSpecification* file = &item.second;
        const pdf::PDFEmbeddedFile* platformFile = file->getPlatformFile();
        if (!file->getPlatformFile() || !platformFile->isValid())
        {
            // Ignore invalid files
            continue;
        }

        FileInfo fileInfo;
        fileInfo.no = QString::number(no++);
        fileInfo.fileName = file->getPlatformFileName();
        fileInfo.description = file->getDescription();
        fileInfo.isSaved = false;
        fileInfo.specification = file;

        QMimeType type = mimeDatabase.mimeTypeForName(platformFile->getSubtype());
        if (!type.isValid())
        {
            type = mimeDatabase.mimeTypeForFile(fileInfo.fileName, QMimeDatabase::MatchExtension);
        }

        fileInfo.mimeType = type.name();
        fileInfo.mimeTypeDescription = type.comment();
        fileInfo.packedSize = platformFile->getStream()->getContent()->length();

        if (options.attachmentsSaveAll ||
            (!options.attachmentsSaveNumber.isEmpty() && fileInfo.no == options.attachmentsSaveNumber) ||
            (!options.attachmentsSaveFileName.isEmpty() && fileInfo.fileName == options.attachmentsSaveFileName))
        {
            fileInfo.isSaved = true;
            savedFileCount++;
        }

        embeddedFiles.push_back(qMove(fileInfo));
    }

    if (savedFileCount == 0)
    {
        // Just print a list of embedded files
        PDFOutputFormatter formatter(options.outputStyle);
        formatter.beginDocument("attachments", PDFToolTranslationContext::tr("Attached files of document %1").arg(options.document));
        formatter.endl();

        formatter.beginTable("overview", PDFToolTranslationContext::tr("Attached files overview"));

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("file-name", PDFToolTranslationContext::tr("File name"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("mime-type", PDFToolTranslationContext::tr("Mime type"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("mime-type-description", PDFToolTranslationContext::tr("Mime type description"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("description", PDFToolTranslationContext::tr("File description"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("packed-size", PDFToolTranslationContext::tr("Packed size [bytes]"), Qt::AlignRight);
        formatter.endTableHeaderRow();

        int ref = 1;
        for (const FileInfo& info : embeddedFiles)
        {
            formatter.beginTableRow("file", ref);

            formatter.writeTableColumn("no", QString::number(ref));
            formatter.writeTableColumn("file-name", info.fileName);
            formatter.writeTableColumn("mime-type", info.mimeType);
            formatter.writeTableColumn("mime-type-description", info.mimeTypeDescription);
            formatter.writeTableColumn("description", info.description);
            formatter.writeTableColumn("packed-size", QString::number(info.packedSize));

            formatter.endTableRow();
            ++ref;
        }

        formatter.endTable();

        formatter.endDocument();
        PDFConsole::writeText(formatter.getString(), options.outputCodec);
    }
    else
    {
        if (savedFileCount > 1 && !options.attachmentsTargetFile.isEmpty())
        {
            PDFConsole::writeError(PDFToolTranslationContext::tr("Target file name must not be specified, if multiple files are being saved."), options.outputCodec);
            return ErrorInvalidArguments;
        }

        for (const FileInfo& info : embeddedFiles)
        {
            if (!info.isSaved)
            {
                // This file is not marked to be saved
                continue;
            }

            QString outputFile = info.fileName;
            if (!options.attachmentsTargetFile.isEmpty())
            {
                outputFile = options.attachmentsTargetFile;
            }

            if (!options.attachmentsOutputDirectory.isEmpty())
            {
                outputFile = QString("%1/%2").arg(options.attachmentsOutputDirectory, outputFile);
            }

            try
            {
                QByteArray data = document.getDecodedStream(info.specification->getPlatformFile()->getStream());

                QFile file(outputFile);
                if (file.open(QFile::WriteOnly | QFile::Truncate))
                {
                    file.write(data);
                    file.close();
                }
                else
                {
                    PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to save attachment to file. %1").arg(file.errorString()), options.outputCodec);
                    return ErrorFailedWriteToFile;
                }
            }
            catch (const pdf::PDFException &e)
            {
                PDFConsole::writeError(PDFToolTranslationContext::tr("Failed to save attachment to file. %1").arg(e.getMessage()), options.outputCodec);
                return ErrorFailedWriteToFile;
            }
        }
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolAttachmentsApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | Attachments;
}

}   // namespace pdftool
