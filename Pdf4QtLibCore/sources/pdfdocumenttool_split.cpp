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

#include "pdfdocumenttool_split.h"

#include "pdfcatalog.h"
#include "pdfdocumentmerger.h"
#include "pdfdocumentsplitter.h"
#include "pdfdocumenttool_merge.h"
#include "pdfobjectutils.h"
#include "pdfoperationcontrol.h"
#include "pdfoutlinebreakpoints.h"
#include "pdfpagerangelist.h"
#include "pdfprogress.h"

#include <QFileInfo>
#include <QJsonArray>

#include <algorithm>
#include <set>

namespace pdf
{

/// Helpers of Split and Extract Pages
class PDFDocumentToolSplitHelper
{
public:
    PDFDocumentToolSplitHelper() = delete;

    static QString name(const char* parameter)
    {
        return QString::fromLatin1(parameter);
    }

    /// Numbers of the pages (from 1) for MCP
    static QJsonArray getPageNumbers(const std::vector<PDFInteger>& pages)
    {
        QJsonArray numbers;
        for (const PDFInteger page : pages)
        {
            numbers.append(qint64(page + 1));
        }
        return numbers;
    }

    /// The pages of the page selection of the parameter (all pages for an empty or a missing one)
    static std::vector<PDFInteger> getSelectedPages(const PDFDocument* document, const QVariantMap& parameters, const char* parameter, QString* errorMessage)
    {
        const QVariant value = parameters.value(name(parameter));
        const std::optional<PDFDocumentToolPageSelection> selection = value.isValid() ? PDFDocumentToolPageSelection::fromVariant(value, errorMessage)
                                                                                     : std::optional<PDFDocumentToolPageSelection>(PDFDocumentToolPageSelection());
        if (!selection)
        {
            return {};
        }
        return selection->getPageIndices(document, errorMessage);
    }

    /// Approximate bytes of an object in a file (its direct content; a stream its data)
    static qint64 getObjectSize(const PDFObject& object, int depth)
    {
        if (depth > 32)
        {
            return 8;
        }

        switch (object.getType())
        {
            case PDFObject::Type::String:
            case PDFObject::Type::Name:
                return 2 + qint64(object.getString().size());

            case PDFObject::Type::Array:
            {
                qint64 size = 2;
                for (const PDFObject& item : *object.getArray())
                {
                    size += 1 + getObjectSize(item, depth + 1);
                }
                return size;
            }

            case PDFObject::Type::Dictionary:
            case PDFObject::Type::Stream:
            {
                const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : object.getDictionary();
                qint64 size = 4;
                for (size_t i = 0; i < dictionary->getCount(); ++i)
                {
                    size += 2 + qint64(dictionary->getKey(i).getString().size()) + getObjectSize(dictionary->getValue(i), depth + 1);
                }
                if (object.isStream())
                {
                    size += 20 + qint64(object.getStream()->getContent()->size());
                }
                return size;
            }

            default:
                return 8;
        }
    }

    /// The bytes of the objects, which a page reaches and the file does not have yet (\p visited),
    /// the objects are added to \p added: never another page and never the page tree (/Parent)
    static qint64 getNewPageBytes(const PDFObjectStorage* storage, PDFObjectReference page, const std::set<PDFObjectReference>& pages,
                                  const std::set<PDFObjectReference>& visited, std::set<PDFObjectReference>* added)
    {
        qint64 bytes = 0;
        std::vector<PDFObjectReference> stack = { page };
        while (!stack.empty())
        {
            const PDFObjectReference reference = stack.back();
            stack.pop_back();
            if ((reference != page && pages.count(reference)) || visited.count(reference) || !added->insert(reference).second)
            {
                continue;
            }

            const PDFObject& object = storage->getObjectByReference(reference);
            bytes += 20 + getObjectSize(object, 0);
            const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : (object.isDictionary() ? object.getDictionary() : nullptr);
            if (dictionary)
            {
                for (size_t i = 0; i < dictionary->getCount(); ++i)
                {
                    if (dictionary->getKey(i).getString() == "Parent")
                    {
                        continue;
                    }
                    for (const PDFObjectReference& child : PDFObjectUtils::getDirectReferences(dictionary->getValue(i)))
                    {
                        stack.push_back(child);
                    }
                }
            }
            else
            {
                for (const PDFObjectReference& child : PDFObjectUtils::getDirectReferences(object))
                {
                    stack.push_back(child);
                }
            }
        }
        return bytes;
    }

    /// The file of the pages from \p first to \p last of \p pages (indices into it)
    static PDFDocumentToolFileOutput::File createFile(const std::vector<PDFInteger>& pages, size_t first, size_t last, const QString& title)
    {
        PDFDocumentToolFileOutput::File file;
        file.pages.assign(pages.cbegin() + qsizetype(first), pages.cbegin() + qsizetype(last) + 1);
        file.title = title;
        return file;
    }
};

// ------------------------------------------------------- PDFDocumentToolFileOutput

void PDFDocumentToolFileOutput::addDefinitions(PDFDocumentToolParameterDefinitions* definitions, PDFDocumentToolTranslator translator, const QString& defaultFileName)
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolSplitHelper;

