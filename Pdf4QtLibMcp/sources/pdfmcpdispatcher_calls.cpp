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
#include "pdfthreadannotations.h"

namespace pdf
{

/// Texts of the calls
class PDFMcpDispatcherCallsHelper
{
public:
    PDFMcpDispatcherCallsHelper() = delete;

    static QString getNoElicitationText(const PDFMcpInputRequests& requests)
    {
        const QString message = requests.requests.empty() ? QString() : requests.requests.front().message;
        return QStringLiteral("PDF4QT needs a choice of the user, but the client cannot ask the user (no elicitation capability). %1").arg(message).trimmed();
    }

    /// Key of an input request of a task in the round \p round ("pdf4qt.choice#2")
    static QString createTaskInputKey(const QString& key, int round)
    {
        return key + QLatin1Char('#') + QString::number(round);
    }
};

// ----------------------------------------------------------------------------
// Starting a call
// ----------------------------------------------------------------------------

PDFMcpDispatcher::PendingCall* PDFMcpDispatcher::findPending(quint64 pendingId)
{
    auto it = m_calls.find(pendingId);
    return it != m_calls.end() ? it->second.get() : nullptr;
}

void PDFMcpDispatcher::startCall(Request& request, PDFMcpCallRequest callRequest, bool isRetryable, const QString& retryName, const QByteArray& argumentsHash)
{
    const QString& key = request.client.assistantKey;
    if (m_assistants->getCallCount(key) >= PDFMcpLimits::MAXIMUM_CALLS_PER_ASSISTANT || int(m_calls.size()) >= PDFMcpLimits::MAXIMUM_CALLS)
    {
        respondRateLimited(request, 1000);
        return;
    }

    callRequest.client = request.client;
    callRequest.id = request.id;
    callRequest.receivedAt = PDFMcpClock::now();

    std::unique_ptr<PendingCall> pending = std::make_unique<PendingCall>();
    pending->id = ++m_nextPendingId;
    pending->exchange = request.exchange;
    pending->request = std::move(callRequest);
    pending->progressToken = request.progressToken;
    pending->isRetryable = isRetryable;
    pending->retryName = retryName;
    pending->argumentsHash = argumentsHash;

    const PDFMcpServerConfig& config = m_shared->getConfig();
    const bool isTaskCandidate = pending->request.kind == PDFMcpCallKind::ToolCall && pending->request.client.supportsTasks();
    pending->timer = new QTimer(this);
    pending->timer->setSingleShot(true);
    connect(pending->timer, &QTimer::timeout, this, &PDFMcpDispatcher::onPendingTimer);

    PendingCall& pendingRef = *pending;
    m_exchangeCalls[request.exchange->getId()] = pending->id;
    m_calls[pending->id] = std::move(pending);
    m_assistants->addCall(key);
    pendingRef.isCounted = true;

    pendingRef.timer->start(qMax(1, isTaskCandidate ? config.taskCreationDelay : config.jsonResponseDelay));
    launchCall(pendingRef);
}

void PDFMcpDispatcher::launchCall(PendingCall& pending)
{
    if (pending.call)
    {
        pending.call->setSink(nullptr);
        m_callIndex.erase(pending.call.get());
    }

    pending.call = PDFMcpCall::create(pending.request);
    m_callIndex[pending.call.get()] = pending.id;
    pending.call->setSink(this);

    // The host lives in the GUI thread and outlives the server (plan 2.6)
    PDFMcpHost* host = m_shared->host.data();
    if (!host)
    {
        pending.call->finish(PDFMcpCallResult::createError(PDFMcpErrorCodes::INTERNAL_ERROR, QStringLiteral("PDF4QT cannot answer the request now")));
        return;
    }

    pending.call->setHost(host);
    PDFThreadAnnotations::happensBefore(pending.call.get());
    PDFThreadAnnotations::TransportScope transportScope;
    QMetaObject::invokeMethod(host, &PDFMcpHost::handleCall, Qt::QueuedConnection, pending.call);
}

void PDFMcpDispatcher::onPendingTimer()
{
    // The timer does not identify its call by the signal
    QTimer* timer = qobject_cast<QTimer*>(sender());
    if (!timer)
    {
        return;
    }

    for (const auto& item : m_calls)
    {
        if (item.second->timer == timer)
        {
            onCallTimer(item.first);
            return;
        }
    }
}

void PDFMcpDispatcher::onCallTimer(quint64 pendingId)
{
    PendingCall* pending = findPending(pendingId);
    if (!pending || !pending->exchange || pending->mode == PendingCall::Mode::Task)
    {
        return;
    }

    // A client of the tasks extension gets a task, the others an SSE stream
    // kept open by keep-alive comments
    if (pending->request.kind == PDFMcpCallKind::ToolCall && pending->request.client.supportsTasks())
    {
        createTask(*pending);
        return;
    }

    if (pending->mode == PendingCall::Mode::Waiting)
    {
        pending->mode = PendingCall::Mode::Stream;
        pending->exchange->beginStream();
        pending->exchange->sendComment("waiting");
    }
}

bool PDFMcpDispatcher::sendCallEvent(PendingCall& pending, QByteArrayView json)
{
    if (!pending.exchange || pending.exchange->isClosed())
    {
        return false;
    }

    pending.mode = PendingCall::Mode::Stream;
    return pending.exchange->sendEvent(json);
}

void PDFMcpDispatcher::cancelCall(PendingCall& pending, PDFMcpCancelReason reason)
{
    if (pending.call && !pending.call->isFinished())
    {
        pending.call->cancel(reason);
    }
}

void PDFMcpDispatcher::removePending(quint64 pendingId)
{
    auto it = m_calls.find(pendingId);
    if (it == m_calls.end())
    {
        return;
    }

    PendingCall& pending = *it->second;
    if (pending.timer)
    {
        pending.timer->stop();
        pending.timer->deleteLater();
    }
    if (pending.call)
    {
        // A worker may still report progress of the call (also after the stop,
        // when this dispatcher is destroyed): it never reaches the dispatcher again
        pending.call->setSink(nullptr);
        m_callIndex.erase(pending.call.get());
    }
    if (pending.exchange)
    {
        auto exchangeIt = m_exchangeCalls.find(pending.exchange->getId());
        if (exchangeIt != m_exchangeCalls.end() && exchangeIt->second == pendingId)
        {
            m_exchangeCalls.erase(exchangeIt);
        }
    }
    if (pending.isCounted)
    {
        m_assistants->removeCall(pending.request.client.assistantKey);
    }
    m_calls.erase(it);
}

// ----------------------------------------------------------------------------
// Events and results of the calls
// ----------------------------------------------------------------------------

void PDFMcpDispatcher::processCall(const std::shared_ptr<PDFMcpCall>& call)
{
    PDFThreadAnnotations::happensAfter(call.get());
    if (!call)
    {
        return;
    }

    const std::vector<PDFMcpCallEvent> events = call->takeEvents();
    auto indexIt = m_callIndex.find(call.get());
    if (indexIt == m_callIndex.end())
    {
        // A call, which was canceled or answered meanwhile ("MUST NOT send any
        // further messages" for it)
        return;
    }

    PendingCall* pending = findPending(indexIt->second);
    if (!pending || pending->call != call)
    {
        return;
    }

    for (const PDFMcpCallEvent& event : events)
    {
        switch (event.type)
        {
            case PDFMcpCallEvent::Type::Progress:
            {
                // Progress of a task is not sent (not on the listen stream either)
                if (pending->mode == PendingCall::Mode::Task || pending->progressToken.isUndefined())
                {
                    break;
                }

                // "The progress value MUST increase with each notification" - also
                // over the calls of one request (a legacy elicitation launches the
                // call again, whose own progress starts from the beginning)
                if (pending->hasProgress && event.progress <= pending->lastProgress)
                {
                    break;
                }
                pending->hasProgress = true;
                pending->lastProgress = event.progress;

                PDFMcpJsonWriter params;
                params.beginObject();
                params.key(QLatin1StringView("progressToken"));
                PDFMcpDispatcherHelper::writeProgressToken(params, pending->progressToken);
                params.writeMember(QLatin1StringView("progress"), event.progress);
                if (event.total > 0.0)
                {
                    params.writeMember(QLatin1StringView("total"), event.total);
                }
                if (!event.message.isEmpty())
                {
                    params.writeMember(QLatin1StringView("message"), QStringView(event.message));
                }
                params.endObject();
                const QByteArray members = params.getData().mid(1, params.getData().size() - 2);
                sendCallEvent(*pending, PDFMcpDispatcherHelper::createNotification(QLatin1StringView("notifications/progress"), members));
                break;
            }

            case PDFMcpCallEvent::Type::Log:
            {
                if (pending->mode == PendingCall::Mode::Task)
                {
                    break;
                }

                PDFMcpJsonWriter params;
                params.beginObject();
                params.writeMember(QLatin1StringView("level"), QStringView(event.level));
                params.writeMember(QLatin1StringView("logger"), QLatin1StringView("pdf4qt"));
                params.writeMember(QLatin1StringView("data"), QStringView(event.message));
                params.endObject();
                const QByteArray members = params.getData().mid(1, params.getData().size() - 2);
                sendCallEvent(*pending, PDFMcpDispatcherHelper::createNotification(QLatin1StringView("notifications/message"), members));
                break;
            }

            case PDFMcpCallEvent::Type::Finished:
                finishCall(*pending);
                return;
        }
    }
}

void PDFMcpDispatcher::finishCall(PendingCall& pending)
{
    const std::optional<PDFMcpCallResult> result = pending.call ? pending.call->getResult() : std::nullopt;
    if (!result)
    {
        return;
    }

    if (pending.timer)
    {
        pending.timer->stop();
    }

    if (result->kind == PDFMcpCallResult::Kind::InputRequired)
    {
        handleInputRequired(pending, result->inputRequests);
        return;
    }

    deliverResult(pending, *result);
}

void PDFMcpDispatcher::deliverMessage(PendingCall& pending, int status, QByteArray message, bool isComplete)
{
    Q_UNUSED(isComplete);

    if (!pending.exchange || pending.exchange->isClosed())
    {
        return;
    }

    if (pending.mode == PendingCall::Mode::Stream || pending.exchange->isStreaming())
    {
        // "The final JSON-RPC response SHOULD terminate the stream"
        pending.exchange->sendEvent(message);
        pending.exchange->endStream();
    }
    else
    {
        respondJson(pending.exchange, status, std::move(message));
    }
}

void PDFMcpDispatcher::deliverResult(PendingCall& pending, const PDFMcpCallResult& result)
{
    const quint64 pendingId = pending.id;
    const bool isToolCall = pending.request.kind == PDFMcpCallKind::ToolCall;
    const PDFMcpEra era = pending.request.client.era;

    QByteArray payload = result.result;
    if (result.kind == PDFMcpCallResult::Kind::Complete && payload.size() > PDFMcpLimits::MAXIMUM_RESPONSE_SIZE)
    {
        payload = isToolCall ? PDFMcpCallResult::createToolError(QStringLiteral("The result is larger than 16 MiB. Request less data (fewer pages, a smaller area or a lower resolution).")).result
                             : QByteArray();
    }

    if (pending.mode == PendingCall::Mode::Task)
    {
        PDFMcpTask* task = m_tasks.find(pending.taskId);
        if (task && !task->isTerminal())
        {
            if (result.kind == PDFMcpCallResult::Kind::Complete && !payload.isEmpty())
            {
                // An isError result ends the task as completed; a protocol error as failed
                task->status = PDFMcpTask::Status::Completed;
                task->result = payload;
                task->statusMessage.clear();
            }
            else
            {
                task->status = PDFMcpTask::Status::Failed;
                task->error = result.kind == PDFMcpCallResult::Kind::Error ? PDFMcpDispatcherHelper::createErrorObject(result.errorCode, result.errorMessage, result.errorData)
                                                                            : PDFMcpDispatcherHelper::createErrorObject(PDFMcpErrorCodes::INTERNAL_ERROR, QStringLiteral("The result is too large"), QByteArrayView());
                task->statusMessage.clear();
            }
            task->inputRequests.clear();
            task->endedAt = task->lastUpdatedAt = PDFMcpClock::now();
            task->callId = 0;
            updateTask(*task);
        }
        removePending(pendingId);
        return;
    }

    QByteArray message;
    int status = 200;
    if (result.kind == PDFMcpCallResult::Kind::Complete && !payload.isEmpty())
    {
        // A document resource is never cached by the client (documents change)
        QByteArray cacheMembers;
        if (pending.request.kind == PDFMcpCallKind::ResourceRead && !payload.contains("\"ttlMs\":"))
        {
            cacheMembers = QByteArrayLiteral("\"ttlMs\":0,\"cacheScope\":\"private\"");
        }
        message = createResultMessage(pending.request.id, appendResultMembers(payload, era, cacheMembers));
    }
    else if (result.kind == PDFMcpCallResult::Kind::Error)
    {
        message = createErrorMessage(pending.request.id, result.errorCode, result.errorMessage, result.errorData);
        status = getErrorStatus(result.errorCode, era);
    }
    else
    {
        message = createErrorMessage(pending.request.id, PDFMcpErrorCodes::INTERNAL_ERROR, QStringLiteral("The result is larger than 16 MiB"), QByteArrayView());
    }

    deliverMessage(pending, status, std::move(message), true);
    removePending(pendingId);
}

void PDFMcpDispatcher::deliverDeclined(PendingCall& pending, const QString& text)
{
    if (pending.request.kind == PDFMcpCallKind::ToolCall)
    {
        deliverResult(pending, PDFMcpCallResult::createToolError(text));
    }
    else
    {
        deliverResult(pending, PDFMcpCallResult::createError(PDFMcpErrorCodes::INVALID_PARAMS, text));
    }
}

// ----------------------------------------------------------------------------
// Elicitation [D4]: MRTR (modern), elicitation/create (legacy), task input
// ----------------------------------------------------------------------------

QByteArray PDFMcpDispatcher::createInputRequestsJson(const PDFMcpInputRequests& requests, PDFMcpEra era) const
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    for (const PDFMcpInputRequest& request : requests.requests)
    {
        writer.key(QStringView(request.key));
        writer.beginObject();
        writer.writeMember(QLatin1StringView("method"), QLatin1StringView("elicitation/create"));
        writer.key(QLatin1StringView("params"));
        writer.beginObject();
        if (era != PDFMcpEra::Legacy20250618)
        {
            writer.writeMember(QLatin1StringView("mode"), QLatin1StringView("form"));
        }
        writer.writeMember(QLatin1StringView("message"), QStringView(request.message));
        writer.key(QLatin1StringView("requestedSchema"));
        writer.writeRaw(request.requestedSchema.isEmpty() ? QByteArrayView("{\"type\":\"object\",\"properties\":{}}") : QByteArrayView(request.requestedSchema));
        writer.endObject();
        writer.endObject();
    }
    writer.endObject();
    return writer.take();
}

