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

#include "pdfmcpserver.h"
#include "pdfmcpdispatcher.h"
#include "pdfmcphttp.h"
#include "pdfmcplistener.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>

namespace pdf
{

// ----------------------------------------------------------------------------
// PDFMcpServerShared
// ----------------------------------------------------------------------------

PDFMcpServerShared::PDFMcpServerShared(const PDFMcpServerConfig& config) :
    link(std::make_shared<PDFMcpDispatcherLink>()),
    m_config(config)
{
    if (!m_config.token.isEmpty())
    {
        m_tokenHash = QCryptographicHash::hash(m_config.token, QCryptographicHash::Sha256);
    }
}

QByteArray PDFMcpServerShared::getServerVersion() const
{
    const QString version = m_config.serverInfo.version;
    return version.isEmpty() ? PDFMcpServer::getDefaultVersion() : version.toLatin1();
}

std::shared_ptr<const PDFMcpCatalog> PDFMcpServerShared::getCatalog() const
{
    QMutexLocker locker(&m_mutex);
    return m_catalog;
}

void PDFMcpServerShared::setCatalog(std::shared_ptr<const PDFMcpCatalog> catalog)
{
    QMutexLocker locker(&m_mutex);
    m_catalog = std::move(catalog);
}

// ----------------------------------------------------------------------------
// PDFMcpPortProbe
// ----------------------------------------------------------------------------

PDFMcpPortProbe::PDFMcpPortProbe(QObject* parent) :
    QObject(parent)
{

}

PDFMcpPortProbe::~PDFMcpPortProbe() = default;

void PDFMcpPortProbe::start(quint16 port, int timeout)
{
    if (m_socket || m_result != Result::None)
    {
        return;
    }

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &PDFMcpPortProbe::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &PDFMcpPortProbe::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &PDFMcpPortProbe::onErrorOccurred);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &PDFMcpPortProbe::onTimeout);
    m_timer->start(timeout);

    m_socket->connectToHost(QHostAddress(QHostAddress::LocalHost), port);
}

void PDFMcpPortProbe::onConnected()
{
    if (!m_socket)
    {
        return;
    }

    const QByteArray host = "127.0.0.1:" + QByteArray::number(m_socket->peerPort());
    m_socket->write("GET /mcp HTTP/1.1\r\nHost: " + host + "\r\nAccept: application/json, text/event-stream\r\nConnection: close\r\n\r\n");
}

void PDFMcpPortProbe::onReadyRead()
{
    if (!m_socket)
    {
        return;
    }

    m_buffer.append(m_socket->readAll());
    const qsizetype end = m_buffer.indexOf("\r\n\r\n");
    if (end < 0)
    {
        if (m_buffer.size() > PDFMcpLimits::MAXIMUM_HEADER_SIZE)
        {
            finish(Result::OtherProgram);
        }
        return;
    }

    const QList<QByteArray> lines = m_buffer.left(end).split('\n');
    for (const QByteArray& line : lines)
    {
        const qsizetype colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == "server")
        {
            m_serverHeader = line.mid(colon + 1).trimmed().left(256);
            break;
        }
    }

    finish(m_serverHeader.startsWith("PDF4QT/") ? Result::Pdf4Qt : Result::OtherProgram);
}

void PDFMcpPortProbe::onErrorOccurred(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::ConnectionRefusedError)
    {
        finish(Result::Unreachable);
        return;
    }

    // A closed connection without an answer (or any other error): not PDF4QT
    if (m_buffer.contains("\r\n\r\n"))
    {
        onReadyRead();
    }
    finish(Result::OtherProgram);
}

void PDFMcpPortProbe::onTimeout()
{
    finish(Result::OtherProgram);
}

void PDFMcpPortProbe::finish(Result result)
{
    if (m_result != Result::None)
    {
        return;
    }

    m_result = result;
    if (m_timer)
    {
        m_timer->stop();
    }
    if (m_socket)
    {
        m_socket->disconnect(this);
        m_socket->abort();
    }

    Q_EMIT finished(result);
}