    definitions->push_back(Definition::createFolderPath(translator, Helper::name(PARAMETER_FOLDER), QT_TR_NOOP("Folder"),
                                                        QT_TR_NOOP("Folder, into which the files are written; empty = the folder of the document")));
    definitions->push_back(Definition::createFileNameTemplate(translator, Helper::name(PARAMETER_FILE_NAME), QT_TR_NOOP("File name"),
                                                              QT_TR_NOOP("Name of the files: # is replaced by the file number (## pads with zeros), @ by the number of its first page, {source_base} by the name of the document, {group_name} by the bookmark, {date} by the date"),
                                                              defaultFileName));
    definitions->push_back(Definition::createEnum(translator, Helper::name(PARAMETER_EXISTING_FILES), QT_TR_NOOP("Existing files"), QT_TR_NOOP("What happens with a file, which exists already"),
                                                  {
                                                      { QString::fromLatin1(EXISTING_ASK), QT_TR_NOOP("Ask"), QT_TR_NOOP("Decide when the first conflict is found") },
                                                      { QString::fromLatin1(EXISTING_OVERWRITE), QT_TR_NOOP("Overwrite"), QT_TR_NOOP("Replace the existing files") },
                                                      { QString::fromLatin1(EXISTING_SKIP), QT_TR_NOOP("Skip"), QT_TR_NOOP("Keep the existing files, write the rest") },
                                                      { QString::fromLatin1(EXISTING_RENAME), QT_TR_NOOP("Rename"), QT_TR_NOOP("Add (2), (3) … to the new files") },
                                                  },
                                                  QString::fromLatin1(EXISTING_ASK)));
}

PDFDocumentToolFileSink::ExistingFiles PDFDocumentToolFileOutput::getPolicy(const QVariantMap& parameters)
{
    const QString value = parameters.value(PDFDocumentToolSplitHelper::name(PARAMETER_EXISTING_FILES)).toString();
    if (value == QLatin1String(EXISTING_OVERWRITE))
    {
        return PDFDocumentToolFileSink::ExistingFiles::Overwrite;
    }
    if (value == QLatin1String(EXISTING_SKIP))
    {
        return PDFDocumentToolFileSink::ExistingFiles::Skip;
    }
    if (value == QLatin1String(EXISTING_RENAME))
    {
        return PDFDocumentToolFileSink::ExistingFiles::Rename;
    }
    return PDFDocumentToolFileSink::ExistingFiles::Ask;
}

QString PDFDocumentToolFileOutput::getPolicyId(PDFDocumentToolFileSink::ExistingFiles policy)
{
    switch (policy)
    {
        case PDFDocumentToolFileSink::ExistingFiles::Ask:
            return QString::fromLatin1(EXISTING_ASK);
        case PDFDocumentToolFileSink::ExistingFiles::Overwrite:
            return QString::fromLatin1(EXISTING_OVERWRITE);
        case PDFDocumentToolFileSink::ExistingFiles::Skip:
            return QString::fromLatin1(EXISTING_SKIP);
        case PDFDocumentToolFileSink::ExistingFiles::Rename:
            return QString::fromLatin1(EXISTING_RENAME);
    }
    return QString::fromLatin1(EXISTING_ASK);
}

QString PDFDocumentToolFileOutput::formatFileNames(const QStringList& fileNames)
{
    switch (fileNames.size())
    {
        case 0:
            return QString();
        case 1:
            return fileNames.front();
        case 2:
            return fileNames.join(QStringLiteral(", "));
        default:
            return QStringLiteral("%1 … %2").arg(fileNames.front(), fileNames.back());
    }
}

bool PDFDocumentToolFileOutput::createFileNames(std::vector<File>* files, const QString& sourceFileName, const QVariantMap& parameters, const QDate& date, QString* errorMessage)
{
    const PDFFileNameTemplate nameTemplate(parameters.value(PDFDocumentToolSplitHelper::name(PARAMETER_FILE_NAME)).toString(), QStringLiteral(".pdf"));
    return createFileNames(files, nameTemplate, sourceFileName, date, errorMessage);
}

