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

#include "pdfdocumenttool.h"
#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumentwriter.h"
#include "pdfexception.h"
#include "pdfform.h"
#include "pdfobjectutils.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfoperationcontrol.h"

#include "pdffilenametemplate.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
#include <QSaveFile>

#include <exception>

namespace pdf
{

/// Helpers of the tools. The texts are translated in the context
/// pdf::PDFDocumentTool - lupdate takes the context of the macro.
class PDFDocumentToolHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentTool)

public:
    /// A run, which is a use of the tool, ends with a full bar (the tool may not have
    /// reached it: a tool without steps, a phase left early); returns \p result
    static const PDFDocumentToolResult& finishProgress(const PDFDocumentToolResult& result, PDFProgressSink* progress)
    {
        if (progress && result.isUse())
        {
            progress->finishPhases();
        }
        return result;
    }

    /// Name of a signature impact in JSON
    static QString getSignatureImpactName(PDFDocumentToolSignatureImpact impact)
    {
        switch (impact)
        {
            case PDFDocumentToolSignatureImpact::None:
                return QStringLiteral("none");
            case PDFDocumentToolSignatureImpact::IncrementalSave:
                return QStringLiteral("incremental-save");
            case PDFDocumentToolSignatureImpact::LostCopyOnly:
                return QStringLiteral("lost-copy-only");
        }
        return QString();
    }

    PDFDocumentToolHelper() = delete;

    /// Failed result of the errors of a validation
    static PDFDocumentToolResult createInvalid(const std::vector<PDFDocumentToolParameterError>& errors)
    {
        QStringList messages;
        QJsonArray array;
        for (const PDFDocumentToolParameterError& error : errors)
        {
            messages << error.message;

            QJsonObject item;
            item.insert(QStringLiteral("parameter"), error.parameter);
            item.insert(QStringLiteral("message"), error.message);
            array.append(item);
        }

        PDFDocumentToolResult result = PDFDocumentToolResult::createFailed(messages.join(QLatin1Char('\n')));
        result.data.insert(QStringLiteral("errors"), array);
        return result;
    }

    /// Names of the modification flags in JSON
    static QJsonArray getFlagNames(PDFModifiedDocument::ModificationFlags flags)
    {
        const std::pair<PDFModifiedDocument::ModificationFlag, const char*> names[] =
        {
            { PDFModifiedDocument::Reset, "reset" },
            { PDFModifiedDocument::PageContents, "pageContents" },
            { PDFModifiedDocument::Annotation, "annotation" },
            { PDFModifiedDocument::FormField, "formField" },
            { PDFModifiedDocument::Authorization, "authorization" },
            { PDFModifiedDocument::XFA_Pagination, "xfaPagination" },
            { PDFModifiedDocument::PreserveUndoRedo, "preserveUndoRedo" },
            { PDFModifiedDocument::PreserveView, "preserveView" },
            { PDFModifiedDocument::Outline, "outline" },
        };

        QJsonArray array;
        for (const auto& item : names)
        {
            if (flags.testFlag(item.first))
            {
                array.append(QString::fromLatin1(item.second));
            }
        }
        return array;
    }

    static QString getStepStateName(PDFDocumentToolStep::State state)
    {
        switch (state)
        {
            case PDFDocumentToolStep::State::Done:
                return QStringLiteral("done");
            case PDFDocumentToolStep::State::Warning:
                return QStringLiteral("warning");
            case PDFDocumentToolStep::State::Failed:
                return QStringLiteral("failed");
            case PDFDocumentToolStep::State::Skipped:
                return QStringLiteral("skipped");
        }
        return QString();
    }

    /// The result of a tool is consistent with its context: every document is
    /// set, a changed document refers to an input document
    static bool isConsistent(const PDFDocumentToolResult& result, const PDFDocumentToolContext& context)
    {
        for (const PDFDocumentToolOutputDocument& document : result.documents)
        {
            if (!document.document)
            {
                return false;
            }
            if (document.kind == PDFDocumentToolOutputDocument::Kind::Changed &&
                (document.inputIndex < 0 || size_t(document.inputIndex) >= context.documents.size()))
            {
                return false;
            }
        }
        return true;
    }
};

