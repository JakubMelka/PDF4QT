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

#include "pdfmcpdispatcher.h"
#include "pdfmcpdispatcher_p.h"
#include "pdfmcpconnection.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QNetworkInterface>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace pdf
{

// ----------------------------------------------------------------------------
// PDFMcpDispatcherLink
// ----------------------------------------------------------------------------

void PDFMcpDispatcherLink::setDispatcher(PDFMcpDispatcher* dispatcher)
{
    QMutexLocker locker(&m_mutex);
    m_dispatcher = dispatcher;
}

void PDFMcpDispatcherLink::setServer(PDFMcpServer* server)
{
    QMutexLocker locker(&m_mutex);
    m_server = server;
}

bool PDFMcpDispatcherLink::submit(std::shared_ptr<PDFMcpExchange> exchange)
{
    // The dispatcher is alive while it is set (it clears itself under this
    // mutex before it is destroyed); a posted event is removed with it
    QMutexLocker locker(&m_mutex);
    if (!m_dispatcher)
    {
        return false;
    }
    QMetaObject::invokeMethod(m_dispatcher, &PDFMcpDispatcher::handleExchange, Qt::QueuedConnection, std::move(exchange));
    return true;
}

void PDFMcpDispatcherLink::exchangeClosed(std::shared_ptr<PDFMcpExchange> exchange)
{
    QMutexLocker locker(&m_mutex);
    if (m_dispatcher)
    {
        QMetaObject::invokeMethod(m_dispatcher, &PDFMcpDispatcher::handleExchangeClosed, Qt::QueuedConnection, std::move(exchange));
    }
}

void PDFMcpDispatcherLink::catalogPublished()
{
    QMutexLocker locker(&m_mutex);
    if (m_dispatcher)
    {
        QMetaObject::invokeMethod(m_dispatcher, &PDFMcpDispatcher::handleCatalogPublished, Qt::QueuedConnection);
    }
}

void PDFMcpDispatcherLink::resourceUpdated(const QString& uri)
{
    QMutexLocker locker(&m_mutex);
    if (m_dispatcher)
    {
        QMetaObject::invokeMethod(m_dispatcher, &PDFMcpDispatcher::handleResourceUpdated, Qt::QueuedConnection, uri);
    }
}

void PDFMcpDispatcherLink::disconnectAssistant(const QString& key)
{
    QMutexLocker locker(&m_mutex);
    if (m_dispatcher)
    {
        QMetaObject::invokeMethod(m_dispatcher, &PDFMcpDispatcher::handleDisconnectAssistant, Qt::QueuedConnection, key);
    }
}

void PDFMcpDispatcherLink::disconnectAll()
{
    QMutexLocker locker(&m_mutex);
    if (m_dispatcher)
    {
        QMetaObject::invokeMethod(m_dispatcher, &PDFMcpDispatcher::handleDisconnectAll, Qt::QueuedConnection);
    }
}

void PDFMcpDispatcherLink::checkTimeouts()
{
    QMutexLocker locker(&m_mutex);
    if (m_dispatcher)
    {
        QMetaObject::invokeMethod(m_dispatcher, &PDFMcpDispatcher::handleMaintenance, Qt::QueuedConnection);
    }
}

bool PDFMcpDispatcherLink::acquireAddress(const QString& address, int limit)
{
    QMutexLocker locker(&m_mutex);
    int& count = m_addresses[address];
    if (count >= limit)
    {
        if (count == 0)
        {
            m_addresses.erase(address);
        }
        return false;
    }
    ++count;
    return true;
}

void PDFMcpDispatcherLink::releaseAddress(const QString& address)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_addresses.find(address);
    if (it != m_addresses.end() && --it->second <= 0)
    {
        m_addresses.erase(it);
    }
}

void PDFMcpDispatcherLink::addIdleConnection(PDFMcpConnection* connection)
{
    QMutexLocker locker(&m_mutex);
    m_idleConnections.push_back(connection);
}

bool PDFMcpDispatcherLink::leaveIdleConnection(PDFMcpConnection* connection)
{
    QMutexLocker locker(&m_mutex);
    std::erase(m_idleConnections, connection);
    return std::find(m_evictedConnections.cbegin(), m_evictedConnections.cend(), connection) != m_evictedConnections.cend();
}

void PDFMcpDispatcherLink::removeConnection(PDFMcpConnection* connection)
{
    QMutexLocker locker(&m_mutex);
    std::erase(m_idleConnections, connection);
    std::erase(m_evictedConnections, connection);
}

