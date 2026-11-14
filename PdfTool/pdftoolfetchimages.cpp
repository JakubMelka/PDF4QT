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

#include "pdftoolfetchimages.h"
#include "pdfdocumenttool_extractimages.h"
#include "pdfdocumenttool_pdftoimages.h"
#include "pdfdocumenttool_split.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace pdftool
{

static PDFToolFetchImages s_fetchImagesApplication;

QString PDFToolFetchImages::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "fetch-images";

        case Name:
            return PDFToolTranslationContext::tr("Fetch images");

        case Description:
            return PDFToolTranslationContext::tr("Fetch image content from document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolFetchImages::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    if (!document.getStorage().getSecurityHandler()->isAllowed(pdf::PDFSecurityHandler::Permission::CopyContent))
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Document doesn't allow to copy content."), options.outputCodec);
        return ErrorPermissions;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    QString errorMessage;
    Options optionFlags = getOptionsFlags();
    if (!options.imageExportSettings.validate(&errorMessage, false, optionFlags.testFlag(ImageExportSettingsFiles), optionFlags.testFlag(ImageExportSettingsResolution)))
    {
        PDFConsole::writeError(errorMessage, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // The tool Extract Images does the work (step 4 of phase 2, B60, B67): the images of the content
    // of all layers, inline images too, each picture once (by its reference, then by its raw stream),
    // never a glyph of a Type 3 font. The command line stays: "%" of the template is the number of
    // the image ("#"); the files of the folder are overwritten, as before.
    std::sort(pageIndices.begin(), pageIndices.end());
    pageIndices.erase(std::unique(pageIndices.begin(), pageIndices.end()), pageIndices.end());

    QVariantMap parameters = pdf::PDFDocumentToolImageFormat::fromWriterSettings(options.imageWriterSettings).toParameters();
    parameters.remove(QString::fromLatin1(pdf::PDFDocumentToolImageFormat::PARAMETER_SUBTYPE));     // Extract Images has no subtype (plan 7)
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolExtractImages::PARAMETER_PAGES), pdf::PDFDocumentToolPageSelection::createRangeText(pageIndices));
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER), options.imageExportSettings.getDirectory());
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), QString(options.imageExportSettings.getFileTemplate()).replace(QLatin1Char('%'), QLatin1Char('#')));
    parameters.insert(QString::fromLatin1(pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES), QString::fromLatin1(pdf::PDFDocumentToolFileOutput::EXISTING_OVERWRITE));

    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(options.cmsSettings);

    pdf::PDFDocumentToolFolderSink sink(options.imageExportSettings.getDirectory(), pdf::PDFDocumentToolFileSink::ExistingFiles::Overwrite);
    pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(document)), sourceData, options.document, false));
    context.cms = cmsManager.getCurrentCMS();
    context.fileSink = &sink;

    pdf::PDFDocumentToolExtractImages tool;
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    if (result.status == pdf::PDFDocumentToolResultStatus::Failed)
    {
        PDFConsole::writeError(result.message, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // A document without images gives an empty overview, as before (NothingToDo)

    // Write information about images
    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("images", PDFToolTranslationContext::tr("Images fetched from document %1").arg(options.document));
    formatter.endl();

    formatter.beginTable("overview", PDFToolTranslationContext::tr("Overview"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("item-no", PDFToolTranslationContext::tr("Image No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("page-no", PDFToolTranslationContext::tr("Page No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("width", PDFToolTranslationContext::tr("Width [pixels]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("height", PDFToolTranslationContext::tr("Height [pixels]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("size", PDFToolTranslationContext::tr("Size [bytes]"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("stored-to", PDFToolTranslationContext::tr("Stored to"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    QLocale locale;
    const QDir folder(options.imageExportSettings.getDirectory());
    for (const QJsonValue& value : result.data.value(QStringLiteral("images")).toArray())
    {
        const QJsonObject image = value.toObject();
        const QJsonArray pages = image.value(QStringLiteral("pages")).toArray();
        const qint64 number = image.value(QStringLiteral("number")).toInteger();

        formatter.beginTableRow("image", int(number - 1));
        formatter.writeTableColumn("item-no", locale.toString(number), Qt::AlignRight);
        formatter.writeTableColumn("page-no", pages.isEmpty() ? QString() : locale.toString(pages.first().toInteger()), Qt::AlignRight);
        formatter.writeTableColumn("width", locale.toString(image.value(QStringLiteral("width")).toInteger()), Qt::AlignRight);
        formatter.writeTableColumn("height", locale.toString(image.value(QStringLiteral("height")).toInteger()), Qt::AlignRight);
        formatter.writeTableColumn("size", locale.toString(image.value(QStringLiteral("streamBytes")).toInteger()), Qt::AlignRight);
        formatter.writeTableColumn("stored-to", QDir::toNativeSeparators(folder.filePath(image.value(QStringLiteral("fileName")).toString())));
        formatter.endTableRow();
    }

    formatter.endTable();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    // A file, which could not be written, is an error of the command
    for (const pdf::PDFDocumentToolFileProblem& problem : result.failedFiles)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot write image to file '%1', because: %2.").arg(QDir::toNativeSeparators(folder.filePath(problem.fileName)), problem.reason), options.outputCodec);
    }

    return result.failedFiles.empty() ? ExitSuccess : ErrorFailedWriteToFile;
}

PDFToolAbstractApplication::Options PDFToolFetchImages::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ImageWriterSettings | ImageExportSettingsFiles | ColorManagementSystem;
}

}   // namespace pdftool
