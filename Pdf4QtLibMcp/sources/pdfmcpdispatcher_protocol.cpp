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

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace pdf
{

/// Texts and small helpers of the protocol methods
class PDFMcpDispatcherProtocolHelper
{
public:
    PDFMcpDispatcherProtocolHelper() = delete;

    static QString getExpiredText()
    {
        return QStringLiteral("The choice of the user expired or is not valid. Call again without inputResponses and requestState.");
    }

    static QString getDeclinedText()
    {
        return QStringLiteral("The user declined the choice in the client. Ask the user how to continue.");
    }

    /// Sanitized member of clientInfo (at most 64 characters)
    static QString getClientInfoText(const QJsonObject& clientInfo, const char* key)
    {
        return PDFMcpJson::sanitizeString(clientInfo.value(QLatin1StringView(key)).toString(),
                                          PDFMcpJson::SanitizeFlag::RemoveBidi | PDFMcpJson::SanitizeFlag::Trim, 64);
    }

    /// The progress token is a string or an integer
    static bool isValidProgressToken(const QJsonValue& value)
    {
        if (value.isString())
        {
            return value.toString().size() <= PDFMcpLimits::MAXIMUM_ID_LENGTH;
        }
        if (value.isDouble())
        {
            const double number = value.toDouble();
            return std::isfinite(number) && std::floor(number) == number && std::abs(number) < 9.0e18;
        }
        return false;
    }

    /// The content of a static resource is text (UTF-8)
    static bool isTextMimeType(const QString& mimeType)
    {
        return mimeType.startsWith(QLatin1String("text/")) || mimeType.contains(QLatin1String("json")) || mimeType.contains(QLatin1String("xml")) ||
               mimeType.contains(QLatin1String("markdown"));
    }
};

// ----------------------------------------------------------------------------
// Eras
// ----------------------------------------------------------------------------

bool PDFMcpDispatcher::touchAssistant(Request& request)
{
    PDFMcpAssistantInfo info;
    info.key = request.client.assistantKey;
    info.name = request.client.clientName;
    info.title = request.client.clientTitle;
    info.address = request.client.remoteAddress;
    info.isRemote = request.client.isRemote;
    info.era = request.era;
    const bool isNew = m_assistants->touch(info, PDFMcpClock::now());
    publishAssistants(isNew);
    return isNew;
}

void PDFMcpDispatcher::handleModern(Request& request)
{
    request.era = PDFMcpEra::Modern;
    const PDFMcpHttpRequest& httpRequest = request.exchange->getRequest();
    const PDFMcpServerConfig& config = m_shared->getConfig();

    const QJsonValue versionValue = request.meta.value(QLatin1String(PDFMcpProtocol::META_PROTOCOL_VERSION));
    if (!versionValue.isString())
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("_meta[\"io.modelcontextprotocol/protocolVersion\"] must be a string"), QByteArray(), 400);
        return;
    }
    const QString requested = versionValue.toString();

    if (request.isNotification)
    {
        // No client notification is defined on HTTP (notifications/cancelled: the
        // closed stream is the cancellation) - accepted and ignored
        respondEmpty(request.exchange, 202);
        return;
    }

    // Headers mirrored from the body (Streamable HTTP, Server Validation)
    if (httpRequest.getHeaderCount("mcp-protocol-version") != 1)
    {
        respondRequestError(request, PDFMcpErrorCodes::HEADER_MISMATCH, QStringLiteral("Header mismatch: MCP-Protocol-Version is required"), QByteArray(), 400);
        return;
    }
    if (httpRequest.getHeader("mcp-protocol-version") != requested.toUtf8())
    {
        respondRequestError(request, PDFMcpErrorCodes::HEADER_MISMATCH, QStringLiteral("Header mismatch: MCP-Protocol-Version does not match the protocol version of _meta"), QByteArray(), 400);
        return;
    }

    if (PDFMcpProtocol::getEra(requested) != PDFMcpEra::Modern)
    {
        // The versions of the per-request _meta (a legacy version needs initialize)
        PDFMcpJsonWriter data;
        data.beginObject();
        data.key(QLatin1StringView("supported"));
        data.writeStringArray({ QString::fromLatin1(PDFMcpProtocol::VERSION_2026_07_28) });
        data.writeMember(QLatin1StringView("requested"), QStringView(PDFMcpJson::sanitizeString(requested, PDFMcpJson::SanitizeFlag::RemoveBidi, 64)));
        data.endObject();
        respondRequestError(request, PDFMcpErrorCodes::UNSUPPORTED_PROTOCOL_VERSION, QStringLiteral("Unsupported protocol version"), data.take(), 400);
        return;
    }

    if (httpRequest.getHeaderCount("mcp-method") != 1 || httpRequest.getHeader("mcp-method") != request.method.toUtf8())
    {
        respondRequestError(request, PDFMcpErrorCodes::HEADER_MISMATCH, QStringLiteral("Header mismatch: Mcp-Method must equal the method of the body"), QByteArray(), 400);
        return;
    }

    QString nameKey;
    if (request.method == QLatin1String("tools/call") || request.method == QLatin1String("prompts/get"))
    {
        nameKey = QStringLiteral("name");
    }
    else if (request.method == QLatin1String("resources/read"))
    {
        nameKey = QStringLiteral("uri");
    }
    else if (request.method.startsWith(QLatin1String("tasks/")))
    {
        nameKey = QStringLiteral("taskId");
    }

    if (!nameKey.isEmpty())
    {
        if (httpRequest.getHeaderCount("mcp-name") != 1)
        {
            respondRequestError(request, PDFMcpErrorCodes::HEADER_MISMATCH, QStringLiteral("Header mismatch: Mcp-Name is required"), QByteArray(), 400);
            return;
        }

        // "servers MUST decode an encoded Mcp-Name ... before comparing it"
        const std::optional<QString> name = decodeHeaderValue(httpRequest.getHeader("mcp-name"));
        const QJsonValue bodyValue = request.params.value(nameKey);
        if (!name || (bodyValue.isString() && *name != bodyValue.toString()))
        {
            respondRequestError(request, PDFMcpErrorCodes::HEADER_MISMATCH, QStringLiteral("Header mismatch: Mcp-Name does not match params.%1").arg(nameKey), QByteArray(), 400);
            return;
        }
    }

    const QJsonValue capabilities = request.meta.value(QLatin1String(PDFMcpProtocol::META_CLIENT_CAPABILITIES));
    if (!capabilities.isObject())
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("_meta[\"io.modelcontextprotocol/clientCapabilities\"] is required"), QByteArray(), 400);
        return;
    }

    QString logLevel;
    const QJsonValue logLevelValue = request.meta.value(QLatin1String(PDFMcpProtocol::META_LOG_LEVEL));
    if (!logLevelValue.isUndefined())
    {
        logLevel = logLevelValue.toString();
        if (!logLevelValue.isString() || PDFMcpProtocol::getLogLevelIndex(logLevel) < 0)
        {
            respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Invalid _meta[\"io.modelcontextprotocol/logLevel\"]"), QByteArray(), 400);
            return;
        }
    }

    const QJsonValue progressToken = request.meta.value(QStringLiteral("progressToken"));
    if (!progressToken.isUndefined() && !PDFMcpDispatcherProtocolHelper::isValidProgressToken(progressToken))
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("_meta.progressToken must be a string or an integer"), QByteArray(), 400);
        return;
    }
    request.progressToken = progressToken;

    // clientInfo is for the display and the identity only, never for security
    const QJsonObject clientInfo = request.meta.value(QLatin1String(PDFMcpProtocol::META_CLIENT_INFO)).toObject();
    PDFMcpClientContext& client = request.client;
    client.era = PDFMcpEra::Modern;
    client.protocolVersion = requested;
    client.clientCapabilities = capabilities.toObject();
    client.clientName = PDFMcpDispatcherProtocolHelper::getClientInfoText(clientInfo, "name");
    if (client.clientName.isEmpty())
    {
        client.clientName = PDFMcpAssistantRegistry::getDefaultClientName();
    }
    client.clientTitle = PDFMcpDispatcherProtocolHelper::getClientInfoText(clientInfo, "title");
    client.clientVersion = PDFMcpDispatcherProtocolHelper::getClientInfoText(clientInfo, "version");
    client.remoteAddress = request.exchange->getPeerAddress();
    client.isRemote = request.exchange->isRemote();
    client.logLevel = logLevel;
    client.hasProgressToken = !progressToken.isUndefined();
    client.assistantKey = PDFMcpAssistantRegistry::createModernKey(config.isTokenRequired() ? config.token : QByteArray(), client.clientName, client.remoteAddress);

    // clientInfo.name is chosen by the client: a new name is a new assistant with
    // its own limits, so the number of the assistants is bounded
    if (!m_assistants->contains(client.assistantKey) && m_assistants->getAssistantCount() >= PDFMcpLimits::MAXIMUM_ASSISTANTS)
    {
        PDFMcpJsonWriter data;
        data.beginObject();
        data.writeMember(QLatin1StringView("retryAfterMs"), PDFMcpLimits::ASSISTANT_IDLE_TIME);
        data.endObject();
        respondError(request.exchange, 429, request.id, PDFMcpErrorCodes::RATE_LIMITED,
                     QStringLiteral("Too many assistants are connected to PDF4QT; disconnect some on the page AI of PDF4QT"), data.take(),
                     { { QByteArrayLiteral("Retry-After"), QByteArray::number(PDFMcpLimits::ASSISTANT_IDLE_TIME / 1000) } });
        return;
    }

    touchAssistant(request);

    qint64 retryAfter = 0;
    if (!m_rateLimiter.allow(client.assistantKey, PDFMcpClock::now(), &retryAfter))
    {
        respondRateLimited(request, retryAfter);
        return;
    }

    handleMethod(request);
}

