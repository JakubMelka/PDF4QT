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

#ifndef PDFMCPDISPATCHER_H
#define PDFMCPDISPATCHER_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcpassistants.h"
#include "pdfmcpcall.h"
#include "pdfmcpcatalog.h"
#include "pdfmcpratelimiter.h"
#include "pdfmcprequeststate.h"
#include "pdfmcpsessions.h"
#include "pdfmcpsubscriptions.h"
#include "pdfmcptasks.h"

#include <QJsonObject>
#include <QMutex>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace pdf
{

class PDFMcpDispatcher;
class PDFMcpExchange;
class PDFMcpHttpRequest;
class PDFMcpServer;
class PDFMcpServerShared;

/// Thread safe link of one run of the server to its dispatcher (plan 2.4):
/// the connections (I/O threads) hand their requests over, the server (GUI
/// thread) its notifications; everything reaches the dispatcher by queued
/// invocations while it lives (setDispatcher(nullptr) at its stop - then
/// submit refuses and the connection answers 503 itself). It also keeps the
/// connections of each remote address and the snapshot of the assistants for
/// PDFMcpServer::getAssistants.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpDispatcherLink
{
public:
    PDFMcpDispatcherLink() = default;
    PDFMcpDispatcherLink(const PDFMcpDispatcherLink&) = delete;
    PDFMcpDispatcherLink& operator=(const PDFMcpDispatcherLink&) = delete;

    /// The dispatcher of the run (control thread), nullptr at its stop
    void setDispatcher(PDFMcpDispatcher* dispatcher);

    /// The server of the run (GUI thread; it outlives the run)
    void setServer(PDFMcpServer* server);

    /// A request of a connection (false = no dispatcher)
    bool submit(std::shared_ptr<PDFMcpExchange> exchange);

    /// The connection of an exchange has gone
    void exchangeClosed(std::shared_ptr<PDFMcpExchange> exchange);

    void catalogPublished();
    void resourceUpdated(const QString& uri);
    void disconnectAssistant(const QString& key);
    void disconnectAll();
    void checkTimeouts();

    /// Counts a connection of a remote address (false = \p limit reached)
    bool acquireAddress(const QString& address, int limit);
    void releaseAddress(const QString& address);

    /// Snapshot of the assistants (\p notify emits PDFMcpServer::assistantsChanged)
    void setAssistants(std::vector<PDFMcpAssistantInfo> assistants, bool notify);
    std::vector<PDFMcpAssistantInfo> getAssistants() const;

private:
    mutable QMutex m_mutex;
    PDFMcpDispatcher* m_dispatcher = nullptr;
    PDFMcpServer* m_server = nullptr;
    std::map<QString, int> m_addresses;
    std::vector<PDFMcpAssistantInfo> m_assistants;
};

/// Dispatcher of the control thread (plan 1, 2.2): the protocol of both eras
/// [D1] above the HTTP transport. For each request it checks the target, the
/// Host and Origin (403), the methods (405), the access token (401, 429 after
/// 20 failures of an address), the media types (406, 415), the JSON (scanner,
/// then QJsonDocument - a document refused only by Qt is -32700), classifies
/// the era by the body, checks the headers of the modern era (-32020, -32022,
/// -32602 of a missing _meta), the rate limits (429, -31001) and answers the
/// list methods from the catalog snapshot, server/discover, initialize, ping,
/// the subscriptions (listen streams of the modern era, the GET streams and
/// resources/subscribe of the legacy sessions), the tasks [D3], the
/// sealing of MRTR and the legacy elicitation [D4], and routes tools/call,
/// prompts/get, the document resources/read and completion/complete to the
/// host as PDFMcpCall. A result ready within 1 s is application/json, a later
/// one (or one after a progress or log notification) an SSE stream with
/// keep-alive comments; a client with the tasks extension gets a task after 1
/// s instead. Closing the stream (the socket) cancels the call.
///
/// Notes of the specification followed where its pages differ:
/// - the server tears a subscription down by its final result
///   {resultType: complete, _meta: {subscriptionId}} on the HTTP transport
///   (Subscriptions page, schema.ts); the server notifications/cancelled of
///   the Cancellation page is the rule of stdio
///   (https://modelcontextprotocol.io/specification/2026-07-28/basic/patterns/subscriptions);
/// - the legacy era: a closed POST stream also cancels its call (no
///   resumability, so the result could not be delivered) - a deliberate
///   deviation from "Disconnection SHOULD NOT be interpreted as the client
///   cancelling its request" of 2025-11-25.
///
/// The answers of the elicitations reach the host verified: the sealed
/// requestState of a modern retry (assistant, method, name, arguments,
/// expiry) or the state of a legacy call; PDFMcpCallRequest::inputResponses
/// contains only the accepted answers, whose values are allowed by
/// PDFMcpInputRequests::allowedValues, in the form of MRTR
/// {"<key>": {"action": "accept", "content": {...}}}; requestState is
/// {"allowedValues": ...} of a verified retry. allowedValues of an input
/// request: {"<key>": [values]} (every property of the answer one of the
/// values) or {"<key>": {"<property>": [values] | "any"}} ("any": a string,
/// number or boolean, strings sanitized). An answer outside of the allowed
/// values is dropped - the host asks again. A declined or canceled choice
/// ends the call ("The user declined ...").
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpDispatcher : public PDFMcpCallSink
{
    Q_OBJECT

private:
    using BaseClass = PDFMcpCallSink;

public:
    PDFMcpDispatcher(std::shared_ptr<PDFMcpServerShared> shared, QObject* parent);
    virtual ~PDFMcpDispatcher() override;

    /// The bound port (the allowed Host values)
    void setPort(quint16 port);

    /// Stop of the server (steps 2-4 of plan 2.6: mark stopping, cancel every
    /// call with ServerStopped and answer it by -31002, end the listen streams
    /// gracefully, close the GET streams, end the sessions)
    void shutdown();

    // Queued invocations of the link ----------------------------------------

    void handleExchange(std::shared_ptr<pdf::PDFMcpExchange> exchange);
    void handleExchangeClosed(std::shared_ptr<pdf::PDFMcpExchange> exchange);
    void handleCatalogPublished();
    void handleResourceUpdated(QString uri);
    void handleDisconnectAssistant(QString key);
    void handleDisconnectAll();
    void handleMaintenance();

    virtual void processCall(std::shared_ptr<pdf::PDFMcpCall> call) override;

    // Pure helpers (tests) ---------------------------------------------------

    /// Inserts serialized members before the closing brace of a serialized object
    static QByteArray appendMembers(QByteArrayView object, QByteArrayView members);

    /// The Accept header lists the media types (also by wildcards)
    static bool isAcceptable(QByteArrayView accept, bool needsJson, bool needsEventStream);

    /// Content-Type application/json (with an optional charset UTF-8)
    static bool isJsonContentType(QByteArrayView contentType);

    /// Value of Mcp-Name: plain visible ASCII, or the base64 sentinel
    /// "=?base64?...?=" of UTF-8; std::nullopt for an invalid value
    static std::optional<QString> decodeHeaderValue(QByteArrayView value);

    /// The Origin is a loopback origin (http(s)://127.0.0.1 | localhost | [::1], any port)
    static bool isLoopbackOrigin(QByteArrayView origin);

    /// The request has exactly one "Authorization: Bearer <token>" with the token,
    /// whose SHA-256 is \p tokenHash (compared in constant time; an empty hash
    /// accepts nothing). Also the connection uses it - a request with a wrong
    /// token is answered before its body is read.
    static bool isAuthorizationValid(const PDFMcpHttpRequest& request, const QByteArray& tokenHash);

    /// Cursor of a list (base64url of "<generation>:<offset>") and its parse
    static QString createCursor(quint64 generation, qint64 offset);
    static std::optional<qint64> parseCursor(const QString& cursor);

    /// The accepted answers of elicitations, whose values are allowed (see the
    /// class description); \p isDeclined = a choice was declined or canceled
    static QJsonObject filterInputResponses(const QJsonObject& responses, const QJsonObject& allowedValues, bool* isDeclined);

private:
    class Request;
    class PendingCall;
    class ListenStream;

    enum class ListKind
    {
        Tools,
        Prompts,
        Resources,
        Templates
    };

    // HTTP level (pdfmcpdispatcher.cpp)
    bool isHostAllowed(QByteArrayView host);
    void refreshAllowedHosts();
    bool checkAuthorization(const std::shared_ptr<PDFMcpExchange>& exchange);
    void handlePost(const std::shared_ptr<PDFMcpExchange>& exchange);
    void handleGet(const std::shared_ptr<PDFMcpExchange>& exchange);
    void handleDelete(const std::shared_ptr<PDFMcpExchange>& exchange);
    void handleClientResponse(const std::shared_ptr<PDFMcpExchange>& exchange, const QJsonObject& message, const PDFMcpRequestId& id);

    // Responses
    void respondEmpty(const std::shared_ptr<PDFMcpExchange>& exchange, int status, std::vector<std::pair<QByteArray, QByteArray>> headers = {});
    void respondJson(const std::shared_ptr<PDFMcpExchange>& exchange, int status, QByteArray body, std::vector<std::pair<QByteArray, QByteArray>> headers = {});
    void respondError(const std::shared_ptr<PDFMcpExchange>& exchange, int status, const PDFMcpRequestId& id, int code, const QString& message, QByteArray data = QByteArray(), std::vector<std::pair<QByteArray, QByteArray>> headers = {});
    void respondRequestError(const Request& request, int code, const QString& message, QByteArray data = QByteArray(), int status = -1);
    void respondResult(const Request& request, QByteArray result, QByteArrayView cacheMembers = QByteArrayView());
    void respondRateLimited(const Request& request, qint64 retryAfter);

    QByteArray createResultMessage(const PDFMcpRequestId& id, QByteArrayView result) const;
    static QByteArray createErrorMessage(const PDFMcpRequestId& id, int code, const QString& message, QByteArrayView data);
    QByteArray appendResultMembers(QByteArrayView result, PDFMcpEra era, QByteArrayView cacheMembers) const;
    QByteArray createServerInfoMeta() const;
    static int getErrorStatus(int code, PDFMcpEra era);

    // Protocol (pdfmcpdispatcher_protocol.cpp)
    void handleModern(Request& request);
    void handleInitialize(Request& request);
    void handleLegacy(Request& request, PDFMcpSession* session);
    void handleMethod(Request& request);
    void handleDiscover(const Request& request);
    void handleList(const Request& request, ListKind kind);
    void handleResourceRead(Request& request);
    void handleToolCall(Request& request);
    void handlePromptGet(Request& request);
    void handleCompletion(Request& request);
    void handleSubscribe(const Request& request, PDFMcpSession* session, bool subscribe);
    void handleSetLevel(const Request& request, PDFMcpSession* session);
    void handleListen(Request& request);
    void handleTaskGet(const Request& request);
    void handleTaskUpdate(const Request& request);
    void handleTaskCancel(const Request& request);
    bool prepareRetry(Request& request, PDFMcpCallRequest& callRequest, const QString& name, const QJsonObject& hashedArguments);
    QByteArray createCapabilities(bool isModern) const;
    QByteArray createServerInfo(PDFMcpEra era) const;
    bool touchAssistant(Request& request);

    // Calls and tasks (pdfmcpdispatcher_calls.cpp)
    void startCall(Request& request, PDFMcpCallRequest callRequest, bool isRetryable, const QString& retryName, const QByteArray& argumentsHash);
    void launchCall(PendingCall& pending);
    void onPendingTimer();
    void onCallTimer(quint64 pendingId);
    void checkCalls(qint64 now);
    void finishCall(PendingCall& pending);
    void deliverResult(PendingCall& pending, const PDFMcpCallResult& result);
    void deliverMessage(PendingCall& pending, int status, QByteArray message, bool isComplete);
    void deliverDeclined(PendingCall& pending, const QString& text);
    void handleInputRequired(PendingCall& pending, const PDFMcpInputRequests& requests);
    void sendNextElicitation(PendingCall& pending);
    void redispatch(PendingCall& pending, const QJsonObject& answers);
    bool sendCallEvent(PendingCall& pending, QByteArrayView json);
    void createTask(PendingCall& pending);
    void updateTask(PDFMcpTask& task);
    void cancelCall(PendingCall& pending, PDFMcpCancelReason reason);
    void removePending(quint64 pendingId);
    PendingCall* findPending(quint64 pendingId);
    QByteArray createInputRequestsJson(const PDFMcpInputRequests& requests, PDFMcpEra era) const;
    QByteArray createTaskJson(const PDFMcpTask& task) const;

    // Streams and notifications (pdfmcpdispatcher_streams.cpp)
    void sendListChanged(ListKind kind);
    void endListenStream(ListenStream& stream, bool closeConnection);
    void endSession(const QString& key, PDFMcpAssistantEvent::Kind kind);
    void flushResourceUpdates();
    void sendKeepAlives(qint64 now);
    void sendTaskNotification(const PDFMcpTask& task);
    void removeAssistant(const QString& key, PDFMcpAssistantEvent::Kind kind);
    void postAssistantEvent(const PDFMcpAssistantEvent& event);
    void publishAssistants(bool changed);
    void onAssistantConnected(const pdf::PDFMcpAssistantEvent& event);

    std::shared_ptr<PDFMcpServerShared> m_shared;
    quint16 m_port = 0;
    bool m_isShutDown = false;

    PDFMcpRequestState m_requestState;
    PDFMcpSessionRegistry m_sessions;
    PDFMcpAssistantRegistry* m_assistants = nullptr;
    PDFMcpTaskStore m_tasks;
    PDFMcpRateLimiter m_rateLimiter;
    PDFMcpRateLimiter m_completionLimiter;
    PDFMcpAuthorizationThrottle m_throttle;
    std::shared_ptr<const PDFMcpCatalog> m_catalog;

    quint64 m_nextPendingId = 0;
    std::map<quint64, std::unique_ptr<PendingCall>> m_calls;   ///< Pending calls by their ids
    std::map<const PDFMcpCall*, quint64> m_callIndex;           ///< Call -> pending call
    std::map<quint64, quint64> m_exchangeCalls;                 ///< Exchange id -> pending call
    std::map<quint64, std::unique_ptr<ListenStream>> m_listenStreams;  ///< By exchange id
    std::map<quint64, QString> m_getStreams;                    ///< Exchange id -> session key

    QPointer<QTimer> m_maintenanceTimer;
    QPointer<QTimer> m_updateTimer;
    QStringList m_allowedHosts;
    qint64 m_hostsRefreshedAt = 0;
    QByteArray m_tokenHash;
};

}   // namespace pdf

#endif // PDFMCPDISPATCHER_H