PDFDocumentToolInputDocument PDFDocumentToolInputDocument::create(PDFDocumentPointer document, QByteArray originalData, const QString& filePath, bool isModified)
{
    PDFDocumentToolInputDocument input;
    input.document = std::move(document);
    input.originalData = std::move(originalData);
    input.fileName = QFileInfo(filePath).fileName();
    input.isModified = isModified;
    return input;
}

PDFDocumentToolFileSink::PDFDocumentToolFileSink(ExistingFiles policy) :
    m_policy(policy)
{

}

PDFDocumentToolFileSink::~PDFDocumentToolFileSink() = default;

bool PDFDocumentToolFileSink::begin(const QStringList& fileNames, QString* errorText)
{
    Q_UNUSED(fileNames);
    if (errorText)
    {
        errorText->clear();
    }
    return true;
}

bool PDFDocumentToolFileSink::isStopped() const
{
    return false;
}

void PDFDocumentToolFileSink::addWrittenFile(const QString& filePath)
{
    m_writtenFiles << filePath;
}

void PDFDocumentToolFileSink::addNotWrittenFile(const QString& fileName)
{
    m_notWrittenFiles << fileName;
}

PDFDocumentToolFolderSink::PDFDocumentToolFolderSink(QString folder, ExistingFiles policy) :
    PDFDocumentToolFileSink(policy),
    m_folder(folder.isEmpty() ? QString() : QDir::cleanPath(QDir(folder).absolutePath()))
{

}

PDFDocumentToolFolderSink::~PDFDocumentToolFolderSink() = default;

QString PDFDocumentToolFolderSink::getKey(const QString& fileName)
{
    return fileName.toCaseFolded();
}

bool PDFDocumentToolFolderSink::begin(const QStringList& fileNames, QString* errorText)
{
    QString dummy;
    QString& error = errorText ? *errorText : dummy;
    error.clear();

    const QFileInfo folderInfo(m_folder);
    if (m_folder.isEmpty() || !folderInfo.isDir())
    {
        error = tr("The folder does not exist.");
        return false;
    }

    m_conflictFiles.clear();
    m_reservedNames.clear();
    const QDir folder(m_folder);
    for (const QString& fileName : fileNames)
    {
        if (!checkFileName(fileName, &error))
        {
            return false;
        }
        const QString key = getKey(fileName);
        if (m_reservedNames.contains(key))
        {
            error = tr("More files would be named %1.").arg(fileName);
            return false;
        }
        m_reservedNames.insert(key);
        if (QFileInfo::exists(folder.filePath(fileName)))
        {
            m_conflictFiles << fileName;
        }
    }
    return true;
}

bool PDFDocumentToolFolderSink::checkFileName(const QString& fileName, QString* errorText) const
{
    if (fileName.isEmpty() || PDFFileNameTemplate::sanitize(fileName) != fileName)
    {
        *errorText = tr("\"%1\" cannot be a name of a file.").arg(fileName);
        return false;
    }
    return true;
}

PDFDocumentToolFileSink::ExistingFiles PDFDocumentToolFolderSink::resolveConflict(const QString& fileName, bool isProtected)
{
    Q_UNUSED(fileName);
    Q_UNUSED(isProtected);
    return getPolicy() == ExistingFiles::Ask ? ExistingFiles::Skip : getPolicy();
}

bool PDFDocumentToolFolderSink::isProtectedPath(const QString& filePath) const
{
    Q_UNUSED(filePath);
    return false;
}