void PDFMcpDispatcher::handleInitialize(Request& request)
{
    request.era = PDFMcpEra::Legacy20251125;

    const QJsonValue versionValue = request.params.value(QStringLiteral("protocolVersion"));
    if (!versionValue.isString())
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.protocolVersion is required"));
        return;
    }

    // A supported legacy version is echoed, anything else gets 2025-11-25
    // ("MUST respond with another protocol version it supports")
    const std::optional<PDFMcpEra> requestedEra = PDFMcpProtocol::getEra(versionValue.toString());
    const PDFMcpEra era = (requestedEra && *requestedEra != PDFMcpEra::Modern) ? *requestedEra : PDFMcpEra::Legacy20251125;

    // Each initialize is a new session: rate-limited per address, so a client
    // cannot open the 64 sessions at once (follow-up 36)
    const qint64 now = PDFMcpClock::now();
    qint64 retryAfter = 0;
    if (!m_initializeLimiter.allow(request.exchange->getPeerAddress(), now, &retryAfter))
    {
        respondRateLimited(request, retryAfter);
        return;
    }

    const QByteArray sessionId = PDFMcpSessionRegistry::createSessionId();
    PDFMcpSession* session = m_sessions.create(sessionId, now);
    if (!session)
    {
        respondRequestError(request, PDFMcpErrorCodes::INTERNAL_ERROR, QStringLiteral("Too many MCP sessions are open in PDF4QT"), QByteArray(), 503);
        return;
    }

    const QJsonObject clientInfo = request.params.value(QStringLiteral("clientInfo")).toObject();
    session->era = era;
    session->protocolVersion = PDFMcpProtocol::getVersion(era);
    session->clientCapabilities = request.params.value(QStringLiteral("capabilities")).toObject();
    session->clientName = PDFMcpDispatcherProtocolHelper::getClientInfoText(clientInfo, "name");
    if (session->clientName.isEmpty())
    {
        session->clientName = PDFMcpAssistantRegistry::getDefaultClientName();
    }
    session->clientTitle = PDFMcpDispatcherProtocolHelper::getClientInfoText(clientInfo, "title");
    session->clientVersion = PDFMcpDispatcherProtocolHelper::getClientInfoText(clientInfo, "version");
    session->remoteAddress = request.exchange->getPeerAddress();
    session->isRemote = request.exchange->isRemote();

    request.era = era;
    request.sessionKey = session->key;
    request.client = session->createClientContext();
    touchAssistant(request);

    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("protocolVersion"), QStringView(session->protocolVersion));
    writer.key(QLatin1StringView("capabilities"));
    writer.writeRaw(createCapabilities(false));
    writer.key(QLatin1StringView("serverInfo"));
    writer.writeRaw(createServerInfo(era));
    if (!m_shared->getConfig().instructions.isEmpty())
    {
        writer.writeMember(QLatin1StringView("instructions"), QStringView(m_shared->getConfig().instructions));
    }
    writer.endObject();

    request.responseHeaders.emplace_back(QByteArrayLiteral("Mcp-Session-Id"), sessionId);
    respondResult(request, writer.take());
}