// ----------------------------------------------------------------------------
// PDFMcpServer
// ----------------------------------------------------------------------------

PDFMcpServer::PDFMcpServer(PDFMcpHost* host, QObject* parent) :
    QObject(parent),
    m_host(host),
    m_catalog(std::make_shared<PDFMcpCatalog>())
{

}

PDFMcpServer::~PDFMcpServer()
{
    stopInternals();
}

QByteArray PDFMcpServer::getDefaultVersion()
{
#ifdef PDF4QT_PROJECT_VERSION
    return QByteArrayLiteral(PDF4QT_PROJECT_VERSION);
#else
    return QByteArrayLiteral("0");
#endif
}

bool PDFMcpServer::start(const PDFMcpServerConfig& config)
{
    if (m_state == PDFMcpServerState::Running || m_state == PDFMcpServerState::Starting || m_state == PDFMcpServerState::Stopping)
    {
        return false;
    }

    stopInternals();

    m_config = config;
    if (m_config.bindMode == PDFMcpBindMode::Remote)
    {
        m_config.requireToken = true;
    }
    m_errorMessage.clear();
    m_port = 0;
    m_requestedPort = config.port;

    std::shared_ptr<PDFMcpServerShared> shared = std::make_shared<PDFMcpServerShared>(m_config);
    shared->host = m_host;
    shared->setCatalog(getCatalog());
    shared->link->setServer(this);
    {
        QMutexLocker locker(&m_catalogMutex);
        m_shared = shared;
    }

    m_ioThreads.start(m_config.ioThreadCount, m_shared);

    const quint64 run = ++m_run;
    m_controlThread = new QThread();
    m_controlThread->setObjectName(QStringLiteral("PDF4QT MCP control"));
    m_control = new PDFMcpControl(run, m_shared, &m_ioThreads);
    m_control->moveToThread(m_controlThread);
    connect(m_control, &PDFMcpControl::listenFinished, this, &PDFMcpServer::onListenFinished, Qt::QueuedConnection);
    m_controlThread->start();

    // The listen is queued before the state is announced: a slot of stateChanged
    // may stop or delete the server (nothing of the server is used after it)
    QMetaObject::invokeMethod(m_control, &PDFMcpControl::listen, Qt::QueuedConnection);
    setState(PDFMcpServerState::Starting);
    return true;
}

void PDFMcpServer::stop()
{
    if (m_state == PDFMcpServerState::Stopped)
    {
        return;
    }

    const bool wasActive = m_state == PDFMcpServerState::Running || m_state == PDFMcpServerState::Starting;
    if (wasActive)
    {
        // A slot of stateChanged may delete the server
        QPointer<PDFMcpServer> self(this);
        setState(PDFMcpServerState::Stopping);
        if (!self)
        {
            return;
        }
    }

    stopInternals();
    m_port = 0;
    setState(PDFMcpServerState::Stopped);
}

void PDFMcpServer::stopInternals()
{
    if (m_probe)
    {
        delete m_probe.data();
    }

    // 1-4: the listeners are closed, the run is marked stopping (new requests
    // get 503), the dispatcher cancels the calls and ends the streams
    if (m_shared)
    {
        m_shared->setStopping();
    }

    if (m_control && m_controlThread && m_controlThread->isRunning())
    {
        QMetaObject::invokeMethod(m_control, &PDFMcpControl::shutdown, Qt::BlockingQueuedConnection);
    }

    // 5: the sockets are closed and the threads joined
    m_ioThreads.stop();

    if (m_controlThread)
    {
        m_controlThread->quit();
        m_controlThread->wait();
    }

    // The control thread has ended: its objects are deleted here
    delete m_control;
    m_control = nullptr;
    delete m_controlThread;
    m_controlThread = nullptr;

    QMutexLocker locker(&m_catalogMutex);
    m_shared.reset();
}

