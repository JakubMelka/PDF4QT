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

#include "pdfappmcpcontroller.h"
#include "pdfappmcpconfirmationui.h"
#include "pdfappmcpclientnames.h"
#include "pdfappmcphost.h"
#include "pdfappmcppermissions.h"
#include "pdfappmcpresources.h"
#include "pdfappcontext.h"
#include "pdfappinstance.h"
#include "pdfappmainwindow.h"
#include "pdfappsysteminfo.h"
#include "pdfsettingdefinitions.h"

#include "pdfmcpjson.h"

#include <QCoreApplication>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace pdf
{

PDFAppMcpController::PDFAppMcpController(PDFAppContext* context, QObject* parent) :
    QObject(parent),
    m_context(context),
    m_instanceId(QString::number(QRandomGenerator::system()->generate64(), 16))
{
    m_host = new PDFAppMcpHost(context, this);

    // The confirmations of the user (package C): a child of the host, its provider
    new PDFAppMcpConfirmationUi(m_host);

    m_server = new PDFMcpServer(m_host, this);
    m_host->setServer(m_server);
    connect(m_server, &PDFMcpServer::stateChanged, this, &PDFAppMcpController::onServerStateChanged);
    connect(m_server, &PDFMcpServer::assistantsChanged, this, &PDFAppMcpController::onServerAssistantsChanged);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(int(PDFMcpRepository::HEARTBEAT_INTERVAL));
    connect(m_heartbeatTimer, &QTimer::timeout, this, &PDFAppMcpController::onHeartbeat);
}

PDFAppMcpController::~PDFAppMcpController()
{
    shutdown();
}

void PDFAppMcpController::initialize()
{
    if (m_isInitialized || m_isShutDown)
    {
        return;
    }
    m_isInitialized = true;

    if (m_context && m_context->getNotifier())
    {
        connect(m_context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppMcpController::onNotifierChanged);
        connect(m_context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppMcpController::onEverythingChanged);
    }

    if (m_host)
    {
        m_host->setIcons(PDFAppMcpHost::createIcons(QStringLiteral("mcp")));
    }
    applyPermissions();

    // The heartbeat also watches the row of another owner (its staleness)
    m_heartbeatTimer->start();
    setLastRow(getServerRow());

    if (m_context && m_context->getDatabase() && m_context->getSettings().getBool(PDFSettingKeys::mcpStartWithApp()))
    {
        start();
    }
}

QString PDFAppMcpController::generateToken()
{
    QByteArray bytes(32, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(bytes.data()), 8);
    return QString::fromLatin1(PDFMcpJson::toBase64Url(bytes));
}

QString PDFAppMcpController::getToken() const
{
    if (!m_context || !m_context->getDatabase())
    {
        return QString();
    }
    return m_context->getSettings().getString(PDFSettingKeys::mcpToken());
}

QString PDFAppMcpController::createNewToken()
{
    const QString token = generateToken();

    // The write and the disconnection notify the application at once: a slot may
    // delete the controller (UnitTestsAppMcpLifetime)
    QPointer<PDFAppMcpController> self(this);
    if (m_context && m_context->getDatabase())
    {
        m_context->getSettings().setValue(PDFSettingKeys::mcpToken(), token);
    }

    // Every old key is unusable: everybody is disconnected and the server runs
    // with the new token (the notification of the setting restarts it too, the
    // restart here makes it immediate)
    if (self && m_server)
    {
        m_server->disconnectAll();
    }
    if (self && isRunning())
    {
        restart();
    }
    return token;
}

PDFMcpServerConfig PDFAppMcpController::createConfig() const
{
    PDFMcpServerConfig config;
    config.portProbeTimeout = m_portProbeTimeout;
    if (!m_context || !m_context->getDatabase())
    {
        return config;
    }

    const PDFSettingsRepository settings = m_context->getSettings();
    config.port = quint16(qBound(qint64(1024), settings.getInteger(PDFSettingKeys::mcpPort()), qint64(65535)));
    config.bindMode = settings.getBool(PDFSettingKeys::mcpAllowRemote()) ? PDFMcpBindMode::Remote : PDFMcpBindMode::Local;
    config.requireToken = settings.getBool(PDFSettingKeys::mcpRequireToken()) || config.bindMode == PDFMcpBindMode::Remote;
    config.token = settings.getString(PDFSettingKeys::mcpToken()).toLatin1();
    config.serverInfo.version = QString::fromLatin1(PDFMcpServer::getDefaultVersion());
    config.serverInfo.description = QStringLiteral("PDF documents open in the PDF4QT window");
    config.serverInfo.websiteUrl = QStringLiteral("https://github.com/JakubMelka/PDF4QT");
    config.instructions = PDFAppMcpResources::getInstructions();
    if (m_host)
    {
        config.serverInfo.icons = m_host->getIcons();
    }
    if (config.bindMode == PDFMcpBindMode::Remote)
    {
        config.additionalHosts = getRemoteHosts(config.port);
    }
    return config;
}

bool PDFAppMcpController::start()
{
    if (m_isShutDown || !m_server || isRunning() || m_server->getState() == PDFMcpServerState::Starting)
    {
        return false;
    }

    // A missing token is generated, also when it is not required (the default of
    // the local mode, owner 2026-10-28): Require access token turned on later
    // works at once with the token the page already shows and copies
    if (m_context && m_context->getDatabase())
    {
        const PDFSettingsRepository settings = m_context->getSettings();
        if (settings.getString(PDFSettingKeys::mcpToken()).isEmpty())
        {
            QPointer<PDFAppMcpController> self(this);
            m_context->getSettings().setValue(PDFSettingKeys::mcpToken(), generateToken());
            if (!self || !m_server || m_isShutDown)
            {
                return false;
            }
        }
    }

    // Another live instance owns the server: no bind is attempted
    if (isOtherOwnerAlive(getServerRow()))
    {
        Q_EMIT stateChanged();
        return false;
    }

    return m_server->start(createConfig());
}

void PDFAppMcpController::stop()
{
    if (!m_server)
    {
        return;
    }

    // The stop emits the state of the server: a slot may delete the controller
    QPointer<PDFAppMcpController> self(this);
    m_server->stop();
    if (self)
    {
        releaseOwnership();
    }
}

void PDFAppMcpController::restart()
{
    if (m_isShutDown)
    {
        return;
    }

    QPointer<PDFAppMcpController> self(this);
    stop();
    if (self)
    {
        start();
    }
}

void PDFAppMcpController::shutdown()
{
    if (m_isShutDown)
    {
        return;
    }
    m_isShutDown = true;

    if (m_heartbeatTimer)
    {
        m_heartbeatTimer->stop();
    }
    if (m_context && m_context->getNotifier())
    {
        m_context->getNotifier()->disconnect(this);
    }

    // 1-5: listeners closed, the run marked stopping, the calls canceled, the
    // streams ended, the threads joined
    if (m_server)
    {
        m_server->disconnect(this);
        m_server->stop();
    }

    // 6: pending confirmations denied (denied_stopped), the jobs waited for
    if (m_host)
    {
        m_host->shutdown();
    }

    // 7: only then the host is destroyed (the server holds no call any more)
    delete m_server.data();
    delete m_host.data();

    // 8: the row mcp_server stopped, the client rows deleted
    releaseOwnership();
}

bool PDFAppMcpController::isRunning() const
{
    return m_server && m_server->isRunning();
}

PDFAppMcpServerState PDFAppMcpController::getState() const
{
    if (m_server)
    {
        switch (m_server->getState())
        {
            case PDFMcpServerState::Running:
                return m_isOwner || !m_context || !m_context->getDatabase() ? PDFAppMcpServerState::Running : PDFAppMcpServerState::OtherInstance;
            case PDFMcpServerState::Starting:
            case PDFMcpServerState::Stopping:
                return PDFAppMcpServerState::Starting;
            case PDFMcpServerState::PortInUseByPdf4Qt:
                return PDFAppMcpServerState::OtherInstance;
            case PDFMcpServerState::PortInUseByOtherProgram:
                return PDFAppMcpServerState::PortInUse;
            case PDFMcpServerState::Failed:
                return PDFAppMcpServerState::Failed;
            case PDFMcpServerState::Stopped:
                break;
        }
    }

    // The row of another live owner (a killed owner, whose process has ended, is
    // gone at once, not only after its heartbeat is stale - the Start of the page)
    if (isOtherOwnerAlive(getServerRow()))
    {
        return PDFAppMcpServerState::OtherInstance;
    }
    return PDFAppMcpServerState::Stopped;
}

quint16 PDFAppMcpController::getPort() const
{
    if (isRunning())
    {
        return m_server->getPort();
    }
    return createConfig().port;
}

QString PDFAppMcpController::getAddress() const
{
    const PDFMcpServerConfig config = createConfig();
    QString host = QStringLiteral("127.0.0.1");
    if (config.bindMode == PDFMcpBindMode::Remote)
    {
        const QString address = findNetworkAddress();
        host = address.isEmpty() ? host : address;
    }
    return QStringLiteral("http://%1:%2/mcp").arg(host).arg(getPort());
}

QString PDFAppMcpController::getErrorMessage() const
{
    return m_server ? m_server->getErrorMessage() : QString();
}

PDFAppMcpHost* PDFAppMcpController::getHost() const
{
    return m_host;
}

PDFMcpServer* PDFAppMcpController::getServer() const
{
    return m_server;
}

PDFMcpServerRow PDFAppMcpController::getServerRow() const
{
    if (!m_context || !m_context->getDatabase())
    {
        return PDFMcpServerRow();
    }
    return PDFMcpRepository(m_context->getDatabase()).getServer();
}

void PDFAppMcpController::disconnectAssistant(const QString& key)
{
    if (isRunning() && m_isOwner)
    {
        QPointer<PDFAppMcpController> self(this);
        m_server->disconnectAssistant(key);
        if (self && m_context && m_context->getDatabase())
        {
            PDFMcpRepository(m_context->getDatabase()).removeClient(key);
        }
        return;
    }

    if (m_context && m_context->getDatabase())
    {
        PDFMcpRepository(m_context->getDatabase()).postRequest(PDFMcpRequestKind::Disconnect, key);
    }
}

void PDFAppMcpController::disconnectAll()
{
    if (isRunning() && m_isOwner)
    {
        QPointer<PDFAppMcpController> self(this);
        m_server->disconnectAll();
        if (self && m_context && m_context->getDatabase())
        {
            PDFMcpRepository(m_context->getDatabase()).clearClients();
        }
        return;
    }

    if (m_context && m_context->getDatabase())
    {
        PDFMcpRepository(m_context->getDatabase()).postRequest(PDFMcpRequestKind::DisconnectAll);
    }
}

void PDFAppMcpController::showOtherWindow()
{
    if (!m_context || !m_context->getDatabase())
    {
        return;
    }

    const PDFMcpServerRow row = getServerRow();
#ifdef Q_OS_WIN
    // The owner may take the foreground only when this process allows it
    if (row.pid > 0 && row.pid <= qint64(0xFFFFFFFF))
    {
        ::AllowSetForegroundWindow(DWORD(row.pid));
    }
#else
    Q_UNUSED(row);
#endif
    PDFMcpRepository(m_context->getDatabase()).postRequest(PDFMcpRequestKind::ShowWindow);
}

void PDFAppMcpController::setHeartbeatInterval(int interval)
{
    if (m_heartbeatTimer)
    {
        m_heartbeatTimer->setInterval(qMax(10, interval));
    }
}

void PDFAppMcpController::setPortProbeTimeout(int timeout)
{
    m_portProbeTimeout = qMax(1, timeout);
}

QString PDFAppMcpController::findNetworkAddress()
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& networkInterface : interfaces)
    {
        const QNetworkInterface::InterfaceFlags flags = networkInterface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning) || flags.testFlag(QNetworkInterface::IsLoopBack) ||
            networkInterface.type() == QNetworkInterface::Virtual)
        {
            continue;
        }

        for (const QNetworkAddressEntry& entry : networkInterface.addressEntries())
        {
            const QHostAddress address = entry.ip();
            if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback() && !address.isLinkLocal())
            {
                return address.toString();
            }
        }
    }
    return QString();
}

