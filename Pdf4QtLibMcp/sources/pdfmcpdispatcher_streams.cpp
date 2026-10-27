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
#include "pdfmcphost.h"

#include <algorithm>

namespace pdf
{

// ----------------------------------------------------------------------------
// Closed exchanges
// ----------------------------------------------------------------------------

void PDFMcpDispatcher::handleExchangeClosed(std::shared_ptr<PDFMcpExchange> exchange)
{
    if (!exchange)
    {
        return;
    }

    const quint64 exchangeId = exchange->getId();

    // "Closing the SSE response stream MUST be treated by the server as
    // cancellation of that request" (also a request, whose response has not begun)
    auto callIt = m_exchangeCalls.find(exchangeId);
    if (callIt != m_exchangeCalls.end())
    {
        const quint64 pendingId = callIt->second;
        if (PendingCall* pending = findPending(pendingId))
        {
            if (pending->mode != PendingCall::Mode::Task)
            {
                cancelCall(*pending, PDFMcpCancelReason::StreamClosed);
                removePending(pendingId);
            }
        }
        m_exchangeCalls.erase(exchangeId);
    }

    auto listenIt = m_listenStreams.find(exchangeId);
    if (listenIt != m_listenStreams.end())
    {
        m_assistants->removeStream(listenIt->second->assistantKey);
        m_listenStreams.erase(listenIt);
    }

    auto getIt = m_getStreams.find(exchangeId);
    if (getIt != m_getStreams.end())
    {
        if (PDFMcpSession* session = m_sessions.findByKey(getIt->second))
        {
            if (session->stream && session->stream->getId() == exchangeId)
            {
                session->stream.reset();
            }
        }
        m_getStreams.erase(getIt);
    }
}

// ----------------------------------------------------------------------------
// Notifications of the lists and of the resources
// ----------------------------------------------------------------------------

void PDFMcpDispatcher::handleCatalogPublished()
{
    std::shared_ptr<const PDFMcpCatalog> catalog = m_shared->getCatalog();
    if (!catalog || catalog == m_catalog)
    {
        return;
    }

    // list_changed is derived from the difference of the serialized lists
    std::shared_ptr<const PDFMcpCatalog> previous = std::move(m_catalog);
    m_catalog = catalog;

    const PDFMcpEra era = PDFMcpEra::Modern;
    if (catalog->getToolsJson(era) != previous->getToolsJson(era))
    {
        sendListChanged(ListKind::Tools);
    }
    if (catalog->getPromptsJson(era) != previous->getPromptsJson(era))
    {
        sendListChanged(ListKind::Prompts);
    }
    if (catalog->getResourcesJson(era) != previous->getResourcesJson(era) || catalog->getTemplatesJson(era) != previous->getTemplatesJson(era))
    {
        sendListChanged(ListKind::Resources);
    }
}

void PDFMcpDispatcher::sendListChanged(ListKind kind)
{
    QLatin1StringView method;
    switch (kind)
    {
        case ListKind::Tools:
            method = QLatin1StringView("notifications/tools/list_changed");
            break;
        case ListKind::Prompts:
            method = QLatin1StringView("notifications/prompts/list_changed");
            break;
        case ListKind::Resources:
        case ListKind::Templates:
            method = QLatin1StringView("notifications/resources/list_changed");
            break;
    }

    // Only to the listen streams, which asked for the type ("MUST NOT send
    // notification types the client has not explicitly requested")
    for (const auto& item : m_listenStreams)
    {
        ListenStream& stream = *item.second;
        const bool isRequested = (kind == ListKind::Tools && stream.filter.toolsListChanged) ||
                                 (kind == ListKind::Prompts && stream.filter.promptsListChanged) ||
                                 ((kind == ListKind::Resources || kind == ListKind::Templates) && stream.filter.resourcesListChanged);
        if (isRequested)
        {
            stream.exchange->sendEvent(PDFMcpDispatcherHelper::createNotification(method, PDFMcpDispatcherHelper::createSubscriptionMeta(stream.requestId)));
        }
    }

    // The legacy sessions get them on their GET streams (capabilities listChanged)
    for (const auto& item : m_sessions.getSessions())
    {
        const PDFMcpSession& session = *item.second;
        if (session.stream)
        {
            session.stream->sendEvent(PDFMcpDispatcherHelper::createNotification(method, QByteArrayView()));
        }
    }
}

void PDFMcpDispatcher::handleResourceUpdated(QString uri)
{
    if (uri.isEmpty() || uri.size() > PDFMcpUriTemplate::MAXIMUM_URI_LENGTH)
    {
        return;
    }

    const qint64 now = PDFMcpClock::now();
    const qint64 interval = m_shared->getConfig().resourceUpdateInterval;
    bool hasPending = false;

    for (const auto& item : m_listenStreams)
    {
        ListenStream& stream = *item.second;
        const QStringList uris = stream.subscriptions.update(uri, now, interval);
        for (const QString& updated : uris)
        {
            const QByteArray params = PDFMcpDispatcherHelper::createSubscriptionMeta(stream.requestId) + ",\"uri\":" + PDFMcpJsonWriter::quote(updated);
            stream.exchange->sendEvent(PDFMcpDispatcherHelper::createNotification(QLatin1StringView("notifications/resources/updated"), params));
        }
        hasPending = hasPending || stream.subscriptions.hasPending();
    }

    for (const auto& item : m_sessions.getSessions())
    {
        PDFMcpSession& session = *item.second;
        const QStringList uris = session.subscriptions.update(uri, now, interval);
        if (session.stream)
        {
            for (const QString& updated : uris)
            {
                const QByteArray params = QByteArrayLiteral("\"uri\":") + PDFMcpJsonWriter::quote(updated);
                session.stream->sendEvent(PDFMcpDispatcherHelper::createNotification(QLatin1StringView("notifications/resources/updated"), params));
            }
        }
        hasPending = hasPending || session.subscriptions.hasPending();
    }

    if (hasPending && m_updateTimer && !m_updateTimer->isActive())
    {
        m_updateTimer->start(qMax(1, int(interval)));
    }
}

void PDFMcpDispatcher::flushResourceUpdates()
{
    const qint64 now = PDFMcpClock::now();
    const qint64 interval = m_shared->getConfig().resourceUpdateInterval;
    bool hasPending = false;

    for (const auto& item : m_listenStreams)
    {
        ListenStream& stream = *item.second;
        const QStringList uris = stream.subscriptions.takeDue(now, interval);
        for (const QString& updated : uris)
        {
            const QByteArray params = PDFMcpDispatcherHelper::createSubscriptionMeta(stream.requestId) + ",\"uri\":" + PDFMcpJsonWriter::quote(updated);
            stream.exchange->sendEvent(PDFMcpDispatcherHelper::createNotification(QLatin1StringView("notifications/resources/updated"), params));
        }
        hasPending = hasPending || stream.subscriptions.hasPending();
    }

    for (const auto& item : m_sessions.getSessions())
    {
        PDFMcpSession& session = *item.second;
        const QStringList uris = session.subscriptions.takeDue(now, interval);
        if (session.stream)
        {
            for (const QString& updated : uris)
            {
                const QByteArray params = QByteArrayLiteral("\"uri\":") + PDFMcpJsonWriter::quote(updated);
                session.stream->sendEvent(PDFMcpDispatcherHelper::createNotification(QLatin1StringView("notifications/resources/updated"), params));
            }
        }
        hasPending = hasPending || session.subscriptions.hasPending();
    }

    if (hasPending && m_updateTimer)
    {
        m_updateTimer->start(qMax(1, int(interval)));
    }
}

void PDFMcpDispatcher::sendTaskNotification(const PDFMcpTask& task)
{
    for (const auto& item : m_listenStreams)
    {
        ListenStream& stream = *item.second;
        if (stream.assistantKey != task.assistantKey || !stream.filter.taskIds.contains(task.taskId))
        {
            continue;
        }

        // Each notification carries the whole task, as tasks/get would return it
        PDFMcpJsonWriter params;
        params.beginObject();
        task.writeMembers(params, PDFMcpLimits::TASK_TTL, PDFMcpLimits::TASK_POLL_INTERVAL);
        params.writeRawMembers(PDFMcpDispatcherHelper::createSubscriptionMeta(stream.requestId));
        params.endObject();
        const QByteArray members = params.getData().mid(1, params.getData().size() - 2);
        stream.exchange->sendEvent(PDFMcpDispatcherHelper::createNotification(QLatin1StringView("notifications/tasks"), members));
    }
}

void PDFMcpDispatcher::endListenStream(ListenStream& stream, bool closeConnection)
{
    // Graceful end: the final result of subscriptions/listen with its
    // subscriptionId, then the end of the stream
    PDFMcpJsonWriter result;
    result.beginObject();
    result.writeMember(QLatin1StringView("resultType"), QLatin1StringView("complete"));
    result.key(QLatin1StringView("_meta"));
    result.beginObject();
    result.key(QLatin1StringView(PDFMcpProtocol::META_SUBSCRIPTION_ID));
    stream.requestId.write(result);
    result.key(QLatin1StringView(PDFMcpProtocol::META_SERVER_INFO));
    result.writeRaw(createServerInfo(PDFMcpEra::Modern));
    result.endObject();
    result.endObject();

    stream.exchange->sendEvent(createResultMessage(stream.requestId, result.getData()));
    stream.exchange->endStream(closeConnection);
}

void PDFMcpDispatcher::sendKeepAlives(qint64 now)
{
    const qint64 interval = m_shared->getConfig().keepAliveInterval;
    for (const auto& item : m_calls)
    {
        const PendingCall& pending = *item.second;
        if (pending.mode == PendingCall::Mode::Stream && pending.exchange && now - pending.exchange->getLastWriteTime() >= interval)
        {
            pending.exchange->sendComment("keep-alive");
        }
    }

    for (const auto& item : m_listenStreams)
    {
        const std::shared_ptr<PDFMcpExchange>& exchange = item.second->exchange;
        if (now - exchange->getLastWriteTime() >= interval)
        {
            exchange->sendComment("keep-alive");
        }
    }

    for (const auto& item : m_sessions.getSessions())
    {
        const std::shared_ptr<PDFMcpExchange>& exchange = item.second->stream;
        if (exchange && now - exchange->getLastWriteTime() >= interval)
        {
            exchange->sendComment("keep-alive");
        }
    }
}

// ----------------------------------------------------------------------------
// Assistants and sessions [D2]
// ----------------------------------------------------------------------------

void PDFMcpDispatcher::postAssistantEvent(const PDFMcpAssistantEvent& event)
{
    PDFMcpHost* host = m_shared->host.data();
    if (host && !event.key.isEmpty())
    {
        QMetaObject::invokeMethod(host, &PDFMcpHost::assistantEvent, Qt::QueuedConnection, event);
    }
}

void PDFMcpDispatcher::publishAssistants(bool changed)
{
    if (m_shared->link)
    {
        m_shared->link->setAssistants(m_assistants->getAssistants(), changed);
    }
}

void PDFMcpDispatcher::onAssistantConnected(const PDFMcpAssistantEvent& event)
{
    postAssistantEvent(event);
}

void PDFMcpDispatcher::removeAssistant(const QString& key, PDFMcpAssistantEvent::Kind kind)
{
    const bool isKnown = m_assistants->contains(key);
    const PDFMcpAssistantEvent event = m_assistants->createEvent(key, kind);
    m_assistants->remove(key);
    m_rateLimiter.remove(key);
    m_completionLimiter.remove(key);

    if (isKnown || kind == PDFMcpAssistantEvent::Kind::SessionEnded)
    {
        postAssistantEvent(event);
    }
    publishAssistants(true);
}

void PDFMcpDispatcher::endSession(const QString& key, PDFMcpAssistantEvent::Kind kind)
{
    std::unique_ptr<PDFMcpSession> session = m_sessions.remove(key);
    if (!session)
    {
        return;
    }

    PDFMcpCancelReason reason = PDFMcpCancelReason::ClientCanceled;
    switch (kind)
    {
        case PDFMcpAssistantEvent::Kind::Disconnected:
            reason = PDFMcpCancelReason::Disconnected;
            break;
        case PDFMcpAssistantEvent::Kind::SessionEnded:
            reason = m_isShutDown ? PDFMcpCancelReason::ServerStopped : PDFMcpCancelReason::ClientCanceled;
            break;
        default:
            break;
    }

    // The calls of the session are canceled; their hosts answer them, or the
    // grace time does
    const qint64 deadline = PDFMcpClock::now() + m_shared->getConfig().cancelGraceTime;
    for (const auto& item : m_calls)
    {
        PendingCall& pending = *item.second;
        if (pending.request.client.sessionKey == key)
        {
            cancelCall(pending, reason);
            pending.isEliciting = false;
            pending.cancelDeadline = deadline;
        }
    }

    if (session->stream)
    {
        m_getStreams.erase(session->stream->getId());
        session->stream->endStream(m_isShutDown);
        session->stream.reset();
    }

    removeAssistant(key, kind);
}

void PDFMcpDispatcher::handleDisconnectAssistant(QString key)
{
    if (!PDFMcpAssistantRegistry::isValidKey(key))
    {
        return;
    }

    if (m_sessions.findByKey(key))
    {
        endSession(key, PDFMcpAssistantEvent::Kind::Disconnected);
        return;
    }

    // Its calls are canceled (denied_disconnected) - also its tasks
    const qint64 now = PDFMcpClock::now();
    const qint64 deadline = now + m_shared->getConfig().cancelGraceTime;
    for (const auto& item : m_calls)
    {
        PendingCall& pending = *item.second;
        if (pending.request.client.assistantKey == key)
        {
            cancelCall(pending, PDFMcpCancelReason::Disconnected);
            pending.cancelDeadline = deadline;
        }
    }

    for (const QString& taskId : m_tasks.getTaskIds())
    {
        PDFMcpTask* task = m_tasks.find(taskId);
        if (task && task->assistantKey == key && task->status == PDFMcpTask::Status::InputRequired)
        {
            // A waiting task has no running call
            task->status = PDFMcpTask::Status::Canceled;
            task->statusMessage = QStringLiteral("The assistant was disconnected in PDF4QT");
            task->endedAt = task->lastUpdatedAt = now;
            const quint64 callId = task->callId;
            task->callId = 0;
            removePending(callId);
            sendTaskNotification(*task);
        }
    }

    // Its listen streams end gracefully (the connections stay)
    std::vector<quint64> streams;
    for (const auto& item : m_listenStreams)
    {
        if (item.second->assistantKey == key)
        {
            streams.push_back(item.first);
        }
    }
    for (quint64 id : streams)
    {
        auto it = m_listenStreams.find(id);
        endListenStream(*it->second, false);
        m_listenStreams.erase(it);
    }

    removeAssistant(key, PDFMcpAssistantEvent::Kind::Disconnected);
}

void PDFMcpDispatcher::handleDisconnectAll()
{
    for (const QString& key : m_sessions.getKeys())
    {
        endSession(key, PDFMcpAssistantEvent::Kind::Disconnected);
    }

    std::vector<QString> keys;
    for (const PDFMcpAssistantInfo& info : m_assistants->getAssistants())
    {
        keys.push_back(info.key);
    }
    for (const auto& item : m_listenStreams)
    {
        keys.push_back(item.second->assistantKey);
    }
    for (const auto& item : m_calls)
    {
        keys.push_back(item.second->request.client.assistantKey);
    }

    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    for (const QString& key : keys)
    {
        handleDisconnectAssistant(key);
    }
}

void PDFMcpDispatcher::handleMaintenance()
{
    if (m_isShutDown)
    {
        return;
    }

    const qint64 now = PDFMcpClock::now();
    sendKeepAlives(now);
    checkCalls(now);

    // Legacy sessions idle for 30 minutes end (an open GET stream keeps them)
    for (const QString& key : m_sessions.getIdleSessions(now, PDFMcpLimits::SESSION_IDLE_TIME))
    {
        bool hasCall = false;
        for (const auto& item : m_calls)
        {
            hasCall = hasCall || item.second->request.client.sessionKey == key;
        }
        if (!hasCall)
        {
            endSession(key, PDFMcpAssistantEvent::Kind::SessionEnded);
        }
    }

    // Legacy sessions, which got no message after their initialize, end after
    // 10 s (follow-up 36: a token holder cannot hold every session slot by
    // initialize alone)
    for (const QString& key : m_sessions.getUnusedSessions(now, PDFMcpLimits::UNUSED_SESSION_TIME))
    {
        bool hasCall = false;
        for (const auto& item : m_calls)
        {
            hasCall = hasCall || item.second->request.client.sessionKey == key;
        }
        if (!hasCall)
        {
            endSession(key, PDFMcpAssistantEvent::Kind::SessionEnded);
        }
    }

    // Modern assistants idle for 30 minutes [D2]
    const std::vector<PDFMcpAssistantEvent> idle = m_assistants->removeIdle(now, PDFMcpLimits::ASSISTANT_IDLE_TIME);
    for (const PDFMcpAssistantEvent& event : idle)
    {
        m_rateLimiter.remove(event.key);
        m_completionLimiter.remove(event.key);
        postAssistantEvent(event);
    }
    if (!idle.empty())
    {
        publishAssistants(true);
    }

    m_tasks.purge(now, PDFMcpLimits::TASK_TTL);
}

// ----------------------------------------------------------------------------
// Stop
// ----------------------------------------------------------------------------

void PDFMcpDispatcher::shutdown()
{
    if (m_isShutDown)
    {
        return;
    }

    // New requests get 503 from their connections from now on
    m_isShutDown = true;
    m_shared->setStopping();
    if (m_shared->link)
    {
        m_shared->link->setDispatcher(nullptr);
    }

    if (m_maintenanceTimer)
    {
        m_maintenanceTimer->stop();
    }
    if (m_updateTimer)
    {
        m_updateTimer->stop();
    }

    // Every call is canceled (denied_stopped) and answered by -31002
    std::vector<quint64> ids;
    for (const auto& item : m_calls)
    {
        ids.push_back(item.first);
    }
    for (quint64 id : ids)
    {
        PendingCall* pending = findPending(id);
        if (!pending)
        {
            continue;
        }

        cancelCall(*pending, PDFMcpCancelReason::ServerStopped);
        if (pending->exchange && !pending->exchange->isClosed())
        {
            const QByteArray message = createErrorMessage(pending->request.id, PDFMcpErrorCodes::SERVER_STOPPING, QStringLiteral("The MCP server of PDF4QT is stopping"), QByteArrayView());
            if (pending->mode == PendingCall::Mode::Stream || pending->exchange->isStreaming())
            {
                pending->exchange->sendEvent(message);
                pending->exchange->endStream(true);
            }
            else
            {
                PDFMcpHttpResponse response;
                response.status = 503;
                response.headers.emplace_back(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
                response.body = message;
                response.closeConnection = true;
                pending->exchange->respond(std::move(response));
            }
        }
        removePending(id);
    }
    m_tasks.clear();

    // The listen streams end gracefully, the GET streams close
    for (const auto& item : m_listenStreams)
    {
        endListenStream(*item.second, true);
    }
    m_listenStreams.clear();

    for (const auto& item : m_sessions.getSessions())
    {
        PDFMcpSession& session = *item.second;
        if (!session.stream)
        {
            continue;
        }

        // A message of the server level, when the session asked for notices
        const int levelIndex = PDFMcpProtocol::getLogLevelIndex(session.logLevel);
        if (levelIndex >= 0 && levelIndex <= PDFMcpProtocol::getLogLevelIndex(u"notice"))
        {
            const QByteArray params = QByteArrayLiteral("\"level\":\"notice\",\"logger\":\"pdf4qt\",\"data\":\"The MCP server of PDF4QT is stopping\"");
            session.stream->sendEvent(PDFMcpDispatcherHelper::createNotification(QLatin1StringView("notifications/message"), params));
        }
    }

    for (const QString& key : m_sessions.getKeys())
    {
        endSession(key, PDFMcpAssistantEvent::Kind::SessionEnded);
    }
    m_getStreams.clear();
    m_exchangeCalls.clear();

    m_assistants->clear();
    publishAssistants(true);
}

}   // namespace pdf