bool PDFDocumentToolFileOutput::createFileNames(std::vector<File>* files, const PDFFileNameTemplate& nameTemplate, const QString& sourceFileName, const QDate& date, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (files->size() > size_t(PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT))
    {
        error = tr("The pages would be split into more than %Ln files.", nullptr, PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT);
        return false;
    }

    if (!nameTemplate.isValid(&error))
    {
        return false;
    }

    const QString sourceName = sourceFileName.isEmpty() ? QStringLiteral("document.pdf") : sourceFileName;
    QStringList names;
    names.reserve(qsizetype(files->size()));
    for (size_t i = 0; i < files->size(); ++i)
    {
        File& file = (*files)[i];
        PDFFileNameTemplate::Values values;
        values.outputIndex = int(i + 1);
        values.outputCount = int(files->size());
        values.pageNumber = file.pages.empty() ? 0 : file.pages.front() + 1;
        values.sourceIndex = 1;
        values.groupIndex = int(i + 1);
        values.groupName = file.title;
        values.sourceFileName = sourceName;
        values.date = date;
        file.fileName = nameTemplate.createFileName(values);
        names << file.fileName;
    }

    const std::vector<int> duplicates = PDFFileNameTemplate::findDuplicates(names);
    if (!duplicates.empty())
    {
        if (int(duplicates.size()) + 1 == int(names.size()))
        {
            // 383:9088: every file has the one name
            error = tr("Every file would be named %1").arg(names.front());
        }
        else
        {
            error = tr("%Ln files would have the same name as another file", nullptr, int(duplicates.size()));
        }
        return false;
    }
    return true;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolFileOutput::checkPermissions(const PDFDocumentToolContext& context)
{
    std::vector<PDFDocumentToolParameterError> errors;
    if (context.documents.empty() || !context.documents.front().document)
    {
        return errors;
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocumentMerger::Permission permission = PDFDocumentMerger::getPermission(input.document.data(), input.isOwnerPasswordVerified);
    if (permission != PDFDocumentMerger::Permission::Permitted)
    {
        errors.push_back({ QString(), PDFDocumentMerger::getPermissionText(permission, input.fileName.isEmpty() ? tr("The document") : input.fileName) });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolFileOutput::writeFiles(const PDFDocumentToolContext& context, const std::vector<File>& files,
                                                            const std::optional<PDFImageOptimizer::Settings>& imageSettings,
                                                            const PDFOperationControl* control, PDFProgressSink* progress)
{
    PDFDocumentToolFileSink* sink = context.fileSink;
    if (!sink)
    {
        return PDFDocumentToolResult::createFailed(tr("The files have no place to be written."));
    }

    const PDFDocumentToolInputDocument& input = context.documents.front();
    QStringList fileNames;
    std::vector<PDFDocumentSplitter::Part> parts;
    parts.reserve(files.size());
    for (const File& file : files)
    {
        fileNames << file.fileName;
        parts.push_back({ file.pages });
    }

    QString error;
    if (!sink->begin(fileNames, &error))
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    // The receiver runs in this thread (PDFDocumentSplitter): the files are written one by
    // one, in their order - the order of the names and Stop
    qint64 sizeTotal = 0;
    qint64 imagesRecompressed = 0;
    bool isStructureRemoved = false;
    bool isAttachmentsRemoved = false;
    qint64 signaturesRemoved = 0;
    qint64 linksRemoved = 0;
    QStringList conformanceRemoved;
    auto receiver = [&](PDFDocumentSplitter::PartResult part) -> bool
    {
        isStructureRemoved = isStructureRemoved || part.statistics.structureTreeRemoved;
        isAttachmentsRemoved = isAttachmentsRemoved || part.attachmentsRemoved;
        signaturesRemoved += part.statistics.signaturesRemoved;
        linksRemoved += part.statistics.linksRemoved;
        imagesRecompressed += part.imagesRecompressed;
        for (const QString& standard : part.conformanceRemoved)
        {
            if (!conformanceRemoved.contains(standard))
            {
                conformanceRemoved << standard;
            }
        }

        QString writeError;
        if (sink->write(fileNames[qsizetype(part.partIndex)], part.data, &writeError))
        {
            sizeTotal += part.data.size();
        }
        return !sink->isStopped();
    };

    PDFDocumentSplitter::Settings settings;
    settings.imageSettings = imageSettings;
    const PDFDocumentSplitter::Result split = PDFDocumentSplitter::split(input.document.data(), parts, settings, control, progress, receiver);

    PDFDocumentToolResult result;
    result.writtenFiles = sink->getWrittenFiles();
    result.notWrittenFiles = sink->getNotWrittenFiles();

    // The files after a stop or a failure are not written either
    for (size_t i = split.receivedCount + ((split.isStopped) ? 1 : 0); i < files.size(); ++i)
    {
        if (!result.notWrittenFiles.contains(fileNames[qsizetype(i)]))
        {
            result.notWrittenFiles << fileNames[qsizetype(i)];
        }
    }

    if (split.isCanceled || split.isStopped || sink->isStopped())
    {
        result.status = PDFDocumentToolResultStatus::Stopped;
    }
    else if (!split.isSuccess)
    {
        result.status = PDFDocumentToolResultStatus::Failed;
        result.message = split.errorMessage;
    }

    QStringList writtenNames;
    for (const QString& path : result.writtenFiles)
    {
        writtenNames << QFileInfo(path).fileName();
    }

    result.facts.push_back({ QStringLiteral("files"), tr("Files"), tr("%Ln · %1", nullptr, int(result.writtenFiles.size())).arg(formatFileNames(writtenNames)) });
    result.facts.push_back({ QStringLiteral("size"), tr("Size"), tr("%1 in total").arg(PDFDocumentToolFormat::formatSize(sizeTotal)) });
    if (!result.notWrittenFiles.isEmpty())
    {
        result.facts.push_back({ QStringLiteral("notWritten"), tr("Not written"), tr("%Ln · %1", nullptr, int(result.notWrittenFiles.size())).arg(formatFileNames(result.notWrittenFiles)) });
    }
    if (isAttachmentsRemoved && !writtenNames.isEmpty())
    {
        // Owner answer Q6: the attachments of the document stay only in the first file
        result.facts.push_back({ QStringLiteral("attachments"), tr("Attachments"), tr("Kept in %1").arg(fileNames.front()) });
    }
    if (isStructureRemoved)
    {
        result.facts.push_back({ QStringLiteral("tags"), tr("Tags"), tr("Removed: the structure of the document is not split") });
    }
    if (signaturesRemoved > 0)
    {
        result.facts.push_back({ QStringLiteral("signatures"), tr("Signatures"), tr("%Ln removed with their pages", nullptr, int(signaturesRemoved)) });
    }
    if (linksRemoved > 0)
    {
        result.facts.push_back({ QStringLiteral("links"), tr("Links"), tr("%Ln links to other files removed", nullptr, int(linksRemoved)) });
    }
    if (imageSettings)
    {
        result.facts.push_back({ QStringLiteral("images"), tr("Images"), tr("%Ln recompressed", nullptr, int(imagesRecompressed)) });
    }
    PDFDocumentTool::addConformanceFact(&result, conformanceRemoved);

    result.data.insert(QStringLiteral("fileCount"), qint64(files.size()));
    result.data.insert(QStringLiteral("writtenCount"), qint64(result.writtenFiles.size()));
    result.data.insert(QStringLiteral("notWrittenCount"), qint64(result.notWrittenFiles.size()));
    result.data.insert(QStringLiteral("sizeTotal"), sizeTotal);
    result.data.insert(QStringLiteral("fileNames"), QJsonArray::fromStringList(fileNames));
    result.data.insert(QStringLiteral("imagesRecompressed"), imagesRecompressed);
    result.data.insert(QStringLiteral("signaturesRemoved"), signaturesRemoved);
    result.data.insert(QStringLiteral("linksRemoved"), linksRemoved);
    result.data.insert(QStringLiteral("structureTreeRemoved"), isStructureRemoved);
    result.data.insert(QStringLiteral("attachmentsRemoved"), isAttachmentsRemoved);
    result.data.insert(QStringLiteral("signatureCount"), 0);
    result.data.insert(QStringLiteral("signatureImpact"), QStringLiteral("none"));
    return result;
}

// ------------------------------------------------------- PDFDocumentToolSplit

std::vector<PDFDocumentToolFileOutput::File> PDFDocumentToolSplit::groupBySize(const PDFDocument* document, const std::vector<PDFInteger>& pages, qint64 limit)
{
    using Helper = PDFDocumentToolSplitHelper;

    std::vector<PDFDocumentToolFileOutput::File> files;
    if (!document || pages.empty())
    {
        return files;
    }

    const PDFCatalog* catalog = document->getCatalog();
    std::set<PDFObjectReference> pageObjects;
    for (size_t i = 0; i < catalog->getPageCount(); ++i)
    {
        pageObjects.insert(catalog->getPage(i)->getPageReference());
    }

    // Greedy: a page joins the file, unless the file would grow over the limit; the objects a
    // file has already (a font of more pages) are counted once, at its first page
    std::set<PDFObjectReference> visited;
    PDFDocumentToolFileOutput::File current;
    current.estimatedSize = 0;
    for (const PDFInteger pageIndex : pages)
    {
        const PDFObjectReference page = catalog->getPage(size_t(pageIndex))->getPageReference();
        std::set<PDFObjectReference> added;
        qint64 bytes = Helper::getNewPageBytes(&document->getStorage(), page, pageObjects, visited, &added);
        if (!current.pages.empty() && current.estimatedSize + bytes > limit)
        {
            files.push_back(std::move(current));
            current = PDFDocumentToolFileOutput::File();
            current.estimatedSize = 0;
            visited.clear();
            added.clear();
            bytes = Helper::getNewPageBytes(&document->getStorage(), page, pageObjects, visited, &added);
        }
        visited.merge(added);
        current.estimatedSize += bytes;
        current.pages.push_back(pageIndex);
    }
    files.push_back(std::move(current));
    return files;
}

std::optional<std::vector<PDFDocumentToolFileOutput::File>> PDFDocumentToolSplit::createFiles(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
{
    using Helper = PDFDocumentToolSplitHelper;
    using File = PDFDocumentToolFileOutput::File;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    if (!document)
    {
        error = tr("There is no document.");
        return std::nullopt;
    }

    const std::vector<PDFInteger> pages = Helper::getSelectedPages(document, parameters, PARAMETER_PAGES, &error);
    if (pages.empty())
    {
        if (error.isEmpty())
        {
            error = tr("No page is selected.");
        }
        return std::nullopt;
    }

    std::vector<File> files;
    const QString rule = parameters.value(Helper::name(PARAMETER_RULE)).toString();
    if (rule == QLatin1String(RULE_EVERY_PAGE))
    {
        for (size_t i = 0; i < pages.size(); ++i)
        {
            files.push_back(Helper::createFile(pages, i, i, QString()));
        }
    }
    else if (rule == QLatin1String(RULE_EVERY_N_PAGES))
    {
        const qint64 pagesPerFile = parameters.value(Helper::name(PARAMETER_PAGES_PER_FILE)).toLongLong();
        if (pagesPerFile < 1 || pagesPerFile > MAXIMUM_PAGES_PER_FILE)
        {
            error = tr("Enter a number of pages from 1 to %1.").arg(MAXIMUM_PAGES_PER_FILE);
            return std::nullopt;
        }
        for (size_t i = 0; i < pages.size(); i += size_t(pagesPerFile))
        {
            files.push_back(Helper::createFile(pages, i, qMin(pages.size(), i + size_t(pagesPerFile)) - 1, QString()));
        }
    }
    else if (rule == QLatin1String(RULE_AT_PAGES))
    {
        // "New file starts at pages" (383:5715): the page numbers of the document
        const PDFInteger pageCount = PDFInteger(document->getCatalog()->getPageCount());
        const std::optional<PDFPageRangeList> cuts = PDFPageRangeList::parse(parameters.value(Helper::name(PARAMETER_CUTS)).toString(), pageCount, &error);
        if (!cuts)
        {
            return std::nullopt;
        }

        std::vector<PDFInteger> cutPages = cuts->getPageIndices();
        std::sort(cutPages.begin(), cutPages.end());
        cutPages.erase(std::unique(cutPages.begin(), cutPages.end()), cutPages.end());

        // A file starts at the first page of the selection from the cut on; the first file
        // starts at the first page always
        std::vector<size_t> starts = { 0 };
        for (const PDFInteger cut : cutPages)
        {
            const size_t position = size_t(std::distance(pages.cbegin(), std::lower_bound(pages.cbegin(), pages.cend(), cut)));
            if (position > 0 && position < pages.size() && position != starts.back())
            {
                starts.push_back(position);
            }
        }
        if (starts.size() < 2)
        {
            error = tr("Enter the page numbers, where a new file starts.");
            return std::nullopt;
        }
        for (size_t i = 0; i < starts.size(); ++i)
        {
            files.push_back(Helper::createFile(pages, starts[i], i + 1 < starts.size() ? starts[i + 1] - 1 : pages.size() - 1, QString()));
        }
    }
    else if (rule == QLatin1String(RULE_AT_BOOKMARKS))
    {
        const std::vector<PDFOutlineBreakPoints::Bookmark> allBookmarks = PDFOutlineBreakPoints::readBookmarks(document);
        if (allBookmarks.empty())
        {
            error = tr("The document has no bookmarks.");
            return std::nullopt;
        }

        std::vector<PDFOutlineBreakPoints::Bookmark> chosen;
        const QString bookmarksText = parameters.value(Helper::name(PARAMETER_BOOKMARKS)).toString().trimmed();
        if (bookmarksText.isEmpty() || bookmarksText == QLatin1String(BOOKMARKS_TOP_LEVEL))
        {
            for (const PDFOutlineBreakPoints::Bookmark& bookmark : allBookmarks)
            {
                if (bookmark.level == 0)
                {
                    chosen.push_back(bookmark);
                }
            }
        }
        else
        {
            const std::optional<PDFPageRangeList> numbers = PDFPageRangeList::parse(bookmarksText, PDFInteger(allBookmarks.size()), &error);
            if (!numbers)
            {
                error = tr("The bookmarks are \"top-level\" or their numbers from 1 to %1.").arg(allBookmarks.size());
                return std::nullopt;
            }
            std::vector<PDFInteger> indices = numbers->getPageIndices();
            std::sort(indices.begin(), indices.end());
            indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
            for (const PDFInteger index : indices)
            {
                chosen.push_back(allBookmarks[size_t(index)]);
            }
        }

        const std::vector<PDFOutlineBreakPoints::Group> groups = PDFOutlineBreakPoints::createGroups(chosen, pages, nullptr);
        if (groups.empty())
        {
            error = tr("No chosen bookmark leads to a page of the selection.");
            return std::nullopt;
        }
        for (const PDFOutlineBreakPoints::Group& group : groups)
        {
            File file;
            for (const PDFInteger page : pages)
            {
                if (page >= group.firstPage && page <= group.lastPage)
                {
                    file.pages.push_back(page);
                }
            }
            file.title = group.title;
            files.push_back(std::move(file));
        }
    }
    else if (rule == QLatin1String(RULE_BY_SIZE))
    {
        const double sizeMB = parameters.value(Helper::name(PARAMETER_SIZE_MB)).toDouble();
        if (!(sizeMB >= 0.1 && sizeMB <= 2047.0))
        {
            error = tr("Enter a size from 0.1 to 2047 MB.");
            return std::nullopt;
        }
        files = groupBySize(document, pages, qint64(sizeMB * 1024.0 * 1024.0));
    }
    else if (rule == QLatin1String(RULE_ODD_EVEN))
    {
        File odd;
        File even;
        odd.title = tr("Odd pages");
        even.title = tr("Even pages");
        for (const PDFInteger page : pages)
        {
            ((page % 2 == 0) ? odd : even).pages.push_back(page);
        }
        for (File* file : { &odd, &even })
        {
            if (!file->pages.empty())
            {
                files.push_back(std::move(*file));
            }
        }
    }
    else
    {
        error = tr("Unknown rule of the split.");
        return std::nullopt;
    }

    if (files.size() > size_t(PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT))
    {
        error = tr("The pages would be split into more than %Ln files.", nullptr, PDFFileNameTemplate::MAXIMUM_OUTPUT_COUNT);
        return std::nullopt;
    }
    return files;
}

QString PDFDocumentToolSplit::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolSplit::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolSplitHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, Helper::name(PARAMETER_PAGES), QT_TR_NOOP("Pages to split"),
                                                          QT_TR_NOOP("The pages, which are split into files; empty = every page")).setStored(false));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_RULE), QT_TR_NOOP("Split"), QT_TR_NOOP("Where a new file starts"),
                                                 {
                                                     { QString::fromLatin1(RULE_EVERY_PAGE), QT_TR_NOOP("Every page"), QT_TR_NOOP("One file per page") },
                                                     { QString::fromLatin1(RULE_EVERY_N_PAGES), QT_TR_NOOP("Every N pages"), QT_TR_NOOP("Files with the same number of pages") },
                                                     { QString::fromLatin1(RULE_AT_PAGES), QT_TR_NOOP("At selected pages"), QT_TR_NOOP("Click between pages to place the cuts") },
                                                     { QString::fromLatin1(RULE_AT_BOOKMARKS), QT_TR_NOOP("At bookmarks"), QT_TR_NOOP("A new file starts at each chosen bookmark") },
                                                     { QString::fromLatin1(RULE_BY_SIZE), QT_TR_NOOP("By file size"), QT_TR_NOOP("Files of about the size you enter") },
                                                     { QString::fromLatin1(RULE_ODD_EVEN), QT_TR_NOOP("Odd and even pages"), QT_TR_NOOP("Two files – odd pages and even pages") },
                                                 },
                                                 QString::fromLatin1(RULE_EVERY_N_PAGES)));
    definitions.push_back(Definition::createInteger(tr, Helper::name(PARAMETER_PAGES_PER_FILE), QT_TR_NOOP("Pages per file"),
                                                    QT_TR_NOOP("Every N pages: the number of pages of each file"), 10, 1, MAXIMUM_PAGES_PER_FILE));
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_CUTS), QT_TR_NOOP("New file starts at pages"),
                                                   QT_TR_NOOP("At selected pages: the page numbers, where a new file starts, such as \"10, 20, 35\""),
                                                   QString(), PDFPageRangeList::MAXIMUM_TEXT_LENGTH).setStored(false));
    definitions.push_back(Definition::createString(tr, Helper::name(PARAMETER_BOOKMARKS), QT_TR_NOOP("Bookmarks"),
                                                   QT_TR_NOOP("At bookmarks: \"top-level\", or the numbers of the bookmarks from 1 in the order of the outline, such as \"1, 3-5\""),
                                                   QString::fromLatin1(BOOKMARKS_TOP_LEVEL), PDFPageRangeList::MAXIMUM_TEXT_LENGTH).setStored(false));
    definitions.push_back(Definition::createReal(tr, Helper::name(PARAMETER_SIZE_MB), QT_TR_NOOP("File size"),
                                                 QT_TR_NOOP("By file size: the approximate size of each file in MB"), 10.0, 0.1, 2047.0));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_OPTIMIZE_IMAGES), QT_TR_NOOP("Optimize"), QT_TR_NOOP("Images of the files"),
                                                 {
                                                     { QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_OFF), QT_TR_NOOP("Off"), QT_TR_NOOP("Images are stored exactly as they are") },
                                                     { QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_PREFER_QUALITY), QT_TR_NOOP("Prefer quality"), QT_TR_NOOP("Downsamples large images, keeps detail") },
                                                     { QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_MINIMUM_SIZE), QT_TR_NOOP("Minimum size"), QT_TR_NOOP("Smallest file, stronger compression") },
                                                 },
                                                 QString::fromLatin1(PDFDocumentToolMerge::OPTIMIZE_OFF)));
    PDFDocumentToolFileOutput::addDefinitions(&definitions, tr, QStringLiteral("{source_base}_#"));
    return definitions;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolSplit::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolSplitHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    // Q4: the files are new documents without encryption (the rule of Merge, Q2 of step 2)
    errors = PDFDocumentToolFileOutput::checkPermissions(context);
    if (!errors.empty())
    {
        return errors;
    }

    QString error;
    std::optional<std::vector<PDFDocumentToolFileOutput::File>> files = createFiles(context.getDocument(), parameters, &error);
    if (!files)
    {
        errors.push_back({ Helper::name(PARAMETER_RULE), error });
        return errors;
    }
    if (!PDFDocumentToolFileOutput::createFileNames(&*files, context.documents.front().fileName, parameters, QDate(), &error))
    {
        errors.push_back({ Helper::name(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), error });
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolSplit::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolSplitHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    QString error;
    std::optional<std::vector<PDFDocumentToolFileOutput::File>> files = createFiles(input.document.data(), parameters, &error);
    if (!files || !PDFDocumentToolFileOutput::createFileNames(&*files, input.fileName, parameters, QDate::currentDate(), &error))
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    PDFDocumentToolResult result = PDFDocumentToolFileOutput::writeFiles(context, *files, PDFDocumentToolMerge::createImageSettings(parameters), control, progress);
    result.data.insert(QStringLiteral("rule"), parameters.value(Helper::name(PARAMETER_RULE)).toString());
    qint64 pageCount = 0;
    for (const PDFDocumentToolFileOutput::File& file : *files)
    {
        pageCount += qint64(file.pages.size());
    }
    result.data.insert(QStringLiteral("pageCount"), pageCount);
    return result;
}