QStringList PDFAppMcpController::getRemoteHosts(quint16 port)
{
    QStringList hosts;
    for (const QHostAddress& address : QNetworkInterface::allAddresses())
    {
        if (address.isLoopback())
        {
            continue;
        }

        QHostAddress plain = address;
        plain.setScopeId(QString());
        const QString text = plain.protocol() == QAbstractSocket::IPv6Protocol ? QStringLiteral("[%1]").arg(plain.toString()) : plain.toString();
        hosts << QStringLiteral("%1:%2").arg(text).arg(port);
    }

    const QString hostName = QHostInfo::localHostName().toLower();
    if (!hostName.isEmpty())
    {
        hosts << QStringLiteral("%1:%2").arg(hostName).arg(port);
        hosts << QStringLiteral("%1.local:%2").arg(hostName).arg(port);
    }
    hosts.removeDuplicates();
    return hosts;
}

bool PDFAppMcpController::isOtherOwnerAlive(const PDFMcpServerRow& row) const
{
    PDFMcpRepository repository(m_context ? m_context->getDatabase() : nullptr);
    return row.instance != m_instanceId && row.isAlive(repository.getCurrentTime(), PDFMcpRepository::STALE_TIME) && PDFAppProcessInfo::isProcessRunning(row.pid);
}

