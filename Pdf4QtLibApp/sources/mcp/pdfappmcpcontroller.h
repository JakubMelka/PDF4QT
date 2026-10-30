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

#ifndef PDFAPPMCPCONTROLLER_H
#define PDFAPPMCPCONTROLLER_H

#include "pdfappglobal.h"
#include "pdfmcprepository.h"

#include "pdfmcpserver.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QTimer;

namespace pdf
{

class PDFAppContext;
class PDFAppMcpHost;

/// State of the MCP server shown by the AI page (plan 5.1)
enum class PDFAppMcpServerState
{
    Stopped,        ///< 653:5814
    Starting,       ///< Binding the port, probing a port in use
    Running,        ///< 653:5231 / 653:6384 (remote)
    OtherInstance,  ///< 940:135321: another PDF4QT runs the server (its row is alive, or its answer of the probe)
    PortInUse,      ///< 955:148587: another program uses the port
    Failed          ///< The port cannot be bound for another reason
};

/// The controller of the MCP server, owned by PDFAppContext (plan 2.3, 2.6):
/// it reads the settings mcp.* and follows the notifier (a change of Connection
/// restarts the server, a change of a permission publishes a new catalog, a new
/// token disconnects everybody), generates the token (256 bits, base64url,
/// stored in mcp.token), starts the server with PDF4QT (mcp.startWithApp),
/// owns the server and the host, claims the row mcp_server with a heartbeat
/// every 2 s, handles the requests of other instances (mcp_request) and keeps
/// the quit order of plan 2.6 (shutdown). GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpController : public QObject
{
    Q_OBJECT

public:
    explicit PDFAppMcpController(PDFAppContext* context, QObject* parent = nullptr);
    virtual ~PDFAppMcpController() override;

    /// Reads the settings, connects the notifier, renders the icons; starts the
    /// server when mcp.startWithApp is on
    void initialize();

    /// Starts the server with the current settings (false = running already or shut down)
    bool start();

    /// Stops the server (the host stays for the next start)
    void stop();

    /// Stops and starts the server again (a change of Connection)
    void restart();

    /// The quit order of plan 2.6 (steps 1-8): the server stops, the host denies
    /// the pending confirmations and waits for its jobs, the host is destroyed,
    /// the row mcp_server is released and the client rows are deleted. Called by
    /// PDFAppContext::close before the database closes; final.
    void shutdown();

    PDFAppMcpServerState getState() const;

    /// The server is running in this instance
    bool isRunning() const;

    /// This instance owns the row mcp_server (it runs the server)
    bool isOwner() const { return m_isOwner; }

    /// Port of the running server, else the port of the settings
    quint16 getPort() const;

    /// Shown address "http://127.0.0.1:8765/mcp" (remote mode: the address of the network)
    QString getAddress() const;

    /// Message of the state Failed
    QString getErrorMessage() const;

    /// The access token of the settings
    QString getToken() const;

    /// Generates a new token, writes it and disconnects every assistant (the
    /// server restarts with it); returns it
    QString createNewToken();

    /// A new random token: 256 bits in base64url (43 characters)
    static QString generateToken();

    /// The configuration of a start from the settings
    PDFMcpServerConfig createConfig() const;

    PDFAppMcpHost* getHost() const;
    PDFMcpServer* getServer() const;

    /// Id of this instance in the row mcp_server (random per process)
    const QString& getInstanceId() const { return m_instanceId; }

    /// The row mcp_server (the owner of another instance)
    PDFMcpServerRow getServerRow() const;

    /// Disconnect / Disconnect All (here, or through mcp_request at the owner)
    void disconnectAssistant(const QString& key);
    void disconnectAll();

    /// Show Other Window: asks the owner to raise its window (AllowSetForegroundWindow on Windows)
    void showOtherWindow();

    /// Interval of the heartbeat (tests; PDFMcpRepository::HEARTBEAT_INTERVAL by default)
    void setHeartbeatInterval(int interval);

    /// Time limit of the probe of a port in use for the next starts (tests - the
    /// slowdown of valgrind; PDFMcpLimits::PORT_PROBE_TIMEOUT by default)
    void setPortProbeTimeout(int timeout);

    /// Address of the network shown in the remote mode: the first up, running,
    /// non-loopback, non-link-local IPv4 of a non-virtual interface (empty = none)
    static QString findNetworkAddress();

    /// Allowed Host values of the remote mode: every address of an up interface,
    /// the host name and <hostname>.local, with the port
    static QStringList getRemoteHosts(quint16 port);

Q_SIGNALS:
    /// The state, the address or the owner changed
    void stateChanged();

    /// The connected assistants changed (rows mcp_client)
    void assistantsChanged();

    /// The activity changed (rows mcp_activity)
    void activityChanged();

    /// Another instance asked to show this window (Show Other Window)
    void showWindowRequested();

private:
    void onNotifierChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onServerStateChanged();
    void onServerAssistantsChanged();

    /// The owner writes the assistants of its server into the rows mcp_client
    void syncClients();
    void onHeartbeat();
    void processRequests();
    void applyPermissions();

    /// Restarts the running server, when the settings of Connection differ from
    /// its configuration (a new token disconnects everybody)
    void applyConnection();
    void claimOwnership();
    void releaseOwnership();
    void raiseWindow();

    /// The row belongs to another instance, which runs the server: its heartbeat
    /// is not stale and its process runs (the start, the state, the heartbeat)
    bool isOtherOwnerAlive(const PDFMcpServerRow& row) const;

    /// Remembers the row of the owner, whether another owner is alive in it and
    /// whether the process of the row runs. \returns true, when the owner has
    /// changed since the last row: another instance, port or address, its end
    /// (released, stale heartbeat, process gone) or the end of the process of a
    /// row, which was stale already (a hung owner killed)
    bool setLastRow(const PDFMcpServerRow& row);

    /// The row has changed and no other owner is alive (it has released the row,
    /// its heartbeat is stale or its process is gone): a start of this instance
    /// refused, because a PDF4QT had the port (PDFMcpServerState::PortInUseByPdf4Qt),
    /// is over - the server is stopped, so Start is enabled. A PDF4QT without a
    /// row (another profile) changes no row and keeps the state: the callers
    /// call it only for a change of the row. The controller may be deleted by a slot.
    void clearRefusedStart();

    QPointer<PDFAppContext> m_context;
    QPointer<PDFAppMcpHost> m_host;
    QPointer<PDFMcpServer> m_server;
    QPointer<QTimer> m_heartbeatTimer;
    QString m_instanceId;
    bool m_isOwner = false;
    bool m_isShutDown = false;
    bool m_isInitialized = false;
    int m_portProbeTimeout = PDFMcpLimits::PORT_PROBE_TIMEOUT;
    PDFMcpServerRow m_lastRow;
    bool m_isLastRowAlive = false;
    bool m_isLastRowProcessRunning = false;
};

}   // namespace pdf

#endif // PDFAPPMCPCONTROLLER_H
