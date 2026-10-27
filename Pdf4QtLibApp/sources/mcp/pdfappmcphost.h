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

#ifndef PDFAPPMCPHOST_H
#define PDFAPPMCPHOST_H

#include "pdfappglobal.h"
#include "pdfappmcpactivity.h"
#include "pdfappmcpclientnames.h"
#include "pdfappmcpconfirmation.h"
#include "pdfappmcpdocumentresolver.h"
#include "pdfappmcppermissions.h"
#include "pdfappmcptoolhandler.h"

#include "pdfmcpcatalog.h"
#include "pdfmcphost.h"
#include "pdfmcpschema.h"

#include <QPointer>
#include <QThreadPool>

#include <deque>
#include <map>
#include <memory>
#include <vector>

class QTimer;

namespace pdf
{

class PDFAppContext;
class PDFAppMcpJob;
class PDFMcpServer;

/// The host of the MCP server in the GUI thread (plan 2.3): the permission
/// policy and the read approvals [D2], the resolution of the documents, the
/// tool handlers, the confirmation queue (the provider of package C), the
/// activity rows (batched) and the pool of the MCP workers (max(2, cores / 2)).
/// It builds the catalog of the server from the contract (PDFAppMcpToolSet),
/// the tools of the catalog (PDFAppMcpCatalogTools), the resources and the
/// prompts, and publishes a new one when the permissions or the registry of
/// the tools change. The server reaches it only by queued invocations; it is
/// destroyed only after the server has stopped (plan 2.6).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpHost : public PDFMcpHost
{
    Q_OBJECT

private:
    using BaseClass = PDFMcpHost;

public:
    explicit PDFAppMcpHost(PDFAppContext* context, QObject* parent = nullptr);
    virtual ~PDFAppMcpHost() override;

    PDFAppContext* getContext() const;

    // Permissions -----------------------------------------------------------

    const PDFAppMcpPermissions& getPermissions() const { return m_policy.getPermissions(); }

    /// New permissions: a new catalog (list_changed) when they differ
    void setPermissions(const PDFAppMcpPermissions& permissions);

    PDFAppMcpPermissionPolicy& getPolicy() { return m_policy; }
    const PDFAppMcpPermissionPolicy& getPolicy() const { return m_policy; }

    // Catalog ---------------------------------------------------------------

    /// The server, which gets the catalogs (nullptr = none)
    void setServer(PDFMcpServer* server);

    /// Icons of the tools and the server (rendered by createIcons)
    void setIcons(std::vector<PDFMcpIcon> icons);
    const std::vector<PDFMcpIcon>& getIcons() const { return m_icons; }

    /// Builds and publishes a new catalog (a new generation)
    void rebuildCatalog();

    std::shared_ptr<const PDFMcpCatalog> getCatalog() const { return m_catalog; }
    quint64 getCatalogGeneration() const;

    /// Rebuilds the catalog, when the registry of the tools has changed (called
    /// by a 1 s timer and at each tools/call)
    void checkToolRegistry();

    /// Data URI icons of the light and the dark theme (48 px PNG, plan 1.4)
    static std::vector<PDFMcpIcon> createIcons(const QString& iconName);

    // Services for the handlers ---------------------------------------------

    const PDFAppMcpDocumentResolver& getResolver() const { return m_resolver; }
    PDFAppMcpActivityLog* getActivityLog() const { return m_activityLog; }

    /// Presentation of the client of a call
    static PDFAppMcpClientInfo getClientInfo(const PDFMcpClientContext& client);

    /// Records an activity row of the call (0 = not logged)
    qint64 recordActivity(const std::shared_ptr<PDFMcpCall>& call, const QString& action, const PDFAppDocument* document,
                          const QJsonObject& detail, PDFMcpActivityStatus status, const std::set<int>& pages = std::set<int>());

    /// Finishes a call refused by a Deny permission by \p text (an execution
    /// error of a tool, a protocol error of a resource) and records its audit
    /// row denied_permission (plan 7: every denial; repeated denials of the same
    /// assistant / action / document within 60 s are one row)
    void finishDenied(const std::shared_ptr<PDFMcpCall>& call, const QString& actionId, const PDFAppDocument* document, PDFMcpCallResult result);

    /// The action exists and its permission is Deny for the assistant of the call
    bool isDeniedAction(const std::shared_ptr<PDFMcpCall>& call, const QString& toolName, const QString& actionName) const;

    /// Finishes the call by an execution error and records a failed activity row
    /// (\p status Failed, DeniedLocked, DeniedBusy, ...)
    void finishWithError(const std::shared_ptr<PDFMcpCall>& call, const QString& action, const PDFAppDocument* document,
                         const QString& text, PDFMcpActivityStatus status = PDFMcpActivityStatus::Failed);

    /// Starts a job in the pool of the MCP workers (refused after shutdown: the
    /// call fails). A heavy job above PDFMcpLimits::MAXIMUM_HEAVY_JOBS waits in
    /// the host, not in a worker - the other jobs never wait for the heavy ones.
    void startJob(std::shared_ptr<PDFAppMcpJob> job);

    /// Jobs started and not yet finished in the GUI thread (also the waiting heavy jobs)
    int getRunningJobCount() const { return m_runningJobs; }

    /// Heavy jobs in the pool / waiting for a place (tests)
    int getRunningHeavyJobCount() const { return m_runningHeavyJobs; }
    int getWaitingHeavyJobCount() const { return int(m_waitingHeavyJobs.size()); }

    QThreadPool* getThreadPool() { return &m_threadPool; }

    /// Waits for every job (step 6 of the quit order); the calls are canceled before
    void waitForJobs();

    // Tool handlers ---------------------------------------------------------

    /// Installs the handler of its tool (replaces the previous one)
    void setToolHandler(std::unique_ptr<PDFAppMcpToolHandler> handler);
    PDFAppMcpToolHandler* findToolHandler(const QString& toolName) const;

    // Confirmations ---------------------------------------------------------

    /// The user interface of the confirmations (package C; nullptr = none, every
    /// confirmation ends as Unavailable). Not owned.
    void setConfirmationProvider(PDFAppMcpConfirmationProvider* provider);
    PDFAppMcpConfirmationProvider* getConfirmationProvider() const { return m_confirmationProvider; }

    /// Queues a confirmation: a waiting activity row, the provider shows it; the
    /// receiver gets the decision from the event loop. Returns its id.
    quint64 requestConfirmation(std::shared_ptr<PDFAppMcpConfirmation> confirmation);

    /// The decision of a confirmation (the provider, a timeout, a closed tab)
    void resolveConfirmation(quint64 confirmationId, PDFAppMcpDecision decision);

    std::vector<std::shared_ptr<PDFAppMcpConfirmation>> getPendingConfirmations() const;

    /// At most this many requests wait for the user in one tab (plan 2.5 / 7)
    static constexpr int MAXIMUM_CONFIRMATIONS_PER_TAB = 16;

    /// The queue of the confirmations of the document is full: a new request,
    /// which needs a confirmation, is refused (getConfirmationQueueFullText)
    bool isConfirmationQueueFull(const PDFAppDocument* document) const;

    /// Text of a request refused by a full queue (docs/errors.md)
    static QString getConfirmationQueueFullText();

    /// Activity status of a decision
    static PDFMcpActivityStatus getActivityStatus(PDFAppMcpDecision decision);

    // Lifetime --------------------------------------------------------------

    /// Stop of the server (steps 3 and 6 of plan 2.6): pending confirmations end
    /// as DeniedStopped, the jobs are waited for, the activity is written. New
    /// calls are refused afterwards.
    void shutdown();
    bool isShutDown() const { return m_isShutDown; }

    // PDFMcpHost ------------------------------------------------------------

    virtual void handleCall(std::shared_ptr<pdf::PDFMcpCall> call) override;
    virtual void callCanceled(std::shared_ptr<pdf::PDFMcpCall> call) override;
    virtual void assistantEvent(const pdf::PDFMcpAssistantEvent& event) override;

    /// A job has ended (queued from the worker)
    void onJobFinished(std::shared_ptr<pdf::PDFAppMcpJob> job);

    /// Continues a tools/call after its read approval (the dispatch to the handler)
    void continueToolCall(const std::shared_ptr<PDFMcpCall>& call, const QString& toolName, const QString& actionName, const QJsonObject& arguments);

Q_SIGNALS:
    /// A new catalog was published
    void catalogChanged(quint64 generation);

    /// The pending confirmations changed (the status items, the banner)
    void confirmationsChanged();

private:
    void handleToolCall(const std::shared_ptr<PDFMcpCall>& call);
    void dispatchToolCall(const std::shared_ptr<PDFMcpCall>& call, const QString& toolName, const QString& actionName,
                          const QJsonObject& arguments, bool isReadApproved);
    void deliverDecision(std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation, pdf::PDFAppMcpDecision decision);
    void endConfirmations(const QString& assistantKey, const std::shared_ptr<PDFMcpCall>& call, PDFAppMcpDecision decision);

    /// Starts the waiting heavy jobs, which have a place now
    void startWaitingHeavyJobs();

    QPointer<PDFAppContext> m_context;
    QPointer<PDFMcpServer> m_server;
    PDFAppMcpPermissionPolicy m_policy;
    PDFAppMcpDocumentResolver m_resolver;
    QPointer<PDFAppMcpActivityLog> m_activityLog;
    std::shared_ptr<const PDFMcpCatalog> m_catalog;
    std::map<QString, PDFMcpSchema> m_inputSchemas;
    std::vector<PDFMcpIcon> m_icons;
    quint64 m_generation = 0;
    quint64 m_registryGeneration = 0;
    QPointer<QTimer> m_registryTimer;
    std::map<QString, std::unique_ptr<PDFAppMcpToolHandler>> m_handlers;
    std::unique_ptr<PDFAppMcpToolHandler> m_catalogToolHandler;
    PDFAppMcpConfirmationProvider* m_confirmationProvider = nullptr;
    std::map<quint64, std::shared_ptr<PDFAppMcpConfirmation>> m_confirmations;
    std::map<quint64, std::pair<std::shared_ptr<PDFAppMcpConfirmation>, PDFAppMcpDecision>> m_undeliveredDecisions;   ///< Resolved, the delivery queued (the shutdown delivers them)
    quint64 m_nextConfirmationId = 1;
    QThreadPool m_threadPool;
    std::deque<std::shared_ptr<PDFAppMcpJob>> m_waitingHeavyJobs;   ///< Heavy jobs above MAXIMUM_HEAVY_JOBS (never a blocked worker)
    int m_runningHeavyJobs = 0;
    int m_runningJobs = 0;
    bool m_isShutDown = false;
};

}   // namespace pdf

#endif // PDFAPPMCPHOST_H
