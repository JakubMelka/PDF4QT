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

#ifndef MCPTESTCLIENT_H
#define MCPTESTCLIENT_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QTcpSocket;

namespace pdftest
{

/// Era of the scripted client (plan of step 7, chapter 1)
enum class McpTestEra
{
    Modern,             ///< 2026-07-28: per-request _meta, headers MCP-Protocol-Version / Mcp-Method / Mcp-Name
    Legacy20251125,     ///< initialize, Mcp-Session-Id
    Legacy20250618
};

/// One HTTP response read by the client
class McpTestResponse
{
public:
    int status = 0;                                     ///< 0 = no response (the connection was refused or closed)
    QByteArray reason;
    QList<QPair<QByteArray, QByteArray>> headers;       ///< Names as received
    QByteArray body;                                    ///< Body of a JSON response, the raw SSE stream of an SSE response
    bool isChunked = false;
    bool isSse = false;
    bool isComplete = false;                            ///< The whole body was read
    std::vector<QByteArray> events;                     ///< The data payloads of the SSE events (in order)
    std::vector<QByteArray> comments;                   ///< SSE comments (keep-alive)

    /// Value of the first header of the name (case insensitive), empty = none
    QByteArray header(const QByteArray& name) const;

    /// The JSON-RPC message of the response: the body of a JSON response, or the
    /// last SSE event, which is a response (has an id and no method)
    QJsonObject json() const;

    /// Every JSON message of the SSE events
    std::vector<QJsonObject> messages() const;
};

/// A long-lived response (a listen stream, a legacy GET stream, a call kept
/// open): the socket stays open while the test does other things; the waits
/// run the event loop of the calling thread
class McpTestStream
{
public:
    McpTestStream();
    ~McpTestStream();

    McpTestStream(const McpTestStream&) = delete;
    McpTestStream& operator=(const McpTestStream&) = delete;

    /// Connects and writes the request (false = no connection)
    bool open(const QString& host, quint16 port, const QByteArray& request, int timeout);

    /// Waits until the stream has at least \p count JSON messages
    bool waitForMessages(int count, int timeout);

    /// Waits until the stream has at least \p count comments
    bool waitForComments(int count, int timeout);

    /// Waits until the response is complete (the last chunk) or the server closed the connection
    bool waitForEnd(int timeout);

    /// The response so far
    McpTestResponse response() const;

    /// The JSON messages so far
    std::vector<QJsonObject> messages() const { return response().messages(); }

    /// The server closed the connection
    bool isClosed() const;

    /// Closes the connection (the client cancels)
    void close();

private:
    /// Takes the data received so far (also from response() / messages(), so a
    /// QTRY loop of a test sees new events without a wait function)
    void read() const;

    std::unique_ptr<QTcpSocket> m_socket;
    mutable QByteArray m_data;
    mutable bool m_isClosed = false;
};

/// Raw-socket scripted MCP client of both protocol eras (plan of step 7, 8 item
/// 6): it writes the HTTP requests itself (also malformed ones - sendRaw), reads
/// JSON and SSE (chunked) responses, keeps the legacy session, answers the
/// elicitations of the server and polls the tasks. Every wait runs the event
/// loop of the calling thread (the host of the server may live in it), bounded
/// by the timeout (multiplied by PDF4QT_TEST_TIMEOUT_FACTOR).
///
/// Wave 0 gives the structure and the HTTP / SSE plumbing; package A completes
/// the protocol behavior together with its tests.
class McpTestClient
{
public:
    explicit McpTestClient(quint16 port, McpTestEra era = McpTestEra::Modern, const QString& host = QStringLiteral("127.0.0.1"));

    void setEra(McpTestEra era) { m_era = era; }
    McpTestEra getEra() const { return m_era; }

    /// Access token (Authorization: Bearer); empty = no header
    void setToken(const QByteArray& token) { m_token = token; }

    /// clientInfo of the client (_meta of the modern era, initialize of the legacy ones)
    void setClientInfo(const QString& name, const QString& version = QStringLiteral("1.0"), const QString& title = QString());

    /// Capabilities of the client (elicitation, extensions)
    void setCapabilities(const QJsonObject& capabilities) { m_capabilities = capabilities; }

    /// Value of the Host header (default "<host>:<port>")
    void setHostHeader(const QByteArray& host) { m_hostHeader = host; }

    /// Additional headers of every request (Origin, ...)
    void setExtraHeaders(const QList<QPair<QByteArray, QByteArray>>& headers) { m_extraHeaders = headers; }

    /// Time limit of one response (ms, before the factor)
    void setTimeout(int timeout) { m_timeout = timeout; }

    /// Answers of the elicitations of the server (legacy elicitation/create on
    /// the SSE stream and MRTR inputRequests): params -> result
    void setElicitationResponder(std::function<QJsonObject(const QJsonObject&)> responder) { m_elicitationResponder = std::move(responder); }