QVariantMap PDFDocumentToolSplit::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    using Helper = PDFDocumentToolSplitHelper;

    if (!from || !to || from == to)
    {
        return parameters;
    }

    QVariantMap mapped = parameters;
    const QString key = Helper::name(PARAMETER_PAGES);
    QString error;
    if (mapped.contains(key))
    {
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(mapped.value(key), &error);
        if (selection)
        {
            mapped.insert(key, selection->mapPages(from, PDFDocumentToolPageSelection::mapPageIndices(from, to)).toVariant());
        }
    }
    return mapped;
}

// ------------------------------------------------------- PDFDocumentToolExtract

std::vector<PDFInteger> PDFDocumentToolExtract::getPages(const PDFDocument* document, const QVariantMap& parameters, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    if (!document)
    {
        error = tr("There is no document.");
        return {};
    }

    const std::vector<PDFInteger> pages = PDFDocumentToolSplitHelper::getSelectedPages(document, parameters, PARAMETER_PAGES, &error);
    if (pages.empty() && error.isEmpty())
    {
        error = tr("No page is selected.");
    }
    return pages;
}

QString PDFDocumentToolExtract::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolExtract::getParameters() const
{
    using Definition = PDFDocumentToolParameterDefinition;
    using Helper = PDFDocumentToolSplitHelper;

    PDFDocumentToolParameterDefinitions definitions;
    definitions.push_back(Definition::createPageSelection(tr, Helper::name(PARAMETER_PAGES), QT_TR_NOOP("Pages to extract"),
                                                          QT_TR_NOOP("The pages, which are copied in the order of the document")).setRequired(true).setStored(false));
    definitions.push_back(Definition::createEnum(tr, Helper::name(PARAMETER_OUTPUT), QT_TR_NOOP("Output"), QT_TR_NOOP("What is made of the pages"),
                                                 {
                                                     { QString::fromLatin1(OUTPUT_ONE_FILE), QT_TR_NOOP("One file"), QT_TR_NOOP("All selected pages in one new document") },
                                                     { QString::fromLatin1(OUTPUT_FILE_PER_PAGE), QT_TR_NOOP("One file per page"), QT_TR_NOOP("Every selected page becomes its own file") },
                                                 },
                                                 QString::fromLatin1(OUTPUT_ONE_FILE)));
    PDFDocumentToolFileOutput::addDefinitions(&definitions, tr, QStringLiteral("{source_base}_p@"));
    return definitions;
}