void PDFMcpDispatcher::handleLegacy(Request& request, PDFMcpSession* session)
{
    const PDFMcpHttpRequest& httpRequest = request.exchange->getRequest();
    request.era = session->era;
    request.sessionKey = session->key;

    // An absent header is accepted in a session; another version is refused
    if (httpRequest.hasHeader("mcp-protocol-version") && httpRequest.getHeader("mcp-protocol-version") != session->protocolVersion.toLatin1())
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_REQUEST, QStringLiteral("MCP-Protocol-Version does not match the version of the session"), QByteArray(), 400);
        return;
    }

    session->lastSeen = PDFMcpClock::now();
    session->isUsed = session->isUsed || request.method != QLatin1String("notifications/initialized");
    request.client = session->createClientContext();

    const QJsonValue progressToken = request.meta.value(QStringLiteral("progressToken"));
    if (!progressToken.isUndefined() && PDFMcpDispatcherProtocolHelper::isValidProgressToken(progressToken))
    {
        request.progressToken = progressToken;
        request.client.hasProgressToken = true;
    }

    touchAssistant(request);

    if (request.isNotification)
    {
        if (request.method == QLatin1String("notifications/initialized"))
        {
            session->isInitialized = true;
        }
        else if (request.method == QLatin1String("notifications/cancelled"))
        {
            // Cancels the call of the session; no response is sent for it
            const PDFMcpRequestId requestId = PDFMcpRequestId::fromJson(request.params.value(QStringLiteral("requestId")));
            if (requestId.isValid())
            {
                for (auto& item : m_calls)
                {
                    PendingCall& pending = *item.second;
                    if (pending.request.client.sessionKey == session->key && pending.request.id == requestId && pending.mode != PendingCall::Mode::Task)
                    {
                        const quint64 pendingId = pending.id;
                        cancelCall(pending, PDFMcpCancelReason::ClientCanceled);
                        if (pending.exchange)
                        {
                            pending.exchange->endStream();
                        }
                        removePending(pendingId);
                        break;
                    }
                }
            }
        }

        respondEmpty(request.exchange, 202);
        return;
    }

    qint64 retryAfter = 0;
    if (!m_rateLimiter.allow(session->key, PDFMcpClock::now(), &retryAfter))
    {
        respondRateLimited(request, retryAfter);
        return;
    }

    handleMethod(request);
}