bool PDFMcpDispatcherLink::evictIdleConnection()
{
    QMutexLocker locker(&m_mutex);
    if (m_idleConnections.empty())
    {
        return false;
    }

    // The connection lives while it is in the list (its destructor removes it
    // under this mutex); an event posted to it is removed with it
    PDFMcpConnection* connection = m_idleConnections.front();
    m_idleConnections.erase(m_idleConnections.begin());
    m_evictedConnections.push_back(connection);
    QMetaObject::invokeMethod(connection, &PDFMcpConnection::evict, Qt::QueuedConnection);
    return true;
}

int PDFMcpDispatcherLink::getPendingEvictions() const
{
    QMutexLocker locker(&m_mutex);
    return int(m_evictedConnections.size());
}

void PDFMcpDispatcherLink::setAssistants(std::vector<PDFMcpAssistantInfo> assistants, bool notify)
{
    QMutexLocker locker(&m_mutex);
    m_assistants = std::move(assistants);
    if (notify && m_server)
    {
        // The server outlives the run (its threads are joined before it is destroyed)
        QMetaObject::invokeMethod(m_server, &PDFMcpServer::onAssistantsChanged, Qt::QueuedConnection);
    }
}

std::vector<PDFMcpAssistantInfo> PDFMcpDispatcherLink::getAssistants() const
{
    QMutexLocker locker(&m_mutex);
    return m_assistants;
}

// ----------------------------------------------------------------------------
// PDFMcpDispatcher - construction, HTTP level
// ----------------------------------------------------------------------------

PDFMcpDispatcher::PDFMcpDispatcher(std::shared_ptr<PDFMcpServerShared> shared, QObject* parent) :
    BaseClass(parent),
    m_shared(std::move(shared)),
    m_completionLimiter(PDFMcpLimits::MAXIMUM_COMPLETIONS_PER_SECOND, PDFMcpLimits::MAXIMUM_COMPLETIONS_PER_SECOND),
    m_initializeLimiter(PDFMcpLimits::INITIALIZE_BURST, PDFMcpLimits::INITIALIZE_RATE)
{
    const PDFMcpServerConfig& config = m_shared->getConfig();
    m_catalog = m_shared->getCatalog();
    if (!m_catalog)
    {
        m_catalog = std::make_shared<const PDFMcpCatalog>();
    }

    if (!config.token.isEmpty())
    {
        m_tokenHash = QCryptographicHash::hash(config.token, QCryptographicHash::Sha256);
    }

    m_assistants = new PDFMcpAssistantRegistry(this);
    connect(m_assistants, &PDFMcpAssistantRegistry::assistantConnected, this, &PDFMcpDispatcher::onAssistantConnected);

    // Keep-alives, idle assistants and sessions, tasks, elicitation timeouts
    m_maintenanceTimer = new QTimer(this);
    m_maintenanceTimer->setInterval(qBound(10, qMin(config.keepAliveInterval, config.cancelGraceTime) / 2, 1000));
    connect(m_maintenanceTimer, &QTimer::timeout, this, &PDFMcpDispatcher::handleMaintenance);
    m_maintenanceTimer->start();

    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    connect(m_updateTimer, &QTimer::timeout, this, &PDFMcpDispatcher::flushResourceUpdates);

    if (m_shared->link)
    {
        m_shared->link->setDispatcher(this);
    }
}

PDFMcpDispatcher::~PDFMcpDispatcher()
{
    shutdown();
}

void PDFMcpDispatcher::setPort(quint16 port)
{
    m_port = port;
    refreshAllowedHosts();
}

void PDFMcpDispatcher::refreshAllowedHosts()
{
    const PDFMcpServerConfig& config = m_shared->getConfig();
    const QString port = QString::number(m_port);

    QStringList hosts = { QStringLiteral("127.0.0.1:") + port, QStringLiteral("localhost:") + port, QStringLiteral("[::1]:") + port };
    if (config.bindMode == PDFMcpBindMode::Remote)
    {
        // Every address of an interface, which is up, and the name of the computer
        const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface& networkInterface : interfaces)
        {
            if (!networkInterface.flags().testFlag(QNetworkInterface::IsUp))
            {
                continue;
            }

            const QList<QNetworkAddressEntry> entries = networkInterface.addressEntries();
            for (const QNetworkAddressEntry& entry : entries)
            {
                QHostAddress address = entry.ip();
                address.setScopeId(QString());
                if (address.protocol() == QAbstractSocket::IPv6Protocol)
                {
                    hosts.append(QLatin1Char('[') + address.toString().toLower() + QStringLiteral("]:") + port);
                }
                else
                {
                    hosts.append(address.toString() + QLatin1Char(':') + port);
                }
            }
        }

        const QString hostName = QHostInfo::localHostName().toLower();
        if (!hostName.isEmpty())
        {
            hosts.append(hostName + QLatin1Char(':') + port);
            hosts.append(hostName + QStringLiteral(".local:") + port);
        }
    }

    for (const QString& host : config.additionalHosts)
    {
        hosts.append(host.toLower());
    }

    hosts.removeDuplicates();
    m_allowedHosts = hosts;
    m_hostsRefreshedAt = PDFMcpClock::now();
}