void PDFMcpDispatcher::handleInputRequired(PendingCall& pending, const PDFMcpInputRequests& requests)
{
    const PDFMcpClientContext& client = pending.request.client;
    if (requests.isEmpty())
    {
        deliverResult(pending, PDFMcpCallResult::createError(PDFMcpErrorCodes::INTERNAL_ERROR, QStringLiteral("An empty question of the server")));
        return;
    }

    // "Servers MUST NOT send an inputRequests that the client has not declared support for"
    if (!client.supportsElicitation())
    {
        deliverDeclined(pending, PDFMcpDispatcherCallsHelper::getNoElicitationText(requests));
        return;
    }

    if (client.era == PDFMcpEra::Modern)
    {
        if (pending.mode == PendingCall::Mode::Task)
        {
            // The task waits; the client answers by tasks/update
            PDFMcpTask* task = m_tasks.find(pending.taskId);
            if (!task)
            {
                removePending(pending.id);
                return;
            }

            if (pending.call)
            {
                pending.call->setSink(nullptr);
                m_callIndex.erase(pending.call.get());
                pending.call.reset();
            }
            // The keys of each round are new ("unique over the lifetime of a
            // single task"): the host keeps its keys, the client sees "<key>#<round>"
            PDFMcpInputRequests taskRequests;
            QJsonObject taskAllowedValues;
            ++pending.taskInputRound;
            pending.taskInputKeys.clear();
            for (const PDFMcpInputRequest& request : requests.requests)
            {
                PDFMcpInputRequest taskRequest = request;
                taskRequest.key = PDFMcpDispatcherCallsHelper::createTaskInputKey(request.key, pending.taskInputRound);
                pending.taskInputKeys[taskRequest.key] = request.key;
                const QJsonValue allowed = requests.allowedValues.value(request.key);
                if (!allowed.isUndefined())
                {
                    taskAllowedValues.insert(taskRequest.key, allowed);
                }
                taskRequests.requests.push_back(std::move(taskRequest));
            }

            task->status = PDFMcpTask::Status::InputRequired;
            task->statusMessage = QStringLiteral("Waiting for a choice of the user in the client");
            task->inputRequests = createInputRequestsJson(taskRequests, client.era);
            task->allowedValues = taskAllowedValues;
            task->lastUpdatedAt = PDFMcpClock::now();
            pending.allowedValues = requests.allowedValues;

            // The client has the time of an elicitation to answer (checkCalls): a
            // task waiting for ever would hold its pending call - the limits of
            // the calls of its assistant and of the server - and keep its
            // assistant connected
            pending.isEliciting = true;
            pending.elicitationDeadline = PDFMcpClock::now() + m_shared->getConfig().elicitationTimeout;
            updateTask(*task);
            return;
        }

        if (!pending.isRetryable)
        {
            deliverResult(pending, PDFMcpCallResult::createError(PDFMcpErrorCodes::INTERNAL_ERROR, QStringLiteral("The request cannot ask for input")));
            return;
        }

        // The state is sealed: bound to the assistant, the method, the name, the
        // arguments and 5 minutes
        PDFMcpRequestStateData data;
        data.assistantKey = client.assistantKey;
        data.method = pending.request.method;
        data.name = pending.retryName;
        data.argumentsHash = pending.argumentsHash;
        data.allowedValues = requests.allowedValues;
        data.expiresAt = PDFMcpClock::now() + PDFMcpLimits::REQUEST_STATE_LIFETIME;
        data.nonce = PDFMcpRequestState::createNonce();
        const QByteArray sealed = m_requestState.seal(data);
        if (sealed.isEmpty())
        {
            deliverDeclined(pending, QStringLiteral("PDF4QT cannot ask the user: the choice is too large. Pass the parameter explicitly."));
            return;
        }

        PDFMcpJsonWriter writer;
        writer.beginObject();
        writer.writeMember(QLatin1StringView("resultType"), QLatin1StringView("input_required"));
        writer.key(QLatin1StringView("inputRequests"));
        writer.writeRaw(createInputRequestsJson(requests, client.era));
        writer.writeMember(QLatin1StringView("requestState"), QLatin1StringView(sealed));
        writer.writeRawMembers(createServerInfoMeta());
        writer.endObject();

        const quint64 pendingId = pending.id;
        deliverMessage(pending, 200, createResultMessage(pending.request.id, writer.getData()), true);
        removePending(pendingId);
        return;
    }

    // Legacy: elicitation/create on the stream of the call, one question at a time
    if (++pending.elicitationRounds > PDFMcpLimits::MAXIMUM_ELICITATION_ROUNDS)
    {
        deliverDeclined(pending, QStringLiteral("PDF4QT asked too many questions. Pass the parameters explicitly."));
        return;
    }

    if (pending.call)
    {
        m_callIndex.erase(pending.call.get());
    }
    pending.elicitations = requests.requests;
    pending.elicitationIndex = 0;
    pending.elicitationAnswers = QJsonObject();
    pending.allowedValues = requests.allowedValues;
    sendNextElicitation(pending);
}

