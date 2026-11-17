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

#include "pdfdocumenttool_report.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfdocumentxmlexport.h"
#include "pdfoperationcontrol.h"

#include <QFile>
#include <QFileInfo>
#include <QLocale>

#include "pdfdbgheap.h"

namespace pdf
{

/// The parts of the run of Document Report
class PDFDocumentToolReportHelper
{
public:
    PDFDocumentToolReportHelper() = delete;

    /// The pages of the parameter "pages" (indices from 0); an empty list and the reason of an invalid selection
    static std::vector<PDFInteger> getPageIndices(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
    {
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(parameters.value(QLatin1String(PDFDocumentToolReport::PARAMETER_PAGES)), errorMessage);
        if (!selection)
        {
            return std::vector<PDFInteger>();
        }
        return selection->getPageIndices(document, errorMessage);
    }

    /// The beginning of the written XML (the preview of 484:18196): at most PREVIEW_MAXIMUM_LENGTH
    /// characters, cut at the end of a line
    static QString readPreview(const QString& filePath)
    {
        QFile file(filePath);
        if (!file.open(QFile::ReadOnly))
        {
            return QString();
        }

        const bool isComplete = file.size() <= PDFDocumentToolReport::PREVIEW_MAXIMUM_LENGTH;
        QString text = QString::fromUtf8(file.read(PDFDocumentToolReport::PREVIEW_MAXIMUM_LENGTH));
        if (!isComplete)
        {
            const qsizetype lineEnd = text.lastIndexOf(QLatin1Char('\n'));
            text.truncate(lineEnd >= 0 ? lineEnd + 1 : 0);
        }
        return text;
    }
};

bool PDFDocumentToolReport::isSectionIncludedByDefault(PDFDocumentReportSection section)
{
    switch (section)
    {
        case PDFDocumentReportSection::FileHashes:
        case PDFDocumentReportSection::Inks:
        case PDFDocumentReportSection::Fonts:
            return false;

        case PDFDocumentReportSection::BasicInformation:
        case PDFDocumentReportSection::Metadata:
        case PDFDocumentReportSection::JavaScript:
        case PDFDocumentReportSection::NamedDestinations:
        case PDFDocumentReportSection::PageBoxes:
        case PDFDocumentReportSection::StructureTree:
            break;
    }
    return true;
}

std::vector<PDFDocumentReportSection> PDFDocumentToolReport::getSections(const QVariantMap& parameters)
{
    std::vector<PDFDocumentReportSection> sections;
    for (const PDFDocumentReportSection section : PDFDocumentReport::getSections())
    {
        if (parameters.value(PDFDocumentReport::getSectionId(section)).toBool())
        {
            sections.push_back(section);
        }
    }
    return sections;
}

QString PDFDocumentToolReport::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolReport::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Section = PDFDocumentReportSection;
    using Report = PDFDocumentReport;

    PDFDocumentToolParameterDefinitions definitions;
    std::vector<PDFDocumentToolParameterOption> kinds;
    kinds.push_back({ QString::fromLatin1(KIND_REPORT), QT_TR_NOOP("Report"), QT_TR_NOOP("A report of the chosen sections") });
    kinds.push_back({ QString::fromLatin1(KIND_STRUCTURE_XML), QT_TR_NOOP("Structure as XML"), QT_TR_NOOP("Every object of the file as XML") });
    definitions.push_back(Definition::createEnum(tr, QString::fromLatin1(PARAMETER_KIND), QT_TR_NOOP("Kind"), QT_TR_NOOP("What is written"), std::move(kinds), QString::fromLatin1(KIND_REPORT)));