void PDFMcpDispatcher::handleMethod(Request& request)
{
    const QString& method = request.method;
    const bool isModern = request.era == PDFMcpEra::Modern;
    PDFMcpSession* session = isModern ? nullptr : m_sessions.findByKey(request.sessionKey);

    if (method == QLatin1String("tools/list"))
    {
        handleList(request, ListKind::Tools);
    }
    else if (method == QLatin1String("prompts/list"))
    {
        handleList(request, ListKind::Prompts);
    }
    else if (method == QLatin1String("resources/list"))
    {
        handleList(request, ListKind::Resources);
    }
    else if (method == QLatin1String("resources/templates/list"))
    {
        handleList(request, ListKind::Templates);
    }
    else if (method == QLatin1String("tools/call"))
    {
        handleToolCall(request);
    }
    else if (method == QLatin1String("prompts/get"))
    {
        handlePromptGet(request);
    }
    else if (method == QLatin1String("resources/read"))
    {
        handleResourceRead(request);
    }
    else if (method == QLatin1String("completion/complete"))
    {
        handleCompletion(request);
    }
    else if (isModern && method == QLatin1String("server/discover"))
    {
        handleDiscover(request);
    }
    else if (isModern && method == QLatin1String("subscriptions/listen"))
    {
        handleListen(request);
    }
    else if (isModern && method == QLatin1String("tasks/get"))
    {
        handleTaskGet(request);
    }
    else if (isModern && method == QLatin1String("tasks/update"))
    {
        handleTaskUpdate(request);
    }
    else if (isModern && method == QLatin1String("tasks/cancel"))
    {
        handleTaskCancel(request);
    }
    else if (session && method == QLatin1String("ping"))
    {
        respondResult(request, QByteArrayLiteral("{}"));
    }
    else if (session && method == QLatin1String("resources/subscribe"))
    {
        handleSubscribe(request, session, true);
    }
    else if (session && method == QLatin1String("resources/unsubscribe"))
    {
        handleSubscribe(request, session, false);
    }
    else if (session && method == QLatin1String("logging/setLevel"))
    {
        handleSetLevel(request, session);
    }
    else
    {
        // "If the server does not implement the requested RPC method, it MUST
        // respond with 404 Not Found and ... -32601" (a legacy session: 200)
        respondRequestError(request, PDFMcpErrorCodes::METHOD_NOT_FOUND,
                            QStringLiteral("Method not found: %1").arg(PDFMcpJson::sanitizeString(method, PDFMcpJson::SanitizeFlag::RemoveBidi, 64)));
    }
}

// ----------------------------------------------------------------------------
// Methods answered by the dispatcher
// ----------------------------------------------------------------------------

QByteArray PDFMcpDispatcher::createCapabilities(bool isModern) const
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.key(QLatin1StringView("tools"));
    writer.beginObject();
    writer.writeMember(QLatin1StringView("listChanged"), true);
    writer.endObject();
    writer.key(QLatin1StringView("resources"));
    writer.beginObject();
    writer.writeMember(QLatin1StringView("subscribe"), true);
    writer.writeMember(QLatin1StringView("listChanged"), true);
    writer.endObject();
    writer.key(QLatin1StringView("prompts"));
    writer.beginObject();
    writer.writeMember(QLatin1StringView("listChanged"), true);
    writer.endObject();
    writer.key(QLatin1StringView("completions"));
    writer.beginObject();
    writer.endObject();
    writer.key(QLatin1StringView("logging"));
    writer.beginObject();
    writer.endObject();
    if (isModern)
    {
        // [D3]: the tasks extension (the legacy experimental tasks are not offered)
        writer.key(QLatin1StringView("extensions"));
        writer.beginObject();
        writer.key(QLatin1StringView(PDFMcpProtocol::TASKS_EXTENSION));
        writer.beginObject();
        writer.endObject();
        writer.endObject();
    }
    writer.endObject();
    return writer.take();
}

QByteArray PDFMcpDispatcher::createServerInfo(PDFMcpEra era) const
{
    const PDFMcpServerInfo& info = m_shared->getConfig().serverInfo;
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("name"), QStringView(info.name));
    if (!info.title.isEmpty())
    {
        writer.writeMember(QLatin1StringView("title"), QStringView(info.title));
    }
    writer.writeMember(QLatin1StringView("version"), QStringView(QString::fromLatin1(m_shared->getServerVersion())));

    // description, websiteUrl and icons are 2025-11-25 and newer
    if (era != PDFMcpEra::Legacy20250618)
    {
        if (!info.description.isEmpty())
        {
            writer.writeMember(QLatin1StringView("description"), QStringView(info.description));
        }
        if (!info.websiteUrl.isEmpty())
        {
            writer.writeMember(QLatin1StringView("websiteUrl"), QStringView(info.websiteUrl));
        }
        if (!info.icons.empty())
        {
            writer.key(QLatin1StringView("icons"));
            writer.beginArray();
            for (const PDFMcpIcon& icon : info.icons)
            {
                icon.write(writer);
            }
            writer.endArray();
        }
    }
    writer.endObject();
    return writer.take();
}

void PDFMcpDispatcher::handleDiscover(const Request& request)
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.key(QLatin1StringView("supportedVersions"));
    writer.writeStringArray(PDFMcpProtocol::getSupportedVersions());
    writer.key(QLatin1StringView("capabilities"));
    writer.writeRaw(createCapabilities(true));
    if (!m_shared->getConfig().instructions.isEmpty())
    {
        writer.writeMember(QLatin1StringView("instructions"), QStringView(m_shared->getConfig().instructions));
    }
    writer.endObject();

    respondResult(request, writer.take(), QByteArrayLiteral("\"ttlMs\":") + QByteArray::number(PDFMcpLimits::DISCOVER_TTL) + ",\"cacheScope\":\"private\"");
}