bool PDFAppMcpController::setLastRow(const PDFMcpServerRow& row)
{
    const bool wasAlive = m_isLastRowAlive;
    const bool wasProcessRunning = m_isLastRowProcessRunning;
    const bool isOwnerChanged = row.instance != m_lastRow.instance || row.port != m_lastRow.port || row.address != m_lastRow.address;
    m_lastRow = row;
    m_isLastRowAlive = isOtherOwnerAlive(row);

    // The process of a row, whose heartbeat is stale already (a hung owner), may
    // still hold the port: its end is a change too (review RMULTI of step 9)
    m_isLastRowProcessRunning = row.exists && PDFAppProcessInfo::isProcessRunning(row.pid);
    return isOwnerChanged || wasAlive != m_isLastRowAlive || wasProcessRunning != m_isLastRowProcessRunning;
}

void PDFAppMcpController::clearRefusedStart()
{
    // Two instances started with PDF4QT at the same time (the second one binds
    // before the first one claims the row), or Start while the owner hangs (its
    // heartbeat is stale, its port is bound): the probe of the port ended the
    // start, and the AI page showed the other window until the restart, also
    // after that window had quit (step 9, UnitTestsAppMultiInstance)
    if (!m_isLastRowAlive && m_server && m_server->getState() == PDFMcpServerState::PortInUseByPdf4Qt)
    {
        m_server->stop();
    }
}