    // The nine sections of 484:15472 in their order
    definitions.push_back(Definition::createBool(tr, Report::getSectionId(Section::BasicInformation), QT_TR_NOOP("Basic information"), QT_TR_NOOP("Title, author, dates, version, security"), isSectionIncludedByDefault(Section::BasicInformation)));
    definitions.push_back(Definition::createBool(tr, Report::getSectionId(Section::FileHashes), QT_TR_NOOP("File hashes"), QT_TR_NOOP("MD5, SHA1, SHA256, SHA384, SHA512"), isSectionIncludedByDefault(Section::FileHashes)));
    definitions.push_back(Definition::createBool(tr, Report::getSectionId(Section::Metadata), QT_TR_NOOP("Metadata (XMP)"), QT_TR_NOOP("The embedded XML metadata"), isSectionIncludedByDefault(Section::Metadata)));
    definitions.push_back(Definition::createBool(tr, Report::getSectionId(Section::JavaScript), QT_TR_NOOP("JavaScript"), QT_TR_NOOP("The scripts of the document, as a text"), isSectionIncludedByDefault(Section::JavaScript)));
    definitions.push_back(Definition::createBool(tr, Report::getSectionId(Section::NamedDestinations), QT_TR_NOOP("Named destinations"), QT_TR_NOOP("The named targets of links"), isSectionIncludedByDefault(Section::NamedDestinations)));
    definitions.push_back(Definition::createBool(tr, Report::getSectionId(Section::PageBoxes), QT_TR_NOOP("Page boxes"), QT_TR_NOOP("Media, crop, bleed, trim and art box"), isSectionIncludedByDefault(Section::PageBoxes)));
    definitions.push_back(Definition::createBool(tr, Report::getSectionId(Section::StructureTree), QT_TR_NOOP("Structure tree"), QT_TR_NOOP("Tags of the tagged document"), isSectionIncludedByDefault(Section::StructureTree)));
    definitions.push_back(Definition::createBool(tr, Report::getSectionId(Section::Inks), QT_TR_NOOP("Inks"), QT_TR_NOOP("Process and spot inks"), isSectionIncludedByDefault(Section::Inks)));
    definitions.push_back(Definition::createBool(tr, Report::getSectionId(Section::Fonts), QT_TR_NOOP("Fonts"), QT_TR_NOOP("Used fonts and how they are embedded"), isSectionIncludedByDefault(Section::Fonts)));

