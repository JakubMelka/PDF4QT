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
#include "pdfexecutionpolicy.h"
#include "pdfform.h"
#include "pdfobjectutils.h"
#include "pdfocrpagepreparer.h"
#include "pdfocrtextlayerwriter.h"
#include "pdfoperationcontrol.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>

#include <new>

namespace pdf
{

/// Helpers of the tools. The texts are translated in the context
/// pdf::PDFDocumentTool - lupdate takes the context of the macro.
class PDFDocumentToolHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentTool)

public:
    /// More threads hold more data at once; the user, who needs less, has the setting. A setting,
    /// which is on already, is no advice.
    static QString getOutOfMemoryText()
    {
        if (!PDFExecutionPolicy::isParallelizing(PDFExecutionPolicy::Scope::Page))
        {
            return tr("There is not enough memory to finish the tool, nothing was changed.");
        }
        return tr("There is not enough memory to finish the tool, nothing was changed. Set Multithreading strategy in Settings › Advanced to Single thread and run it again: it holds one image at a time.");
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

PDFDocumentToolResult PDFDocumentToolResult::createOutOfMemory()
{
    PDFDocumentToolResult result = createFailed(PDFDocumentToolHelper::getOutOfMemoryText());
    result.failureKind = PDFDocumentToolFailureKind::OutOfMemory;
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

PDFDocumentToolProgressListener::~PDFDocumentToolProgressListener() = default;

PDFDocumentToolProgressAdapter::PDFDocumentToolProgressAdapter(PDFDocumentToolProgressListener* listener) :
    PDFProgress(nullptr),
    m_listener(listener)
{
    connect(this, &PDFProgress::progressStep, this, &PDFDocumentToolProgressAdapter::onStep, Qt::DirectConnection);
    connect(this, &PDFProgress::progressStepText, this, &PDFDocumentToolProgressAdapter::onStepText, Qt::DirectConnection);
}

void PDFDocumentToolProgressAdapter::onStep(int percentage)
{
    m_percentage = percentage;
    m_listener->onToolProgress(m_percentage, m_stepText);
}

void PDFDocumentToolProgressAdapter::onStepText(QString text)
{
    if (m_stepText != text)
    {
        m_stepText = std::move(text);
        m_listener->onToolProgress(m_percentage, m_stepText);
    }
}

PDFDocumentTool::~PDFDocumentTool() = default;

const PDFDocumentToolCatalogEntry* PDFDocumentTool::getInfo() const
{
    return PDFDocumentToolCatalog::findTool(getId());
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

PDFDocumentToolResult PDFDocumentTool::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const
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

PDFDocumentToolFact PDFDocumentTool::createConformanceFact(const QStringList& removed)
{
    return { QStringLiteral("conformance"), PDFDocumentToolHelper::tr("Declared standard"),
             PDFDocumentToolHelper::tr("The result no longer declares %1: it has not been checked against the standard").arg(removed.join(QStringLiteral(", "))) };
}

std::vector<PDFDocumentToolParameterError> PDFDocumentTool::checkGivenParameters(const QVariantMap& given) const
{
    Q_UNUSED(given);
    return { };
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

    std::vector<PDFDocumentToolParameterError> errors = PDFDocumentToolParameters::normalize(definitions, parameters, normalizedParameters);
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

PDFDocumentToolResult PDFDocumentTool::run(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const
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
    catch (const std::bad_alloc&)
    {
        return PDFDocumentToolResult::createOutOfMemory();
    }
    catch (const std::exception&)
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("The tool failed with an unexpected error."));
    }

    // A canceled run is never used, also when the tool finished meanwhile
    if (result.status != PDFDocumentToolResultStatus::Failed && PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    if (!PDFDocumentToolHelper::isConsistent(result, context))
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("The tool returned an invalid result."));
    }
    return result;
}

PDFDocumentToolResult PDFDocumentTool::runPreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgress* progress) const
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
    catch (const std::bad_alloc&)
    {
        return PDFDocumentToolResult::createOutOfMemory();
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
        return result;
    }

    // A preview has facts only
    result.documents.clear();
    result.outputs.clear();
    result.writtenFiles.clear();
    return result;
}

}   // namespace pdf
