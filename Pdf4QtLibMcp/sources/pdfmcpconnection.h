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

#ifndef PDFMCPCONNECTION_H
#define PDFMCPCONNECTION_H

#include "pdf4qtlibmcp_export.h"
#include "pdfmcphttp.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTcpSocket>

#include <atomic>
#include <memory>
#include <vector>

class QTimer;

namespace pdf
{

class PDFMcpConnection;
class PDFMcpServerShared;

/// One HTTP request and its response (plan 2.2, 2.4): created by the connection
/// in its I/O thread, answered by the dispatcher in the control thread. Every
/// function is thread safe: the output is queued under a mutex and the
/// connection is told by one queued invocation per batch; the connection takes
/// the output in its thread. A response is either complete (respond) or a
/// stream of SSE events (beginStream, sendEvent, sendComment, endStream). After
/// the client has gone (isClosed) every output is dropped.
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpExchange
{
public:
    PDFMcpExchange(PDFMcpHttpRequest request, QString peerAddress, bool isRemote, QByteArray serverVersion);
    ~PDFMcpExchange();

    PDFMcpExchange(const PDFMcpExchange&) = delete;
    PDFMcpExchange& operator=(const PDFMcpExchange&) = delete;

    /// Unique id of the exchange in the process
    quint64 getId() const { return m_id; }

    const PDFMcpHttpRequest& getRequest() const { return m_request; }

    /// Address of the peer without the port (IPv4 for IPv4-mapped addresses)
    const QString& getPeerAddress() const { return m_peerAddress; }

    /// The peer is not a loopback address
    bool isRemote() const { return m_isRemote; }

    /// Complete response (JSON or empty); the exchange is finished. false = the
    /// exchange is closed or already answered.
    bool respond(PDFMcpHttpResponse response);

    /// Starts a stream response (200, text/event-stream, chunked); \p headers
    /// are additional headers (Mcp-Session-Id)
    bool beginStream(const std::vector<std::pair<QByteArray, QByteArray>>& headers = {});

    /// An SSE event of the JSON message (the stream is begun when it was not)
    bool sendEvent(QByteArrayView json);

    /// An SSE comment (keep-alive)
    bool sendComment(QByteArrayView text);

    /// Ends the stream by its last chunk; \p closeConnection closes the
    /// connection after it (the stop of the server)
    bool endStream(bool closeConnection = false);

    /// The client has gone (the socket is closed) or the server dropped the exchange
    bool isClosed() const { return m_isClosed.load(); }

    /// The response is complete (respond or endStream)
    bool isFinished() const;

    /// The response is a stream (beginStream)
    bool isStreaming() const;

    /// Time of the last output (PDFMcpClock milliseconds; keep-alive)
    qint64 getLastWriteTime() const;

    /// Version of the Server header
    const QByteArray& getServerVersion() const { return m_serverVersion; }

    // Side of the connection (its I/O thread) ------------------------------

    /// Output, which was queued since the last call; \p isFinished / \p
    /// closeConnection tell, whether the response is complete
    QByteArray takeOutput(bool* isFinished, bool* closeConnection);

    /// The connection, which is told about new output (nullptr = detached)
    void attach(PDFMcpConnection* connection);

    /// The connection dies or the client has gone: no more output
    void markClosed();

private:
    bool queue(QByteArray data, bool finish, bool closeConnection);

    const quint64 m_id;
    const PDFMcpHttpRequest m_request;
    const QString m_peerAddress;
    const bool m_isRemote;
    const QByteArray m_serverVersion;

    mutable QMutex m_mutex;
    std::atomic<bool> m_isClosed = false;
    PDFMcpConnection* m_connection = nullptr;
    QByteArray m_output;
    bool m_isNotified = false;
    bool m_isStreaming = false;
    bool m_isFinished = false;
    bool m_isOutputFinished = false;
    bool m_closeConnection = false;
    qint64 m_lastWriteTime = 0;
};

/// One TCP connection in an I/O thread (plan 2.2). It parses the requests by
/// PDFMcpHttpParser (one at a time, a pipelined request waits for the response
/// of the previous one), enforces the times of the head (408) and of the body,
/// the connection limits (64 in total - at the limit the oldest connection,
/// which has not completed a request, is evicted, 503 only without one; 16
/// per remote address - 503) and the
/// backpressure (more than 32 MiB unsent - closed), hands each request to the
/// dispatcher of the run as a PDFMcpExchange and writes its response. A client,
/// which closes the connection, closes its exchange - the dispatcher cancels the
/// call ("closing the SSE response stream is itself the cancellation signal").
class PDF4QTLIBMCPSHARED_EXPORT PDFMcpConnection : public QObject
{
    Q_OBJECT

public:
    /// \p parent is the I/O worker of the thread
    PDFMcpConnection(std::shared_ptr<PDFMcpServerShared> shared, QObject* parent);
    virtual ~PDFMcpConnection() override;

    /// Takes the socket (in the I/O thread). false = the descriptor cannot be used.
    bool start(qintptr socketDescriptor);

    /// Writes the queued output and aborts the connection (the stop of the server)
    void abort();

    /// New output of the current exchange (queued from any thread)
    void flushExchange();

    /// The connection was chosen for the eviction at the limit of the
    /// connections (PDFMcpDispatcherLink::evictIdleConnection, queued): it has
    /// not completed a request - it gets 503 and closes (follow-up 22)
    void evict();

private:
    enum class State
    {
        Waiting,        ///< Between requests (a new connection or after a response)
        Reading,        ///< A request is being received
        Dispatched,     ///< The request is with the dispatcher, the response is written
        Closing         ///< The last response is written, the connection closes
    };

    void onReadyRead();
    void onBytesWritten(qint64 bytes);
    void onDisconnected();
    void onTimeout();
    void processInput();
    void dispatchRequest();
    void dispatchExchange(PDFMcpHttpRequest request);
    void respondAndClose(const PDFMcpHttpResponse& response);
    void startClosing();
    void finishExchange(bool closeConnection);
    void closeExchange();
    void writeOutput(const QByteArray& data);
    void startTimer(int timeout);

    /// The connection is no longer a candidate of the eviction (the head of
    /// its request is complete, or it closes); true = it was chosen before
    bool leaveIdle();

    /// Time of the discarded input after a closing response, before the connection is closed (ms)
    static constexpr int LINGER_TIME = 1000;

    /// Largest input buffered while a response is written (a pipelined request)
    static constexpr qsizetype MAXIMUM_PIPELINED_INPUT = PDFMcpLimits::MAXIMUM_HEADER_SIZE;

public:
    /// A request without a valid access token, whose body has more bytes to come,
    /// is answered after its head - its body is never read (unauthenticated
    /// clients cannot make the server buffer large bodies)
    static constexpr qsizetype EARLY_REJECTION_SIZE = qsizetype(64) * 1024;

private:

    std::shared_ptr<PDFMcpServerShared> m_shared;
    QPointer<QTcpSocket> m_socket;
    QPointer<QTimer> m_timer;
    PDFMcpHttpParser m_parser;
    std::shared_ptr<PDFMcpExchange> m_exchange;
    State m_state = State::Waiting;
    QString m_peerAddress;
    bool m_isRemote = false;
    bool m_isCounted = false;
    bool m_isAddressCounted = false;
    bool m_isIdle = false;              ///< A candidate of the eviction (PDFMcpDispatcherLink)
    bool m_hadRequest = false;
    bool m_closeAfterResponse = false;
    bool m_isLingering = false;
};

}   // namespace pdf

#endif // PDFMCPCONNECTION_H
