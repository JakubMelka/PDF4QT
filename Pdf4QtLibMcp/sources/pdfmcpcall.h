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

#ifndef PDFMCPCALL_H
#define PDFMCPCALL_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcpjson.h"
#include "pdfoperationcontrol.h"

#include <QByteArray>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace pdf
{

class PDFMcpHost;
class PDFMcpCall;

/// Protocol era of a request (plan of step 7, chapter 1, [D1])
enum class PDFMcpEra
{
    Modern,             ///< 2026-07-28: stateless, per-request _meta
    Legacy20251125,     ///< 2025-11-25: initialize, sessions
    Legacy20250618      ///< 2025-06-18: initialize, sessions, no icons, no elicitation mode
};

/// Protocol versions and their eras
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpProtocol
{
public:
    PDFMcpProtocol() = delete;

    static constexpr const char* VERSION_2026_07_28 = "2026-07-28";
    static constexpr const char* VERSION_2025_11_25 = "2025-11-25";
    static constexpr const char* VERSION_2025_06_18 = "2025-06-18";

    /// Meta keys of the modern era
    static constexpr const char* META_PROTOCOL_VERSION = "io.modelcontextprotocol/protocolVersion";
    static constexpr const char* META_CLIENT_CAPABILITIES = "io.modelcontextprotocol/clientCapabilities";
    static constexpr const char* META_CLIENT_INFO = "io.modelcontextprotocol/clientInfo";
    static constexpr const char* META_LOG_LEVEL = "io.modelcontextprotocol/logLevel";
    static constexpr const char* META_SERVER_INFO = "io.modelcontextprotocol/serverInfo";
    static constexpr const char* META_SUBSCRIPTION_ID = "io.modelcontextprotocol/subscriptionId";

    /// Name of the tasks extension [D3]
    static constexpr const char* TASKS_EXTENSION = "io.modelcontextprotocol/tasks";

    /// Version of the era ("2026-07-28", ...)
    static QString getVersion(PDFMcpEra era);

    /// Era of a version, std::nullopt for an unsupported version
    static std::optional<PDFMcpEra> getEra(QStringView version);

    /// Supported versions, the newest first
    static QStringList getSupportedVersions();

    /// Log levels of MCP (RFC 5424 names) in their order
    static QStringList getLogLevels();

    /// Index of a log level (0 = debug ... 7 = emergency), -1 for an unknown one
    static int getLogLevelIndex(QStringView level);
};

/// Who sent a request: the era, the client and its identity (plan 2.2, 2.7)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpClientContext
{
public:
    PDFMcpEra era = PDFMcpEra::Modern;
    QString protocolVersion;            ///< Negotiated (legacy) or requested (modern) version
    QJsonObject clientCapabilities;     ///< Capabilities of the client (of this request in the modern era)
    QString clientName;                 ///< clientInfo.name, sanitized (at most 64 characters), "MCP client" without it
    QString clientTitle;                ///< clientInfo.title, sanitized (may be empty)
    QString clientVersion;              ///< clientInfo.version, sanitized (may be empty)
    QString remoteAddress;              ///< Address of the peer without the port ("127.0.0.1", "::1", "192.168.1.57")
    bool isRemote = false;              ///< The peer is not a loopback address
    QString logLevel;                   ///< Requested log level, empty = no notifications/message
    bool hasProgressToken = false;      ///< The request carried _meta.progressToken
    QString assistantKey;               ///< "a:<32 hex>" (modern) or "s:<32 hex>" (legacy), never a raw token or session id
    QString sessionKey;                 ///< Hash of the legacy session (empty in the modern era)

    /// The client declared form elicitation (an empty elicitation object means form)
    bool supportsElicitation() const;

    /// The client declared the tasks extension on this request [D3]
    bool supportsTasks() const;
};

/// Kind of a call routed to the host
enum class PDFMcpCallKind
{
    ToolCall,       ///< tools/call
    ResourceRead,   ///< resources/read of a document resource (static ones are answered by the server)
    PromptGet,      ///< prompts/get
    Completion      ///< completion/complete
};

/// Request of one call (immutable after the call is created)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpCallRequest
{
public:
    PDFMcpCallKind kind = PDFMcpCallKind::ToolCall;
    QString method;                 ///< JSON-RPC method ("tools/call")
    QString name;                   ///< Tool or prompt name
    QString uri;                    ///< URI of resources/read
    QJsonObject arguments;          ///< Arguments of the tool / prompt, params of completion
    QJsonObject inputResponses;     ///< Answers of a retried MRTR request (verified by the dispatcher)
    QJsonObject requestState;       ///< Unsealed requestState (verified by the dispatcher; empty without a retry)
    PDFMcpClientContext client;
    PDFMcpRequestId id;
    qint64 receivedAt = 0;          ///< Milliseconds since the epoch
};

/// One question of an elicitation (MRTR inputRequests entry or a legacy elicitation/create)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpInputRequest
{
public:
    QString key;                    ///< Key of the inputRequests map ("pdf4qt.choice")
    QString message;                ///< Text shown by the client
    QByteArray requestedSchema;     ///< Flat object schema of primitives (PDFMcpSchema::toJson)
};

/// Questions of a call and the state, which the dispatcher seals into requestState
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpInputRequests
{
public:
    std::vector<PDFMcpInputRequest> requests;
    QJsonObject allowedValues;      ///< Values allowed in the answers ({ key: [values] }), sealed and checked on the retry

    bool isEmpty() const { return requests.empty(); }
};

/// Result of a call: a complete result object (without resultType / _meta - the
/// dispatcher adds the members of the era before the closing brace), an input
/// request (MRTR / legacy elicitation) or a protocol error
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpCallResult
{
public:
    enum class Kind
    {
        Complete,
        InputRequired,
        Error
    };

    Kind kind = Kind::Complete;
    QByteArray result;              ///< Complete: the serialized result object "{...}"
    bool isError = false;           ///< Complete tool result with isError true
    int errorCode = 0;              ///< Error: JSON-RPC code
    QString errorMessage;           ///< Error: message
    QByteArray errorData;           ///< Error: serialized data value (empty = none)
    PDFMcpInputRequests inputRequests;  ///< InputRequired

    static PDFMcpCallResult createComplete(QByteArray result, bool isError = false);

    /// Tool result with isError true and one text block (execution errors are
    /// written for a model: what happened, why, what to pass or ask the user)
    static PDFMcpCallResult createToolError(const QString& text);

    static PDFMcpCallResult createError(int code, const QString& message, QByteArray data = QByteArray());
    static PDFMcpCallResult createInputRequired(PDFMcpInputRequests requests);

    /// Parsed result object (tests and logs; empty for an error)
    QJsonObject toQJson() const;

    /// Text of the first text content block of the result (tests)
    QString getFirstText() const;
};

/// Why a call was canceled by the transport or the server
enum class PDFMcpCancelReason
{
    None,
    StreamClosed,   ///< The response stream or the socket was closed (activity "canceled")
    TaskCanceled,   ///< tasks/cancel (activity "canceled")
    ClientCanceled, ///< Legacy notifications/cancelled (activity "canceled")
    Disconnected,   ///< The assistant was disconnected (activity "denied_disconnected")
    ServerStopped   ///< The server is stopping (activity "denied_stopped")
};

/// Event of a call for the transport
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpCallEvent
{
public:
    enum class Type
    {
        Progress,
        Log,
        Finished
    };

    Type type = Type::Progress;
    double progress = 0.0;
    double total = 0.0;     ///< <= 0 = unknown
    QString message;        ///< Progress message (English) or log text
    QString level;          ///< Log level
};

/// Receiver of the events of the calls in the control thread of the server
/// (the dispatcher of package A). PDFMcpCall posts processCall by a queued
/// invocation once per batch of events; the receiver takes them by takeEvents.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpCallSink : public QObject
{
    Q_OBJECT

public:
    explicit PDFMcpCallSink(QObject* parent = nullptr);
    virtual ~PDFMcpCallSink() override;

    /// New events of the call (progress, log, finish) are available
    virtual void processCall(std::shared_ptr<pdf::PDFMcpCall> call);
};

/// One call of a client in flight (plan 2.2): created by the dispatcher, handed
/// to the host in the GUI thread, passed to the workers as their
/// PDFOperationControl. Every function is thread safe (state under a mutex,
/// the cancellation flag atomic). The first finish (or requestInput) wins; later
/// progress and log messages are dropped ("MUST stop after completion").
/// Progress values must grow strictly, a smaller or equal value is dropped.
/// A canceled call is reported to its host by PDFMcpHost::callCanceled (queued
/// to the thread of the host) - jobs poll isCanceled / isOperationCancelled.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpCall : public PDFOperationControl, public std::enable_shared_from_this<PDFMcpCall>
{
public:
    explicit PDFMcpCall(PDFMcpCallRequest request);
    virtual ~PDFMcpCall() override;

    PDFMcpCall(const PDFMcpCall&) = delete;
    PDFMcpCall& operator=(const PDFMcpCall&) = delete;

    /// Creates a call (the only way - the call is always held by std::shared_ptr)
    static std::shared_ptr<PDFMcpCall> create(PDFMcpCallRequest request);

    const PDFMcpCallRequest& getRequest() const { return m_request; }

    /// Unique id of the call in the process ("c1", "c2", ...) - activity callId
    const QString& getCallId() const { return m_callId; }

    /// Progress of the call (only sent, when the request carried a progress
    /// token); \p total <= 0 = unknown; \p message in English
    void reportProgress(double progress, double total, const QString& message);

    /// Log message [D4] (only sent, when the request asked for this level)
    void log(const QString& level, const QString& text);

    /// The call needs a choice of the user in the client (MRTR / legacy
    /// elicitation, plan 2.8): finishes the call with an InputRequired result
    void requestInput(PDFMcpInputRequests requests);

    /// Finishes the call (the first finish wins; false = already finished)
    bool finish(PDFMcpCallResult result);

    bool isFinished() const;

    /// Result of a finished call
    std::optional<PDFMcpCallResult> getResult() const;

    /// Cancels the call (transport / server side; the first reason wins)
    void cancel(PDFMcpCancelReason reason);

    bool isCanceled() const { return m_isCanceled.load(); }
    PDFMcpCancelReason getCancelReason() const;

    /// PDFOperationControl
    virtual bool isOperationCancelled() const override { return isCanceled(); }

    /// The host, which is told about the cancellation (set by the dispatcher)
    void setHost(PDFMcpHost* host);

    /// The receiver of the events in the control thread (set by the dispatcher)
    void setSink(PDFMcpCallSink* sink);

    /// Events since the last call (the transport sends them)
    std::vector<PDFMcpCallEvent> takeEvents();

    /// Last reported progress (tests)
    double getLastProgress() const;

    /// Most events kept until the transport takes them (the oldest are dropped)
    static constexpr size_t MAXIMUM_PENDING_EVENTS = 4096;

private:
    void postEvent(PDFMcpCallEvent event);

    PDFMcpCallRequest m_request;
    QString m_callId;

    mutable QMutex m_mutex;
    std::atomic<bool> m_isCanceled = false;
    PDFMcpCancelReason m_cancelReason = PDFMcpCancelReason::None;
    std::optional<PDFMcpCallResult> m_result;
    double m_lastProgress = -1.0;
    std::vector<PDFMcpCallEvent> m_events;
    bool m_isSinkNotified = false;
    QPointer<PDFMcpHost> m_host;
    QPointer<PDFMcpCallSink> m_sink;
};

}   // namespace pdf

#endif // PDFMCPCALL_H