void PDFMcpDispatcher::sendNextElicitation(PendingCall& pending)
{
    PDFMcpSession* session = m_sessions.findByKey(pending.request.client.sessionKey);
    if (!session || pending.elicitationIndex >= pending.elicitations.size())
    {
        deliverResult(pending, PDFMcpCallResult::createError(PDFMcpErrorCodes::INTERNAL_ERROR, QStringLiteral("The session has ended")));
        return;
    }

    const PDFMcpInputRequest& request = pending.elicitations[pending.elicitationIndex];
    pending.elicitationRequestId = session->nextServerRequestId++;
    pending.elicitationDeadline = PDFMcpClock::now() + m_shared->getConfig().elicitationTimeout;
    pending.isEliciting = true;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("jsonrpc"), QLatin1StringView("2.0"));
    writer.writeMember(QLatin1StringView("id"), pending.elicitationRequestId);
    writer.writeMember(QLatin1StringView("method"), QLatin1StringView("elicitation/create"));
    writer.key(QLatin1StringView("params"));
    writer.beginObject();
    if (session->era != PDFMcpEra::Legacy20250618)
    {
        writer.writeMember(QLatin1StringView("mode"), QLatin1StringView("form"));
    }
    writer.writeMember(QLatin1StringView("message"), QStringView(request.message));
    writer.key(QLatin1StringView("requestedSchema"));
    writer.writeRaw(request.requestedSchema.isEmpty() ? QByteArrayView("{\"type\":\"object\",\"properties\":{}}") : QByteArrayView(request.requestedSchema));
    writer.endObject();
    writer.endObject();
    sendCallEvent(pending, writer.getData());
}