void PDFMcpDispatcher::handleList(const Request& request, ListKind kind)
{
    qint64 offset = 0;
    const QJsonValue cursorValue = request.params.value(QStringLiteral("cursor"));
    if (!cursorValue.isUndefined())
    {
        const std::optional<qint64> parsed = cursorValue.isString() ? parseCursor(cursorValue.toString()) : std::nullopt;
        if (!parsed)
        {
            respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Invalid cursor"));
            return;
        }
        offset = *parsed;
    }

    std::shared_ptr<const PDFMcpCatalog> catalog = m_shared->getCatalog();
    if (!catalog)
    {
        catalog = m_catalog;
    }

    QLatin1StringView key;
    qint64 count = 0;
    qint64 pageSize = std::numeric_limits<qint64>::max();
    qint64 ttl = 0;
    const QByteArray* cached = nullptr;
    switch (kind)
    {
        case ListKind::Tools:
            key = QLatin1StringView("tools");
            count = qint64(catalog->getTools().size());
            ttl = PDFMcpLimits::TOOLS_LIST_TTL;
            cached = &catalog->getToolsJson(request.era);
            break;
        case ListKind::Prompts:
            key = QLatin1StringView("prompts");
            count = qint64(catalog->getPrompts().size());
            ttl = PDFMcpLimits::STATIC_LIST_TTL;
            cached = &catalog->getPromptsJson(request.era);
            break;
        case ListKind::Resources:
            // Documents change: the list is never cached by the client
            key = QLatin1StringView("resources");
            count = qint64(catalog->getResources().size());
            pageSize = PDFMcpLimits::RESOURCE_PAGE_SIZE;
            ttl = 0;
            cached = &catalog->getResourcesJson(request.era);
            break;
        case ListKind::Templates:
            key = QLatin1StringView("resourceTemplates");
            count = qint64(catalog->getTemplates().size());
            ttl = PDFMcpLimits::STATIC_LIST_TTL;
            cached = &catalog->getTemplatesJson(request.era);
            break;
    }

    offset = qBound<qint64>(0, offset, count);
    const qint64 end = (count - offset > pageSize) ? offset + pageSize : count;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.key(key);
    if (offset == 0 && end == count)
    {
        writer.writeRaw(*cached);
    }
    else
    {
        writer.beginArray();
        for (qint64 i = offset; i < end; ++i)
        {
            switch (kind)
            {
                case ListKind::Tools:
                    catalog->getTools()[size_t(i)].write(writer, request.era);
                    break;
                case ListKind::Prompts:
                    catalog->getPrompts()[size_t(i)].write(writer, request.era);
                    break;
                case ListKind::Resources:
                    catalog->getResources()[size_t(i)].write(writer, request.era);
                    break;
                case ListKind::Templates:
                    catalog->getTemplates()[size_t(i)].write(writer, request.era);
                    break;
            }
        }
        writer.endArray();
    }

    if (end < count)
    {
        writer.writeMember(QLatin1StringView("nextCursor"), QStringView(createCursor(catalog->getGeneration(), end)));
    }
    writer.endObject();

    respondResult(request, writer.take(), QByteArrayLiteral("\"ttlMs\":") + QByteArray::number(ttl) + ",\"cacheScope\":\"private\"");
}

void PDFMcpDispatcher::handleResourceRead(Request& request)
{
    const QJsonValue uriValue = request.params.value(QStringLiteral("uri"));
    const QString uri = uriValue.toString();
    if (!uriValue.isString() || uri.isEmpty() || uri.size() > PDFMcpUriTemplate::MAXIMUM_URI_LENGTH)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.uri must be a URI of at most 2048 characters"));
        return;
    }

    std::shared_ptr<const PDFMcpCatalog> catalog = m_shared->getCatalog();
    if (!catalog)
    {
        catalog = m_catalog;
    }

    const PDFMcpResourceDefinition* resource = catalog->findResource(uri);
    if (resource && resource->isStatic)
    {
        // A static file of the server: no permission, public, cacheable
        PDFMcpJsonWriter writer;
        writer.reserve(resource->content.size() * 2 + 256);
        writer.beginObject();
        writer.key(QLatin1StringView("contents"));
        writer.beginArray();
        writer.beginObject();
        writer.writeMember(QLatin1StringView("uri"), QStringView(resource->uri));
        if (!resource->mimeType.isEmpty())
        {
            writer.writeMember(QLatin1StringView("mimeType"), QStringView(resource->mimeType));
        }
        if (PDFMcpDispatcherProtocolHelper::isTextMimeType(resource->mimeType))
        {
            writer.writeMember(QLatin1StringView("text"), QStringView(QString::fromUtf8(resource->content)));
        }
        else
        {
            writer.key(QLatin1StringView("blob"));
            writer.writeBase64(resource->content);
        }
        writer.endObject();
        writer.endArray();
        writer.endObject();

        respondResult(request, writer.take(), QByteArrayLiteral("\"ttlMs\":") + QByteArray::number(PDFMcpLimits::STATIC_LIST_TTL) + ",\"cacheScope\":\"public\"");
        return;
    }

    QMap<QString, QString> values;
    const PDFMcpResourceTemplateDefinition* resourceTemplate = resource ? nullptr : catalog->matchTemplate(uri, &values);
    if (resource || (resourceTemplate && !resourceTemplate->isStatic))
    {
        // A document resource: the host checks the permissions and reads it
        PDFMcpCallRequest callRequest;
        callRequest.kind = PDFMcpCallKind::ResourceRead;
        callRequest.method = request.method;
        callRequest.uri = uri;

        QJsonObject hashed;
        hashed.insert(QStringLiteral("uri"), uri);
        if (!prepareRetry(request, callRequest, uri, hashed))
        {
            return;
        }
        startCall(request, std::move(callRequest), true, uri, PDFMcpRequestState::hashArguments(hashed));
        return;
    }

    // Not found: -32602 of the modern era, -32002 of the legacy ones (with data.uri)
    PDFMcpJsonWriter data;
    data.beginObject();
    data.writeMember(QLatin1StringView("uri"), QStringView(uri));
    data.endObject();
    respondRequestError(request, request.era == PDFMcpEra::Modern ? PDFMcpErrorCodes::INVALID_PARAMS : PDFMcpErrorCodes::RESOURCE_NOT_FOUND_LEGACY,
                        QStringLiteral("Resource not found"), data.take());
}