PDFDocumentToolResultKind PDFDocumentToolExtract::getResultKind(const QVariantMap& parameters) const
{
    return parameters.value(PDFDocumentToolSplitHelper::name(PARAMETER_OUTPUT)).toString() == QLatin1String(OUTPUT_FILE_PER_PAGE) ? PDFDocumentToolResultKind::Files
                                                                                                                                   : PDFDocumentToolResultKind::NewDocument;
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolExtract::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    using Helper = PDFDocumentToolSplitHelper;

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentTool::validate(context, parameters);
    if (!errors.empty())
    {
        return errors;
    }

    errors = PDFDocumentToolFileOutput::checkPermissions(context);
    if (!errors.empty())
    {
        return errors;
    }

    QString error;
    const std::vector<PDFInteger> pages = getPages(context.getDocument(), parameters, &error);
    if (pages.empty())
    {
        errors.push_back({ Helper::name(PARAMETER_PAGES), error });
        return errors;
    }

    if (getResultKind(parameters) == PDFDocumentToolResultKind::Files)
    {
        std::vector<PDFDocumentToolFileOutput::File> files;
        for (const PDFInteger page : pages)
        {
            files.push_back({ { page }, QString(), -1, QString() });
        }
        if (!PDFDocumentToolFileOutput::createFileNames(&files, context.documents.front().fileName, parameters, QDate(), &error))
        {
            errors.push_back({ Helper::name(PDFDocumentToolFileOutput::PARAMETER_FILE_NAME), error });
        }
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentToolExtract::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    using Helper = PDFDocumentToolSplitHelper;

    const PDFDocumentToolInputDocument& input = context.documents.front();
    const PDFDocument* document = input.document.data();
    QString error;
    const std::vector<PDFInteger> pages = getPages(document, parameters, &error);
    if (pages.empty())
    {
        return PDFDocumentToolResult::createFailed(error);
    }

    if (getResultKind(parameters) == PDFDocumentToolResultKind::Files)
    {
        std::vector<PDFDocumentToolFileOutput::File> files;
        for (const PDFInteger page : pages)
        {
            files.push_back({ { page }, QString(), -1, QString() });
        }
        if (!PDFDocumentToolFileOutput::createFileNames(&files, input.fileName, parameters, QDate::currentDate(), &error))
        {
            return PDFDocumentToolResult::createFailed(error);
        }

        PDFDocumentToolResult result = PDFDocumentToolFileOutput::writeFiles(context, files, std::nullopt, control, progress);
        result.data.insert(QStringLiteral("output"), QString::fromLatin1(OUTPUT_FILE_PER_PAGE));
        result.data.insert(QStringLiteral("pageCount"), qint64(pages.size()));
        result.data.insert(QStringLiteral("pages"), Helper::getPageNumbers(pages));
        return result;
    }

    // One file: the merger of one source with the other pages left out (R3 and Q18 of step 2:
    // its catalog, the rules of removePages)
    PDFDocumentMerger::Source source = PDFDocumentMerger::Source::createDocument(input.document, input.fileName);
    source.isOwnerPasswordVerified = input.isOwnerPasswordVerified;
    std::vector<PDFDocumentMerger::Item> items;
    items.reserve(pages.size());
    for (const PDFInteger page : pages)
    {
        PDFDocumentMerger::Item item;
        item.source = 0;
        item.pageIndex = page;
        items.push_back(item);
    }

    PDFProgressPhases phases(progress);
    const int extractPhase = phases.addPhase(80, tr("Copying the pages"), true);
    const int measurePhase = phases.addPhase(20, tr("Measuring the result"), false);
    phases.start();
    phases.beginPhase(extractPhase);
    // A new file of the pages: the dates of its creation, as a part of Split (plan 5.6)
    PDFDocumentMerger::Settings mergerSettings;
    mergerSettings.isNewFile = true;
    PDFDocumentMerger::Result merged = PDFDocumentMerger::merge({ source }, items, mergerSettings, control, phases.getPhase(extractPhase));
    if (merged.isCanceled || PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }
    if (!merged.isSuccess)
    {
        return PDFDocumentToolResult::createFailed(merged.errorMessage);
    }

    phases.beginPhase(measurePhase);
    QByteArray writtenData = writeDocumentData(&merged.document, &error);
    if (writtenData.isEmpty())
    {
        return PDFDocumentToolResult::createFailed(error);
    }
    phases.finish();

    const QString sourceName = input.fileName.isEmpty() ? tr("The document") : input.fileName;
    PDFDocumentToolResult result;
    result.facts.push_back({ QStringLiteral("pages"), tr("Pages"), tr("%Ln · %1", nullptr, int(pages.size())).arg(PDFDocumentPageOperations::formatPageNumbers(pages)) });
    result.facts.push_back({ QStringLiteral("source"), tr("Source"), tr("%1 · not changed").arg(sourceName) });
    result.facts.push_back({ QStringLiteral("size"), tr("Size"), PDFDocumentToolFormat::formatSize(writtenData.size()) });
    if (merged.statistics.structureTreeRemoved)
    {
        result.facts.push_back({ QStringLiteral("tags"), tr("Tags"), tr("Removed: the structure of the document is not split") });
    }
    if (merged.statistics.signaturesRemoved > 0)
    {
        result.facts.push_back({ QStringLiteral("signatures"), tr("Signatures"), tr("%Ln signatures removed", nullptr, int(merged.statistics.signaturesRemoved)) });
    }
    addConformanceFact(&result, merged.statistics.conformanceRemoved);

    result.data.insert(QStringLiteral("output"), QString::fromLatin1(OUTPUT_ONE_FILE));
    result.data.insert(QStringLiteral("pageCount"), qint64(pages.size()));
    result.data.insert(QStringLiteral("pages"), Helper::getPageNumbers(pages));
    result.data.insert(QStringLiteral("sizeAfter"), qint64(writtenData.size()));
    result.data.insert(QStringLiteral("signaturesRemoved"), merged.statistics.signaturesRemoved);
    result.data.insert(QStringLiteral("structureTreeRemoved"), merged.statistics.structureTreeRemoved);
    result.data.insert(QStringLiteral("signatureCount"), 0);
    result.data.insert(QStringLiteral("signatureImpact"), QStringLiteral("none"));

    PDFDocumentToolOutputDocument output;
    output.kind = PDFDocumentToolOutputDocument::Kind::New;
    output.inputIndex = 0;
    output.document = PDFDocumentPointer(new PDFDocument(std::move(merged.document)));
    output.flags = PDFModifiedDocument::Reset;
    const QString baseName = QFileInfo(input.fileName).completeBaseName();
    output.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + QStringLiteral("_extracted.pdf");
    output.writtenData = std::move(writtenData);
    result.documents.push_back(std::move(output));
    return result;
}

QVariantMap PDFDocumentToolExtract::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    using Helper = PDFDocumentToolSplitHelper;

    if (!from || !to || from == to)
    {
        return parameters;
    }

    QVariantMap mapped = parameters;
    const QString key = Helper::name(PARAMETER_PAGES);
    QString error;
    if (mapped.contains(key))
    {
        const std::optional<PDFDocumentToolPageSelection> selection = PDFDocumentToolPageSelection::fromVariant(mapped.value(key), &error);
        if (selection)
        {
            mapped.insert(key, selection->mapPages(from, PDFDocumentToolPageSelection::mapPageIndices(from, to)).toVariant());
        }
    }
    return mapped;
}

}   // namespace pdf