void PDFAppMcpController::claimOwnership()
{
    if (!m_context || !m_context->getDatabase() || !m_server)
    {
        // Without a database (no context) the running server is owned anyway
        m_isOwner = isRunning();
        return;
    }

    bool claimed = false;
    PDFMcpRepository repository(m_context->getDatabase());
    QPointer<PDFAppMcpController> self(this);
    repository.claimServer(m_instanceId, QCoreApplication::applicationPid(), m_server->getPort(), PDFMcpServerRowState::Running, getAddress(), &claimed);
    if (!self)
    {
        // The notification of the row deleted the controller (with its server)
        return;
    }
    m_isOwner = claimed;
    if (!claimed && m_server)
    {
        // Another live instance owns the server (it changed the port meanwhile):
        // only one server runs
        m_server->stop();
    }
}

void PDFAppMcpController::releaseOwnership()
{
    // The flag is cleared before the write: its notification may delete the controller
    const bool wasOwner = m_isOwner;
    m_isOwner = false;
    if (wasOwner && m_context && m_context->getDatabase())
    {
        PDFMcpRepository(m_context->getDatabase()).releaseServer(m_instanceId);
    }
}

void PDFAppMcpController::syncClients()
{
    // The owner writes the connected assistants of its server into the rows
    // mcp_client (plan 6), the other instances show the rows (package E)
    if (!m_isOwner || !m_server || !m_context || !m_context->getDatabase())
    {
        return;
    }

    PDFMcpRepository repository(m_context->getDatabase());
    std::vector<PDFMcpClientRow> rows;
    QStringList keys;
    for (const PDFMcpAssistantInfo& assistant : m_server->getAssistants())
    {
        if (keys.contains(assistant.key) || int(rows.size()) >= PDFMcpRepository::MAXIMUM_CLIENTS)
        {
            continue;
        }

        PDFMcpClientRow row;
        row.key = assistant.key;
        row.name = PDFAppMcpClientNames::getDisplayName(assistant.name, assistant.title);
        row.address = assistant.isRemote ? assistant.address.left(PDFMcpRepository::MAXIMUM_ADDRESS_LENGTH) : QString();
        row.connected = assistant.connectedAt;
        keys << row.key;
        rows.push_back(std::move(row));
    }

    QStringList removedKeys;
    std::vector<PDFMcpClientRow> currentRows = repository.getClients();
    for (const PDFMcpClientRow& current : currentRows)
    {
        if (!keys.contains(current.key))
        {
            removedKeys << current.key;
        }
    }
    // Every write notifies the application at once: a slot may delete the controller
    QPointer<PDFAppMcpController> self(this);
    if (!removedKeys.isEmpty())
    {
        repository.removeClients(removedKeys);
    }

    for (const PDFMcpClientRow& row : rows)
    {
        if (!self)
        {
            return;
        }

        // A row, which is the same, is not written again (no notification)
        bool isSame = false;
        for (const PDFMcpClientRow& current : currentRows)
        {
            if (current.key == row.key)
            {
                isSame = current.name == row.name && current.address == row.address && current.connected == row.connected;
                break;
            }
        }
        if (!isSame)
        {
            repository.upsertClient(row);
        }
    }
}

