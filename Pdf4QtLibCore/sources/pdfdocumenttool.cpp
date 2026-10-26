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
#include "pdfdocumenttoolcatalog.h"
#include "pdfexception.h"
#include "pdfoperationcontrol.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <new>

namespace pdf
{

/// Helpers of the tools. The texts are translated in the context
/// pdf::PDFDocumentTool - lupdate takes the context of the macro.
class PDFDocumentToolHelper
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentTool)

public:
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

PDFDocumentTool::~PDFDocumentTool() = default;

const PDFDocumentToolCatalogEntry* PDFDocumentTool::getInfo() const
{
    return PDFDocumentToolCatalog::findTool(getId());
}

std::vector<PDFDocumentToolParameterError> PDFDocumentTool::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    Q_UNUSED(parameters);

    std::vector<PDFDocumentToolParameterError> errors;
    for (const PDFDocumentToolInputDocument& document : context.documents)
    {
        if (!document.document)
        {
            errors.push_back({ QString(), PDFDocumentToolHelper::tr("An input document is missing.") });
            return errors;
        }
    }

    const PDFDocumentToolCatalogEntry* info = getInfo();
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

PDFDocumentToolResult PDFDocumentTool::computePreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control) const
{
    Q_UNUSED(context);
    Q_UNUSED(parameters);
    Q_UNUSED(control);
    return PDFDocumentToolResult();
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
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("There is not enough memory to finish the tool."));
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

PDFDocumentToolResult PDFDocumentTool::runPreview(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control) const
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

        result = computePreview(context, normalizedParameters, control);
    }
    catch (const PDFException& exception)
    {
        return PDFDocumentToolResult::createFailed(exception.getMessage());
    }
    catch (const std::bad_alloc&)
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("There is not enough memory to finish the tool."));
    }
    catch (const std::exception&)
    {
        return PDFDocumentToolResult::createFailed(PDFDocumentToolHelper::tr("The tool failed with an unexpected error."));
    }

    if (result.status != PDFDocumentToolResultStatus::Failed && PDFOperationControl::isOperationCancelled(control))
    {
        return PDFDocumentToolResult::createStopped();
    }

    // A preview has facts only
    result.documents.clear();
    result.writtenFiles.clear();
    return result;
}

}   // namespace pdf