QString PDFDocumentToolFolderSink::getRenamedFileName(const QString& fileName) const
{
    // "Report.pdf" -> "Report (2).pdf" (383:4447); the extension is the last suffix
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    const QString base = dot > 0 ? fileName.left(dot) : fileName;
    const QString extension = dot > 0 ? fileName.mid(dot) : QString();
    const QDir folder(m_folder);
    for (int number = 2; number < 100000; ++number)
    {
        const QString suffix = QStringLiteral(" (%1)").arg(number);
        QString candidate = base;
        const int maximumBase = PDFFileNameTemplate::MAXIMUM_FILE_NAME_LENGTH - int(suffix.size() + extension.size());
        if (candidate.size() > maximumBase)
        {
            candidate = PDFFileNameTemplate::sanitize(candidate.left(qMax(1, maximumBase)));
        }
        candidate += suffix + extension;
        if (!m_reservedNames.contains(getKey(candidate)) && !QFileInfo::exists(folder.filePath(candidate)) && !isProtectedPath(folder.filePath(candidate)))
        {
            return candidate;
        }
    }
    return QString();
}

bool PDFDocumentToolFolderSink::write(const QString& fileName, const QByteArray& data, QString* errorText)
{
    QString dummy;
    QString& error = errorText ? *errorText : dummy;
    error.clear();

    if (isStopped())
    {
        error = tr("The export has been stopped.");
        addNotWrittenFile(fileName);
        return false;
    }
    if (!checkFileName(fileName, &error))
    {
        m_lastProblem = error;
        addNotWrittenFile(fileName);
        return false;
    }

    const QDir folder(m_folder);
    QString targetName = fileName;
    QString targetPath = folder.filePath(targetName);
    const bool isProtected = isProtectedPath(targetPath);
    const bool isExisting = QFileInfo::exists(targetPath);
    if (isExisting || isProtected)
    {
        const ExistingFiles decision = resolveConflict(fileName, isProtected);
        if (isStopped())
        {
            error = tr("The export has been stopped.");
            addNotWrittenFile(fileName);
            return false;
        }

        switch (decision)
        {
            case ExistingFiles::Overwrite:
                if (isProtected)
                {
                    // R14: a source or a file open in PDF4QT is never replaced
                    error = tr("%1 is open in PDF4QT or is the source of the files - it is not overwritten.").arg(fileName);
                    m_lastProblem = error;
                    addNotWrittenFile(fileName);
                    return false;
                }
                break;

            case ExistingFiles::Rename:
                targetName = getRenamedFileName(fileName);
                if (targetName.isEmpty())
                {
                    error = tr("%1 exists and no free name was found.").arg(fileName);
                    m_lastProblem = error;
                    addNotWrittenFile(fileName);
                    return false;
                }
                targetPath = folder.filePath(targetName);
                break;

            case ExistingFiles::Ask:
            case ExistingFiles::Skip:
                error = tr("%1 exists - it was skipped.").arg(fileName);
                m_lastProblem = error;
                addNotWrittenFile(fileName);
                return false;
        }
    }

    // Whole or not at all: the file appears only when every byte is written
    const bool isOverwritten = QFileInfo::exists(targetPath);
    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        error = tr("%1 cannot be written: %2").arg(targetName, file.errorString());
        m_lastProblem = error;
        addNotWrittenFile(fileName);
        return false;
    }

    m_reservedNames.insert(getKey(targetName));
    const QString writtenPath = QDir::cleanPath(QFileInfo(targetPath).absoluteFilePath());
    if (isOverwritten)
    {
        m_overwrittenFiles << writtenPath;
    }
    addWrittenFile(writtenPath);
    return true;
}

PDFDocumentToolContext PDFDocumentToolContext::create(PDFDocumentToolInputDocument input)
{
    PDFDocumentToolContext context;
    context.documents.push_back(std::move(input));
    return context;
}

const PDFDocument* PDFDocumentToolContext::getDocument() const
{
    return !documents.empty() ? documents.front().document.data() : nullptr;
}

PDFDocumentToolResult PDFDocumentToolResult::createFailed(const QString& message)
{
    PDFDocumentToolResult result;
    result.status = PDFDocumentToolResultStatus::Failed;
    result.message = message;
    return result;
}

PDFDocumentToolResult PDFDocumentToolResult::createStopped()
{
    PDFDocumentToolResult result;
    result.status = PDFDocumentToolResultStatus::Stopped;
    result.message = PDFDocumentToolHelper::tr("The tool was stopped.");
    return result;
}

