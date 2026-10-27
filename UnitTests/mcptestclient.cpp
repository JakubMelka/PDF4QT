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

#include "mcptestclient.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpSocket>

namespace pdftest
{

QByteArray McpTestResponse::header(const QByteArray& name) const
{
    for (const auto& item : headers)
    {
        if (item.first.compare(name, Qt::CaseInsensitive) == 0)
        {
            return item.second;
        }
    }
    return QByteArray();
}

std::vector<QJsonObject> McpTestResponse::messages() const
{
    std::vector<QJsonObject> result;
    for (const QByteArray& event : events)
    {
        const QJsonDocument document = QJsonDocument::fromJson(event);
        if (document.isObject())
        {
            result.push_back(document.object());
        }
    }
    return result;
}

QJsonObject McpTestResponse::json() const
{
    if (!isSse)
    {
        return QJsonDocument::fromJson(body).object();
    }

    const std::vector<QJsonObject> all = messages();
    for (auto it = all.rbegin(); it != all.rend(); ++it)
    {
        if (it->contains(QStringLiteral("id")) && !it->contains(QStringLiteral("method")))
        {
            return *it;
        }
    }
    return QJsonObject();
}

// ----------------------------------------------------------------------------
// McpTestStream
// ----------------------------------------------------------------------------

McpTestStream::McpTestStream() = default;

McpTestStream::~McpTestStream()
{
    close();
}

bool McpTestStream::open(const QString& host, quint16 port, const QByteArray& request, int timeout)
{
    m_socket = std::make_unique<QTcpSocket>();
    m_socket->connectToHost(QHostAddress(host), port);

    QElapsedTimer timer;
    timer.start();
    const qint64 limit = qint64(timeout) * McpTestClient::getTimeoutFactor();
    while (m_socket->state() != QAbstractSocket::ConnectedState && m_socket->state() != QAbstractSocket::UnconnectedState && timer.elapsed() < limit)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    }

    if (m_socket->state() != QAbstractSocket::ConnectedState)
    {
        m_isClosed = true;
        return false;
    }

    m_socket->write(request);
    return true;
}

void McpTestStream::read() const
{
    if (!m_socket)
    {
        return;
    }

    if (m_socket->isOpen())
    {
        m_data += m_socket->readAll();
    }
    if (m_socket->state() == QAbstractSocket::UnconnectedState)
    {
        m_isClosed = true;
    }
}

McpTestResponse McpTestStream::response() const
{
    read();
    return McpTestClient::parseResponse(m_data, m_isClosed);
}

bool McpTestStream::isClosed() const
{
    return m_isClosed;
}

bool McpTestStream::waitForMessages(int count, int timeout)
{
    QElapsedTimer timer;
    timer.start();
    const qint64 limit = qint64(timeout) * McpTestClient::getTimeoutFactor();
    while (true)
    {
        read();
        if (int(response().messages().size()) >= count)
        {
            return true;
        }
        if (m_isClosed || timer.elapsed() > limit)
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    }
}

bool McpTestStream::waitForComments(int count, int timeout)
{
    QElapsedTimer timer;
    timer.start();
    const qint64 limit = qint64(timeout) * McpTestClient::getTimeoutFactor();
    while (true)
    {
        read();
        if (int(response().comments.size()) >= count)
        {
            return true;
        }
        if (m_isClosed || timer.elapsed() > limit)
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    }
}

bool McpTestStream::waitForEnd(int timeout)
{
    QElapsedTimer timer;
    timer.start();
    const qint64 limit = qint64(timeout) * McpTestClient::getTimeoutFactor();
    while (true)
    {
        read();
        const McpTestResponse current = response();
        if (m_isClosed || (current.status != 0 && current.isComplete))
        {
            return true;
        }
        if (timer.elapsed() > limit)
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    }
}

void McpTestStream::close()
{
    if (m_socket)
    {
        read();
        m_socket->abort();
        m_socket.reset();
    }
    m_isClosed = true;
}

// ----------------------------------------------------------------------------
// McpTestClient
// ----------------------------------------------------------------------------

McpTestClient::McpTestClient(quint16 port, McpTestEra era, const QString& host) :
    m_port(port),
    m_host(host),
    m_era(era)
{

}

void McpTestClient::setClientInfo(const QString& name, const QString& version, const QString& title)
{
    m_clientName = name;
    m_clientVersion = version;
    m_clientTitle = title;
}