bool PDFMcpDispatcher::isHostAllowed(QByteArrayView host)
{
    const QString value = QString::fromLatin1(host).toLower();
    if (m_allowedHosts.contains(value))
    {
        return true;
    }

    // The addresses of the computer may have changed (remote mode): the list
    // is refreshed on a miss, at most once per second
    if (m_shared->getConfig().bindMode == PDFMcpBindMode::Remote && PDFMcpClock::now() - m_hostsRefreshedAt >= 1000)
    {
        refreshAllowedHosts();
        return m_allowedHosts.contains(value);
    }
    return false;
}

bool PDFMcpDispatcher::isLoopbackOrigin(QByteArrayView origin)
{
    const QUrl url(QString::fromLatin1(origin), QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")) ||
        !url.userInfo().isEmpty() || (!url.path().isEmpty() && url.path() != QLatin1String("/")) || url.hasQuery() || url.hasFragment())
    {
        return false;
    }

    const QString host = url.host().toLower();
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost") || host == QLatin1String("::1");
}

bool PDFMcpDispatcher::isAuthorizationValid(const PDFMcpHttpRequest& request, const QByteArray& tokenHash)
{
    // Authorization: Bearer <token>, compared in constant time (by the digests)
    if (request.getHeaderCount("authorization") != 1 || tokenHash.isEmpty())
    {
        return false;
    }

    const QByteArray value = request.getHeader("authorization");
    if (value.size() <= 7 || value.left(7).compare("Bearer ", Qt::CaseInsensitive) != 0)
    {
        return false;
    }

    const QByteArray token = value.mid(7).trimmed();
    const QByteArray hash = QCryptographicHash::hash(token, QCryptographicHash::Sha256);
    return PDFMcpRequestState::isEqualConstantTime(hash, tokenHash);
}

bool PDFMcpDispatcher::checkAuthorization(const std::shared_ptr<PDFMcpExchange>& exchange)
{
    const PDFMcpServerConfig& config = m_shared->getConfig();
    if (!config.isTokenRequired())
    {
        return true;
    }

    const qint64 now = PDFMcpClock::now();
    const QString& address = exchange->getPeerAddress();
    qint64 retryAfter = 0;
    if (m_throttle.isBlocked(address, now, &retryAfter))
    {
        // Local mode: every local process shares the loopback address, so a
        // flood of wrong tokens of another process would lock the assistant
        // with the token out. The valid token (256 bits) passes the block there;
        // missing and wrong tokens stay blocked (follow-up 21). The remote mode
        // blocks the address also for the token (the hosts of a LAN differ).
        if (config.bindMode != PDFMcpBindMode::Remote && isAuthorizationValid(exchange->getRequest(), m_tokenHash))
        {
            return true;
        }

        PDFMcpJsonWriter data;
        data.beginObject();
        data.writeMember(QLatin1StringView("retryAfterMs"), retryAfter);
        data.endObject();
        respondUnauthorized(exchange, 429, PDFMcpErrorCodes::RATE_LIMITED, QStringLiteral("Too many failed authorizations; try again later"),
                            data.take(), { { QByteArrayLiteral("Retry-After"), QByteArray::number((retryAfter + 999) / 1000) } });
        return false;
    }

    if (!isAuthorizationValid(exchange->getRequest(), m_tokenHash))
    {
        m_throttle.recordFailure(address, now);

        // No resource_metadata: clients configured with a static header never start OAuth
        respondUnauthorized(exchange, 401, PDFMcpErrorCodes::INVALID_REQUEST,
                            QStringLiteral("Unauthorized: PDF4QT requires its access token (Authorization: Bearer <token>)"), QByteArrayView(),
                            { { QByteArrayLiteral("WWW-Authenticate"), QByteArrayLiteral("Bearer realm=\"pdf4qt\"") } });
        return false;
    }
    return true;
}

bool PDFMcpDispatcher::isAcceptable(QByteArrayView accept, bool needsJson, bool needsEventStream)
{
    bool hasJson = false;
    bool hasEventStream = false;
    const QList<QByteArray> ranges = accept.toByteArray().split(',');
    for (const QByteArray& range : ranges)
    {
        const QByteArray type = range.split(';').front().trimmed().toLower();
        if (type == "*/*")
        {
            hasJson = true;
            hasEventStream = true;
        }
        else if (type == "application/json" || type == "application/*")
        {
            hasJson = true;
        }
        else if (type == "text/event-stream" || type == "text/*")
        {
            hasEventStream = true;
        }
    }
    return (!needsJson || hasJson) && (!needsEventStream || hasEventStream);
}