void PDFAppMcpController::onServerAssistantsChanged()
{
    QPointer<PDFAppMcpController> self(this);
    syncClients();
    if (self)
    {
        Q_EMIT assistantsChanged();
    }
}

void PDFAppMcpController::onServerStateChanged()
{
    if (!m_server)
    {
        return;
    }

    // The writes of the row notify the application at once: a slot may delete the
    // controller (UnitTestsAppMcpLifetime)
    QPointer<PDFAppMcpController> self(this);
    if (m_server->getState() == PDFMcpServerState::Running && !m_isOwner)
    {
        claimOwnership();
        if (self)
        {
            syncClients();
        }
        if (self)
        {
            processRequests();
        }

        // A change of the connection while the server was starting (Require
        // access token, the port, the bind mode or the token - also written by
        // another instance) found no running server: the server, which listens
        // with an older configuration than the settings, restarts (review RTOK -
        // Require access token turned on during the start ran without the token)
        if (self)
        {
            applyConnection();
        }
    }
    else if (m_server->getState() != PDFMcpServerState::Running && m_server->getState() != PDFMcpServerState::Stopping && m_isOwner)
    {
        releaseOwnership();
    }

    if (self)
    {
        Q_EMIT stateChanged();
    }
}

void PDFAppMcpController::onHeartbeat()
{
    if (!m_context || !m_context->getDatabase())
    {
        return;
    }

    PDFMcpRepository repository(m_context->getDatabase());
    if (m_isOwner)
    {
        bool isOwner = false;
        if (repository.heartbeat(m_instanceId, &isOwner).isError())
        {
            // A refused write (the lock of another process, a full disk) is not a
            // loss of the row: the next heartbeat tries again, the server runs on
            return;
        }
        if (!isOwner)
        {
            // Another instance took the row (this one was stale): it runs the server
            m_isOwner = false;
            if (m_server)
            {
                m_server->stop();
            }
            Q_EMIT stateChanged();
            return;
        }
        processRequests();
        return;
    }

    // The row of another owner: its end (its heartbeat stale, its process gone)
    // enables Start (plan 2.6). Whether the owner was alive is remembered from the
    // previous tick: the previous row tested against the current time is stale
    // at the same moment as the current one, so the end of an owner, which was
    // killed, was never announced (step 9, UnitTestsAppMultiInstance)
    if (setLastRow(repository.getServer()))
    {
        QPointer<PDFAppMcpController> guard(this);
        clearRefusedStart();
        if (!guard)
        {
            return;
        }
        Q_EMIT stateChanged();
        if (!guard)
        {
            return;
        }
        Q_EMIT assistantsChanged();
    }
}

void PDFAppMcpController::processRequests()
{
    if (!m_isOwner || !m_context || !m_context->getDatabase())
    {
        return;
    }

    PDFMcpRepository repository(m_context->getDatabase());
    QPointer<PDFAppMcpController> self(this);
    for (const PDFMcpRequestRow& request : repository.takeRequests())
    {
        if (!self)
        {
            return;
        }
        switch (request.kind)
        {
            case PDFMcpRequestKind::Disconnect:
                disconnectAssistant(request.key);
                break;
            case PDFMcpRequestKind::DisconnectAll:
                disconnectAll();
                break;
            case PDFMcpRequestKind::ShowWindow:
                raiseWindow();
                break;
        }
    }
}

