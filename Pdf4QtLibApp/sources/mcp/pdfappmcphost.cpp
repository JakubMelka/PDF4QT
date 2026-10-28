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

#include "pdfappmcphost.h"
#include "pdfappmcpcatalogtools.h"
#include "pdfappmcpcompletion.h"
#include "pdfappmcpdocumentresources.h"
#include "pdfappmcpjob.h"
#include "pdfappmcpprompts.h"
#include "pdfappmcpreadsupport.h"
#include "pdfappmcpresources.h"
#include "pdfappmcptools.h"
#include "pdfappmcptoolset.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"

#include "pdfdocumenttoolregistry.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"
#include "pdfmcpserver.h"
#include "pdfthemecolors.h"
#include "pdfthemeiconengine.h"

#include <QBuffer>
#include <QDateTime>
#include <QJsonDocument>
#include <QPixmap>
#include <QTimer>

namespace pdf
{

/// Helpers of the host
class PDFAppMcpHostHelper
{
public:
    PDFAppMcpHostHelper() = delete;

    /// Permission of an action of a tool of phase 1 or of the catalog (Read for an unknown one)
    static PDFAppMcpPermissionKind getActionPermission(const QString& toolName, const QString& actionName)
    {
        if (const PDFAppMcpToolSpec* tool = PDFAppMcpToolSet::findTool(toolName))
        {
            const PDFAppMcpActionSpec* action = tool->findAction(actionName);
            return action ? action->permission : PDFAppMcpPermissionKind::Read;
        }
        if (const PDFDocumentToolCatalogEntry* entry = PDFDocumentToolCatalog::findTool(PDFAppMcpCatalogTools::getToolId(toolName, actionName)))
        {
            return PDFAppMcpCatalogTools::getBasePermission(entry->getResultKind());
        }
        return PDFAppMcpPermissionKind::Read;
    }

    /// The action exists (a tool of phase 1 or of the catalog), so a call of it,
    /// which is not offered, is a denial (not an unknown name)
    static bool isKnownAction(const QString& toolName, const QString& actionName)
    {
        if (const PDFAppMcpToolSpec* tool = PDFAppMcpToolSet::findTool(toolName))
        {
            return tool->findAction(actionName) != nullptr;
        }
        return PDFDocumentToolCatalog::findTool(PDFAppMcpCatalogTools::getToolId(toolName, actionName)) != nullptr;
    }

    /// The action id of the activity of a request ("text.get_text"; empty for a
    /// request, which is not a call of a tool - not logged)
    static QString getCallActionId(const PDFMcpCallRequest& request)
    {
        if (request.kind != PDFMcpCallKind::ToolCall)
        {
            return QString();
        }
        return request.name + QLatin1Char('.') + request.arguments.value(QStringLiteral("action")).toString().left(64);
    }

    /// The open document named by documentId of the arguments (the audit row of a
    /// denial names it; nullptr when there is none)
    static const PDFAppDocument* findArgumentDocument(const PDFAppMcpDocumentResolver& resolver, const QJsonObject& arguments)
    {
        const QString documentId = arguments.value(QStringLiteral("documentId")).toString();
        return documentId.isEmpty() ? nullptr : resolver.resolve(documentId, false).document;
    }

    /// The receiver of the confirmation learns the decision (it releases the
    /// holder, by which a waiting receiver keeps itself alive)
    static void deliverDecision(const std::shared_ptr<PDFAppMcpConfirmation>& confirmation, PDFAppMcpDecision decision)
    {
        if (!confirmation)
        {
            return;
        }

        std::shared_ptr<PDFAppMcpConfirmationReceiver> receiver = confirmation->receiver.lock();
        std::shared_ptr<PDFAppMcpConfirmationReceiver> holder = std::move(confirmation->receiverHolder);
        if (!receiver)
        {
            receiver = holder;
        }

        if (receiver)
        {
            receiver->confirmationFinished(confirmation->id, decision);
        }
    }
};

/// Receiver of the read approval of a tools/call (plan 2.7): the call goes on
/// to its handler after Allow, it ends by an execution error otherwise
class PDFAppMcpReadApproval : public PDFAppMcpConfirmationReceiver
{
public:
    PDFAppMcpReadApproval(PDFAppMcpHost* host, std::shared_ptr<PDFMcpCall> call, QString toolName, QString actionName, QJsonObject arguments) :
        m_host(host),
        m_call(std::move(call)),
        m_toolName(std::move(toolName)),
        m_actionName(std::move(actionName)),
        m_arguments(std::move(arguments))
    {

    }