bool PDFMcpDispatcher::prepareRetry(Request& request, PDFMcpCallRequest& callRequest, const QString& name, const QJsonObject& hashedArguments)
{
    if (request.era != PDFMcpEra::Modern)
    {
        return true;
    }

    const bool isToolCall = callRequest.kind == PDFMcpCallKind::ToolCall;
    const QJsonValue responsesValue = request.params.value(QStringLiteral("inputResponses"));
    const QJsonValue stateValue = request.params.value(QStringLiteral("requestState"));
    if (!responsesValue.isUndefined() && !responsesValue.isObject())
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.inputResponses must be an object"));
        return false;
    }

    // Answers without a state cannot be verified: they are ignored and the host asks again
    if (stateValue.isUndefined())
    {
        return true;
    }

    const QJsonObject responses = responsesValue.toObject();
    if (PDFMcpJson::toCompactJson(responses).size() > PDFMcpLimits::MAXIMUM_INPUT_RESPONSES_SIZE)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.inputResponses is larger than 64 KiB"));
        return false;
    }

    PDFMcpRequestStateData data;
    PDFMcpRequestStateResult result = PDFMcpRequestStateResult::Malformed;
    if (stateValue.isString() && stateValue.toString().size() <= PDFMcpLimits::MAXIMUM_REQUEST_STATE_SIZE)
    {
        // "servers MUST treat requestState as an attacker-controlled input ...
        // MUST reject state that fails verification"
        result = m_requestState.unseal(stateValue.toString().toLatin1(), PDFMcpClock::now(), request.client.assistantKey, callRequest.method, name,
                                       PDFMcpRequestState::hashArguments(hashedArguments), &data);
    }

    if (result != PDFMcpRequestStateResult::Valid)
    {
        if (isToolCall)
        {
            respondResult(request, PDFMcpCallResult::createToolError(PDFMcpDispatcherProtocolHelper::getExpiredText()).result);
        }
        else
        {
            respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, PDFMcpDispatcherProtocolHelper::getExpiredText());
        }
        return false;
    }

    bool isDeclined = false;
    const QJsonObject accepted = filterInputResponses(responses, data.allowedValues, &isDeclined);
    if (isDeclined)
    {
        if (isToolCall)
        {
            respondResult(request, PDFMcpCallResult::createToolError(PDFMcpDispatcherProtocolHelper::getDeclinedText()).result);
        }
        else
        {
            respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, PDFMcpDispatcherProtocolHelper::getDeclinedText());
        }
        return false;
    }

    QJsonObject state;
    state.insert(QStringLiteral("allowedValues"), data.allowedValues);
    callRequest.inputResponses = accepted;
    callRequest.requestState = state;
    return true;
}

void PDFMcpDispatcher::handleToolCall(Request& request)
{
    const QJsonValue nameValue = request.params.value(QStringLiteral("name"));
    const QString name = nameValue.toString();
    if (!nameValue.isString() || name.isEmpty() || name.size() > 256)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.name must be the name of a tool"));
        return;
    }

    const QJsonValue argumentsValue = request.params.value(QStringLiteral("arguments"));
    if (!argumentsValue.isUndefined() && !argumentsValue.isObject())
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.arguments must be an object"));
        return;
    }

    PDFMcpCallRequest callRequest;
    callRequest.kind = PDFMcpCallKind::ToolCall;
    callRequest.method = request.method;
    callRequest.name = name;
    callRequest.arguments = argumentsValue.toObject();
    if (!prepareRetry(request, callRequest, name, callRequest.arguments))
    {
        return;
    }

    const QByteArray hash = PDFMcpRequestState::hashArguments(callRequest.arguments);
    startCall(request, std::move(callRequest), true, name, hash);
}