void PDFMcpDispatcher::redispatch(PendingCall& pending, const QJsonObject& answers)
{
    bool isDeclined = false;
    const QJsonObject accepted = filterInputResponses(answers, pending.allowedValues, &isDeclined);
    pending.isEliciting = false;
    pending.elicitationRequestId = 0;
    if (isDeclined)
    {
        deliverDeclined(pending, QStringLiteral("The user declined the choice in the client. Ask the user how to continue."));
        return;
    }

    QJsonObject state;
    state.insert(QStringLiteral("allowedValues"), pending.allowedValues);
    pending.request.inputResponses = accepted;
    pending.request.requestState = state;
    pending.request.receivedAt = PDFMcpClock::now();
    launchCall(pending);
}

// ----------------------------------------------------------------------------
// Tasks [D3]
// ----------------------------------------------------------------------------

QByteArray PDFMcpDispatcher::createTaskJson(const PDFMcpTask& task) const
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    task.writeMembers(writer, PDFMcpLimits::TASK_TTL, PDFMcpLimits::TASK_POLL_INTERVAL);
    writer.endObject();
    return writer.take();
}

void PDFMcpDispatcher::createTask(PendingCall& pending)
{
    PDFMcpTask* task = m_tasks.create(pending.request.client.assistantKey, PDFMcpClock::now());
    if (!task)
    {
        // The limits of the tasks: the request stays open as a stream
        if (pending.mode == PendingCall::Mode::Waiting)
        {
            pending.mode = PendingCall::Mode::Stream;
            pending.exchange->beginStream();
            pending.exchange->sendComment("waiting");
        }
        return;
    }

    // "A server MUST NOT return CreateTaskResult until the task is durably created"
    task->statusMessage = QStringLiteral("PDF4QT is working on the request");
    task->callId = pending.id;
    pending.taskId = task->taskId;

    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("resultType"), QLatin1StringView("task"));
    task->writeMembers(writer, PDFMcpLimits::TASK_TTL, PDFMcpLimits::TASK_POLL_INTERVAL);
    writer.writeRawMembers(createServerInfoMeta());
    writer.endObject();

    deliverMessage(pending, 200, createResultMessage(pending.request.id, writer.getData()), true);

    pending.mode = PendingCall::Mode::Task;
    m_exchangeCalls.erase(pending.exchange->getId());
    pending.exchange.reset();
}