bool PDFMcpDispatcher::isJsonContentType(QByteArrayView contentType)
{
    const QList<QByteArray> parts = contentType.toByteArray().split(';');
    if (parts.front().trimmed().toLower() != "application/json")
    {
        return false;
    }

    for (qsizetype i = 1; i < parts.size(); ++i)
    {
        const QByteArray parameter = parts[i].trimmed();
        if (parameter.isEmpty())
        {
            continue;
        }

        const qsizetype equals = parameter.indexOf('=');
        if (equals <= 0)
        {
            return false;
        }

        const QByteArray name = parameter.left(equals).trimmed().toLower();
        QByteArray value = parameter.mid(equals + 1).trimmed().toLower();
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
        {
            value = value.mid(1, value.size() - 2);
        }

        if (name == "charset" && value != "utf-8" && value != "utf8")
        {
            return false;
        }
    }
    return true;
}

std::optional<QString> PDFMcpDispatcher::decodeHeaderValue(QByteArrayView value)
{
    if (value.startsWith("=?base64?") && value.endsWith("?=") && value.size() >= 11)
    {
        const QByteArray encoded = value.sliced(9, value.size() - 11).toByteArray();
        const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
        {
            return std::nullopt;
        }

        // Valid UTF-8. Control characters are allowed: the sentinel exists to carry
        // them ("line1\nline2" of Value Encoding) - the decoded value is only
        // compared with the body, which decides (an unknown tool is -32602 there)
        const QString text = QString::fromUtf8(decoded.decoded);
        if (text.toUtf8() != decoded.decoded)
        {
            return std::nullopt;
        }
        return text;
    }

    for (char c : value)
    {
        const uchar byte = uchar(c);
        if (byte < 0x20 || byte >= 0x7F)
        {
            return std::nullopt;
        }
    }
    return QString::fromLatin1(value);
}

QString PDFMcpDispatcher::createCursor(quint64 generation, qint64 offset)
{
    return QString::fromLatin1(PDFMcpJson::toBase64Url(QByteArray::number(generation) + ':' + QByteArray::number(offset)));
}

std::optional<qint64> PDFMcpDispatcher::parseCursor(const QString& cursor)
{
    if (cursor.isEmpty() || cursor.size() > PDFMcpLimits::MAXIMUM_CURSOR_LENGTH)
    {
        return std::nullopt;
    }

    const std::optional<QByteArray> decoded = PDFMcpJson::fromBase64Url(cursor.toLatin1());
    if (!decoded)
    {
        return std::nullopt;
    }

    const QList<QByteArray> parts = decoded->split(':');
    if (parts.size() != 2 || parts[0].isEmpty() || parts[1].isEmpty() || parts[0].size() > 19 || parts[1].size() > 9)
    {
        return std::nullopt;
    }

    for (const QByteArray& part : parts)
    {
        for (char c : part)
        {
            if (c < '0' || c > '9')
            {
                return std::nullopt;
            }
        }
    }

    // A stale generation restarts from the offset (the lists are small)
    return parts[1].toLongLong();
}

QJsonObject PDFMcpDispatcher::filterInputResponses(const QJsonObject& responses, const QJsonObject& allowedValues, bool* isDeclined)
{
    QJsonObject result;
    for (auto it = allowedValues.begin(); it != allowedValues.end(); ++it)
    {
        const QJsonValue responseValue = responses.value(it.key());
        if (!responseValue.isObject())
        {
            continue;
        }

        const QJsonObject response = responseValue.toObject();
        const QString action = response.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("decline") || action == QLatin1String("cancel"))
        {
            if (isDeclined)
            {
                *isDeclined = true;
            }
            continue;
        }

        if (action != QLatin1String("accept") || !response.value(QStringLiteral("content")).isObject())
        {
            continue;
        }

        const QJsonObject content = response.value(QStringLiteral("content")).toObject();
        const QJsonValue rule = it.value();
        QJsonObject accepted;
        bool isValid = !content.isEmpty();
        for (auto contentIt = content.begin(); isValid && contentIt != content.end(); ++contentIt)
        {
            QJsonValue allowed;
            if (rule.isArray())
            {
                allowed = rule;
            }
            else if (rule.isObject())
            {
                allowed = rule.toObject().value(contentIt.key());
            }

            const QJsonValue value = contentIt.value();
            if (allowed.isArray())
            {
                isValid = allowed.toArray().contains(value);
                if (isValid)
                {
                    accepted.insert(contentIt.key(), value);
                }
            }
            else if (allowed.isString() && allowed.toString() == QLatin1String("any"))
            {
                if (value.isString())
                {
                    accepted.insert(contentIt.key(), PDFMcpJson::sanitizeString(value.toString(), PDFMcpJson::SanitizeFlag::RemoveBidi, 4096));
                }
                else if (value.isBool() || (value.isDouble() && std::isfinite(value.toDouble())))
                {
                    accepted.insert(contentIt.key(), value);
                }
                else
                {
                    isValid = false;
                }
            }
            // Properties, which were not asked, are ignored
        }

        if (isValid && !accepted.isEmpty())
        {
            QJsonObject answer;
            answer.insert(QStringLiteral("action"), QStringLiteral("accept"));
            answer.insert(QStringLiteral("content"), accepted);
            result.insert(it.key(), answer);
        }
    }
    return result;
}