PDFDocumentToolResult PDFDocumentToolResult::createNothingToDo(const QString& message)
{
    PDFDocumentToolResult result;
    result.status = PDFDocumentToolResultStatus::NothingToDo;
    result.message = message;
    return result;
}

QString PDFDocumentToolResult::getStatusName(PDFDocumentToolResultStatus status)
{
    switch (status)
    {
        case PDFDocumentToolResultStatus::Success:
            return QStringLiteral("success");
        case PDFDocumentToolResultStatus::Stopped:
            return QStringLiteral("stopped");
        case PDFDocumentToolResultStatus::Failed:
            return QStringLiteral("failed");
        case PDFDocumentToolResultStatus::NothingToDo:
            return QStringLiteral("nothing-to-do");
    }
    return QString();
}

QJsonObject PDFDocumentToolResult::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("status"), getStatusName(status));
    if (!message.isEmpty())
    {
        object.insert(QStringLiteral("message"), message);
    }

    QJsonArray documentArray;
    for (const PDFDocumentToolOutputDocument& document : documents)
    {
        QJsonObject item;
        const bool isChanged = document.kind == PDFDocumentToolOutputDocument::Kind::Changed;
        item.insert(QStringLiteral("kind"), isChanged ? QStringLiteral("changed") : QStringLiteral("new"));
        if (isChanged)
        {
            item.insert(QStringLiteral("inputIndex"), document.inputIndex);
        }
        item.insert(QStringLiteral("pageCount"), document.document ? qint64(document.document->getCatalog()->getPageCount()) : qint64(0));
        item.insert(QStringLiteral("flags"), PDFDocumentToolHelper::getFlagNames(document.flags));
        if (!document.suggestedFileName.isEmpty())
        {
            item.insert(QStringLiteral("suggestedFileName"), document.suggestedFileName);
        }
        documentArray.append(item);
    }
    object.insert(QStringLiteral("documents"), documentArray);
    object.insert(QStringLiteral("files"), QJsonArray::fromStringList(writtenFiles));
    if (!notWrittenFiles.isEmpty())
    {
        object.insert(QStringLiteral("notWrittenFiles"), QJsonArray::fromStringList(notWrittenFiles));
    }

    if (!outputs.empty())
    {
        QJsonArray outputArray;
        for (const PDFDocumentToolOutputFile& output : outputs)
        {
            QJsonObject item;
            item.insert(QStringLiteral("suggestedFileName"), output.suggestedFileName);
            item.insert(QStringLiteral("mimeType"), output.mimeType);
            item.insert(QStringLiteral("size"), qint64(output.data.size()));
            outputArray.append(item);
        }
        object.insert(QStringLiteral("outputs"), outputArray);
    }

    QJsonArray factArray;
    for (const PDFDocumentToolFact& fact : facts)
    {
        QJsonObject item;
        item.insert(QStringLiteral("id"), fact.id);
        item.insert(QStringLiteral("title"), fact.title);
        item.insert(QStringLiteral("value"), fact.value);
        factArray.append(item);
    }
    object.insert(QStringLiteral("facts"), factArray);

    QJsonArray stepArray;
    for (const PDFDocumentToolStep& step : steps)
    {
        QJsonObject item;
        item.insert(QStringLiteral("text"), step.text);
        item.insert(QStringLiteral("state"), PDFDocumentToolHelper::getStepStateName(step.state));
        stepArray.append(item);
    }
    object.insert(QStringLiteral("steps"), stepArray);
    object.insert(QStringLiteral("data"), data);
    return object;
}

QString PDFDocumentToolFormat::formatSize(qint64 bytes)
{
    return QLocale().formattedDataSize(qMax(qint64(0), bytes), 1, QLocale::DataSizeTraditionalFormat);
}

QString PDFDocumentToolFormat::formatSizeChange(qint64 before, qint64 after)
{
    return PDFDocumentToolHelper::tr("%1 → %2").arg(formatSize(before), formatSize(after));
}