void PDFMcpDispatcher::handlePromptGet(Request& request)
{
    const QJsonValue nameValue = request.params.value(QStringLiteral("name"));
    const QString name = nameValue.toString();
    std::shared_ptr<const PDFMcpCatalog> catalog = m_shared->getCatalog();
    if (!catalog)
    {
        catalog = m_catalog;
    }

    if (!nameValue.isString() || !catalog->findPrompt(name))
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS,
                            QStringLiteral("Unknown prompt: %1").arg(PDFMcpJson::sanitizeString(name, PDFMcpJson::SanitizeFlag::RemoveBidi, 64)));
        return;
    }

    const QJsonValue argumentsValue = request.params.value(QStringLiteral("arguments"));
    if (!argumentsValue.isUndefined() && !argumentsValue.isObject())
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.arguments must be an object of strings"));
        return;
    }

    const QJsonObject arguments = argumentsValue.toObject();
    for (auto it = arguments.begin(); it != arguments.end(); ++it)
    {
        if (!it.value().isString())
        {
            respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.arguments must be an object of strings"));
            return;
        }
    }

    PDFMcpCallRequest callRequest;
    callRequest.kind = PDFMcpCallKind::PromptGet;
    callRequest.method = request.method;
    callRequest.name = name;
    callRequest.arguments = arguments;
    if (!prepareRetry(request, callRequest, name, arguments))
    {
        return;
    }
    startCall(request, std::move(callRequest), true, name, PDFMcpRequestState::hashArguments(arguments));
}

void PDFMcpDispatcher::handleCompletion(Request& request)
{
    if (!request.params.value(QStringLiteral("ref")).isObject() || !request.params.value(QStringLiteral("argument")).isObject())
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.ref and params.argument are required"));
        return;
    }

    // Completion is asked at typing: 20 per second per assistant
    qint64 retryAfter = 0;
    if (!m_completionLimiter.allow(request.client.assistantKey, PDFMcpClock::now(), &retryAfter))
    {
        respondRateLimited(request, retryAfter);
        return;
    }

    PDFMcpCallRequest callRequest;
    callRequest.kind = PDFMcpCallKind::Completion;
    callRequest.method = request.method;
    callRequest.arguments = request.params;
    callRequest.arguments.remove(QStringLiteral("_meta"));

    // No MRTR for completion ("not allowed")
    startCall(request, std::move(callRequest), false, QString(), QByteArray());
}

void PDFMcpDispatcher::handleSubscribe(const Request& request, PDFMcpSession* session, bool subscribe)
{
    const QJsonValue uriValue = request.params.value(QStringLiteral("uri"));
    const QString uri = uriValue.toString();
    if (!uriValue.isString() || !PDFMcpSubscriptions::isValidUri(uri))
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.uri must be a pdf4qt:// URI"));
        return;
    }

    if (subscribe)
    {
        if (!session->subscriptions.add(uri))
        {
            respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("At most 256 resources can be subscribed"));
            return;
        }
    }
    else
    {
        session->subscriptions.remove(uri);
    }
    respondResult(request, QByteArrayLiteral("{}"));
}

void PDFMcpDispatcher::handleSetLevel(const Request& request, PDFMcpSession* session)
{
    const QJsonValue levelValue = request.params.value(QStringLiteral("level"));
    if (!levelValue.isString() || PDFMcpProtocol::getLogLevelIndex(levelValue.toString()) < 0)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.level must be a log level of RFC 5424"));
        return;
    }

    session->logLevel = levelValue.toString();
    respondResult(request, QByteArrayLiteral("{}"));
}

void PDFMcpDispatcher::handleListen(Request& request)
{
    QString error;
    std::optional<PDFMcpSubscriptionFilter> filter = PDFMcpSubscriptionFilter::parse(request.params.value(QStringLiteral("notifications")), &error);
    if (!filter)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, error);
        return;
    }

    const QString& key = request.client.assistantKey;
    if (filter->hasTaskIds)
    {
        if (!request.client.supportsTasks())
        {
            // "If a client requests task status notifications but does not declare
            // the io.modelcontextprotocol/tasks extension capability, the server MUST return ... -32021"
            respondRequestError(request, PDFMcpErrorCodes::MISSING_CLIENT_CAPABILITY, QStringLiteral("Missing required client capability"),
                                QByteArrayLiteral("{\"requiredCapabilities\":{\"extensions\":{\"io.modelcontextprotocol/tasks\":{}}}}"), 400);
            return;
        }

        // Only the own existing tasks are acknowledged
        QStringList taskIds;
        for (const QString& taskId : filter->taskIds)
        {
            if (m_tasks.findOwned(taskId, key))
            {
                taskIds.append(taskId);
            }
        }
        filter->taskIds = taskIds;
    }

    int ownStreams = 0;
    for (const auto& item : m_listenStreams)
    {
        if (item.second->assistantKey == key)
        {
            ++ownStreams;
        }
    }

    if (ownStreams >= PDFMcpLimits::MAXIMUM_LISTEN_STREAMS_PER_ASSISTANT || int(m_listenStreams.size()) >= PDFMcpLimits::MAXIMUM_LISTEN_STREAMS)
    {
        respondRateLimited(request, 1000);
        return;
    }

    std::unique_ptr<ListenStream> stream = std::make_unique<ListenStream>();
    stream->exchange = request.exchange;
    stream->requestId = request.id;
    stream->assistantKey = key;
    stream->filter = *filter;
    for (const QString& uri : filter->resourceSubscriptions)
    {
        stream->subscriptions.add(uri);
    }

    // The acknowledgment is the first message of the stream
    PDFMcpJsonWriter params;
    params.beginObject();
    params.writeRawMembers(PDFMcpDispatcherHelper::createSubscriptionMeta(request.id));
    params.key(QLatin1StringView("notifications"));
    filter->write(params);
    params.endObject();
    QByteArray paramsMembers = params.take();
    paramsMembers = paramsMembers.mid(1, paramsMembers.size() - 2);

    m_assistants->addStream(key);
    request.exchange->beginStream();
    request.exchange->sendEvent(PDFMcpDispatcherHelper::createNotification(QLatin1StringView("notifications/subscriptions/acknowledged"), paramsMembers));
    m_listenStreams[request.exchange->getId()] = std::move(stream);
}