void PDFMcpDispatcher::updateTask(PDFMcpTask& task)
{
    sendTaskNotification(task);
}

// ----------------------------------------------------------------------------
// Times of the calls
// ----------------------------------------------------------------------------

void PDFMcpDispatcher::checkCalls(qint64 now)
{
    std::vector<quint64> elicitationTimeouts;
    std::vector<quint64> cancelTimeouts;
    for (const auto& item : m_calls)
    {
        const PendingCall& pending = *item.second;
        if (pending.isEliciting && now >= pending.elicitationDeadline)
        {
            elicitationTimeouts.push_back(item.first);
        }
        else if (pending.cancelDeadline > 0 && now >= pending.cancelDeadline)
        {
            cancelTimeouts.push_back(item.first);
        }
    }

    for (quint64 id : elicitationTimeouts)
    {
        if (PendingCall* pending = findPending(id))
        {
            pending->isEliciting = false;
            deliverDeclined(*pending, QStringLiteral("The user did not answer the question in the client in time. Ask the user before trying again."));
        }
    }

    for (quint64 id : cancelTimeouts)
    {
        if (PendingCall* pending = findPending(id))
        {
            if (pending->call)
            {
                m_callIndex.erase(pending->call.get());
            }
            deliverResult(*pending, PDFMcpCallResult::createError(PDFMcpErrorCodes::INTERNAL_ERROR, QStringLiteral("The request was canceled in PDF4QT")));
        }
    }
}

}   // namespace pdf