QString PDFDocumentToolFormat::formatDuration(qint64 milliseconds)
{
    const qint64 seconds = qMax(qint64(1), (milliseconds + 500) / 1000);
    if (seconds < 60)
    {
        return PDFDocumentToolHelper::tr("%1 s").arg(seconds);
    }
    return PDFDocumentToolHelper::tr("%1 min %2 s").arg(seconds / 60).arg(seconds % 60);
}

int PDFDocumentToolFormat::getSavedPercent(qint64 before, qint64 after)
{
    if (before <= 0 || after >= before)
    {
        return 0;
    }
    return qBound(0, int((double(before - after) * 100.0) / double(before) + 0.5), 100);
}

PDFDocumentTool::~PDFDocumentTool() = default;

const PDFDocumentToolCatalogEntry* PDFDocumentTool::getInfo() const
{
    return PDFDocumentToolCatalog::findTool(getId());
}

PDFDocumentToolResultKind PDFDocumentTool::getResultKind(const QVariantMap& parameters) const
{
    Q_UNUSED(parameters);
    const PDFDocumentToolCatalogEntry* entry = getInfo();
    return entry ? entry->getResultKind() : PDFDocumentToolResultKind::ChangeDocument;
}

std::vector<PDFDocumentToolResultKind> PDFDocumentTool::getPossibleResultKinds() const
{
    const PDFDocumentToolCatalogEntry* entry = getInfo();
    return entry ? entry->getPossibleResultKinds() : std::vector<PDFDocumentToolResultKind>({ PDFDocumentToolResultKind::ChangeDocument });
}

PDFDocumentToolInputCountRange PDFDocumentTool::getInputCountRange() const
{
    const PDFDocumentToolCatalogEntry* entry = getInfo();
    return entry ? entry->getInputCountRange() : PDFDocumentToolInputCountRange();
}

PDFDocumentToolPreselectedResult PDFDocumentTool::getPreselectedResult(const QVariantMap& parameters) const
{
    Q_UNUSED(parameters);
    const PDFDocumentToolCatalogEntry* entry = getInfo();
    return entry ? entry->getPreselectedResult() : PDFDocumentToolPreselectedResult::None;
}

PDFDocumentToolSignatureImpact PDFDocumentTool::getSignatureImpact(const QVariantMap& parameters) const
{
    Q_UNUSED(parameters);
    const PDFDocumentToolCatalogEntry* entry = getInfo();
    return entry ? entry->getSignatureImpact() : PDFDocumentToolSignatureImpact::None;
}

bool PDFDocumentTool::isNewDocumentOfInputs(const QVariantMap& parameters) const
{
    Q_UNUSED(parameters);
    const PDFDocumentToolCatalogEntry* entry = getInfo();
    return entry && entry->isNewDocumentOfInputs();
}

PDFDocumentToolOutputTraits PDFDocumentTool::getOutputTraits(const QVariantMap& parameters) const
{
    PDFDocumentToolOutputTraits traits;
    traits.entry = getInfo();
    traits.resultKind = getResultKind(parameters);
    traits.preselectedResult = getPreselectedResult(parameters);
    traits.signatureImpact = getSignatureImpact(parameters);
    traits.isNewDocumentOfInputs = isNewDocumentOfInputs(parameters);
    return traits;
}

PDFDocumentToolOutputTraits::PDFDocumentToolOutputTraits(const PDFDocumentToolCatalogEntry* catalogEntry) :
    entry(catalogEntry)
{
    if (entry)
    {
        resultKind = entry->getResultKind();
        preselectedResult = entry->getPreselectedResult();
        signatureImpact = entry->getSignatureImpact();
        isNewDocumentOfInputs = entry->isNewDocumentOfInputs();
    }
}