// ----------------------------------------------------------------------------
// Tasks [D3]
// ----------------------------------------------------------------------------

void PDFMcpDispatcher::handleTaskGet(const Request& request)
{
    if (!request.client.supportsTasks())
    {
        respondRequestError(request, PDFMcpErrorCodes::MISSING_CLIENT_CAPABILITY, QStringLiteral("Missing required client capability"),
                            QByteArrayLiteral("{\"requiredCapabilities\":{\"extensions\":{\"io.modelcontextprotocol/tasks\":{}}}}"), 400);
        return;
    }

    PDFMcpTask* task = m_tasks.findOwned(request.params.value(QStringLiteral("taskId")).toString(), request.client.assistantKey);
    if (!task)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Unknown task"));
        return;
    }

    respondResult(request, createTaskJson(*task));
}

void PDFMcpDispatcher::handleTaskUpdate(const Request& request)
{
    if (!request.client.supportsTasks())
    {
        respondRequestError(request, PDFMcpErrorCodes::MISSING_CLIENT_CAPABILITY, QStringLiteral("Missing required client capability"),
                            QByteArrayLiteral("{\"requiredCapabilities\":{\"extensions\":{\"io.modelcontextprotocol/tasks\":{}}}}"), 400);
        return;
    }

    PDFMcpTask* task = m_tasks.findOwned(request.params.value(QStringLiteral("taskId")).toString(), request.client.assistantKey);
    if (!task)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Unknown task"));
        return;
    }

    const QJsonValue responsesValue = request.params.value(QStringLiteral("inputResponses"));
    if (!responsesValue.isObject() || PDFMcpJson::toCompactJson(responsesValue.toObject()).size() > PDFMcpLimits::MAXIMUM_INPUT_RESPONSES_SIZE)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("params.inputResponses must be an object of at most 64 KiB"));
        return;
    }

    if (task->status != PDFMcpTask::Status::InputRequired)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("The task is not waiting for input"));
        return;
    }

    PendingCall* pending = findPending(task->callId);
    bool isDeclined = false;
    const QJsonObject acceptedOfClient = filterInputResponses(responsesValue.toObject(), task->allowedValues, &isDeclined);

    // The keys of the round of the client back to the keys of the host
    // (follow-up 32); a key of an older round is not outstanding any more
    QJsonObject accepted;
    for (auto it = acceptedOfClient.begin(); pending && it != acceptedOfClient.end(); ++it)
    {
        const auto keyIt = pending->taskInputKeys.find(it.key());
        if (keyIt != pending->taskInputKeys.cend())
        {
            accepted.insert(keyIt->second, it.value());
        }
    }
    if (!pending)
    {
        task->status = PDFMcpTask::Status::Failed;
        task->error = PDFMcpDispatcherHelper::createErrorObject(PDFMcpErrorCodes::INTERNAL_ERROR, QStringLiteral("The task was lost"), QByteArrayView());
        task->endedAt = task->lastUpdatedAt = PDFMcpClock::now();
        updateTask(*task);
    }
    else if (isDeclined)
    {
        deliverDeclined(*pending, PDFMcpDispatcherProtocolHelper::getDeclinedText());
    }
    else if (!accepted.isEmpty())
    {
        task->status = PDFMcpTask::Status::Working;
        task->statusMessage = QStringLiteral("PDF4QT is working on the request");
        task->inputRequests.clear();
        task->lastUpdatedAt = PDFMcpClock::now();
        updateTask(*task);
        redispatch(*pending, accepted);
    }

    // The acknowledgment is eventually consistent (an incomplete answer keeps the task waiting)
    respondResult(request, QByteArrayLiteral("{}"));
}

void PDFMcpDispatcher::handleTaskCancel(const Request& request)
{
    if (!request.client.supportsTasks())
    {
        respondRequestError(request, PDFMcpErrorCodes::MISSING_CLIENT_CAPABILITY, QStringLiteral("Missing required client capability"),
                            QByteArrayLiteral("{\"requiredCapabilities\":{\"extensions\":{\"io.modelcontextprotocol/tasks\":{}}}}"), 400);
        return;
    }

    PDFMcpTask* task = m_tasks.findOwned(request.params.value(QStringLiteral("taskId")).toString(), request.client.assistantKey);
    if (!task)
    {
        respondRequestError(request, PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Unknown task"));
        return;
    }

    if (!task->isTerminal())
    {
        const quint64 callId = task->callId;
        task->status = PDFMcpTask::Status::Canceled;
        task->statusMessage = QStringLiteral("Canceled by the client");
        task->endedAt = task->lastUpdatedAt = PDFMcpClock::now();
        task->callId = 0;
        updateTask(*task);

        if (PendingCall* pending = findPending(callId))
        {
            cancelCall(*pending, PDFMcpCancelReason::TaskCanceled);
            removePending(callId);
        }
    }

    respondResult(request, QByteArrayLiteral("{}"));
}

}   // namespace pdf