void PDFAppMcpController::raiseWindow()
{
    QPointer<PDFAppMcpController> guard(this);
    Q_EMIT showWindowRequested();
    if (!guard || !m_host)
    {
        return;
    }

    // The window of the owner as a forwarded request without a token: a minimized
    // maximized window stays maximized, outside of Windows the user is alerted, when
    // the window system refuses the activation (the request of the other instance
    // carries no activation token)
    if (PDFAppMainWindow* window = m_host->getResolver().getWindow())
    {
        PDFAppActivation::activateWindow(window, QString());
    }
}

void PDFAppMcpController::applyPermissions()
{
    if (m_host && m_context && m_context->getDatabase())
    {
        m_host->setPermissions(PDFAppMcpPermissions::fromSettings(m_context->getSettings()));
    }
}

void PDFAppMcpController::applyConnection()
{
    if (!isRunning() || !m_isOwner)
    {
        return;
    }

    const PDFMcpServerConfig config = createConfig();
    const PDFMcpServerConfig& current = m_server->getConfig();
    const bool isTokenChanged = config.token != current.token;
    const bool isRestartNeeded = config.port != current.port || config.bindMode != current.bindMode || config.requireToken != current.requireToken || isTokenChanged;
    QPointer<PDFAppMcpController> self(this);
    if (isTokenChanged)
    {
        // Every old key is unusable: everybody is disconnected
        m_server->disconnectAll();
    }
    if (self && isRestartNeeded)
    {
        restart();
    }
}

void PDFAppMcpController::onNotifierChanged(const QString& topic, const QString& key)
{
    if (m_isShutDown)
    {
        return;
    }

    if (topic == PDFSettingsRepository::getTopic())
    {
        if (key == PDFSettingKeys::mcpPermissionRead() || key == PDFSettingKeys::mcpPermissionEdit() || key == PDFSettingKeys::mcpPermissionFiles())
        {
            applyPermissions();
        }
        else if (key == PDFSettingKeys::mcpPort() || key == PDFSettingKeys::mcpAllowRemote() || key == PDFSettingKeys::mcpRequireToken() || key == PDFSettingKeys::mcpToken())
        {
            // Connection: the server restarts (a new token disconnects everybody)
            QPointer<PDFAppMcpController> self(this);
            if (key == PDFSettingKeys::mcpToken() && m_server)
            {
                m_server->disconnectAll();
            }
            if (self)
            {
                applyConnection();
            }
            if (self)
            {
                Q_EMIT stateChanged();
            }
        }
        return;
    }

    if (topic == PDFMcpRepository::getRequestTopic())
    {
        processRequests();
    }
    else if (topic == PDFMcpRepository::getServerTopic())
    {
        QPointer<PDFAppMcpController> guard(this);
        if (setLastRow(getServerRow()))
        {
            clearRefusedStart();
        }
        if (guard)
        {
            Q_EMIT stateChanged();
        }
    }
    else if (topic == PDFMcpRepository::getClientTopic())
    {
        Q_EMIT assistantsChanged();
    }
    else if (topic == PDFMcpRepository::getActivityTopic())
    {
        Q_EMIT activityChanged();
    }
}

void PDFAppMcpController::onEverythingChanged()
{
    if (m_isShutDown)
    {
        return;
    }

    // Every setting may have changed (an imported backup): the permissions and
    // the connection - a server running with an older port, bind mode or token
    // rule than the settings shown by the AI page is restarted (review RC7-H)
    QPointer<PDFAppMcpController> guard(this);
    applyPermissions();
    if (guard)
    {
        applyConnection();
    }
    if (guard)
    {
        processRequests();
    }
    if (!guard)
    {
        return;
    }
    // Only a change of the row ends a refused start: a PDF4QT of another profile
    // still has the port (review RMULTI of step 9)
    if (setLastRow(getServerRow()))
    {
        clearRefusedStart();
        if (!guard)
        {
            return;
        }
    }

    Q_EMIT stateChanged();
    if (!guard)
    {
        return;
    }
    Q_EMIT assistantsChanged();
    if (!guard)
    {
        return;
    }
    Q_EMIT activityChanged();
}

}   // namespace pdf