QString McpTestClient::getProtocolVersion() const
{
    switch (m_era)
    {
        case McpTestEra::Modern:
            return QStringLiteral("2026-07-28");
        case McpTestEra::Legacy20251125:
            return QStringLiteral("2025-11-25");
        case McpTestEra::Legacy20250618:
            return QStringLiteral("2025-06-18");
    }
    return QStringLiteral("2026-07-28");
}

int McpTestClient::getTimeoutFactor()
{
    bool ok = false;
    const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &ok);
    return ok && factor > 0 ? qMin(factor, 1000) : 1;
}

McpTestResponse McpTestClient::parseResponse(const QByteArray& data, bool isClosed)
{
    McpTestResponse response;
    const qsizetype headEnd = data.indexOf("\r\n\r\n");
    if (headEnd < 0)
    {
        return response;
    }

    const QList<QByteArray> lines = data.left(headEnd).split('\n');
    if (lines.isEmpty())
    {
        return response;
    }

    const QList<QByteArray> statusParts = lines.front().trimmed().split(' ');
    if (statusParts.size() < 2 || !statusParts[0].startsWith("HTTP/"))
    {
        return response;
    }
    response.status = statusParts[1].toInt();
    response.reason = statusParts.mid(2).join(' ');

    for (qsizetype i = 1; i < lines.size(); ++i)
    {
        const QByteArray line = lines[i].trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
        {
            response.headers.append(qMakePair(line.left(colon).trimmed(), line.mid(colon + 1).trimmed()));
        }
    }

    const QByteArray raw = data.mid(headEnd + 4);
    response.isChunked = response.header("Transfer-Encoding").toLower().contains("chunked");
    response.isSse = response.header("Content-Type").toLower().startsWith("text/event-stream");

    if (response.isChunked)
    {
        qsizetype position = 0;
        while (true)
        {
            const qsizetype lineEnd = raw.indexOf("\r\n", position);
            if (lineEnd < 0)
            {
                break;
            }

            bool ok = false;
            const qsizetype size = raw.mid(position, lineEnd - position).trimmed().toLongLong(&ok, 16);
            if (!ok || size < 0)
            {
                break;
            }
            if (size == 0)
            {
                response.isComplete = true;
                break;
            }
            if (raw.size() < lineEnd + 2 + size + 2)
            {
                break;
            }
            response.body += raw.mid(lineEnd + 2, size);
            position = lineEnd + 2 + size + 2;
        }
    }
    else
    {
        bool ok = false;
        const qsizetype length = response.header("Content-Length").toLongLong(&ok);
        if (ok)
        {
            response.body = raw.left(length);
            response.isComplete = raw.size() >= length;
        }
        else
        {
            response.body = raw;
            response.isComplete = isClosed;
        }
    }
    response.isComplete = response.isComplete || isClosed;

    if (response.isSse)
    {
        QByteArray stream = response.body;
        stream.replace("\r\n", "\n");
        const QList<QByteArray> blocks = stream.split('\n');
        QByteArray eventData;
        for (const QByteArray& line : blocks)
        {
            if (line.isEmpty())
            {
                if (!eventData.isEmpty())
                {
                    response.events.push_back(eventData);
                    eventData.clear();
                }
                continue;
            }
            if (line.startsWith(':'))
            {
                response.comments.push_back(line.mid(1).trimmed());
            }
            else if (line.startsWith("data:"))
            {
                if (!eventData.isEmpty())
                {
                    eventData += '\n';
                }
                eventData += line.mid(5).trimmed();
            }
        }
    }
    return response;
}

