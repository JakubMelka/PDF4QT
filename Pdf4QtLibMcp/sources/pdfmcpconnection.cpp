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

#include "pdfmcpconnection.h"
#include "pdfmcpassistants.h"
#include "pdfmcpdispatcher.h"
#include "pdfmcperrorcodes.h"
#include "pdfthreadannotations.h"
#include "pdfmcpserver.h"
#include "pdfmcpsse.h"

#include <QHostAddress>
#include <QMutexLocker>
#include <QTimer>

namespace pdf
{

// ----------------------------------------------------------------------------
// PDFMcpExchange
// ----------------------------------------------------------------------------

/// Counter of the ids of the exchanges of the process
class PDFMcpExchangeCounter
{
public:
    PDFMcpExchangeCounter() = delete;

    static quint64 createId()
    {
        static std::atomic<quint64> s_counter = 0;
        return ++s_counter;
    }
};

PDFMcpExchange::PDFMcpExchange(PDFMcpHttpRequest request, QString peerAddress, bool isRemote, QByteArray serverVersion) :
    m_id(PDFMcpExchangeCounter::createId()),
    m_request(std::move(request)),
    m_peerAddress(std::move(peerAddress)),
    m_isRemote(isRemote),
    m_serverVersion(std::move(serverVersion)),
    m_lastWriteTime(PDFMcpClock::now())
{

}

PDFMcpExchange::~PDFMcpExchange() = default;

bool PDFMcpExchange::queue(QByteArray data, bool finish, bool closeConnection)
{
    // Called under the mutex
    if (m_isClosed.load() || m_isFinished)
    {
        return false;
    }

    m_output.append(data);
    m_lastWriteTime = PDFMcpClock::now();
    if (finish)
    {
        m_isFinished = true;
        m_isOutputFinished = true;
        m_closeConnection = closeConnection;
    }

    if (m_connection && !m_isNotified)
    {
        // The connection lives while it is attached (its destructor detaches it
        // under this mutex); an event posted to it is removed with it
        m_isNotified = true;
        PDFThreadAnnotations::TransportScope transportScope;
        QMetaObject::invokeMethod(m_connection, &PDFMcpConnection::flushExchange, Qt::QueuedConnection);
    }
    return true;
}

bool PDFMcpExchange::respond(PDFMcpHttpResponse response)
{
    QMutexLocker locker(&m_mutex);
    if (m_isStreaming)
    {
        return false;
    }

    // A request with "Connection: close" (also a request answered before its
    // body is read) is the last one of its connection: the response says so
    if (m_request.isConnectionClose())
    {
        response.closeConnection = true;
    }
    return queue(response.serialize(m_serverVersion), true, response.closeConnection);
}

bool PDFMcpExchange::beginStream(const std::vector<std::pair<QByteArray, QByteArray>>& headers)
{
    QMutexLocker locker(&m_mutex);
    if (m_isStreaming)
    {
        return !m_isFinished && !m_isClosed.load();
    }

    PDFMcpHttpResponse response;
    response.status = 200;
    response.headers = headers;
    if (!queue(response.serializeStreamHead(m_serverVersion), false, false))
    {
        return false;
    }
    m_isStreaming = true;
    return true;
}

bool PDFMcpExchange::sendEvent(QByteArrayView json)
{
    if (!beginStream())
    {
        return false;
    }

    QMutexLocker locker(&m_mutex);
    return queue(PDFMcpSseWriter::createEvent(json), false, false);
}

bool PDFMcpExchange::sendComment(QByteArrayView text)
{
    if (!beginStream())
    {
        return false;
    }

    QMutexLocker locker(&m_mutex);
    return queue(PDFMcpSseWriter::createComment(text), false, false);
}

bool PDFMcpExchange::endStream(bool closeConnection)
{
    if (!beginStream())
    {
        return false;
    }

    QMutexLocker locker(&m_mutex);
    return queue(PDFMcpSseWriter::createLastChunk(), true, closeConnection);
}

bool PDFMcpExchange::isFinished() const
{
    QMutexLocker locker(&m_mutex);
    return m_isFinished;
}

bool PDFMcpExchange::isStreaming() const
{
    QMutexLocker locker(&m_mutex);
    return m_isStreaming;
}

qint64 PDFMcpExchange::getLastWriteTime() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastWriteTime;
}

QByteArray PDFMcpExchange::takeOutput(bool* isFinished, bool* closeConnection)
{
    QMutexLocker locker(&m_mutex);
    QByteArray output;
    output.swap(m_output);
    m_isNotified = false;

    if (isFinished)
    {
        *isFinished = m_isOutputFinished;
    }
    if (closeConnection)
    {
        *closeConnection = m_closeConnection;
    }
    m_isOutputFinished = false;
    return output;
}