std::vector<PDFDocumentToolParameterError> PDFDocumentTool::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    Q_UNUSED(parameters);

    std::vector<PDFDocumentToolParameterError> errors;
    const PDFDocumentToolCatalogEntry* info = getInfo();
    for (const PDFDocumentToolInputDocument& document : context.documents)
    {
        if (!document.document)
        {
            errors.push_back({ QString(), PDFDocumentToolHelper::tr("An input document is missing.") });
            return errors;
        }

        if (info && !info->isPermitted(document.document.data()))
        {
            errors.push_back({ QString(), PDFDocumentToolHelper::tr("The permissions of the document do not allow this tool.") });
            return errors;
        }
    }

    switch (info ? info->getInputKind() : PDFDocumentToolInputKind::One)
    {
        case PDFDocumentToolInputKind::None:
            break;

        case PDFDocumentToolInputKind::One:
            if (context.documents.size() != 1)
            {
                errors.push_back({ QString(), PDFDocumentToolHelper::tr("The tool needs one document.") });
            }
            break;

        case PDFDocumentToolInputKind::Multiple:
            if (context.documents.empty())
            {
                errors.push_back({ QString(), PDFDocumentToolHelper::tr("The tool needs at least one document.") });
            }
            break;
    }
    return errors;
}

PDFDocumentToolResult PDFDocumentTool::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(context);
    Q_UNUSED(parameters);
    Q_UNUSED(control);
    Q_UNUSED(progress);
    return PDFDocumentToolResult();
}

PDFDocument PDFDocumentTool::createWholeFileDocument(PDFObjectStorage storage, const PDFDocument* input)
{
    PDFObjectUtils::removeFileStructureObjects(storage);
    return PDFDocument(std::move(storage), input->getInfo()->version, input->getSourceDataHash());
}

qint64 PDFDocumentTool::getWholeFileInputSize(const PDFDocumentToolInputDocument& input)
{
    return !input.originalData.isEmpty() && !input.isModified ? qint64(input.originalData.size()) : PDFDocumentWriter::getDocumentFileSize(input.document.data());
}

PDFDocumentTool::WholeFileSize PDFDocumentTool::getWholeFileOutputSize(const PDFDocument* output)
{
    WholeFileSize result;
    result.size = PDFDocumentWriter::getDocumentFileSize(output);
    if (result.size < 0)
    {
        result.error = PDFDocumentToolHelper::tr("The result cannot be written.");
    }
    else if (result.size > MAXIMUM_RESULT_SIZE)
    {
        result.size = -1;
        result.error = PDFDocumentToolHelper::tr("The result is too large to be saved.");
    }
    return result;
}

QByteArray PDFDocumentTool::writeDocumentData(const PDFDocument* document, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QBuffer::WriteOnly);
    PDFDocumentWriter writer(nullptr);
    const PDFOperationResult result = writer.write(&buffer, document);
    buffer.close();
    if (!result)
    {
        error = result.getErrorMessage();
        return QByteArray();
    }
    if (data.isEmpty())
    {
        error = PDFDocumentToolHelper::tr("The result cannot be written.");
        return QByteArray();
    }
    if (data.size() > MAXIMUM_RESULT_SIZE)
    {
        error = PDFDocumentToolHelper::tr("The result is too large to be saved.");
        return QByteArray();
    }
    return data;
}

void PDFDocumentTool::finishWholeFileResult(PDFDocumentToolResult* result, const PDFDocumentToolInputDocument& input, PDFDocument output, qint64 sizeBefore, qint64 sizeAfter, const QString& fileNameSuffix, WholeFileDocument documentRule) const
{
    result->data.insert(QStringLiteral("sizeBefore"), sizeBefore);
    result->data.insert(QStringLiteral("sizeAfter"), sizeAfter);
    result->data.insert(QStringLiteral("savedBytes"), qMax(qint64(0), sizeBefore - sizeAfter));
    result->data.insert(QStringLiteral("savedPercent"), PDFDocumentToolFormat::getSavedPercent(sizeBefore, sizeAfter));

    const PDFDocumentToolCatalogEntry* info = getInfo();
    result->data.insert(QStringLiteral("signatureCount"), PDFForm::getSignatureCount(input.document.data()));
    result->data.insert(QStringLiteral("signatureImpact"), PDFDocumentToolHelper::getSignatureImpactName(info ? info->getSignatureImpact() : PDFDocumentToolSignatureImpact::None));

    if (documentRule == WholeFileDocument::Always || sizeAfter < sizeBefore)
    {
        PDFDocumentToolOutputDocument outputDocument;
        outputDocument.kind = PDFDocumentToolOutputDocument::Kind::Changed;
        outputDocument.inputIndex = 0;
        outputDocument.document = PDFDocumentPointer(new PDFDocument(std::move(output)));
        outputDocument.flags = PDFModifiedDocument::Reset;

        const QString baseName = QFileInfo(input.fileName).completeBaseName();
        outputDocument.suggestedFileName = (baseName.isEmpty() ? QStringLiteral("document") : baseName) + fileNameSuffix + QStringLiteral(".pdf");
        result->documents.push_back(std::move(outputDocument));
    }
}