    /// MRTR of the modern era: a result input_required of callTool / getPrompt is
    /// answered by the elicitation responder and the request is retried with
    /// inputResponses and requestState (at most \p rounds times; 0 = off)
    void setInputRetryRounds(int rounds) { m_inputRetryRounds = rounds; }

    /// Session id of the legacy session (empty = none)
    const QByteArray& getSessionId() const { return m_sessionId; }
    void setSessionId(const QByteArray& sessionId) { m_sessionId = sessionId; }

    /// Protocol version of the era ("2026-07-28")
    QString getProtocolVersion() const;

    // Raw HTTP --------------------------------------------------------------

    /// Malformed-input mode: sends the bytes as they are on a new connection and
    /// reads one response (status 0, when the server closes without one)
    McpTestResponse sendRaw(const QByteArray& data, int timeout = -1);

    /// POST /mcp of a body with the headers of the era (Accept, Content-Type,
    /// Authorization, MCP-Protocol-Version, Mcp-Method, Mcp-Name, Mcp-Session-Id)
    /// plus \p headers (a header of the same name replaces the default one; an
    /// empty value removes it)
    McpTestResponse post(const QByteArray& body, const QList<QPair<QByteArray, QByteArray>>& headers = {});

    /// GET /mcp (legacy stream; reads until \p eventCount events arrived or the timeout)
    McpTestResponse get(int eventCount = 0);

    /// A POST of the message kept open as a stream (listen streams, calls,
    /// which the test cancels by closing); nullptr = no connection
    std::unique_ptr<McpTestStream> openStream(const QJsonObject& message, const QList<QPair<QByteArray, QByteArray>>& headers = {});

    /// The legacy GET stream of the session kept open
    std::unique_ptr<McpTestStream> openGetStream();

    /// The bytes of an HTTP request of the client (method "POST" / "GET" /
    /// "DELETE") with the headers of the era (malformed variants in the tests)
    QByteArray createHttpRequest(const QByteArray& method, const QJsonObject& message, const QList<QPair<QByteArray, QByteArray>>& headers = {}) const;

    /// DELETE /mcp of the session
    McpTestResponse deleteSession();

    // JSON-RPC --------------------------------------------------------------

    /// A request message of the era: id, method, params (+ _meta of the modern era)
    QJsonObject createRequest(const QString& method, const QJsonObject& params = QJsonObject());

    /// Sends a request and returns its response (the response message, empty on
    /// an HTTP error - see lastResponse)
    QJsonObject request(const QString& method, const QJsonObject& params = QJsonObject(), const QList<QPair<QByteArray, QByteArray>>& headers = {});

    /// Sends a notification (no id); the HTTP status of the answer
    int notify(const QString& method, const QJsonObject& params = QJsonObject());

    /// Legacy: initialize + notifications/initialized; returns the result of initialize
    QJsonObject initialize();

    /// Modern: server/discover
    QJsonObject discover();

    QJsonObject listTools(const QString& cursor = QString());
    QJsonObject callTool(const QString& name, const QJsonObject& arguments, const QJsonObject& meta = QJsonObject());

    /// prompts/get (with the MRTR retries of setInputRetryRounds)
    QJsonObject getPrompt(const QString& name, const QJsonObject& arguments = QJsonObject());

    /// Polls tasks/get until the task ends (tasks extension) or the timeout
    QJsonObject pollTask(const QString& taskId);

    /// The last HTTP response
    const McpTestResponse& lastResponse() const { return m_lastResponse; }

    /// Next id of a request
    qint64 nextId() { return ++m_id; }

    /// Time limit factor of the tests (PDF4QT_TEST_TIMEOUT_FACTOR, 20 under valgrind, 1 by default)
    static int getTimeoutFactor();

    /// Parses an HTTP response (also a partial one: isComplete false)
    static McpTestResponse parseResponse(const QByteArray& data, bool isClosed);

private:
    QByteArray buildRequest(const QByteArray& method, const QByteArray& body, const QList<QPair<QByteArray, QByteArray>>& headers, const QJsonObject& message) const;
    McpTestResponse exchange(const QByteArray& request, int timeout, int eventCount);
    void answerElicitations(const McpTestResponse& response, size_t* answered);
    QJsonObject requestWithRetries(const QString& method, const QJsonObject& params);

    quint16 m_port = 0;
    QString m_host;
    McpTestEra m_era = McpTestEra::Modern;
    QByteArray m_token;
    QString m_clientName = QStringLiteral("mcp-test-client");
    QString m_clientVersion = QStringLiteral("1.0");
    QString m_clientTitle;
    QJsonObject m_capabilities;
    QByteArray m_hostHeader;
    QList<QPair<QByteArray, QByteArray>> m_extraHeaders;
    QByteArray m_sessionId;
    int m_timeout = 10000;
    qint64 m_id = 0;
    std::function<QJsonObject(const QJsonObject&)> m_elicitationResponder;
    int m_inputRetryRounds = 4;
    McpTestResponse m_lastResponse;
};

}   // namespace pdftest

#endif // MCPTESTCLIENT_H