McpTestResponse McpTestClient::exchange(const QByteArray& request, int timeout, int eventCount)
{
    const int limit = (timeout >= 0 ? timeout : m_timeout) * getTimeoutFactor();

    QTcpSocket socket;
    socket.connectToHost(QHostAddress(m_host), m_port);

    QElapsedTimer timer;
    timer.start();
    while (socket.state() != QAbstractSocket::ConnectedState && socket.state() != QAbstractSocket::UnconnectedState && timer.elapsed() < limit)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    }

    McpTestResponse response;
    if (socket.state() != QAbstractSocket::ConnectedState)
    {
        m_lastResponse = response;
        return response;
    }

    socket.write(request);

    QByteArray data;
    bool isClosed = false;
    size_t answered = 0;
    while (timer.elapsed() < limit)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
        data += socket.readAll();
        isClosed = socket.state() == QAbstractSocket::UnconnectedState;

        response = parseResponse(data, isClosed);

        // Legacy elicitation/create on the stream of a call is answered while
        // the stream is open (the server waits for the answer)
        answerElicitations(response, &answered);
        if (response.status != 0 && response.isComplete)
        {
            break;
        }
        if (eventCount > 0 && int(response.events.size()) >= eventCount)
        {
            break;
        }
        if (isClosed)
        {
            break;
        }
    }

    data += socket.readAll();
    response = parseResponse(data, socket.state() == QAbstractSocket::UnconnectedState || isClosed);
    socket.abort();

    if (!response.header("Mcp-Session-Id").isEmpty())
    {
        m_sessionId = response.header("Mcp-Session-Id");
    }
    m_lastResponse = response;
    return response;
}

McpTestResponse McpTestClient::sendRaw(const QByteArray& data, int timeout)
{
    return exchange(data, timeout, 0);
}