    definitions.push_back(Definition::createPageSelection(tr, QString::fromLatin1(PARAMETER_PAGES), QT_TR_NOOP("Pages"), QT_TR_NOOP("Pages of the JavaScript, the named destinations and the page boxes")));
    definitions.push_back(PDFDocumentToolTableWriter::createFormatParameter({ PDFDocumentToolTableFormat::None, PDFDocumentToolTableFormat::Text, PDFDocumentToolTableFormat::Html,
                                                                              PDFDocumentToolTableFormat::Xml }, PDFDocumentToolTableFormat::Html));
    definitions.push_back(PDFDocumentToolTableWriter::createDateFormatParameter());
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_CHARACTER_MAPS), QT_TR_NOOP("Character maps"), QT_TR_NOOP("The fonts list the characters they map"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_EXPORT_STREAMS), QT_TR_NOOP("Export streams"), QT_TR_NOOP("The data of the streams are written into the XML"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_STREAMS_AS_TEXT), QT_TR_NOOP("Streams as text"), QT_TR_NOOP("The data of the streams are written as a text"), false));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_INDENT), QT_TR_NOOP("Indent"), QT_TR_NOOP("The XML is indented"), true));
    definitions.push_back(Definition::createBool(tr, QString::fromLatin1(PARAMETER_ALWAYS_BINARY_STRINGS), QT_TR_NOOP("Strings always binary"), QT_TR_NOOP("Every string is written as hexadecimal"), false));
    return definitions;
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolReport::getRequiredPermissions(const QVariantMap& parameters) const
{
    // Q7.2: the JavaScript, the XMP and the data of the streams are content extracted from the document (bit 5)
    std::vector<PDFSecurityHandler::Permission> permissions = PDFDocumentTool::getRequiredPermissions(parameters);
    const bool isStructure = parameters.value(QLatin1String(PARAMETER_KIND)).toString() == QLatin1String(KIND_STRUCTURE_XML);
    bool isExtraction = false;
    if (isStructure)
    {
        // The data of the streams as hexadecimal digits or as their decoded text (each one alone writes
        // the content; review RS7CORE: the text without exportStreams needed no permission)
        isExtraction = parameters.value(QLatin1String(PARAMETER_EXPORT_STREAMS)).toBool() || parameters.value(QLatin1String(PARAMETER_STREAMS_AS_TEXT)).toBool();
    }
    else
    {
        isExtraction = parameters.value(PDFDocumentReport::getSectionId(PDFDocumentReportSection::JavaScript)).toBool() ||
                       parameters.value(PDFDocumentReport::getSectionId(PDFDocumentReportSection::Metadata)).toBool();
    }

    if (isExtraction)
    {
        permissions.push_back(PDFSecurityHandler::Permission::CopyContent);
    }
    return permissions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolReport::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    const PDFDocument* document = context.getDocument();
    if (!errors.empty() || !document)
    {
        return errors;
    }

    // 484:20937: a report of nothing is not made
    const bool isStructure = parameters.value(QLatin1String(PARAMETER_KIND)).toString() == QLatin1String(KIND_STRUCTURE_XML);
    if (!isStructure && getSections(parameters).empty())
    {
        errors.push_back({ QString::fromLatin1(PARAMETER_KIND), tr("Choose at least one section of the report.") });
    }

    // The pages of a document without pages are none (its report has the other sections)
    if (!isStructure && document->getCatalog()->getPageCount() > 0)
    {
        QString errorMessage;
        PDFDocumentToolReportHelper::getPageIndices(document, parameters, &errorMessage);
        if (!errorMessage.isEmpty())
        {
            errors.push_back({ QString::fromLatin1(PARAMETER_PAGES), errorMessage });
        }
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolReport::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolReportHelper;

    if (context.documents.empty() || !context.documents.front().document)
    {
        return PDFDocumentToolResult::createFailed(tr("No document is open."));
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    const QString baseName = getBaseName(input.fileName, parameters);

    // The facts are machine-readable (MCP, PdfTool): the C locale; the GUI makes its own by PDFAppLocale
    const QLocale locale = QLocale::c();

    PDFDocumentToolResult result;
    if (parameters.value(QLatin1String(PARAMETER_KIND)).toString() == QLatin1String(KIND_STRUCTURE_XML))
    {
        // Streamed into a temporary file in the job (a huge document is never in the memory whole)
        QString errorMessage;
        std::shared_ptr<PDFDocumentToolTemporaryFile> temporaryFile = PDFDocumentToolTemporaryFile::create(context.temporaryFolder, QStringLiteral(".xml"), &errorMessage);
        if (!temporaryFile)
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }

        QFile file(temporaryFile->getFilePath());
        if (!file.open(QFile::WriteOnly | QFile::Truncate))
        {
            return PDFDocumentToolResult::createFailed(tr("The XML cannot be written: %1").arg(file.errorString()));
        }

        PDFDocumentXmlExport::Options options;
        options.isStreamExported = parameters.value(QLatin1String(PARAMETER_EXPORT_STREAMS)).toBool();
        options.isStreamWrittenAsText = parameters.value(QLatin1String(PARAMETER_STREAMS_AS_TEXT)).toBool();
        options.isIndented = parameters.value(QLatin1String(PARAMETER_INDENT), true).toBool();
        options.isStringAlwaysBinary = parameters.value(QLatin1String(PARAMETER_ALWAYS_BINARY_STRINGS)).toBool();
        const bool isWritten = PDFDocumentXmlExport::write(document, options, &file, control, progress, &errorMessage);
        file.close();
        if (PDFOperationControl::isOperationCanceled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }
        if (!isWritten)
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }

        const qint64 size = temporaryFile->getSize();
        result.data.insert(QLatin1String(DATA_KIND), QString::fromLatin1(KIND_STRUCTURE_XML));
        result.data.insert(QLatin1String(DATA_TEXT), Helper::readPreview(temporaryFile->getFilePath()));
        result.data.insert(QLatin1String(DATA_SIZE), size);
        result.facts.push_back({ QStringLiteral("objects"), tr("Objects"), locale.toString(qint64(document->getStorage().getObjects().size())) });
        result.facts.push_back({ QStringLiteral("size"), tr("Size"), PDFDocumentToolFormat::formatSize(size, locale) });
        result.outputs.push_back(PDFDocumentToolOutputFile::createTemporary(baseName + QStringLiteral(".xml"), QStringLiteral("text/xml"), std::move(temporaryFile)));
        return result;
    }

    const std::vector<PDFDocumentReportSection> sections = getSections(parameters);
    if (sections.empty())
    {
        return PDFDocumentToolResult::createFailed(tr("Choose at least one section of the report."));
    }

    PDFDocumentReport::Options options;
    options.sections = sections;
    options.isCharacterMapIncluded = parameters.value(QLatin1String(PARAMETER_CHARACTER_MAPS)).toBool();
    options.fileName = QFileInfo(input.fileName).fileName();
    options.isRepaired = input.isRestored;

    // FD-2: the hashes only of the file, whose bytes are this very document
    options.originalData = input.isModified ? QByteArray() : input.originalData;
    if (document->getCatalog()->getPageCount() > 0)
    {
        QString errorMessage;
        options.pageIndices = Helper::getPageIndices(document, parameters, &errorMessage);
        if (!errorMessage.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }
    }

    const QJsonObject data = PDFDocumentReport::createData(document, options, control, progress);
    if (PDFOperationControl::isOperationCanceled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    result.data.insert(QLatin1String(DATA_KIND), QString::fromLatin1(KIND_REPORT));
    result.data.insert(QLatin1String(DATA_SECTIONS), data);
    result.facts.push_back({ QStringLiteral("sections"), tr("Sections"), locale.toString(qint64(sections.size())) });

    if (PDFDocumentToolTableWriter::getFormat(parameters) != PDFDocumentToolTableFormat::None)
    {
        QString errorMessage;
        PDFDocumentToolOutputFile output = writeOutput(result.data, parameters, PDFDocumentToolTableWriter::Options(), baseName, &errorMessage);
        if (output.suggestedFileName.isEmpty())
        {
            return PDFDocumentToolResult::createFailed(errorMessage);
        }
        result.facts.push_back({ QStringLiteral("size"), tr("Size"), PDFDocumentToolFormat::formatSize(output.data.size(), locale) });
        result.outputs.push_back(std::move(output));
    }
    return result;
}

PDFDocumentToolOutputFile PDFDocumentToolReport::writeOutput(const QJsonObject& data, const QVariantMap& parameters, const PDFDocumentToolTableWriter::Options& options, const QString& baseName, QString* errorMessage)
{
    if (data.value(QLatin1String(DATA_KIND)).toString() == QLatin1String(KIND_STRUCTURE_XML))
    {
        if (errorMessage)
        {
            *errorMessage = tr("The structure of the document is written by its run; it cannot be written from the data of a result.");
        }
        return PDFDocumentToolOutputFile();
    }

    const PDFDocumentToolTableFormat format = PDFDocumentToolTableWriter::getFormat(parameters);
    PDFDocumentToolTableWriter::Options writerOptions = options;
    if (parameters.contains(QLatin1String(PARAMETER_DATE_FORMAT)))
    {
        writerOptions.dateFormat = PDFDocumentToolTableWriter::getDateFormat(parameters.value(QLatin1String(PARAMETER_DATE_FORMAT)).toString());
    }

    // XML is data: the C locale and ISO 8601 also in the texts composed of values
    const bool isData = format == PDFDocumentToolTableFormat::Xml || format == PDFDocumentToolTableFormat::Csv;
    const PDFDocumentToolTableSection model = isData ? createModel(data, nullptr, PDFDocumentToolDateFormat::Iso) : createModel(data, writerOptions.formatter, writerOptions.dateFormat);
    return PDFDocumentToolTableWriter::createOutput(model, format, writerOptions, baseName, errorMessage);
}

PDFDocumentToolTableSection PDFDocumentToolReport::createModel(const QJsonObject& data, const PDFDocumentToolValueFormatter* formatter, PDFDocumentToolDateFormat dateFormat)
{
    return PDFDocumentReport::createModel(data.value(QLatin1String(DATA_SECTIONS)).toObject(), formatter, dateFormat);
}

QString PDFDocumentToolReport::getBaseName(const QString& fileName, const QVariantMap& parameters)
{
    QString baseName = QFileInfo(fileName).completeBaseName();
    if (baseName.isEmpty())
    {
        baseName = QStringLiteral("document");
    }

    const bool isStructure = parameters.value(QLatin1String(PARAMETER_KIND)).toString() == QLatin1String(KIND_STRUCTURE_XML);
    return isStructure ? tr("%1 - structure").arg(baseName) : tr("%1 - report").arg(baseName);
}

}   // namespace pdf