    virtual void confirmationFinished(quint64 confirmationId, PDFAppMcpDecision decision) override
    {
        Q_UNUSED(confirmationId);

        if (!m_host || !m_call || m_call->isFinished())
        {
            return;
        }

        if (decision == PDFAppMcpDecision::Allowed)
        {
            m_host->continueToolCall(m_call, m_toolName, m_actionName, m_arguments);
            return;
        }

        // The texts of the deferred reads (a stopped server, a canceled call or a
        // disconnected assistant is not a denial of the user)
        m_call->finish(PDFMcpCallResult::createToolError(PDFAppMcpReadSupport::getReadRefusedText(decision)));
    }

private:
    QPointer<PDFAppMcpHost> m_host;
    std::shared_ptr<PDFMcpCall> m_call;
    QString m_toolName;
    QString m_actionName;
    QJsonObject m_arguments;
};

PDFAppMcpHost::PDFAppMcpHost(PDFAppContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context),
    m_resolver(context)
{
    m_threadPool.setMaxThreadCount(qMax(2, QThread::idealThreadCount() / 2));
    m_threadPool.setObjectName(QStringLiteral("PDF4QT MCP workers"));

    m_activityLog = new PDFAppMcpActivityLog(context ? context->getDatabase() : nullptr, this);

    // The handlers of the tools of phase 1 (packages B and C replace the stubs)
    for (std::unique_ptr<PDFAppMcpToolHandler>& handler : PDFAppMcpTools::createHandlers())
    {
        setToolHandler(std::move(handler));
    }
    m_catalogToolHandler = PDFAppMcpCatalogTools::createHandler();

    m_registryGeneration = PDFDocumentToolRegistry::getGeneration();
    m_registryTimer = new QTimer(this);
    m_registryTimer->setInterval(1000);
    connect(m_registryTimer, &QTimer::timeout, this, &PDFAppMcpHost::checkToolRegistry);
    m_registryTimer->start();

    // Package B: the open documents give the listed resources and their updates
    new PDFAppMcpDocumentWatcher(this);

    rebuildCatalog();
}

PDFAppMcpHost::~PDFAppMcpHost()
{
    // A dying host emits nothing (the controller shuts it down before)
    blockSignals(true);
    shutdown();
    m_handlers.clear();
    m_catalogToolHandler.reset();
}

PDFAppContext* PDFAppMcpHost::getContext() const
{
    return m_context;
}

void PDFAppMcpHost::setPermissions(const PDFAppMcpPermissions& permissions)
{
    if (m_policy.getPermissions() == permissions)
    {
        return;
    }

    m_policy.setPermissions(permissions);
    rebuildCatalog();
}

void PDFAppMcpHost::setServer(PDFMcpServer* server)
{
    m_server = server;
    if (m_server && m_catalog)
    {
        m_server->publishCatalog(m_catalog);
    }
}

void PDFAppMcpHost::setIcons(std::vector<PDFMcpIcon> icons)
{
    m_icons = std::move(icons);
    rebuildCatalog();
}

quint64 PDFAppMcpHost::getCatalogGeneration() const
{
    return m_catalog ? m_catalog->getGeneration() : 0;
}

void PDFAppMcpHost::rebuildCatalog()
{
    const PDFAppMcpPermissions& permissions = m_policy.getPermissions();

    std::vector<PDFMcpToolDefinition> tools = PDFAppMcpToolSet::createDefinitions(permissions, m_icons);
    std::vector<PDFMcpToolDefinition> catalogTools = PDFAppMcpCatalogTools::createDefinitions(permissions, m_icons);
    tools.insert(tools.end(), std::make_move_iterator(catalogTools.begin()), std::make_move_iterator(catalogTools.end()));

    // The parsed input schemas validate the arguments of the calls
    m_inputSchemas.clear();
    for (const PDFMcpToolDefinition& tool : tools)
    {
        QString error;
        std::optional<PDFMcpSchema> schema = PDFMcpSchema::fromQJson(QJsonDocument::fromJson(tool.inputSchema).object(), &error);
        if (schema)
        {
            m_inputSchemas.emplace(tool.name, std::move(*schema));
        }
    }

    std::vector<PDFMcpResourceDefinition> resources = PDFAppMcpResources::createStaticResources();
    std::vector<PDFMcpResourceDefinition> documentResources = PDFAppMcpResources::createDocumentResources(this);
    resources.insert(resources.end(), std::make_move_iterator(documentResources.begin()), std::make_move_iterator(documentResources.end()));

    m_catalog = std::make_shared<const PDFMcpCatalog>(++m_generation, std::move(tools), std::move(resources),
                                                      PDFAppMcpResources::createTemplates(), PDFAppMcpPrompts::createDefinitions(m_icons));
    if (m_server)
    {
        m_server->publishCatalog(m_catalog);
    }

    Q_EMIT catalogChanged(m_catalog->getGeneration());
}

void PDFAppMcpHost::checkToolRegistry()
{
    const quint64 generation = PDFDocumentToolRegistry::getGeneration();
    if (generation != m_registryGeneration)
    {
        m_registryGeneration = generation;
        rebuildCatalog();
    }
}

std::vector<PDFMcpIcon> PDFAppMcpHost::createIcons(const QString& iconName)
{
    std::vector<PDFMcpIcon> icons;
    for (PDFThemeColors::Scheme scheme : { PDFThemeColors::Scheme::Light, PDFThemeColors::Scheme::Dark })
    {
        QColor color;
        for (const PDFThemeColors& theme : PDFThemeColors::getBuiltinThemes())
        {
            if (theme.getScheme() == scheme)
            {
                color = theme.getColor(PDFThemeColors::AccentTealIcon);
            }
        }
        const QPixmap pixmap = PDFThemeIcons::getPixmap(iconName, QSizeF(48, 48), 1.0, color);
        if (pixmap.isNull())
        {
            continue;
        }

        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        if (!pixmap.save(&buffer, "PNG"))
        {
            continue;
        }

        PDFMcpIcon icon;
        icon.source = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(data.toBase64());
        icon.mimeType = QStringLiteral("image/png");
        icon.sizes = QStringList({ QStringLiteral("48x48") });
        icon.theme = scheme == PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
        icons.push_back(std::move(icon));
    }
    return icons;
}

PDFAppMcpClientInfo PDFAppMcpHost::getClientInfo(const PDFMcpClientContext& client)
{
    return PDFAppMcpClientNames::describe(client.clientName, client.clientTitle);
}

qint64 PDFAppMcpHost::recordActivity(const std::shared_ptr<PDFMcpCall>& call, const QString& action, const PDFAppDocument* document,
                                     const QJsonObject& detail, PDFMcpActivityStatus status, const std::set<int>& pages)
{
    if (!m_activityLog || !call)
    {
        return 0;
    }

    const PDFMcpClientContext& client = call->getRequest().client;
    PDFAppMcpActivityEntry entry;
    entry.assistantKey = client.assistantKey;
    entry.client = getClientInfo(client).displayName;
    entry.action = action;
    entry.document = document ? document->getTitle() : QString();
    entry.documentId = document ? document->getId() : QString();
    entry.pages = pages;
    entry.detail = detail;
    entry.detail.insert(QStringLiteral("callId"), call->getCallId());
    if (client.isRemote)
    {
        entry.detail.insert(QStringLiteral("address"), client.remoteAddress);
    }
    entry.status = status;
    return m_activityLog->record(entry);
}

void PDFAppMcpHost::finishDenied(const std::shared_ptr<PDFMcpCall>& call, const QString& actionId, const PDFAppDocument* document, PDFMcpCallResult result)
{
    if (!call)
    {
        return;
    }

    recordActivity(call, actionId, document, QJsonObject(), PDFMcpActivityStatus::DeniedPermission);
    call->finish(std::move(result));
}

bool PDFAppMcpHost::isDeniedAction(const std::shared_ptr<PDFMcpCall>& call, const QString& toolName, const QString& actionName) const
{
    if (!call || !PDFAppMcpHostHelper::isKnownAction(toolName, actionName))
    {
        return false;
    }

    const PDFAppMcpPermissionKind permission = PDFAppMcpHostHelper::getActionPermission(toolName, actionName);
    const bool hasOpenDocuments = !m_resolver.getDocuments().empty();
    return permission != PDFAppMcpPermissionKind::None && m_policy.evaluate(permission, call->getRequest().client.assistantKey, hasOpenDocuments) == PDFAppMcpPolicyResult::Denied;
}

void PDFAppMcpHost::finishWithError(const std::shared_ptr<PDFMcpCall>& call, const QString& action, const PDFAppDocument* document,
                                    const QString& text, PDFMcpActivityStatus status)
{
    if (!call)
    {
        return;
    }

    QJsonObject detail;
    if (status == PDFMcpActivityStatus::Failed)
    {
        detail.insert(QStringLiteral("reason"), PDFMcpJson::sanitizeString(text, PDFMcpJson::SanitizeFlag::RemoveBidi, 200));
    }
    recordActivity(call, action, document, detail, status);
    call->finish(PDFMcpCallResult::createToolError(text));
}

void PDFAppMcpHost::startJob(std::shared_ptr<PDFAppMcpJob> job)
{
    if (!job)
    {
        return;
    }

    if (m_isShutDown)
    {
        if (job->getCall())
        {
            job->getCall()->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping")));
        }
        return;
    }

    ++m_runningJobs;

    // A heavy job above the limit waits here: a worker blocked by it would hold
    // up the light jobs of every assistant behind it (review RC7-H)
    if (job->isHeavy())
    {
        if (m_runningHeavyJobs >= PDFMcpLimits::MAXIMUM_HEAVY_JOBS)
        {
            m_waitingHeavyJobs.push_back(std::move(job));
            return;
        }
        ++m_runningHeavyJobs;
    }
    m_threadPool.start(new PDFAppMcpJobRunnable(std::move(job), this));
}

void PDFAppMcpHost::startWaitingHeavyJobs()
{
    while (!m_isShutDown && !m_waitingHeavyJobs.empty() && m_runningHeavyJobs < PDFMcpLimits::MAXIMUM_HEAVY_JOBS)
    {
        std::shared_ptr<PDFAppMcpJob> job = std::move(m_waitingHeavyJobs.front());
        m_waitingHeavyJobs.pop_front();
        ++m_runningHeavyJobs;
        m_threadPool.start(new PDFAppMcpJobRunnable(std::move(job), this));
    }
}

void PDFAppMcpHost::onJobFinished(std::shared_ptr<PDFAppMcpJob> job)
{
    m_runningJobs = qMax(0, m_runningJobs - 1);
    if (!job)
    {
        return;
    }

    // The place of a heavy job goes to the next waiting one (a canceled call is
    // not executed by the worker - it ends at once)
    if (job->isHeavy())
    {
        m_runningHeavyJobs = qMax(0, m_runningHeavyJobs - 1);
        startWaitingHeavyJobs();
    }

    std::shared_ptr<PDFMcpCall> call = job->getCall();
    if (m_isShutDown)
    {
        if (call && !call->isFinished())
        {
            call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping")));
        }
        return;
    }

    job->finish(this);

    // A canceled call left open by its job (canceled before the job ran, and a
    // job, which does not answer a canceled call) is answered here - the
    // dispatcher would wait its grace time otherwise; every cancellation has its
    // activity row. A call, which is not canceled, may go on in another job or
    // a confirmation started by the job.
    if (call && !call->isFinished() && call->isCanceled())
    {
        recordActivity(call, PDFAppMcpHostHelper::getCallActionId(call->getRequest()), nullptr, QJsonObject(), PDFMcpActivityStatus::Canceled);
        call->finish(PDFAppMcpJobRunnable::createFailure(*call, QStringLiteral("The request was canceled.")));
    }
}

void PDFAppMcpHost::waitForJobs()
{
    m_threadPool.waitForDone();
}

void PDFAppMcpHost::setToolHandler(std::unique_ptr<PDFAppMcpToolHandler> handler)
{
    if (handler)
    {
        const QString name = handler->getToolName();
        m_handlers[name] = std::move(handler);
    }
}

PDFAppMcpToolHandler* PDFAppMcpHost::findToolHandler(const QString& toolName) const
{
    auto it = m_handlers.find(toolName);
    if (it != m_handlers.end())
    {
        return it->second.get();
    }
    return PDFAppMcpCatalogTools::isCatalogToolName(toolName) ? m_catalogToolHandler.get() : nullptr;
}

void PDFAppMcpHost::setConfirmationProvider(PDFAppMcpConfirmationProvider* provider)
{
    m_confirmationProvider = provider;
}

PDFMcpActivityStatus PDFAppMcpHost::getActivityStatus(PDFAppMcpDecision decision)
{
    switch (decision)
    {
        case PDFAppMcpDecision::Allowed:
            return PDFMcpActivityStatus::Allowed;
        case PDFAppMcpDecision::DeniedUser:
            return PDFMcpActivityStatus::DeniedUser;
        case PDFAppMcpDecision::DeniedTimeout:
        case PDFAppMcpDecision::DeniedQueueTimeout:
            return PDFMcpActivityStatus::DeniedTimeout;
        case PDFAppMcpDecision::DeniedClosed:
            return PDFMcpActivityStatus::DeniedClosed;
        case PDFAppMcpDecision::DeniedDisconnected:
            return PDFMcpActivityStatus::DeniedDisconnected;
        case PDFAppMcpDecision::DeniedStopped:
            return PDFMcpActivityStatus::DeniedStopped;
        case PDFAppMcpDecision::Canceled:
            return PDFMcpActivityStatus::Canceled;
        case PDFAppMcpDecision::Unavailable:
            return PDFMcpActivityStatus::Failed;
    }
    return PDFMcpActivityStatus::Failed;
}

quint64 PDFAppMcpHost::requestConfirmation(std::shared_ptr<PDFAppMcpConfirmation> confirmation)
{
    if (!confirmation)
    {
        return 0;
    }

    confirmation->id = m_nextConfirmationId++;
    confirmation->askedAt = QDateTime::currentMSecsSinceEpoch();
    if (confirmation->call)
    {
        const PDFMcpClientContext& client = confirmation->call->getRequest().client;
        confirmation->assistantKey = client.assistantKey;
        confirmation->clientName = getClientInfo(client).displayName;
        confirmation->clientNameRaw = client.clientName;
        confirmation->remoteAddress = client.remoteAddress;
        confirmation->isRemote = client.isRemote;
    }
    confirmation->activityId = recordActivity(confirmation->call, confirmation->actionId.isEmpty() ? QStringLiteral("document.get_info") : confirmation->actionId,
                                              confirmation->document, QJsonObject(), PDFMcpActivityStatus::Waiting);

    const quint64 id = confirmation->id;
    m_confirmations[id] = confirmation;

    // A slot may delete the host (its controller): the shutdown has denied the
    // request then. A slot may also close the tab of the document (or the window):
    // the interface would take the request for one without a document then and
    // let it wait until its timeout (UnitTestsAppMcpLifetime)
    const bool hadDocument = confirmation->document != nullptr;
    QPointer<PDFAppMcpHost> self(this);
    Q_EMIT confirmationsChanged();
    if (!self)
    {
        return id;
    }

    if (hadDocument && !confirmation->document)
    {
        QMetaObject::invokeMethod(this, &PDFAppMcpHost::resolveConfirmation, Qt::QueuedConnection, id, PDFAppMcpDecision::DeniedClosed);
    }
    else if (m_isShutDown)
    {
        // A host shut down already (also by a slot of the signal above) is deleted
        // by its controller before the event loop runs again: a queued decision
        // would be dropped and the receiver, which keeps itself alive while it
        // waits, would never end - the request ends now (review RT7, item 45)
        resolveConfirmation(id, PDFAppMcpDecision::DeniedStopped);
    }
    else if (m_confirmationProvider)
    {
        m_confirmationProvider->showConfirmation(confirmation);
    }
    else
    {
        // No user interface: the request ends from the event loop
        QMetaObject::invokeMethod(this, &PDFAppMcpHost::resolveConfirmation, Qt::QueuedConnection, id, PDFAppMcpDecision::Unavailable);
    }
    return id;
}

void PDFAppMcpHost::resolveConfirmation(quint64 confirmationId, PDFAppMcpDecision decision)
{
    auto it = m_confirmations.find(confirmationId);
    if (it == m_confirmations.end())
    {
        return;
    }

    std::shared_ptr<PDFAppMcpConfirmation> confirmation = std::move(it->second);
    m_confirmations.erase(it);

    if (m_activityLog && confirmation->activityId > 0)
    {
        // The bound of the queue is told apart from the 2 minutes of a shown
        // dialog by the reason of the row (no new stored status - item 59)
        const QJsonObject detail = decision == PDFAppMcpDecision::DeniedQueueTimeout ? QJsonObject({ { QStringLiteral("reason"), PDFAppMcpActivity::getQueueTimeoutReason() } }) : QJsonObject();
        m_activityLog->update(confirmation->activityId, getActivityStatus(decision), detail);
    }

    if (decision == PDFAppMcpDecision::Allowed && confirmation->permissions.testFlag(PDFAppMcpPermissionKind::Read))
    {
        m_policy.approveRead(confirmation->assistantKey);
    }

    // A slot may delete the host (its controller), and a shut down host is deleted
    // by the controller before the event loop runs again: a queued delivery would
    // be dropped then and the receiver, which keeps itself alive while it waits,
    // would never end (a leak of the receiver and of its call). The receiver learns
    // the decision at once in both cases - its pointer to the host is null or the
    // host refuses further work (UnitTestsAppMcpLifetime).
    QPointer<PDFAppMcpHost> self(this);
    Q_EMIT confirmationsChanged();
    if (!self || m_isShutDown)
    {
        PDFAppMcpHostHelper::deliverDecision(confirmation, decision);
        return;
    }

    // The receiver learns the decision from the event loop (never inside of the
    // code of the provider, which may still use its dialog); the shutdown delivers
    // a decision, whose delivery has not run yet (the host is deleted before the
    // event loop runs again)
    m_undeliveredDecisions[confirmation->id] = std::make_pair(confirmation, decision);
    QMetaObject::invokeMethod(this, &PDFAppMcpHost::deliverDecision, Qt::QueuedConnection, confirmation, decision);
}

void PDFAppMcpHost::deliverDecision(std::shared_ptr<PDFAppMcpConfirmation> confirmation, PDFAppMcpDecision decision)
{
    // Delivered by the shutdown already: never twice
    if (!confirmation || m_undeliveredDecisions.erase(confirmation->id) == 0)
    {
        return;
    }
    PDFAppMcpHostHelper::deliverDecision(confirmation, decision);
}

std::vector<std::shared_ptr<PDFAppMcpConfirmation>> PDFAppMcpHost::getPendingConfirmations() const
{
    std::vector<std::shared_ptr<PDFAppMcpConfirmation>> confirmations;
    for (const auto& item : m_confirmations)
    {
        confirmations.push_back(item.second);
    }
    return confirmations;
}

bool PDFAppMcpHost::isConfirmationQueueFull(const PDFAppDocument* document) const
{
    if (!document)
    {
        return false;
    }

    int count = 0;
    for (const auto& item : m_confirmations)
    {
        if (item.second && item.second->document.data() == document)
        {
            ++count;
        }
    }
    return count >= MAXIMUM_CONFIRMATIONS_PER_TAB;
}

QString PDFAppMcpHost::getConfirmationQueueFullText()
{
    return QStringLiteral("Too many requests are waiting for the user in this document (at most %1). Wait for the answers; send fewer calls with more items each.")
        .arg(MAXIMUM_CONFIRMATIONS_PER_TAB);
}

void PDFAppMcpHost::endConfirmations(const QString& assistantKey, const std::shared_ptr<PDFMcpCall>& call, PDFAppMcpDecision decision)
{
    std::vector<quint64> ids;
    for (const auto& item : m_confirmations)
    {
        const bool isMatch = (!call && assistantKey.isEmpty()) || (call && item.second->call == call) || (!assistantKey.isEmpty() && item.second->assistantKey == assistantKey);
        if (isMatch)
        {
            ids.push_back(item.first);
        }
    }

    for (quint64 id : ids)
    {
        if (m_confirmationProvider)
        {
            m_confirmationProvider->withdrawConfirmation(id, decision);
        }
        resolveConfirmation(id, decision);
    }
}

void PDFAppMcpHost::shutdown()
{
    if (m_isShutDown)
    {
        return;
    }

    m_isShutDown = true;
    if (m_registryTimer)
    {
        m_registryTimer->stop();
    }

    // Step 3: pending confirmations end as denied_stopped; step 6: the jobs
    endConfirmations(QString(), nullptr, PDFAppMcpDecision::DeniedStopped);

    // The decisions resolved before, whose queued delivery has not run yet: the
    // controller deletes the host before the event loop runs again, a receiver
    // would wait for ever (it keeps itself alive while it waits - a leak)
    while (!m_undeliveredDecisions.empty())
    {
        const std::pair<std::shared_ptr<PDFAppMcpConfirmation>, PDFAppMcpDecision> item = m_undeliveredDecisions.begin()->second;
        m_undeliveredDecisions.erase(m_undeliveredDecisions.begin());
        PDFAppMcpHostHelper::deliverDecision(item.first, item.second);
    }

    // The heavy jobs waiting for a place never run
    std::deque<std::shared_ptr<PDFAppMcpJob>> waitingHeavyJobs = std::move(m_waitingHeavyJobs);
    m_waitingHeavyJobs.clear();
    for (const std::shared_ptr<PDFAppMcpJob>& job : waitingHeavyJobs)
    {
        m_runningJobs = qMax(0, m_runningJobs - 1);
        const std::shared_ptr<PDFMcpCall> call = job ? job->getCall() : std::shared_ptr<PDFMcpCall>();
        if (call && !call->isFinished())
        {
            call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping")));
        }
    }
    waitForJobs();

    if (m_activityLog)
    {
        m_activityLog->flush();
    }
}

void PDFAppMcpHost::handleCall(std::shared_ptr<PDFMcpCall> call)
{
    if (!call || call->isFinished())
    {
        return;
    }

    if (m_isShutDown)
    {
        call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping")));
        return;
    }

    call->setHost(this);
    switch (call->getRequest().kind)
    {
        case PDFMcpCallKind::ToolCall:
            handleToolCall(call);
            break;
        case PDFMcpCallKind::ResourceRead:
            PDFAppMcpResources::readDocumentResource(this, call);
            break;
        case PDFMcpCallKind::PromptGet:
            PDFAppMcpPrompts::getPrompt(this, call);
            break;
        case PDFMcpCallKind::Completion:
            PDFAppMcpCompletion::complete(this, call);
            break;
    }
}

void PDFAppMcpHost::handleToolCall(const std::shared_ptr<PDFMcpCall>& call)
{
    checkToolRegistry();

    const PDFMcpCallRequest& request = call->getRequest();
    const QString toolName = request.name;
    const PDFMcpToolDefinition* definition = m_catalog ? m_catalog->findTool(toolName) : nullptr;
    auto schemaIt = m_inputSchemas.find(toolName);
    const QString requestedAction = request.arguments.value(QStringLiteral("action")).toString().left(64);
    if (!definition || schemaIt == m_inputSchemas.end())
    {
        // A tool, whose every action is denied, is not offered: the same answer
        // as an unknown tool, but the denial is audited (plan 7)
        if (isDeniedAction(call, toolName, requestedAction))
        {
            finishDenied(call, toolName + QLatin1Char('.') + requestedAction, PDFAppMcpHostHelper::findArgumentDocument(m_resolver, request.arguments),
                         PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Unknown tool: %1").arg(PDFMcpJson::sanitizeString(toolName, PDFMcpJson::SanitizeFlag::RemoveBidi, 64))));
            return;
        }

        // A tool of the catalog, which was removed meanwhile: an execution error (3.10)
        if (PDFAppMcpCatalogTools::isCatalogToolName(toolName))
        {
            finishWithError(call, toolName, nullptr, QStringLiteral("This tool is not available. Call tools/list for the current tools."));
            return;
        }
        call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Unknown tool: %1").arg(PDFMcpJson::sanitizeString(toolName, PDFMcpJson::SanitizeFlag::RemoveBidi, 64))));
        return;
    }

    // An action, which is not offered (unknown or denied), is an unknown action (1.4)
    const QJsonValue actionValue = request.arguments.value(QStringLiteral("action"));
    const PDFMcpSchema* actionSchema = schemaIt->second.findProperty(QStringLiteral("action"));
    if (!actionValue.isString() || !actionSchema || !actionSchema->getEnum().contains(actionValue.toString()))
    {
        PDFMcpCallResult result = PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS,
                                                                QStringLiteral("Unknown action of the tool %1; the actions are: %2").arg(toolName, actionSchema ? actionSchema->getEnum().join(QStringLiteral(", ")) : QString()));
        if (isDeniedAction(call, toolName, requestedAction))
        {
            finishDenied(call, toolName + QLatin1Char('.') + requestedAction, PDFAppMcpHostHelper::findArgumentDocument(m_resolver, request.arguments), std::move(result));
            return;
        }
        call->finish(std::move(result));
        return;
    }
    const QString actionName = actionValue.toString();

    // Input validation errors are execution errors (SEP-1303)
    QStringList errors;
    if (!schemaIt->second.validate(request.arguments, QStringLiteral("arguments"), &errors))
    {
        finishWithError(call, toolName + QLatin1Char('.') + actionName, nullptr,
                        QStringLiteral("Invalid arguments: %1. See pdf4qt://docs/tools/%2.md").arg(errors.join(QStringLiteral("; ")), toolName));
        return;
    }

    // The permission of the action, checked again at the call (the catalog may be older)
    const PDFAppMcpPermissionKind permission = PDFAppMcpHostHelper::getActionPermission(toolName, actionName);

    const bool hasOpenDocuments = !m_resolver.getDocuments().empty();
    if (permission != PDFAppMcpPermissionKind::None && m_policy.evaluate(permission, request.client.assistantKey, hasOpenDocuments) == PDFAppMcpPolicyResult::Denied)
    {
        finishDenied(call, toolName + QLatin1Char('.') + actionName, PDFAppMcpHostHelper::findArgumentDocument(m_resolver, request.arguments),
                     PDFMcpCallResult::createToolError(QStringLiteral("The user does not allow this action in PDF4QT (its permission is set to Deny on the AI page).")));
        return;
    }

    // The read approval [D2]: once per assistant while a document is open; Edit
    // and Files are confirmed by the handlers with the data of the change - a
    // change, which needs the read approval too, is one dialog with both
    // reasons (958:28030; the handlers of package C ask Read with Edit)
    const bool isCatalogTool = PDFAppMcpCatalogTools::isCatalogToolName(toolName);
    if (m_policy.getKindsToConfirm(PDFAppMcpPermissionKind::Read, request.client.assistantKey, hasOpenDocuments).testFlag(PDFAppMcpPermissionKind::Read) &&
        (permission == PDFAppMcpPermissionKind::Read || (permission == PDFAppMcpPermissionKind::Edit && isCatalogTool)))
    {
        std::shared_ptr<PDFAppMcpReadApproval> receiver = std::make_shared<PDFAppMcpReadApproval>(this, call, toolName, actionName, request.arguments);

        std::shared_ptr<PDFAppMcpConfirmation> confirmation = std::make_shared<PDFAppMcpConfirmation>();
        confirmation->kind = PDFAppMcpConfirmationKind::ReadDocuments;
        confirmation->permissions = PDFAppMcpPermissionKind::Read;
        confirmation->call = call;
        confirmation->document = m_resolver.getActiveDocument();
        confirmation->actionId = toolName + QLatin1Char('.') + actionName;
        confirmation->receiver = receiver;
        confirmation->receiverHolder = receiver;
        requestConfirmation(confirmation);
        return;
    }

    dispatchToolCall(call, toolName, actionName, request.arguments, false);
}

void PDFAppMcpHost::continueToolCall(const std::shared_ptr<PDFMcpCall>& call, const QString& toolName, const QString& actionName, const QJsonObject& arguments)
{
    if (!call || call->isFinished())
    {
        return;
    }

    // A call canceled (or a server stopped) after the decision was taken and
    // before it was delivered: no confirmation is left to end it, so it is
    // answered here - the dispatcher would wait its grace time otherwise, and
    // every cancellation has its activity row (review RC7-H)
    if (m_isShutDown)
    {
        call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping")));
        return;
    }
    if (call->isCanceled())
    {
        recordActivity(call, toolName + QLatin1Char('.') + actionName, nullptr, QJsonObject(), PDFMcpActivityStatus::Canceled);
        call->finish(PDFAppMcpJobRunnable::createFailure(*call, QStringLiteral("The request was canceled.")));
        return;
    }

    // The permission may have been set to Deny while the question waited (plan 7:
    // a change during a call is an execution error)
    const PDFAppMcpPermissionKind permission = PDFAppMcpHostHelper::getActionPermission(toolName, actionName);
    if (permission != PDFAppMcpPermissionKind::None && m_policy.evaluate(permission, call->getRequest().client.assistantKey, true) == PDFAppMcpPolicyResult::Denied)
    {
        finishDenied(call, toolName + QLatin1Char('.') + actionName, PDFAppMcpHostHelper::findArgumentDocument(m_resolver, arguments),
                     PDFMcpCallResult::createToolError(QStringLiteral("The user does not allow this action in PDF4QT (its permission is set to Deny on the AI page).")));
        return;
    }
    dispatchToolCall(call, toolName, actionName, arguments, true);
}

void PDFAppMcpHost::dispatchToolCall(const std::shared_ptr<PDFMcpCall>& call, const QString& toolName, const QString& actionName,
                                     const QJsonObject& arguments, bool isReadApproved)
{
    Q_UNUSED(isReadApproved);

    PDFAppMcpToolHandler* handler = findToolHandler(toolName);
    if (!handler)
    {
        call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Unknown tool: %1").arg(toolName)));
        return;
    }

    PDFAppMcpToolInvocation invocation;
    invocation.call = call;
    invocation.host = this;
    invocation.toolName = toolName;
    invocation.actionName = actionName;
    invocation.arguments = arguments;
    invocation.actionArguments = arguments.value(actionName).toObject();
    invocation.documentId = arguments.value(QStringLiteral("documentId")).toString();
    invocation.tool = PDFAppMcpToolSet::findTool(toolName);
    invocation.action = invocation.tool ? invocation.tool->findAction(actionName) : nullptr;
    handler->handle(invocation);
}

void PDFAppMcpHost::callCanceled(std::shared_ptr<PDFMcpCall> call)
{
    if (!call)
    {
        return;
    }

    PDFAppMcpDecision decision = PDFAppMcpDecision::Canceled;
    switch (call->getCancelReason())
    {
        case PDFMcpCancelReason::Disconnected:
            decision = PDFAppMcpDecision::DeniedDisconnected;
            break;
        case PDFMcpCancelReason::ServerStopped:
            decision = PDFAppMcpDecision::DeniedStopped;
            break;
        default:
            break;
    }
    endConfirmations(QString(), call, decision);
}

void PDFAppMcpHost::assistantEvent(const PDFMcpAssistantEvent& event)
{
    switch (event.kind)
    {
        case PDFMcpAssistantEvent::Kind::Connected:
            break;
        case PDFMcpAssistantEvent::Kind::Idle:
        case PDFMcpAssistantEvent::Kind::Disconnected:
        case PDFMcpAssistantEvent::Kind::SessionEnded:
            // The read approval lasts until the assistant goes idle or is disconnected [D2]
            m_policy.forgetRead(event.key);
            if (!event.key.isEmpty())
            {
                endConfirmations(event.key, nullptr, PDFAppMcpDecision::DeniedDisconnected);
            }
            break;
    }
}

}   // namespace pdf