void PDFMcpExchange::attach(PDFMcpConnection* connection)
{
    QMutexLocker locker(&m_mutex);
    m_connection = connection;
    m_isNotified = false;
}

void PDFMcpExchange::markClosed()
{
    QMutexLocker locker(&m_mutex);
    m_isClosed.store(true);
    m_output.clear();
}

// ----------------------------------------------------------------------------
// PDFMcpConnection
// ----------------------------------------------------------------------------

PDFMcpConnection::PDFMcpConnection(std::shared_ptr<PDFMcpServerShared> shared, QObject* parent) :
    QObject(parent),
    m_shared(std::move(shared))
{

}

PDFMcpConnection::~PDFMcpConnection()
{
    if (m_shared && m_shared->link)
    {
        // Under the mutex of the link: no eviction is posted to it any more
        m_shared->link->removeConnection(this);
    }

    if (m_exchange)
    {
        m_exchange->markClosed();
        m_exchange->attach(nullptr);
        m_exchange.reset();
    }

    if (m_shared)
    {
        if (m_isCounted)
        {
            --m_shared->connectionCount;
        }
        if (m_isAddressCounted && m_shared->link)
        {
            m_shared->link->releaseAddress(m_peerAddress);
        }
    }
}

bool PDFMcpConnection::start(qintptr socketDescriptor)
{
    m_socket = new QTcpSocket(this);
    if (!m_socket->setSocketDescriptor(socketDescriptor))
    {
        return false;
    }

    m_isCounted = true;
    const int count = ++m_shared->connectionCount;

    // An IPv4 peer of a dual-stack socket is reported as IPv4
    QHostAddress address = m_socket->peerAddress();
    bool isIPv4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isIPv4);
    if (isIPv4)
    {
        address = QHostAddress(ipv4);
    }
    address.setScopeId(QString());
    m_peerAddress = address.toString();
    m_isRemote = !address.isLoopback();

    // A pipelined request is kept by the socket while a response is written;
    // a client, which sends more, waits by the flow control of TCP
    m_socket->setReadBufferSize(MAXIMUM_PIPELINED_INPUT + 64 * 1024);

    connect(m_socket, &QTcpSocket::readyRead, this, &PDFMcpConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::bytesWritten, this, &PDFMcpConnection::onBytesWritten);
    connect(m_socket, &QTcpSocket::disconnected, this, &PDFMcpConnection::onDisconnected);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &PDFMcpConnection::onTimeout);

    if (m_shared->isStopping() || !m_shared->link)
    {
        respondAndClose(PDFMcpHttpResponse::createJsonRpcError(503, PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is not available")));
        return true;
    }

    if (m_isRemote)
    {
        m_isAddressCounted = m_shared->link->acquireAddress(m_peerAddress, PDFMcpLimits::MAXIMUM_CONNECTIONS_PER_REMOTE_ADDRESS);
        if (!m_isAddressCounted)
        {
            respondAndClose(PDFMcpHttpResponse::createJsonRpcError(503, PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("Too many connections from this address")));
            return true;
        }
    }

    // At the limit the oldest connection, which has not completed a request,
    // is evicted (follow-up 22: idle connections - a slow or missing head -
    // cannot hold every slot); the chosen connections, which are still closing,
    // do not count. Only without such a connection the new one is refused.
    if (count - m_shared->link->getPendingEvictions() > PDFMcpLimits::MAXIMUM_CONNECTIONS && !m_shared->link->evictIdleConnection())
    {
        respondAndClose(PDFMcpHttpResponse::createJsonRpcError(503, PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is not available")));
        return true;
    }

    m_isIdle = true;
    m_shared->link->addIdleConnection(this);

    m_parser = PDFMcpHttpParser();
    startTimer(m_shared->getConfig().headerTimeout);

    // Data may have arrived before the signals were connected
    if (m_socket->bytesAvailable() > 0)
    {
        processInput();
    }
    return true;
}

bool PDFMcpConnection::leaveIdle()
{
    if (!m_isIdle)
    {
        return false;
    }

    m_isIdle = false;
    return m_shared->link && m_shared->link->leaveIdleConnection(this);
}

void PDFMcpConnection::evict()
{
    // A connection closing already (also one evicted by processInput, when its
    // head was completed after the choice) has nothing to do. A chosen
    // connection never dispatches a request (processInput evicts it first).
    if (!m_socket || m_state == State::Closing)
    {
        return;
    }

    m_isIdle = false;
    respondAndClose(PDFMcpHttpResponse::createJsonRpcError(503, PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("Too many connections; this connection sent no request")));
}

void PDFMcpConnection::startTimer(int timeout)
{
    if (m_timer)
    {
        m_timer->start(qMax(1, timeout));
    }
}

void PDFMcpConnection::abort()
{
    // The stop of the server: the last output of the exchange (the final
    // events of the streams) is handed to the system before the socket closes
    if (m_socket && m_exchange)
    {
        bool isFinished = false;
        bool closeConnection = false;
        const QByteArray data = m_exchange->takeOutput(&isFinished, &closeConnection);
        if (!data.isEmpty() && m_socket->state() == QAbstractSocket::ConnectedState)
        {
            m_socket->write(data);
        }
    }

    if (m_socket)
    {
        if (m_socket->state() == QAbstractSocket::ConnectedState)
        {
            m_socket->flush();
        }
        closeExchange();
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

void PDFMcpConnection::onReadyRead()
{
    if (!m_socket)
    {
        return;
    }

    switch (m_state)
    {
        case State::Waiting:
        case State::Reading:
            processInput();
            break;

        case State::Dispatched:
            // A pipelined request waits in the buffer of the socket
            break;

        case State::Closing:
            m_socket->readAll();
            break;
    }
}

void PDFMcpConnection::processInput()
{
    constexpr qint64 READ_SIZE = 64 * 1024;

    while (m_socket && (m_state == State::Waiting || m_state == State::Reading))
    {
        const QByteArray data = m_socket->read(READ_SIZE);
        const bool wasHeadComplete = m_parser.isHeadComplete();
        const PDFMcpHttpParser::Status status = data.isEmpty() ? m_parser.parse() : m_parser.feed(data);

        if (m_state == State::Waiting && m_parser.hasStarted())
        {
            // The timer of the head goes on (it started when the connection began
            // to wait), so a slow head cannot hold the connection longer
            m_state = State::Reading;
        }

        switch (status)
        {
            case PDFMcpHttpParser::Status::Error:
            {
                PDFMcpHttpResponse response = PDFMcpHttpResponse::createJsonRpcError(m_parser.getErrorStatus(), PDFMcpErrorCodes::INVALID_REQUEST, m_parser.getErrorMessage());
                respondAndClose(response);
                return;
            }

            case PDFMcpHttpParser::Status::RequestComplete:
                if (leaveIdle())
                {
                    // Chosen for the eviction before its head was complete
                    evict();
                    return;
                }
                dispatchRequest();
                return;

            case PDFMcpHttpParser::Status::NeedMoreData:
                if (!wasHeadComplete && m_parser.isHeadComplete())
                {
                    // A request in progress is never evicted
                    if (leaveIdle())
                    {
                        evict();
                        return;
                    }

                    // A client without the token never makes the server buffer a
                    // large body (8 MiB x 64 connections): it is answered (401 /
                    // 429 by the dispatcher) right after its head
                    if (m_parser.getMissingBodySize() > EARLY_REJECTION_SIZE && m_shared->getConfig().isTokenRequired() &&
                        !PDFMcpDispatcher::isAuthorizationValid(m_parser.getRequest(), m_shared->getTokenHash()))
                    {
                        m_closeAfterResponse = true;
                        dispatchExchange(m_parser.takeHead());
                        return;
                    }
                    startTimer(m_shared->getConfig().bodyTimeout);
                }
                break;
        }

        if (data.isEmpty())
        {
            break;
        }
    }
}

void PDFMcpConnection::dispatchRequest()
{
    PDFMcpHttpRequest request = m_parser.takeRequest();
    m_closeAfterResponse = request.isConnectionClose();
    dispatchExchange(std::move(request));
}

void PDFMcpConnection::dispatchExchange(PDFMcpHttpRequest request)
{
    if (m_timer)
    {
        m_timer->stop();
    }

    m_hadRequest = true;
    m_exchange = std::make_shared<PDFMcpExchange>(std::move(request), m_peerAddress, m_isRemote, m_shared->getServerVersion());
    m_exchange->attach(this);
    m_state = State::Dispatched;

    if (m_shared->isStopping() || !m_shared->link || !m_shared->link->submit(m_exchange))
    {
        m_exchange->respond(PDFMcpHttpResponse::createJsonRpcError(503, PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping")));
    }
}

void PDFMcpConnection::flushExchange()
{
    if (!m_exchange || !m_socket)
    {
        return;
    }

    bool isFinished = false;
    bool closeConnection = false;
    const QByteArray data = m_exchange->takeOutput(&isFinished, &closeConnection);
    if (!data.isEmpty())
    {
        writeOutput(data);
    }

    if (m_socket && m_exchange && isFinished)
    {
        finishExchange(closeConnection);
    }
}

void PDFMcpConnection::writeOutput(const QByteArray& data)
{
    if (!m_socket)
    {
        return;
    }

    m_socket->write(data);

    // Backpressure: a client, which does not read, loses the connection
    if (m_socket->bytesToWrite() > PDFMcpLimits::MAXIMUM_PENDING_OUTPUT)
    {
        closeExchange();
        m_socket->disconnect(this);
        m_socket->abort();
        deleteLater();
    }
}

void PDFMcpConnection::finishExchange(bool closeConnection)
{
    if (m_exchange)
    {
        m_exchange->attach(nullptr);
        m_exchange.reset();
    }

    if (closeConnection || m_closeAfterResponse || m_shared->isStopping())
    {
        startClosing();
        return;
    }

    // The next request of the connection (keep-alive, also a pipelined one)
    m_parser.reset();
    m_state = State::Waiting;
    startTimer(m_shared->getConfig().headerTimeout);
    processInput();
}

void PDFMcpConnection::closeExchange()
{
    if (!m_exchange)
    {
        return;
    }

    std::shared_ptr<PDFMcpExchange> exchange = std::move(m_exchange);
    m_exchange.reset();
    const bool isFinished = exchange->isFinished();
    exchange->markClosed();
    exchange->attach(nullptr);

    // The dispatcher cancels the call of an exchange, which was not answered
    // (or ends the stream of a subscription)
    if (m_shared->link && (!isFinished || exchange->isStreaming()))
    {
        m_shared->link->exchangeClosed(exchange);
    }
}

void PDFMcpConnection::respondAndClose(const PDFMcpHttpResponse& response)
{
    if (!m_socket || m_state == State::Closing)
    {
        return;
    }

    leaveIdle();
    PDFMcpHttpResponse closingResponse = response;
    closingResponse.closeConnection = true;
    m_state = State::Closing;
    if (m_timer)
    {
        m_timer->stop();
    }
    m_socket->write(closingResponse.serialize(m_shared->getServerVersion()));
    startClosing();
}

void PDFMcpConnection::startClosing()
{
    leaveIdle();
    m_state = State::Closing;
    if (m_timer)
    {
        m_timer->stop();
    }
    if (m_socket && m_socket->bytesToWrite() == 0)
    {
        onBytesWritten(0);
    }
    else if (m_socket)
    {
        // The rest of the response is written: a client, which stops reading
        // it, would hold the connection for ever (no further bytesWritten)
        startTimer(m_shared->getConfig().bodyTimeout);
    }
}

void PDFMcpConnection::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);

    if (!m_socket || m_state != State::Closing || m_isLingering)
    {
        return;
    }

    if (m_socket->bytesToWrite() == 0)
    {
        // The client may still be sending (an error before the end of its
        // request): a close with unread data would reset the connection and the
        // client would lose the response. The input is discarded for a while,
        // the client closes the connection after it has read the response.
        m_isLingering = true;
        m_socket->readAll();
        startTimer(LINGER_TIME);
    }
    else
    {
        // The client reads: the time of the writing starts again
        startTimer(m_shared->getConfig().bodyTimeout);
    }
}

void PDFMcpConnection::onDisconnected()
{
    closeExchange();
    if (m_timer)
    {
        m_timer->stop();
    }
    deleteLater();
}

void PDFMcpConnection::onTimeout()
{
    if (!m_socket)
    {
        return;
    }

    switch (m_state)
    {
        case State::Waiting:
            if (!m_hadRequest)
            {
                // A new connection without a request
                respondAndClose(PDFMcpHttpResponse::createJsonRpcError(408, PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("The request was not completed in time")));
            }
            else
            {
                // An idle kept-alive connection closes without a response
                startClosing();
            }
            break;

        case State::Reading:
            respondAndClose(PDFMcpHttpResponse::createJsonRpcError(408, PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("The request was not completed in time")));
            break;

        case State::Dispatched:
            break;

        case State::Closing:
            if (!m_isLingering)
            {
                // The client has not read the last response for the time of a
                // body: the connection (one of the limited slots) is dropped
                m_socket->disconnect(this);
                m_socket->abort();
                deleteLater();
                break;
            }

            m_socket->readAll();
            m_socket->disconnectFromHost();
            break;
    }
}

}   // namespace pdf