PDFDocumentTool::ConformanceRemoval PDFDocumentTool::removeConformanceDeclaration(PDFObjectStorage* storage, const PDFDocument* document)
{
    ConformanceRemoval removal;
    QStringList declarations;
    if (!PDFOCRPagePreparer::hasConformanceDeclaration(document, &declarations))
    {
        return removal;
    }

    PDFDocumentBuilder builder(document);
    if (!PDFOCRTextLayerWriter::removeConformanceDeclaration(&builder, document))
    {
        removal.kept = declarations;
        return removal;
    }

    *storage = *builder.getStorage();
    removal.removed = declarations;
    return removal;
}

QStringList PDFDocumentTool::getConformanceDeclarations(const PDFDocument* document)
{
    QStringList declarations;
    if (!document)
    {
        return declarations;
    }

    PDFOCRPagePreparer::hasConformanceDeclaration(document, &declarations);
    const PDFDictionary* trailer = document->getTrailerDictionary();
    const PDFDictionary* catalog = trailer ? document->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    // Held by value: a reference to the result of getObject may outlive its temporary
    const PDFObject intents = catalog ? document->getObject(catalog->get("OutputIntents")) : PDFObject();
    for (size_t i = 0; intents.isArray() && i < intents.getArray()->getCount(); ++i)
    {
        const PDFDictionary* intent = document->getDictionaryFromObject(intents.getArray()->getItem(i));
        const PDFObject subtype = intent ? document->getObject(intent->get("S")) : PDFObject();
        if (subtype.isName() && subtype.getString() == "GTS_PDFX" && !declarations.contains(QStringLiteral("PDF/X")))
        {
            declarations << QStringLiteral("PDF/X");
        }
    }
    return declarations;
}

PDFDocumentToolFact PDFDocumentTool::createConformanceFact(const QStringList& removed)
{
    return { QStringLiteral("conformance"), PDFDocumentToolHelper::tr("Declared standard"),
             PDFDocumentToolHelper::tr("The result no longer declares %1: it has not been checked against the standard").arg(removed.join(QStringLiteral(", "))) };
}

PDFDocument PDFDocumentTool::createWholeFileDocument(PDFObjectStorage storage, const PDFDocument* input, ConformanceRemoval* conformance)
{
    PDFDocument result = createWholeFileDocument(std::move(storage), input);
    if (conformance)
    {
        PDFObjectStorage resultStorage = result.getStorage();
        *conformance = removeConformanceDeclaration(&resultStorage, &result);
        if (!conformance->removed.isEmpty())
        {
            result = PDFDocument(std::move(resultStorage), input->getInfo()->version, input->getSourceDataHash());
        }
    }
    return result;
}

PDFDocumentToolResult PDFDocumentTool::createConformanceFailure(const QStringList& kept)
{
    return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("The document declares %1 and its metadata cannot be changed. The result would keep a declaration, which is not true, so nothing was made.").arg(kept.join(QStringLiteral(", "))));
}

void PDFDocumentTool::addConformanceFact(PDFDocumentToolResult* result, const QStringList& removed)
{
    if (!removed.isEmpty())
    {
        result->facts.push_back(createConformanceFact(removed));
        result->data.insert(QStringLiteral("conformanceRemoved"), QJsonArray::fromStringList(removed));
    }
}