void PDFMcpDispatcher::handleExchange(std::shared_ptr<PDFMcpExchange> exchange)
{
    if (!exchange || exchange->isClosed())
    {
        return;
    }

    if (m_isShutDown)
    {
        respondError(exchange, 503, PDFMcpRequestId(), PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping"));
        return;
    }

    const PDFMcpHttpRequest& request = exchange->getRequest();

    // The endpoint is exactly /mcp; a query is refused (a token must never be
    // in a URI), every other path (also /.well-known/*) is unknown
    if (request.target != "/mcp")
    {
        if (request.target.startsWith("/mcp?"))
        {
            respondError(exchange, 400, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("The MCP endpoint takes no query string"));
        }
        else
        {
            respondError(exchange, 404, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Not found; the MCP endpoint is /mcp"));
        }
        return;
    }

    // DNS rebinding: the Host must be an address of this server, and a browser
    // page (Origin) is refused - a loopback origin only with the token
    if (!isHostAllowed(request.getHeader("host")))
    {
        respondError(exchange, 403, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Forbidden: invalid Host header"));
        return;
    }

    if (request.hasHeader("origin"))
    {
        // "If the Origin header is present and invalid, servers MUST respond with HTTP 403"
        if (request.getHeaderCount("origin") != 1 || !isLoopbackOrigin(request.getHeader("origin")) || !m_shared->getConfig().isTokenRequired())
        {
            respondError(exchange, 403, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Forbidden: invalid Origin header"));
            return;
        }
    }

    // No CORS: OPTIONS (and every other method) is not allowed
    if (request.method != "POST" && request.method != "GET" && request.method != "DELETE")
    {
        respondError(exchange, 405, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Method not allowed"), QByteArray(),
                     { { QByteArrayLiteral("Allow"), QByteArrayLiteral("GET, POST, DELETE") } });
        return;
    }

    if (!checkAuthorization(exchange))
    {
        return;
    }

    if (request.method == "POST")
    {
        handlePost(exchange);
    }
    else if (request.method == "GET")
    {
        handleGet(exchange);
    }
    else
    {
        handleDelete(exchange);
    }
}

void PDFMcpDispatcher::handlePost(const std::shared_ptr<PDFMcpExchange>& exchange)
{
    const PDFMcpHttpRequest& httpRequest = exchange->getRequest();
    if (!isJsonContentType(httpRequest.getHeader("content-type")))
    {
        respondError(exchange, 415, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Content-Type must be application/json"));
        return;
    }

    if (!isAcceptable(httpRequest.getHeader("accept"), true, true))
    {
        respondError(exchange, 406, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Accept must list application/json and text/event-stream"));
        return;
    }

    // The scanner checks the bytes before QJsonDocument parses them
    const PDFMcpJsonScanResult scan = PDFMcpJsonScanner().scan(httpRequest.body);
    if (!scan.isValid())
    {
        respondError(exchange, 400, PDFMcpRequestId(), scan.errorCode, scan.errorMessage);
        return;
    }

    if (scan.topLevel == PDFMcpJsonScanResult::TopLevel::Array)
    {
        respondError(exchange, 400, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("The body must be a single JSON-RPC message (no batch)"));
        return;
    }

    if (scan.topLevel != PDFMcpJsonScanResult::TopLevel::Object)
    {
        respondError(exchange, 400, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("The body must be a JSON-RPC message object"));
        return;
    }

    // Qt refuses some documents, which the scanner accepts (lone surrogates of
    // \u escapes): a parse error too
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(httpRequest.body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        respondError(exchange, 400, PDFMcpRequestId(), PDFMcpErrorCodes::PARSE_ERROR, QStringLiteral("Parse error"));
        return;
    }

    Request request;
    request.exchange = exchange;
    request.message = document.object();

    if (scan.hasId && !scan.id.isValid())
    {
        respondError(exchange, 400, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Invalid request id"));
        return;
    }
    request.id = scan.id;

    if (request.message.value(QStringLiteral("jsonrpc")).toString() != QLatin1String("2.0"))
    {
        respondError(exchange, 400, request.id, PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("jsonrpc must be \"2.0\""));
        return;
    }

    if (!request.message.contains(QStringLiteral("method")))
    {
        if (scan.hasId && (request.message.contains(QStringLiteral("result")) || request.message.contains(QStringLiteral("error"))))
        {
            handleClientResponse(exchange, request.message, request.id);
            return;
        }

        respondError(exchange, 400, request.id, PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Not a JSON-RPC request"));
        return;
    }

    const QJsonValue methodValue = request.message.value(QStringLiteral("method"));
    request.method = methodValue.toString();
    if (!methodValue.isString() || request.method.isEmpty() || request.method.size() > 128)
    {
        respondError(exchange, 400, request.id, PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Invalid method"));
        return;
    }

    request.isNotification = !scan.hasId;

    const QJsonValue paramsValue = request.message.value(QStringLiteral("params"));
    if (!paramsValue.isUndefined() && !paramsValue.isObject())
    {
        respondError(exchange, 400, request.id, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params must be an object"));
        return;
    }
    request.params = paramsValue.toObject();

    const QJsonValue metaValue = request.params.value(QStringLiteral("_meta"));
    if (!metaValue.isUndefined() && !metaValue.isObject())
    {
        respondError(exchange, 400, request.id, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params._meta must be an object"));
        return;
    }
    request.meta = metaValue.toObject();

    // The era is decided by the body (basic/versioning, "dual-era server")
    if (request.meta.contains(QLatin1String(PDFMcpProtocol::META_PROTOCOL_VERSION)))
    {
        // A modern request ignores Mcp-Session-Id ("do not mint or echo session IDs")
        handleModern(request);
        return;
    }

    if (request.method == QLatin1String("initialize") && !request.isNotification)
    {
        handleInitialize(request);
        return;
    }

    if (httpRequest.hasHeader("mcp-session-id"))
    {
        PDFMcpSession* session = (httpRequest.getHeaderCount("mcp-session-id") == 1) ? m_sessions.find(httpRequest.getHeader("mcp-session-id")) : nullptr;
        if (!session)
        {
            // "it MUST start a new session" - the client initializes again
            respondError(exchange, 404, request.id, PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Session not found; send initialize"));
            return;
        }

        handleLegacy(request, session);
        return;
    }

    if (request.isNotification)
    {
        // A notification without a session (notifications/cancelled of a
        // modern client - closing the stream is the cancellation on HTTP)
        respondEmpty(exchange, 202);
        return;
    }

    if (PDFMcpProtocol::getEra(QString::fromLatin1(httpRequest.getHeader("mcp-protocol-version"))) == PDFMcpEra::Modern)
    {
        // "A request missing any required field is malformed ... -32602 ... 400"
        respondError(exchange, 400, request.id, PDFMcpErrorCodes::INVALID_PARAMS,
                     QStringLiteral("Missing params._meta[\"io.modelcontextprotocol/protocolVersion\"]"));
        return;
    }

    // Not a recognized modern error: a dual-era client falls back to initialize
    respondError(exchange, 400, request.id, PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Bad Request: no session; send initialize"));
}

void PDFMcpDispatcher::handleGet(const std::shared_ptr<PDFMcpExchange>& exchange)
{
    const PDFMcpHttpRequest& httpRequest = exchange->getRequest();
    if (!httpRequest.hasHeader("mcp-session-id"))
    {
        // The modern era has no GET stream (subscriptions/listen replaces it)
        respondError(exchange, 405, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Method not allowed; POST JSON-RPC messages"), QByteArray(),
                     { { QByteArrayLiteral("Allow"), QByteArrayLiteral("POST") } });
        return;
    }

    PDFMcpSession* session = (httpRequest.getHeaderCount("mcp-session-id") == 1) ? m_sessions.find(httpRequest.getHeader("mcp-session-id")) : nullptr;
    if (!session)
    {
        respondError(exchange, 404, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Session not found; send initialize"));
        return;
    }

    if (!isAcceptable(httpRequest.getHeader("accept"), false, true))
    {
        respondError(exchange, 406, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Accept must list text/event-stream"));
        return;
    }

    if (httpRequest.hasHeader("mcp-protocol-version") && httpRequest.getHeader("mcp-protocol-version") != session->protocolVersion.toLatin1())
    {
        respondError(exchange, 400, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("MCP-Protocol-Version does not match the session"));
        return;
    }

    // One stream per session: a new GET replaces the old one ("MUST NOT broadcast")
    if (session->stream)
    {
        m_getStreams.erase(session->stream->getId());
        session->stream->endStream();
        session->stream.reset();
    }

    session->lastSeen = PDFMcpClock::now();
    session->isUsed = true;
    PDFMcpAssistantInfo info;
    info.key = session->key;
    info.name = session->clientName;
    info.title = session->clientTitle;
    info.address = session->remoteAddress;
    info.isRemote = session->isRemote;
    info.era = session->era;
    if (m_assistants->touch(info, session->lastSeen))
    {
        publishAssistants(true);
    }

    session->stream = exchange;
    m_getStreams[exchange->getId()] = session->key;
    exchange->beginStream();
    exchange->sendComment("stream");
}

void PDFMcpDispatcher::handleDelete(const std::shared_ptr<PDFMcpExchange>& exchange)
{
    const PDFMcpHttpRequest& httpRequest = exchange->getRequest();
    if (!httpRequest.hasHeader("mcp-session-id"))
    {
        respondError(exchange, 405, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Method not allowed; POST JSON-RPC messages"), QByteArray(),
                     { { QByteArrayLiteral("Allow"), QByteArrayLiteral("POST") } });
        return;
    }

    PDFMcpSession* session = (httpRequest.getHeaderCount("mcp-session-id") == 1) ? m_sessions.find(httpRequest.getHeader("mcp-session-id")) : nullptr;
    if (!session)
    {
        respondError(exchange, 404, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Session not found"));
        return;
    }

    endSession(session->key, PDFMcpAssistantEvent::Kind::SessionEnded);
    respondEmpty(exchange, 200);
}

void PDFMcpDispatcher::handleClientResponse(const std::shared_ptr<PDFMcpExchange>& exchange, const QJsonObject& message, const PDFMcpRequestId& id)
{
    // A response of a client is accepted only for a legacy session (the answer
    // of an elicitation/create); "The client MUST NOT send JSON-RPC responses" (modern)
    const PDFMcpHttpRequest& httpRequest = exchange->getRequest();
    PDFMcpSession* session = (httpRequest.getHeaderCount("mcp-session-id") == 1) ? m_sessions.find(httpRequest.getHeader("mcp-session-id")) : nullptr;
    if (!session)
    {
        if (httpRequest.hasHeader("mcp-session-id"))
        {
            respondError(exchange, 404, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("Session not found"));
        }
        else
        {
            respondError(exchange, 400, PDFMcpRequestId(), PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("JSON-RPC responses are accepted only in a session"));
        }
        return;
    }

    session->lastSeen = PDFMcpClock::now();
    session->isUsed = true;
    respondEmpty(exchange, 202);

    if (id.getKind() != PDFMcpRequestIdKind::Integer)
    {
        return;
    }

    for (auto& item : m_calls)
    {
        PendingCall& pending = *item.second;
        if (!pending.isEliciting || pending.request.client.sessionKey != session->key || pending.elicitationRequestId != id.getInteger() ||
            pending.elicitationIndex >= pending.elicitations.size())
        {
            continue;
        }

        // An error of the client is a canceled choice
        QJsonObject answer = message.value(QStringLiteral("result")).toObject();
        if (!message.value(QStringLiteral("result")).isObject())
        {
            answer = QJsonObject();
            answer.insert(QStringLiteral("action"), QStringLiteral("cancel"));
        }

        pending.elicitationAnswers.insert(pending.elicitations[pending.elicitationIndex].key, answer);
        ++pending.elicitationIndex;
        pending.isEliciting = false;
        pending.elicitationRequestId = 0;

        if (pending.elicitationIndex < pending.elicitations.size())
        {
            sendNextElicitation(pending);
        }
        else
        {
            redispatch(pending, pending.elicitationAnswers);
        }
        return;
    }
}

// ----------------------------------------------------------------------------
// Responses
// ----------------------------------------------------------------------------

void PDFMcpDispatcher::respondEmpty(const std::shared_ptr<PDFMcpExchange>& exchange, int status, std::vector<std::pair<QByteArray, QByteArray>> headers)
{
    PDFMcpHttpResponse response;
    response.status = status;
    response.headers = std::move(headers);
    exchange->respond(std::move(response));
}

void PDFMcpDispatcher::respondJson(const std::shared_ptr<PDFMcpExchange>& exchange, int status, QByteArray body, std::vector<std::pair<QByteArray, QByteArray>> headers)
{
    PDFMcpHttpResponse response;
    response.status = status;
    response.headers = std::move(headers);
    response.headers.emplace_back(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
    response.body = std::move(body);
    exchange->respond(std::move(response));
}

void PDFMcpDispatcher::respondError(const std::shared_ptr<PDFMcpExchange>& exchange, int status, const PDFMcpRequestId& id, int code, const QString& message,
                                    QByteArray data, std::vector<std::pair<QByteArray, QByteArray>> headers)
{
    respondJson(exchange, status, createErrorMessage(id, code, message, data), std::move(headers));
}

void PDFMcpDispatcher::respondUnauthorized(const std::shared_ptr<PDFMcpExchange>& exchange, int status, int code, const QString& message, QByteArrayView data,
                                           std::vector<std::pair<QByteArray, QByteArray>> headers)
{
    PDFMcpHttpResponse response;
    response.status = status;
    response.headers = std::move(headers);
    response.headers.emplace_back(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
    response.body = createErrorMessage(PDFMcpRequestId(), code, message, data);
    response.closeConnection = true;
    exchange->respond(std::move(response));
}

void PDFMcpDispatcher::respondRequestError(const Request& request, int code, const QString& message, QByteArray data, int status)
{
    respondError(request.exchange, status >= 0 ? status : getErrorStatus(code, request.era), request.id, code, message, std::move(data), request.responseHeaders);
}

void PDFMcpDispatcher::respondResult(const Request& request, QByteArray result, QByteArrayView cacheMembers)
{
    respondJson(request.exchange, 200, createResultMessage(request.id, appendResultMembers(result, request.era, cacheMembers)), request.responseHeaders);
}

void PDFMcpDispatcher::respondRateLimited(const Request& request, qint64 retryAfter)
{
    PDFMcpJsonWriter data;
    data.beginObject();
    data.writeMember(QLatin1StringView("retryAfterMs"), retryAfter);
    data.endObject();
    respondError(request.exchange, 429, request.id, PDFMcpErrorCodes::RATE_LIMITED, QStringLiteral("Too many requests; try again later"), data.take(),
                 { { QByteArrayLiteral("Retry-After"), QByteArray::number(qMax<qint64>(1, (retryAfter + 999) / 1000)) } });
}

int PDFMcpDispatcher::getErrorStatus(int code, PDFMcpEra era)
{
    // A legacy client reads 404 as an expired session: an unknown method is 200 there
    if (era != PDFMcpEra::Modern && code == PDFMcpErrorCodes::METHOD_NOT_FOUND)
    {
        return 200;
    }
    return PDFMcpErrorCodes::getHttpStatus(code);
}

QByteArray PDFMcpDispatcher::appendMembers(QByteArrayView object, QByteArrayView members)
{
    QByteArrayView trimmed = object.trimmed();
    if (trimmed.size() < 2 || !trimmed.endsWith('}') || members.isEmpty())
    {
        return object.toByteArray();
    }

    QByteArrayView body = trimmed.first(trimmed.size() - 1);
    const bool isEmpty = body.trimmed() == "{";
    QByteArray result;
    result.reserve(trimmed.size() + members.size() + 1);
    result.append(body);
    if (!isEmpty)
    {
        result.append(',');
    }
    result.append(members);
    result.append('}');
    return result;
}

QByteArray PDFMcpDispatcher::createServerInfoMeta() const
{
    return QByteArrayLiteral("\"_meta\":{\"") + PDFMcpProtocol::META_SERVER_INFO + "\":" + createServerInfo(PDFMcpEra::Modern) + '}';
}

QByteArray PDFMcpDispatcher::appendResultMembers(QByteArrayView result, PDFMcpEra era, QByteArrayView cacheMembers) const
{
    // The legacy eras know neither resultType, ttlMs, cacheScope nor serverInfo of results
    if (era != PDFMcpEra::Modern)
    {
        return result.toByteArray();
    }

    QByteArray members = QByteArrayLiteral("\"resultType\":\"complete\"");
    if (!cacheMembers.isEmpty())
    {
        members += ',';
        members += cacheMembers;
    }
    members += ',';
    members += createServerInfoMeta();
    return appendMembers(result, members);
}

QByteArray PDFMcpDispatcher::createResultMessage(const PDFMcpRequestId& id, QByteArrayView result) const
{
    PDFMcpJsonWriter writer;
    writer.reserve(result.size() + 64);
    writer.beginObject();
    writer.writeMember(QLatin1StringView("jsonrpc"), QLatin1StringView("2.0"));
    writer.key(QLatin1StringView("id"));
    id.write(writer);
    writer.key(QLatin1StringView("result"));
    writer.writeRaw(result);
    writer.endObject();
    return writer.take();
}

QByteArray PDFMcpDispatcher::createErrorMessage(const PDFMcpRequestId& id, int code, const QString& message, QByteArrayView data)
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("jsonrpc"), QLatin1StringView("2.0"));

    // An id, which could not be read (or none at all), is omitted - both MCP
    // schemas say "id?: RequestId" of an error (owner decision of 2026-10-27,
    // follow-up 31), never "id": null
    if (id.isValid())
    {
        writer.key(QLatin1StringView("id"));
        id.write(writer);
    }
    writer.key(QLatin1StringView("error"));
    writer.beginObject();
    writer.writeMember(QLatin1StringView("code"), code);
    writer.writeMember(QLatin1StringView("message"), QStringView(message));
    if (!data.isEmpty())
    {
        writer.key(QLatin1StringView("data"));
        writer.writeRaw(data);
    }
    writer.endObject();
    writer.endObject();
    return writer.take();
}

}   // namespace pdf