QByteArray McpTestClient::buildRequest(const QByteArray& method, const QByteArray& body, const QList<QPair<QByteArray, QByteArray>>& headers, const QJsonObject& message) const
{
    QList<QPair<QByteArray, QByteArray>> all;
    const QByteArray host = m_host.contains(QLatin1Char(':')) ? ('[' + m_host.toLatin1() + ']') : m_host.toLatin1();
    all.append(qMakePair(QByteArrayLiteral("Host"), m_hostHeader.isEmpty() ? (host + ':' + QByteArray::number(m_port)) : m_hostHeader));
    all.append(qMakePair(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json, text/event-stream")));
    if (method == "POST")
    {
        all.append(qMakePair(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json")));
    }
    if (!m_token.isEmpty())
    {
        all.append(qMakePair(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + m_token));
    }

    const QString methodName = message.value(QStringLiteral("method")).toString();
    if (m_era == McpTestEra::Modern)
    {
        all.append(qMakePair(QByteArrayLiteral("MCP-Protocol-Version"), getProtocolVersion().toLatin1()));
        if (!methodName.isEmpty())
        {
            all.append(qMakePair(QByteArrayLiteral("Mcp-Method"), methodName.toUtf8()));
        }

        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        QString name;
        if (methodName == QLatin1String("tools/call") || methodName == QLatin1String("prompts/get"))
        {
            name = params.value(QStringLiteral("name")).toString();
        }
        else if (methodName == QLatin1String("resources/read"))
        {
            name = params.value(QStringLiteral("uri")).toString();
        }
        else if (methodName.startsWith(QLatin1String("tasks/")))
        {
            name = params.value(QStringLiteral("taskId")).toString();
        }
        if (!name.isEmpty())
        {
            all.append(qMakePair(QByteArrayLiteral("Mcp-Name"), name.toUtf8()));
        }
    }
    else
    {
        if (methodName != QLatin1String("initialize"))
        {
            all.append(qMakePair(QByteArrayLiteral("MCP-Protocol-Version"), getProtocolVersion().toLatin1()));
        }
        if (!m_sessionId.isEmpty())
        {
            all.append(qMakePair(QByteArrayLiteral("Mcp-Session-Id"), m_sessionId));
        }
    }

    // Extra headers of the client and of the call replace the defaults of the same name
    QList<QPair<QByteArray, QByteArray>> overrides = m_extraHeaders;
    overrides.append(headers);
    for (const auto& item : overrides)
    {
        for (qsizetype i = all.size() - 1; i >= 0; --i)
        {
            if (all[i].first.compare(item.first, Qt::CaseInsensitive) == 0)
            {
                all.removeAt(i);
            }
        }
        if (!item.second.isEmpty())
        {
            all.append(item);
        }
    }

    QByteArray request = method + " /mcp HTTP/1.1\r\n";
    for (const auto& item : all)
    {
        request += item.first + ": " + item.second + "\r\n";
    }
    if (method == "POST")
    {
        request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    request += "Connection: close\r\n\r\n";
    request += body;
    return request;
}

McpTestResponse McpTestClient::post(const QByteArray& body, const QList<QPair<QByteArray, QByteArray>>& headers)
{
    // Server requests on the stream of the call (legacy elicitation/create) are
    // answered by the exchange while the stream is open
    const QJsonObject message = QJsonDocument::fromJson(body).object();
    return exchange(buildRequest("POST", body, headers, message), -1, 0);
}

void McpTestClient::answerElicitations(const McpTestResponse& response, size_t* answered)
{
    if (!response.isSse)
    {
        return;
    }

    const std::vector<QJsonObject> events = response.messages();
    while (*answered < events.size())
    {
        const QJsonObject& event = events[*answered];
        ++*answered;
        if (event.value(QStringLiteral("method")).toString() != QLatin1String("elicitation/create") || !event.contains(QStringLiteral("id")))
        {
            continue;
        }

        QJsonObject answer;
        answer.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        answer.insert(QStringLiteral("id"), event.value(QStringLiteral("id")));
        if (m_elicitationResponder)
        {
            answer.insert(QStringLiteral("result"), m_elicitationResponder(event.value(QStringLiteral("params")).toObject()));
        }
        else
        {
            QJsonObject cancel;
            cancel.insert(QStringLiteral("action"), QStringLiteral("cancel"));
            answer.insert(QStringLiteral("result"), cancel);
        }

        // The answer goes on its own connection (the stream of the call stays open)
        const McpTestResponse saved = m_lastResponse;
        exchange(buildRequest("POST", QJsonDocument(answer).toJson(QJsonDocument::Compact), {}, answer), -1, 0);
        m_lastResponse = saved;
    }
}

QByteArray McpTestClient::createHttpRequest(const QByteArray& method, const QJsonObject& message, const QList<QPair<QByteArray, QByteArray>>& headers) const
{
    const QByteArray body = message.isEmpty() ? QByteArray() : QJsonDocument(message).toJson(QJsonDocument::Compact);
    return buildRequest(method, body, headers, message);
}

std::unique_ptr<McpTestStream> McpTestClient::openStream(const QJsonObject& message, const QList<QPair<QByteArray, QByteArray>>& headers)
{
    std::unique_ptr<McpTestStream> stream = std::make_unique<McpTestStream>();
    if (!stream->open(m_host, m_port, createHttpRequest("POST", message, headers), m_timeout))
    {
        return nullptr;
    }
    return stream;
}

std::unique_ptr<McpTestStream> McpTestClient::openGetStream()
{
    std::unique_ptr<McpTestStream> stream = std::make_unique<McpTestStream>();
    if (!stream->open(m_host, m_port, buildRequest("GET", QByteArray(), {}, QJsonObject()), m_timeout))
    {
        return nullptr;
    }
    return stream;
}

QJsonObject McpTestClient::requestWithRetries(const QString& method, const QJsonObject& params)
{
    QJsonObject response = request(method, params);
    for (int round = 0; round < m_inputRetryRounds && m_era == McpTestEra::Modern && m_elicitationResponder; ++round)
    {
        const QJsonObject result = response.value(QStringLiteral("result")).toObject();
        if (result.value(QStringLiteral("resultType")).toString() != QLatin1String("input_required"))
        {
            break;
        }

        // MRTR: the inputRequests are answered, the request is retried with a new id
        QJsonObject responses;
        const QJsonObject inputRequests = result.value(QStringLiteral("inputRequests")).toObject();
        for (auto it = inputRequests.begin(); it != inputRequests.end(); ++it)
        {
            const QJsonObject inputRequest = it.value().toObject();
            if (inputRequest.value(QStringLiteral("method")).toString() == QLatin1String("elicitation/create"))
            {
                responses.insert(it.key(), m_elicitationResponder(inputRequest.value(QStringLiteral("params")).toObject()));
            }
        }

        QJsonObject retry = params;
        retry.insert(QStringLiteral("inputResponses"), responses);
        if (result.contains(QStringLiteral("requestState")))
        {
            retry.insert(QStringLiteral("requestState"), result.value(QStringLiteral("requestState")));
        }
        response = request(method, retry);
    }
    return response;
}

QJsonObject McpTestClient::getPrompt(const QString& name, const QJsonObject& arguments)
{
    QJsonObject params;
    params.insert(QStringLiteral("name"), name);
    if (!arguments.isEmpty())
    {
        params.insert(QStringLiteral("arguments"), arguments);
    }
    return requestWithRetries(QStringLiteral("prompts/get"), params);
}

McpTestResponse McpTestClient::get(int eventCount)
{
    return exchange(buildRequest("GET", QByteArray(), {}, QJsonObject()), -1, eventCount);
}

McpTestResponse McpTestClient::deleteSession()
{
    McpTestResponse response = exchange(buildRequest("DELETE", QByteArray(), {}, QJsonObject()), -1, 0);
    if (response.status == 200)
    {
        m_sessionId.clear();
    }
    return response;
}

QJsonObject McpTestClient::createRequest(const QString& method, const QJsonObject& params)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("id"), nextId());
    message.insert(QStringLiteral("method"), method);

    QJsonObject allParams = params;
    if (m_era == McpTestEra::Modern)
    {
        QJsonObject clientInfo;
        clientInfo.insert(QStringLiteral("name"), m_clientName);
        clientInfo.insert(QStringLiteral("version"), m_clientVersion);
        if (!m_clientTitle.isEmpty())
        {
            clientInfo.insert(QStringLiteral("title"), m_clientTitle);
        }

        QJsonObject meta = allParams.value(QStringLiteral("_meta")).toObject();
        meta.insert(QStringLiteral("io.modelcontextprotocol/protocolVersion"), getProtocolVersion());
        meta.insert(QStringLiteral("io.modelcontextprotocol/clientCapabilities"), m_capabilities);
        meta.insert(QStringLiteral("io.modelcontextprotocol/clientInfo"), clientInfo);
        allParams.insert(QStringLiteral("_meta"), meta);
    }

    if (!allParams.isEmpty())
    {
        message.insert(QStringLiteral("params"), allParams);
    }
    return message;
}

QJsonObject McpTestClient::request(const QString& method, const QJsonObject& params, const QList<QPair<QByteArray, QByteArray>>& headers)
{
    const QJsonObject message = createRequest(method, params);
    const McpTestResponse response = post(QJsonDocument(message).toJson(QJsonDocument::Compact), headers);
    return response.json();
}

int McpTestClient::notify(const QString& method, const QJsonObject& params)
{
    QJsonObject message;
    message.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    message.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
    {
        message.insert(QStringLiteral("params"), params);
    }
    return post(QJsonDocument(message).toJson(QJsonDocument::Compact)).status;
}

QJsonObject McpTestClient::initialize()
{
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), m_clientName);
    clientInfo.insert(QStringLiteral("version"), m_clientVersion);
    if (!m_clientTitle.isEmpty())
    {
        clientInfo.insert(QStringLiteral("title"), m_clientTitle);
    }

    QJsonObject params;
    params.insert(QStringLiteral("protocolVersion"), getProtocolVersion());
    params.insert(QStringLiteral("capabilities"), m_capabilities);
    params.insert(QStringLiteral("clientInfo"), clientInfo);

    m_sessionId.clear();
    const QJsonObject response = request(QStringLiteral("initialize"), params);
    if (!m_sessionId.isEmpty())
    {
        notify(QStringLiteral("notifications/initialized"));
    }
    return response.value(QStringLiteral("result")).toObject();
}

QJsonObject McpTestClient::discover()
{
    return request(QStringLiteral("server/discover")).value(QStringLiteral("result")).toObject();
}

QJsonObject McpTestClient::listTools(const QString& cursor)
{
    QJsonObject params;
    if (!cursor.isEmpty())
    {
        params.insert(QStringLiteral("cursor"), cursor);
    }
    return request(QStringLiteral("tools/list"), params);
}

QJsonObject McpTestClient::callTool(const QString& name, const QJsonObject& arguments, const QJsonObject& meta)
{
    QJsonObject params;
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("arguments"), arguments);
    if (!meta.isEmpty())
    {
        params.insert(QStringLiteral("_meta"), meta);
    }
    return requestWithRetries(QStringLiteral("tools/call"), params);
}

QJsonObject McpTestClient::pollTask(const QString& taskId)
{
    QElapsedTimer timer;
    timer.start();
    QJsonObject last;
    while (timer.elapsed() < m_timeout * getTimeoutFactor())
    {
        QJsonObject params;
        params.insert(QStringLiteral("taskId"), taskId);
        last = request(QStringLiteral("tasks/get"), params);
        const QString status = last.value(QStringLiteral("result")).toObject().value(QStringLiteral("status")).toString();
        if (status != QLatin1String("working") && status != QLatin1String("input_required"))
        {
            break;
        }

        QElapsedTimer pause;
        pause.start();
        while (pause.elapsed() < 100)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
        }
    }
    return last;
}

}   // namespace pdftest
