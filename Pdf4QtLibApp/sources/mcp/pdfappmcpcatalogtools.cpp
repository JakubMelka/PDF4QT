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

#include "pdfappmcpcatalogtools.h"
#include "pdfappmcparguments.h"
#include "pdfappmcpchange.h"
#include "pdfappmcpconfirmationui.h"
#include "pdfappmcphost.h"
#include "pdfappmcpjob.h"
#include "pdfappmcptoolset.h"
#include "pdfappdocument.h"
#include "pdfappdocumentsave.h"

#include "pdfdocumenttool.h"
#include "pdfdocumenttoolparameter.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfprogress.h"

#include "pdfmcpjson.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace pdf
{

/// Helpers of the bridge of the catalog tools
class PDFAppMcpCatalogToolsHelper
{
public:
    PDFAppMcpCatalogToolsHelper() = delete;

    /// Most attempts of a commit after the document changed meanwhile (plan 2.5)
    static constexpr int MAXIMUM_ATTEMPTS = 3;

    static const char* getCatalogHandlerName() { return "*catalog*"; }

    static bool usesDocument(PDFDocumentToolInputKind kind)
    {
        return kind != PDFDocumentToolInputKind::None;
    }

    static QStringList getResultValueNames()
    {
        return { QStringLiteral("keep"), QStringLiteral("save_copy"), QStringLiteral("overwrite"), QStringLiteral("open_new_tab"), QStringLiteral("save_file") };
    }

    /// Structured content and the text block of a result of a tool
    static PDFMcpCallResult createResult(const QString& actionName, const PDFDocumentToolResult& result, const PDFAppDocument* document,
                                         const QString& undoDescription, const QString& writtenPath)
    {
        QJsonObject data = result.toJson();
        if (document)
        {
            data.insert(QStringLiteral("documentId"), document->getId());
            data.insert(QStringLiteral("title"), document->getTitle());
        }
        if (!undoDescription.isEmpty())
        {
            data.insert(QStringLiteral("undoDescription"), undoDescription);
        }
        if (!writtenPath.isEmpty())
        {
            data.insert(QStringLiteral("path"), writtenPath);
        }

        PDFMcpJsonWriter writer;
        writer.beginObject();
        writer.writeMember(QLatin1StringView("action"), QStringView(actionName));
        writer.key(QStringView(actionName));
        writer.writeQJson(data);
        writer.endObject();
        const QByteArray structured = writer.take();

        PDFMcpContentBuilder builder;
        builder.addText(QString::fromUtf8(structured));
        builder.setStructuredContent(structured);
        return builder.createCallResult(false);
    }
};

/// Adapter of PDFProgress of a tool to the progress of the call (lives in the worker thread)
class PDFAppMcpCatalogToolProgress : public QObject
{
public:
    PDFAppMcpCatalogToolProgress(std::shared_ptr<PDFMcpCall> call, QString message) :
        m_call(std::move(call)),
        m_message(std::move(message))
    {

    }

    void onProgressStep(int percentage)
    {
        if (m_call)
        {
            m_call->reportProgress(percentage, 100.0, m_message);
        }
    }

private:
    std::shared_ptr<PDFMcpCall> m_call;
    QString m_message;
};

/// One run of a catalog tool: the run of the tool in a worker, the decision
/// about its result in the GUI thread (permissions raised by the result, the
/// confirmation, the commit or the write of the copy in a second job)
class PDFAppMcpCatalogToolJob : public PDFAppMcpJob, public PDFAppMcpConfirmationReceiver
{
public:
    enum class Phase
    {
        Run,
        Write
    };

    PDFAppMcpCatalogToolJob(std::shared_ptr<PDFMcpCall> call,
                            PDFAppDocumentSnapshot snapshot,
                            std::shared_ptr<const PDFDocumentTool> tool,
                            QString toolName,
                            QString actionName,
                            QVariantMap parameters,
                            PDFAppMcpToolResultMode mode,
                            QString path,
                            PDFAppDocument* document,
                            int attempt) :
        PDFAppMcpJob(std::move(call), std::move(snapshot)),
        m_tool(std::move(tool)),
        m_toolName(std::move(toolName)),
        m_actionName(std::move(actionName)),
        m_parameters(std::move(parameters)),
        m_mode(mode),
        m_path(std::move(path)),
        m_document(document),
        m_attempt(attempt)
    {

    }

    QString getActionId() const { return m_toolName + QLatin1Char('.') + m_actionName; }

    virtual void execute() override
    {
        if (m_phase == Phase::Write)
        {
            // The file may have appeared since the arguments were checked (the
            // confirmation waits up to 2 minutes): a copy never replaces a file -
            // an overwrite is always confirmed as such (plan 3.9, review RW7-0)
            const QFileInfo target(m_path);
            if (target.exists() || target.isSymLink())
            {
                m_writeResult = PDFAppDocumentFileWriter::Result();
                m_writeResult.requestedFilePath = m_path;
                m_writeResult.errorMessage = QStringLiteral("the file exists already; choose another name.");
                return;
            }

            PDFAppDocumentFileWriter::Request request;
            request.document = m_outputDocument;
            request.filePath = m_path;
            request.version = getSnapshot().version;
            request.markSourceFilePath = getSnapshot().filePath;
            m_writeResult = PDFAppDocumentFileWriter::write(request);
            return;
        }

        PDFDocumentToolContext context;
        if (getSnapshot().isValid())
        {
            PDFDocumentToolInputDocument input;
            input.document = getSnapshot().document;
            input.originalData = getSnapshot().originalData;
            input.fileName = QFileInfo(getSnapshot().filePath).fileName();
            context.documents.push_back(std::move(input));
        }

        PDFProgress progress(nullptr);
        PDFAppMcpCatalogToolProgress adapter(getCall(), QStringLiteral("Running %1").arg(getActionId()));
        QObject::connect(&progress, &PDFProgress::progressStep, &adapter, &PDFAppMcpCatalogToolProgress::onProgressStep, Qt::DirectConnection);

        m_result = m_tool->run(context, m_parameters, getCall().get(), &progress);
    }

    virtual void finish(PDFAppMcpHost* host) override
    {
        m_host = host;
        const std::shared_ptr<PDFMcpCall>& call = getCall();
        if (!call || call->isFinished())
        {
            return;
        }

        if (call->isCanceled())
        {
            host->finishWithError(call, getActionId(), m_document, QStringLiteral("The run was canceled."), PDFMcpActivityStatus::Canceled);
            return;
        }

        if (m_phase == Phase::Write)
        {
            finishWrite();
            return;
        }

        switch (m_result.status)
        {
            case PDFDocumentToolResultStatus::Stopped:
                host->finishWithError(call, getActionId(), m_document, QStringLiteral("The run of the tool was stopped."), PDFMcpActivityStatus::Canceled);
                return;

            case PDFDocumentToolResultStatus::Failed:
            {
                const QString message = m_result.message.isEmpty() ? QStringLiteral("The tool failed.") : m_result.message;
                host->finishWithError(call, getActionId(), m_document, message, PDFMcpActivityStatus::Failed);
                return;
            }

            case PDFDocumentToolResultStatus::Success:
            case PDFDocumentToolResultStatus::NothingToDo:
                break;
        }

        // The changed document of the result (a new document needs the result
        // writer of phase 2)
        for (const PDFDocumentToolOutputDocument& output : m_result.documents)
        {
            if (output.kind == PDFDocumentToolOutputDocument::Kind::New)
            {
                host->finishWithError(call, getActionId(), m_document,
                                      QStringLiteral("The tool created a new document, which this version of PDF4QT cannot deliver to assistants yet."), PDFMcpActivityStatus::Failed);
                return;
            }
            if (output.kind == PDFDocumentToolOutputDocument::Kind::Changed && output.inputIndex == 0 && output.document)
            {
                m_outputDocument = output.document;
                m_outputFlags = output.flags;
            }
        }

        if (!m_outputDocument)
        {
            // Analysis or nothing to do: Read was checked before the run
            host->recordActivity(call, getActionId(), m_document, QJsonObject(), PDFMcpActivityStatus::Ok);
            call->finish(PDFAppMcpCatalogToolsHelper::createResult(m_actionName, m_result, m_document, QString(), QString()));
            return;
        }

        // A display version of an XFA form (step 8) is never changed, nor written
        // as a changed copy: its pages are not the pages of the file
        if (m_document && m_document->isPagedXFA())
        {
            host->finishWithError(call, getActionId(), m_document, PDFAppMcpChangeJob::getPagedXFAText(), PDFMcpActivityStatus::Failed);
            return;
        }

        // A locked document is never changed by an assistant - no question (O20)
        if (m_mode == PDFAppMcpToolResultMode::Keep && (!m_document || m_document->isLocked()))
        {
            host->finishWithError(call, getActionId(), m_document,
                                  m_document ? QStringLiteral("The document is locked in PDF4QT. Ask the user to unlock it or to use Edit a Copy.")
                                             : QStringLiteral("The tab of the document was closed."),
                                  m_document ? PDFMcpActivityStatus::DeniedLocked : PDFMcpActivityStatus::DeniedClosed);
            return;
        }

        // The permission is raised to what the result contains (plan 3.10)
        PDFAppMcpPermissionKinds kinds = PDFAppMcpPermissionKind::Read;
        kinds |= (m_mode == PDFAppMcpToolResultMode::SaveCopy) ? PDFAppMcpPermissionKind::Files : PDFAppMcpPermissionKind::Edit;

        const QString assistantKey = call->getRequest().client.assistantKey;
        const PDFAppMcpPermissionPolicy& policy = host->getPolicy();
        const PDFAppMcpPolicyResult policyResult = policy.evaluate(kinds, assistantKey, true);
        switch ((m_isConfirmed && policyResult == PDFAppMcpPolicyResult::Ask) ? PDFAppMcpPolicyResult::Allowed : policyResult)
        {
            case PDFAppMcpPolicyResult::Denied:
                host->finishWithError(call, getActionId(), m_document,
                                      m_mode == PDFAppMcpToolResultMode::SaveCopy ? QStringLiteral("The user does not allow assistants to save files (Open and save files is set to Deny in PDF4QT).")
                                                                                  : QStringLiteral("The tool changed the document, but the user does not allow assistants to edit documents (Edit documents is set to Deny in PDF4QT)."),
                                      PDFMcpActivityStatus::DeniedUser);
                return;

            case PDFAppMcpPolicyResult::Ask:
            {
                // At most 16 requests wait in one tab (plan 2.5)
                if (host->isConfirmationQueueFull(m_document))
                {
                    host->finishWithError(call, getActionId(), m_document, PDFAppMcpHost::getConfirmationQueueFullText(), PDFMcpActivityStatus::Failed);
                    return;
                }

                std::shared_ptr<PDFAppMcpConfirmation> confirmation = std::make_shared<PDFAppMcpConfirmation>();
                confirmation->kind = m_mode == PDFAppMcpToolResultMode::SaveCopy ? PDFAppMcpConfirmationKind::SaveCopy : PDFAppMcpConfirmationKind::Change;
                confirmation->permissions = policy.getKindsToConfirm(kinds, assistantKey, true);
                confirmation->call = call;
                confirmation->document = m_document;
                confirmation->actionId = getActionId();
                confirmation->details.insert(QStringLiteral("tool"), m_toolName + QLatin1Char('.') + m_actionName);
                confirmation->details.insert(QStringLiteral("facts"), m_result.toJson().value(QStringLiteral("facts")));
                if (!m_path.isEmpty())
                {
                    confirmation->details.insert(QStringLiteral("path"), m_path);
                }
                // The confirmation keeps the job alive while it waits (never the job
                // itself: a decision, which is never delivered - the host destroyed
                // at the quit -, would leave a cycle holding the call and the document)
                std::shared_ptr<PDFAppMcpConfirmationReceiver> receiver(shared_from_this(), static_cast<PDFAppMcpConfirmationReceiver*>(this));
                confirmation->receiver = receiver;
                confirmation->receiverHolder = receiver;
                host->requestConfirmation(confirmation);
                return;
            }

            case PDFAppMcpPolicyResult::Allowed:
                break;
        }

        proceed(m_isConfirmed ? PDFMcpActivityStatus::Allowed : PDFMcpActivityStatus::Ok);
    }

    virtual void confirmationFinished(quint64 confirmationId, PDFAppMcpDecision decision) override
    {
        Q_UNUSED(confirmationId);

        // The host holds this job (the receiver of the confirmation) during the call
        const std::shared_ptr<PDFMcpCall>& call = getCall();
        if (!m_host || !call || call->isFinished())
        {
            return;
        }

        if (decision != PDFAppMcpDecision::Allowed)
        {
            // The waiting row already shows the decision; the texts of the changes
            // (a stopped server, a disconnected assistant or a canceled call is not
            // a denial of the user - review RC7-H)
            call->finish(PDFMcpCallResult::createToolError(PDFAppMcpChangeJob::getDecisionText(decision)));
            return;
        }

        m_isConfirmed = true;
        proceed(PDFMcpActivityStatus::Allowed);
    }

private:
    void proceed(PDFMcpActivityStatus status)
    {
        PDFAppMcpHost* host = m_host;
        const std::shared_ptr<PDFMcpCall>& call = getCall();
        if (!host || !call)
        {
            return;
        }

        if (m_mode == PDFAppMcpToolResultMode::SaveCopy)
        {
            // The rules of the paths checked again after the confirmation (up to 2
            // minutes): a folder of the path replaced meanwhile by a link to the
            // data of PDF4QT or to a network location (as files.save_copy, review RC7-H)
            QString pathError;
            const QString path = PDFAppMcpArguments::checkPath(m_path, true, host->getContext(), &pathError);
            if (path.isEmpty() || path != m_path)
            {
                host->finishWithError(call, getActionId(), m_document,
                                      QStringLiteral("path: %1").arg(path.isEmpty() ? pathError : QStringLiteral("the path has changed since the confirmation; try again")),
                                      PDFMcpActivityStatus::Failed);
                return;
            }

            // The write runs in a second job (a worker)
            std::shared_ptr<PDFAppMcpCatalogToolJob> writeJob = std::make_shared<PDFAppMcpCatalogToolJob>(call, getSnapshot(), m_tool, m_toolName, m_actionName,
                                                                                                          m_parameters, m_mode, m_path, m_document, m_attempt);
            writeJob->m_phase = Phase::Write;
            writeJob->m_result = m_result;
            writeJob->m_outputDocument = m_outputDocument;
            writeJob->m_status = status;
            host->startJob(writeJob);
            return;
        }

        PDFAppDocument* document = m_document;
        if (!document)
        {
            host->finishWithError(call, getActionId(), nullptr, QStringLiteral("The tab of the document was closed."), PDFMcpActivityStatus::DeniedClosed);
            return;
        }

        if (document->isPagedXFA())
        {
            host->finishWithError(call, getActionId(), document, PDFAppMcpChangeJob::getPagedXFAText(), PDFMcpActivityStatus::Failed);
            return;
        }

        if (document->isLocked())
        {
            host->finishWithError(call, getActionId(), document, QStringLiteral("The document is locked in PDF4QT. Ask the user to unlock it or to use Edit a Copy."), PDFMcpActivityStatus::DeniedLocked);
            return;
        }

        if (document->getVersion() != getSnapshot().version)
        {
            // The document changed meanwhile: the tool runs again on the current
            // version (plan 2.5, up to 3 tries)
            if (m_attempt + 1 >= PDFAppMcpCatalogToolsHelper::MAXIMUM_ATTEMPTS)
            {
                host->finishWithError(call, getActionId(), document, QStringLiteral("The document changed in the meantime. Call the tool again."), PDFMcpActivityStatus::Failed);
                return;
            }

            std::shared_ptr<PDFAppMcpCatalogToolJob> rerun = std::make_shared<PDFAppMcpCatalogToolJob>(call, document->getSnapshot(), m_tool, m_toolName, m_actionName,
                                                                                                       m_parameters, m_mode, m_path, document, m_attempt + 1);
            rerun->m_isConfirmed = m_isConfirmed;
            host->startJob(rerun);
            return;
        }

        const PDFAppMcpClientInfo client = PDFAppMcpHost::getClientInfo(call->getRequest().client);
        const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(getActionId());
        const QString undoDescription = QCoreApplication::translate("pdf::PDFAppMcpCatalogTools", "%1 ran %2").arg(client.displayName, entry ? entry->getTitle() : getActionId());

        PDFModifiedDocument modified(m_outputDocument, document->getOptionalContentActivity(), m_outputFlags);
        if (!document->commit(modified, undoDescription, getSnapshot().version, PDFAppChangeSource::Mcp))
        {
            host->finishWithError(call, getActionId(), document, QStringLiteral("PDF4QT is busy with another operation on the document. Try again when it ends."), PDFMcpActivityStatus::DeniedBusy);
            return;
        }

        // The notification bar of every change of an assistant (owner Q3, package C)
        PDFAppMcpCommitInfo info;
        info.document = document;
        info.clientName = client.displayName;
        info.clientNameRaw = call->getRequest().client.clientName;
        info.text = undoDescription;
        info.version = document->getVersion();
        PDFAppMcpConfirmationUi::notifyCommitted(host, info);

        host->recordActivity(call, getActionId(), document, QJsonObject(), status);
        call->finish(PDFAppMcpCatalogToolsHelper::createResult(m_actionName, m_result, document, undoDescription, QString()));
    }

    void finishWrite()
    {
        PDFAppMcpHost* host = m_host;
        const std::shared_ptr<PDFMcpCall>& call = getCall();
        if (!m_writeResult.isSaved)
        {
            host->finishWithError(call, getActionId(), m_document, QStringLiteral("The copy could not be written: %1").arg(m_writeResult.errorMessage), PDFMcpActivityStatus::Failed);
            return;
        }

        host->recordActivity(call, getActionId(), m_document, QJsonObject(), m_status);
        call->finish(PDFAppMcpCatalogToolsHelper::createResult(m_actionName, m_result, m_document, QString(), m_writeResult.filePath));
    }

    std::shared_ptr<const PDFDocumentTool> m_tool;
    QString m_toolName;
    QString m_actionName;
    QVariantMap m_parameters;
    PDFAppMcpToolResultMode m_mode = PDFAppMcpToolResultMode::Keep;
    QString m_path;
    QPointer<PDFAppDocument> m_document;
    int m_attempt = 0;
    Phase m_phase = Phase::Run;
    PDFDocumentToolResult m_result;
    PDFDocumentPointer m_outputDocument;
    PDFModifiedDocument::ModificationFlags m_outputFlags = PDFModifiedDocument::Reset;
    PDFAppDocumentFileWriter::Result m_writeResult;
    PDFMcpActivityStatus m_status = PDFMcpActivityStatus::Ok;
    bool m_isConfirmed = false;                 ///< The user has allowed the change (a rerun does not ask again)
    QPointer<PDFAppMcpHost> m_host;
};

/// The handler of every category tool
class PDFAppMcpCatalogToolHandler : public PDFAppMcpToolHandler
{
public:
    virtual QString getToolName() const override
    {
        return QString::fromLatin1(PDFAppMcpCatalogToolsHelper::getCatalogHandlerName());
    }

    virtual void handle(const PDFAppMcpToolInvocation& invocation) override
    {
        PDFAppMcpHost* host = invocation.host;
        const std::shared_ptr<PDFMcpCall>& call = invocation.call;
        const QString toolId = PDFAppMcpCatalogTools::getToolId(invocation.toolName, invocation.actionName);
        const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(toolId);
        std::shared_ptr<const PDFDocumentTool> tool = PDFDocumentToolRegistry::getTool(toolId);
        if (!entry || !tool)
        {
            host->finishWithError(call, toolId, nullptr, QStringLiteral("This tool is not available. Call tools/list for the current tools."), PDFMcpActivityStatus::Failed);
            return;
        }

        PDFMcpJsonReader reader(invocation.arguments, QStringLiteral("arguments"));
        const PDFDocumentToolResultKind resultKind = entry->getResultKind();
        const QStringList resultValues = PDFAppMcpCatalogTools::getResultValues(resultKind);
        const std::optional<QString> resultValue = resultValues.isEmpty() ? std::nullopt : reader.getEnum(QLatin1StringView("result"), resultValues, false);
        const std::optional<QString> pathValue = reader.getString(QLatin1StringView("path"), PDFAppMcpToolSet::MAXIMUM_PATH_LENGTH, false, PDFMcpJson::SanitizeFlag::None);
        if (reader.hasErrors())
        {
            host->finishWithError(call, toolId, nullptr, QStringLiteral("Invalid arguments: %1").arg(reader.getErrorText()), PDFMcpActivityStatus::Failed);
            return;
        }

        const PDFAppMcpToolResultMode mode = PDFAppMcpCatalogTools::getResultMode(resultValue.value_or(PDFAppMcpCatalogTools::getDefaultResult(resultKind))).value_or(PDFAppMcpToolResultMode::Keep);
        if (mode == PDFAppMcpToolResultMode::Overwrite || mode == PDFAppMcpToolResultMode::OpenNewTab || mode == PDFAppMcpToolResultMode::SaveFile)
        {
            host->finishWithError(call, toolId, nullptr, QStringLiteral("The result \"%1\" is not available in this version of PDF4QT; use keep or save_copy.").arg(resultValue.value_or(QString())), PDFMcpActivityStatus::Failed);
            return;
        }

        QString path;
        if (mode == PDFAppMcpToolResultMode::SaveCopy)
        {
            QString pathError;
            path = PDFAppMcpArguments::checkPath(pathValue.value_or(QString()), true, host->getContext(), &pathError);
            if (path.isEmpty())
            {
                host->finishWithError(call, toolId, nullptr, QStringLiteral("path: %1").arg(pathValue ? pathError : QStringLiteral("save_copy needs an absolute path of a .pdf file")), PDFMcpActivityStatus::Failed);
                return;
            }
            if (QFileInfo::exists(path))
            {
                host->finishWithError(call, toolId, nullptr, QStringLiteral("path: the file exists already; choose another name."), PDFMcpActivityStatus::Failed);
                return;
            }
        }

        // The input document
        PDFAppDocument* document = nullptr;
        PDFAppDocumentSnapshot snapshot;
        if (PDFAppMcpCatalogToolsHelper::usesDocument(entry->getInputKind()))
        {
            const PDFAppMcpResolvedDocument resolved = host->getResolver().resolve(invocation.documentId, true);
            if (!resolved.isResolved())
            {
                host->finishWithError(call, toolId, nullptr, resolved.errorText, PDFMcpActivityStatus::Failed);
                return;
            }
            document = resolved.document;

            if (document->isLocked() && entry->getLockedAccess() == PDFDocumentToolLockedAccess::Unavailable && mode == PDFAppMcpToolResultMode::Keep)
            {
                host->finishWithError(call, toolId, document, QStringLiteral("The document is locked in PDF4QT. Ask the user to unlock it or to use Edit a Copy."), PDFMcpActivityStatus::DeniedLocked);
                return;
            }
            if (document->isBlockedByOperation())
            {
                host->finishWithError(call, toolId, document, QStringLiteral("PDF4QT is busy with another operation on the document (saving, printing or a tool). Try again when it ends."), PDFMcpActivityStatus::DeniedBusy);
                return;
            }
            snapshot = document->getSnapshot();
        }

        // The parameters of the tool are the object of the action (normalized and
        // validated again by PDFDocumentTool::run)
        std::shared_ptr<PDFAppMcpCatalogToolJob> job = std::make_shared<PDFAppMcpCatalogToolJob>(call, snapshot, tool, invocation.toolName, invocation.actionName,
                                                                                                invocation.actionArguments.toVariantMap(), mode, path, document, 0);
        host->startJob(job);
    }
};

bool PDFAppMcpCatalogTools::isCatalogToolName(QStringView name)
{
    for (const PDFDocumentToolCategoryInfo& category : PDFDocumentToolCatalog::getCategories())
    {
        if (getToolName(category.getCategory()) == name)
        {
            return true;
        }
    }
    return false;
}

QString PDFAppMcpCatalogTools::getToolName(PDFDocumentToolCategory category)
{
    return PDFDocumentToolCatalog::getCategoryId(category);
}

QString PDFAppMcpCatalogTools::getToolId(const QString& toolName, const QString& actionName)
{
    return toolName + QLatin1Char('.') + actionName;
}

std::vector<const PDFDocumentToolCatalogEntry*> PDFAppMcpCatalogTools::getImplementedTools(PDFDocumentToolCategory category)
{
    std::vector<const PDFDocumentToolCatalogEntry*> tools;
    const QStringList implemented = PDFDocumentToolRegistry::getImplementedTools();
    for (const PDFDocumentToolCatalogEntry* entry : PDFDocumentToolCatalog::getTools(category))
    {
        if (implemented.contains(entry->getId()))
        {
            tools.push_back(entry);
        }
    }
    return tools;
}

QStringList PDFAppMcpCatalogTools::getResultValues(PDFDocumentToolResultKind kind)
{
    switch (kind)
    {
        case PDFDocumentToolResultKind::ChangeDocument:
            return { QStringLiteral("keep"), QStringLiteral("save_copy"), QStringLiteral("overwrite") };
        case PDFDocumentToolResultKind::Analysis:
            // An analysis, which changes the document anyway, is raised to Edit (3.10)
            return { QStringLiteral("keep"), QStringLiteral("save_copy") };
        case PDFDocumentToolResultKind::NewDocument:
            return { QStringLiteral("open_new_tab"), QStringLiteral("save_file") };
        case PDFDocumentToolResultKind::Files:
            break;
    }
    return QStringList();
}

QString PDFAppMcpCatalogTools::getDefaultResult(PDFDocumentToolResultKind kind)
{
    const QStringList values = getResultValues(kind);
    return values.isEmpty() ? QString() : values.front();
}

std::optional<PDFAppMcpToolResultMode> PDFAppMcpCatalogTools::getResultMode(const QString& value)
{
    const qsizetype index = PDFAppMcpCatalogToolsHelper::getResultValueNames().indexOf(value);
    if (index < 0)
    {
        return std::nullopt;
    }
    return PDFAppMcpToolResultMode(index);
}

PDFAppMcpPermissionKind PDFAppMcpCatalogTools::getBasePermission(PDFDocumentToolResultKind kind)
{
    switch (kind)
    {
        case PDFDocumentToolResultKind::Analysis:
            return PDFAppMcpPermissionKind::Read;
        case PDFDocumentToolResultKind::ChangeDocument:
            return PDFAppMcpPermissionKind::Edit;
        case PDFDocumentToolResultKind::NewDocument:
        case PDFDocumentToolResultKind::Files:
            return PDFAppMcpPermissionKind::Files;
    }
    return PDFAppMcpPermissionKind::Edit;
}

std::optional<PDFMcpSchema> PDFAppMcpCatalogTools::createInputSchema(PDFDocumentToolCategory category, const PDFAppMcpPermissions& permissions)
{
    QStringList actionNames;
    std::vector<std::pair<QString, PDFMcpSchema>> actionSchemas;
    bool usesDocument = false;
    bool usesDocuments = false;
    QStringList resultValues;

    for (const PDFDocumentToolCatalogEntry* entry : getImplementedTools(category))
    {
        if (!permissions.isAvailable(getBasePermission(entry->getResultKind())))
        {
            continue;
        }

        std::shared_ptr<const PDFDocumentTool> tool = PDFDocumentToolRegistry::getTool(entry->getId());
        if (!tool)
        {
            continue;
        }

        QString error;
        std::optional<PDFMcpSchema> schema = PDFMcpSchema::fromQJson(PDFDocumentToolParameters::toJsonSchema(tool->getParameters()), &error);
        if (!schema)
        {
            qWarning("The parameters of the tool %s give no MCP schema: %s", qPrintable(entry->getId()), qPrintable(error));
            continue;
        }

        const QString actionName = entry->getId().section(QLatin1Char('.'), 1);
        schema->setDescription(QStringLiteral("Parameters of %1: %2").arg(entry->getSourceTitle(), entry->getSourceDescription()));
        actionNames << actionName;
        actionSchemas.emplace_back(actionName, *schema);

        usesDocument = usesDocument || entry->getInputKind() != PDFDocumentToolInputKind::None;
        usesDocuments = usesDocuments || entry->getInputKind() == PDFDocumentToolInputKind::Multiple;
        for (const QString& value : getResultValues(entry->getResultKind()))
        {
            if (!resultValues.contains(value))
            {
                resultValues << value;
            }
        }
    }

    if (actionNames.isEmpty())
    {
        return std::nullopt;
    }

    using B = PDFMcpSchemaBuilder;
    PDFMcpSchema input = B::object();
    input.addProperty(QStringLiteral("action"), B::enumeration(QStringLiteral("The tool; its parameters are in the property of the same name"), actionNames), true);
    if (usesDocument)
    {
        input.addProperty(QStringLiteral("documentId"), B::string(QStringLiteral("Id of an open document (from application.get_state); omitted = the active document tab, else the only open document"),
                                                                  PDFAppMcpToolSet::DOCUMENT_ID_LENGTH));
    }
    if (usesDocuments)
    {
        input.addProperty(QStringLiteral("documentIds"), B::array(QStringLiteral("Ids of the open documents of a tool, which works with more documents (the first is the main one)"),
                                                                  B::string(QString(), PDFAppMcpToolSet::DOCUMENT_ID_LENGTH), 1, 100));
    }
    if (!resultValues.isEmpty())
    {
        input.addProperty(QStringLiteral("result"), B::enumeration(QStringLiteral("What happens with a changed document: keep (one undo step in its tab), save_copy (written to path, the tab "
                                                                                  "is unchanged), overwrite, open_new_tab, save_file"), resultValues));
        input.addProperty(QStringLiteral("path"), B::string(QStringLiteral("Absolute path of the written file (save_copy, save_file)"), PDFAppMcpToolSet::MAXIMUM_PATH_LENGTH));
    }
    for (const auto& item : actionSchemas)
    {
        input.addProperty(item.first, item.second);
    }
    return input;
}

PDFMcpSchema PDFAppMcpCatalogTools::createOutputSchema(PDFDocumentToolCategory category, const PDFAppMcpPermissions& permissions)
{
    using B = PDFMcpSchemaBuilder;

    QStringList actionNames;
    PDFMcpSchema output = B::object(QString(), false);
    PDFMcpSchema result = B::object(QStringLiteral("Result of the tool"), false);
    result.addProperty(QStringLiteral("status"), B::enumeration(QString(), { QStringLiteral("success"), QStringLiteral("stopped"), QStringLiteral("failed"), QStringLiteral("nothing-to-do") }));
    result.addProperty(QStringLiteral("message"), B::string(QString()));
    result.addProperty(QStringLiteral("documents"), B::array(QString(), B::object(QString(), false)));
    result.addProperty(QStringLiteral("files"), B::array(QString(), B::string(QString())));
    result.addProperty(QStringLiteral("facts"), B::array(QString(), B::object(QString(), false)));
    result.addProperty(QStringLiteral("steps"), B::array(QString(), B::object(QString(), false)));
    result.addProperty(QStringLiteral("data"), B::object(QStringLiteral("Structured data of the tool"), false));
    result.addProperty(QStringLiteral("documentId"), B::string(QString()));
    result.addProperty(QStringLiteral("title"), B::string(QString()));
    result.addProperty(QStringLiteral("undoDescription"), B::string(QString()));
    result.addProperty(QStringLiteral("path"), B::string(QString()));

    for (const PDFDocumentToolCatalogEntry* entry : getImplementedTools(category))
    {
        if (permissions.isAvailable(getBasePermission(entry->getResultKind())))
        {
            actionNames << entry->getId().section(QLatin1Char('.'), 1);
        }
    }

    output.addProperty(QStringLiteral("action"), B::enumeration(QStringLiteral("The tool of the call"), actionNames), true);
    for (const QString& name : actionNames)
    {
        output.addProperty(name, result);
    }
    return output;
}

std::vector<PDFMcpToolDefinition> PDFAppMcpCatalogTools::createDefinitions(const PDFAppMcpPermissions& permissions, const std::vector<PDFMcpIcon>& icons)
{
    std::vector<PDFMcpToolDefinition> definitions;
    for (const PDFDocumentToolCategoryInfo& category : PDFDocumentToolCatalog::getCategories())
    {
        std::optional<PDFMcpSchema> input = createInputSchema(category.getCategory(), permissions);
        if (!input)
        {
            continue;
        }

        // A tool of the catalog is never read-only (also an analysis may change the
        // document, 3.10); destructive, when a tool changes the document
        bool isDestructive = false;
        for (const PDFDocumentToolCatalogEntry* entry : getImplementedTools(category.getCategory()))
        {
            isDestructive = isDestructive || entry->getResultKind() == PDFDocumentToolResultKind::ChangeDocument;
        }

        PDFMcpToolDefinition definition;
        definition.name = getToolName(category.getCategory());
        // English texts for the model (the titles of the catalog are translated)
        definition.title = QStringLiteral("PDF4QT tools: %1").arg(category.getId());
        definition.description = QStringLiteral("Tools of the category \"%1\" of PDF4QT (one action per tool). A tool runs on a copy of the document; a changed "
                                                "document is kept as one undo step or saved as a copy, after the confirmation of the user when it is set to Ask.")
                                     .arg(category.getId());
        definition.inputSchema = input->toJson();
        definition.outputSchema = createOutputSchema(category.getCategory(), permissions).toJson();
        definition.annotations.title = definition.title;
        definition.annotations.readOnlyHint = false;
        definition.annotations.destructiveHint = isDestructive;
        definition.annotations.idempotentHint = false;
        definition.annotations.openWorldHint = false;
        definition.icons = icons;
        definitions.push_back(std::move(definition));
    }
    return definitions;
}

std::unique_ptr<PDFAppMcpToolHandler> PDFAppMcpCatalogTools::createHandler()
{
    return std::make_unique<PDFAppMcpCatalogToolHandler>();
}

}   // namespace pdf