std::vector<PDFDocumentToolParameterError> PDFDocumentTool::checkGivenParameters(const QVariantMap& given) const
{
    Q_UNUSED(given);
    return { };
}

QVariantMap PDFDocumentTool::completeGivenParameters(const PDFDocumentToolContext& context, const QVariantMap& given) const
{
    Q_UNUSED(context);
    return given;
}

QVariantMap PDFDocumentTool::mapParameters(const QVariantMap& parameters, const PDFDocument* from, const PDFDocument* to) const
{
    Q_UNUSED(from);
    Q_UNUSED(to);
    return parameters;
}

PDFDocumentToolResult PDFDocumentTool::prepare(const PDFDocumentToolContext& context, const QVariantMap& parameters, QVariantMap* normalizedParameters) const
{
    if (!getInfo())
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("Unknown tool."));
    }

    const PDFDocumentToolParameterDefinitions definitions = getParameters();
    if (!PDFDocumentToolParameters::isValid(definitions))
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("The parameters of the tool are not valid."));
    }

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentToolParameters::normalize(definitions, completeGivenParameters(context, parameters), normalizedParameters);
    if (errors.empty())
    {
        errors = validate(context, *normalizedParameters);
    }
    if (!errors.empty())
    {
        return PDFDocumentToolHelper::createInvalid(errors);
    }
    return PDFDocumentToolResult();
}

PDFDocumentToolResult PDFDocumentTool::run(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    PDFDocumentToolResult result;
    try
    {
        QVariantMap normalizedParameters;
        result = prepare(context, parameters, &normalizedParameters);
        if (result.status != PDFDocumentToolResultStatus::Success)
        {
            return result;
        }

        if (PDFOperationControl::isOperationCancelled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }

        result = execute(context, normalizedParameters, control, progress);
    }
    catch (const PDFException& exception)
    {
        return PDFDocumentToolResult::createFailed(exception.getMessage());
    }
    catch (const std::exception&)
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("The tool failed with an unexpected error."));
    }

    // A canceled run is never used, also when the tool finished meanwhile; the files, which a
    // tool of more files has written before the stop, are on the disk - the stopped result names
    // them (R3 of step 3, Split stopped: Written, Not written)
    if (result.status != PDFDocumentToolResultStatus::Failed && PDFOperationControl::isOperationCancelled(control))
    {
        PDFDocumentToolResult stopped = PDFDocumentToolResult::createStopped();
        stopped.writtenFiles = result.writtenFiles;
        stopped.notWrittenFiles = result.notWrittenFiles;
        return stopped;
    }

    if (!PDFDocumentToolHelper::isConsistent(result, context))
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("The tool returned an invalid result."));
    }
    return PDFDocumentToolHelper::finishProgress(result, progress);
}

PDFDocumentToolResult PDFDocumentTool::runPreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    PDFDocumentToolResult result;
    try
    {
        QVariantMap normalizedParameters;
        result = prepare(context, parameters, &normalizedParameters);
        if (result.status != PDFDocumentToolResultStatus::Success)
        {
            return result;
        }

        if (PDFOperationControl::isOperationCancelled(control))
        {
            return PDFDocumentToolResult::createStopped();
        }

        result = computePreview(context, normalizedParameters, control, progress);
    }
    catch (const PDFException& exception)
    {
        return PDFDocumentToolResult::createFailed(exception.getMessage());
    }
    catch (const std::exception&)
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("The tool failed with an unexpected error."));
    }

    if (result.status != PDFDocumentToolResultStatus::Failed && PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    if (isPreviewFinal())
    {
        // The preview is the result: the same checks as a run
        if (!PDFDocumentToolHelper::isConsistent(result, context))
        {
            return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("The tool returned an invalid result."));
        }
        return PDFDocumentToolHelper::finishProgress(result, progress);
    }

    // A preview has facts only
    result.documents.clear();
    result.outputs.clear();
    result.writtenFiles.clear();
    return PDFDocumentToolHelper::finishProgress(result, progress);
}

}   // namespace pdf
