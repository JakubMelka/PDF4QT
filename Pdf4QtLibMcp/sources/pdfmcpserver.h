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

#ifndef PDFMCPSERVER_H
#define PDFMCPSERVER_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcpcatalog.h"
#include "pdfmcphost.h"
#include "pdfmcpiothreads.h"
#include "pdfmcplimits.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTcpSocket>

#include <atomic>
#include <memory>
#include <vector>

class QThread;
class QTimer;

namespace pdf
{

class PDFMcpControl;
class PDFMcpDispatcherLink;
class PDFMcpPortProbe;

/// Where the server listens (plan 2.6)
enum class PDFMcpBindMode
{
    Local,      ///< 127.0.0.1 and ::1 (a failure of ::1 is ignored)
    Remote      ///< Every address (QHostAddress::Any); the token is always required
};

/// Server information of the results (serverInfo / _meta serverInfo, plan 1.2)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpServerInfo
{
public:
    QString name = QStringLiteral("pdf4qt");
    QString title = QStringLiteral("PDF4QT");
    QString version;                ///< "1.6.0.0"
    QString description;
    QString websiteUrl;
    std::vector<PDFMcpIcon> icons;
};

/// Configuration of one run of the server
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpServerConfig
{
public:
    PDFMcpBindMode bindMode = PDFMcpBindMode::Local;
    quint16 port = 8765;            ///< 0 = any free port (tests)
    QByteArray token;               ///< Access token (never logged, never stored by the library)
    bool requireToken = true;       ///< Forced on in the remote mode
    PDFMcpServerInfo serverInfo;
    QString instructions;           ///< Instructions of server/discover and initialize (plan 4.4)
    int ioThreadCount = PDFMcpLimits::IO_THREAD_COUNT;
    int headerTimeout = PDFMcpLimits::HEADER_TIMEOUT;           ///< ms (tests lower it)
    int portProbeTimeout = PDFMcpLimits::PORT_PROBE_TIMEOUT;    ///< ms
    QStringList additionalHosts;    ///< Additional allowed Host values of the remote mode ("192.168.1.24:8765")

    // Times of the transport (ms; the tests lower them, the defaults are PDFMcpLimits)
    int bodyTimeout = PDFMcpLimits::BODY_TIMEOUT;
    int keepAliveInterval = PDFMcpLimits::KEEP_ALIVE_INTERVAL;
    int jsonResponseDelay = PDFMcpLimits::JSON_RESPONSE_DELAY;
    int taskCreationDelay = PDFMcpLimits::TASK_CREATION_DELAY;
    int elicitationTimeout = PDFMcpLimits::ELICITATION_TIMEOUT;
    int resourceUpdateInterval = PDFMcpLimits::RESOURCE_UPDATE_INTERVAL;
    int cancelGraceTime = PDFMcpLimits::CANCEL_GRACE_TIME;

    /// The token is required for this configuration (always in the remote mode)
    bool isTokenRequired() const { return requireToken || bindMode == PDFMcpBindMode::Remote; }
};

/// State of the server
enum class PDFMcpServerState
{
    Stopped,
    Starting,               ///< Binding (and probing a port in use)
    Running,
    Stopping,
    PortInUseByPdf4Qt,      ///< The port is used by another PDF4QT (its answer has Server: PDF4QT/…)
    PortInUseByOtherProgram,///< The port is used by another program (955:148587)
    Failed                  ///< The port cannot be bound for another reason (getErrorMessage)
};

/// State shared by the threads of one run of the server (thread safe)
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpServerShared
{
public:
    explicit PDFMcpServerShared(const PDFMcpServerConfig& config);

    const PDFMcpServerConfig& getConfig() const { return m_config; }

    /// SHA-256 of the access token (empty without a token)
    const QByteArray& getTokenHash() const { return m_tokenHash; }

    /// Value of the Server header ("1.6.0.0" of "PDF4QT/1.6.0.0")
    QByteArray getServerVersion() const;

    bool isStopping() const { return m_isStopping.load(); }
    void setStopping() { m_isStopping.store(true); }

    /// Open connections of the run
    std::atomic<int> connectionCount = 0;

    std::shared_ptr<const PDFMcpCatalog> getCatalog() const;
    void setCatalog(std::shared_ptr<const PDFMcpCatalog> catalog);

    /// The host (GUI thread); reached only by queued invocations
    QPointer<PDFMcpHost> host;

    /// The link of the connections and of the server to the dispatcher of the
    /// run (package A; created with the shared state)
    const std::shared_ptr<PDFMcpDispatcherLink> link;

private:
    PDFMcpServerConfig m_config;
    QByteArray m_tokenHash;
    std::atomic<bool> m_isStopping = false;
    mutable QMutex m_mutex;
    std::shared_ptr<const PDFMcpCatalog> m_catalog;
};

/// Probe of a port, which cannot be bound (plan 2.6): sends GET /mcp to
/// 127.0.0.1:port and looks at the Server header of the answer. Asynchronous
/// (the GUI thread never waits on the network).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpPortProbe : public QObject
{
    Q_OBJECT

public:
    enum class Result
    {
        None,           ///< Not finished
        Pdf4Qt,         ///< The answer has Server: PDF4QT/…
        OtherProgram,   ///< Another answer, no answer within the timeout, or a closed connection
        Unreachable     ///< Nothing listens on 127.0.0.1:port (connection refused)
    };

    explicit PDFMcpPortProbe(QObject* parent = nullptr);
    virtual ~PDFMcpPortProbe() override;

    /// Starts the probe (once)
    void start(quint16 port, int timeout = PDFMcpLimits::PORT_PROBE_TIMEOUT);

    Result getResult() const { return m_result; }

    /// Value of the Server header of the answer (empty = none)
    const QByteArray& getServerHeader() const { return m_serverHeader; }

Q_SIGNALS:
    void finished(pdf::PDFMcpPortProbe::Result result);

private:
    void onConnected();
    void onReadyRead();
    void onErrorOccurred(QAbstractSocket::SocketError error);
    void onTimeout();
    void finish(Result result);

    QPointer<QTcpSocket> m_socket;
    QPointer<QTimer> m_timer;
    QByteArray m_buffer;
    QByteArray m_serverHeader;
    Result m_result = Result::None;
};

/// The MCP server (plan 2.2), a QObject of the GUI thread. It owns the control
/// thread (listeners, dispatcher) and the pool of the I/O threads; the host is
/// owned by the application and must outlive every run of the server (stop()
/// before the host is destroyed). start() is asynchronous: the state goes
/// Starting -> Running, or -> PortInUseByPdf4Qt / PortInUseByOtherProgram /
/// Failed (after the probe of the port). stop() is synchronous: it closes the
/// listeners, marks the run stopping (new requests get 503), cancels the calls,
/// ends the streams and joins the threads (steps 1-5 of plan 2.6).
///
/// The transport of package A: the dispatcher of the run answers both protocol
/// eras; notifyResourceUpdated, disconnectAssistant, disconnectAll and
/// checkTimeouts are thread safe (queued to the control thread of the run).
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpServer : public QObject
{
    Q_OBJECT

public:
    explicit PDFMcpServer(PDFMcpHost* host, QObject* parent = nullptr);
    virtual ~PDFMcpServer() override;

    /// Starts a run. false = the server is running or starting (stop it first).
    bool start(const PDFMcpServerConfig& config);

    /// Stops the run (synchronous, steps 1-5 of plan 2.6); nothing for a stopped server
    void stop();

    PDFMcpServerState getState() const { return m_state; }
    bool isRunning() const { return m_state == PDFMcpServerState::Running; }

    /// Bound port of a running server (0 otherwise)
    quint16 getPort() const { return m_port; }

    /// Message of the state Failed (English, for the log)
    const QString& getErrorMessage() const { return m_errorMessage; }

    /// Configuration of the last run
    const PDFMcpServerConfig& getConfig() const { return m_config; }

    PDFMcpHost* getHost() const { return m_host; }

    /// Publishes a new catalog (thread safe; atomic swap - the list_changed
    /// notifications are derived by the dispatcher from the difference)
    void publishCatalog(std::shared_ptr<const PDFMcpCatalog> catalog);

    /// The catalog of the server (an empty one before the first publish)
    std::shared_ptr<const PDFMcpCatalog> getCatalog() const;

    /// A document resource changed (prefix match of the subscriptions)
    void notifyResourceUpdated(const QString& uri);

    /// Disconnects an assistant (plan 2.7) / every assistant (a new token)
    void disconnectAssistant(const QString& key);
    void disconnectAll();

    /// Connected assistants (plan 2.7)
    std::vector<PDFMcpAssistantInfo> getAssistants() const;

    /// Runs the checks of the times now (idle assistants and sessions, expired
    /// tasks, elicitation timeouts, keep-alives) - the dispatcher runs them every
    /// second by itself; the tests call it after PDFMcpClock::setOffset
    void checkTimeouts();

    /// Called by the dispatcher of the run (queued): the list of the assistants changed
    void onAssistantsChanged();

    /// Version of the Server header of the server ("1.6.0.0")
    static QByteArray getDefaultVersion();

Q_SIGNALS:
    void stateChanged();
    void assistantsChanged();

private:
    void setState(PDFMcpServerState state);
    void onListenFinished(quint64 run, bool ok, int socketError, const QString& message, quint16 port);
    void onProbeFinished(pdf::PDFMcpPortProbe::Result result);
    void stopInternals();
    std::shared_ptr<PDFMcpServerShared> getShared() const;

    QPointer<PDFMcpHost> m_host;
    PDFMcpServerConfig m_config;
    PDFMcpServerState m_state = PDFMcpServerState::Stopped;
    QString m_errorMessage;
    quint16 m_port = 0;
    quint16 m_requestedPort = 0;
    quint64 m_run = 0;

    std::shared_ptr<PDFMcpServerShared> m_shared;
    mutable QMutex m_catalogMutex;
    std::shared_ptr<const PDFMcpCatalog> m_catalog;
    PDFMcpIoThreads m_ioThreads;
    QThread* m_controlThread = nullptr;
    PDFMcpControl* m_control = nullptr;
    QPointer<PDFMcpPortProbe> m_probe;
};

}   // namespace pdf

#endif // PDFMCPSERVER_H