void PDFMcpServer::setState(PDFMcpServerState state)
{
    if (m_state != state)
    {
        m_state = state;
        Q_EMIT stateChanged();
    }
}

void PDFMcpServer::onListenFinished(quint64 run, bool ok, int socketError, const QString& message, quint16 port)
{
    // A result of an older run (stopped meanwhile) is dropped
    if (run != m_run || m_state != PDFMcpServerState::Starting)
    {
        return;
    }

    if (ok)
    {
        m_port = port;
        setState(PDFMcpServerState::Running);
        return;
    }

    stopInternals();
    m_errorMessage = message;

    if (socketError == int(QAbstractSocket::AddressInUseError) && m_requestedPort != 0)
    {
        // Another program or another PDF4QT: the probe decides (plan 2.6)
        m_probe = new PDFMcpPortProbe(this);
        connect(m_probe, &PDFMcpPortProbe::finished, this, &PDFMcpServer::onProbeFinished);
        m_probe->start(m_requestedPort, m_config.portProbeTimeout);
        return;
    }

    setState(PDFMcpServerState::Failed);
}

void PDFMcpServer::onProbeFinished(PDFMcpPortProbe::Result result)
{
    if (m_probe)
    {
        m_probe->deleteLater();
        m_probe = nullptr;
    }

    if (m_state != PDFMcpServerState::Starting)
    {
        return;
    }

    switch (result)
    {
        case PDFMcpPortProbe::Result::Pdf4Qt:
            setState(PDFMcpServerState::PortInUseByPdf4Qt);
            break;

        case PDFMcpPortProbe::Result::Unreachable:
        case PDFMcpPortProbe::Result::OtherProgram:
        case PDFMcpPortProbe::Result::None:
            setState(PDFMcpServerState::PortInUseByOtherProgram);
            break;
    }
}

void PDFMcpServer::publishCatalog(std::shared_ptr<const PDFMcpCatalog> catalog)
{
    if (!catalog)
    {
        return;
    }

    std::shared_ptr<PDFMcpServerShared> shared;
    {
        QMutexLocker locker(&m_catalogMutex);
        m_catalog = catalog;
        shared = m_shared;
    }

    // The dispatcher compares the lists with the previous catalog and sends
    // list_changed to the streams, which asked for it
    if (shared)
    {
        shared->setCatalog(std::move(catalog));
        shared->link->catalogPublished();
    }
}

std::shared_ptr<const PDFMcpCatalog> PDFMcpServer::getCatalog() const
{
    QMutexLocker locker(&m_catalogMutex);
    return m_catalog;
}

std::shared_ptr<PDFMcpServerShared> PDFMcpServer::getShared() const
{
    QMutexLocker locker(&m_catalogMutex);
    return m_shared;
}

void PDFMcpServer::notifyResourceUpdated(const QString& uri)
{
    if (std::shared_ptr<PDFMcpServerShared> shared = getShared())
    {
        shared->link->resourceUpdated(uri);
    }
}

void PDFMcpServer::disconnectAssistant(const QString& key)
{
    if (std::shared_ptr<PDFMcpServerShared> shared = getShared())
    {
        shared->link->disconnectAssistant(key);
    }
}

void PDFMcpServer::disconnectAll()
{
    if (std::shared_ptr<PDFMcpServerShared> shared = getShared())
    {
        shared->link->disconnectAll();
    }
}

std::vector<PDFMcpAssistantInfo> PDFMcpServer::getAssistants() const
{
    if (std::shared_ptr<PDFMcpServerShared> shared = getShared())
    {
        return shared->link->getAssistants();
    }
    return std::vector<PDFMcpAssistantInfo>();
}

void PDFMcpServer::checkTimeouts()
{
    if (std::shared_ptr<PDFMcpServerShared> shared = getShared())
    {
        shared->link->checkTimeouts();
    }
}

void PDFMcpServer::onAssistantsChanged()
{
    Q_EMIT assistantsChanged();
}

}   // namespace pdf
