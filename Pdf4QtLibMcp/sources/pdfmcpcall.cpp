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

#include "pdfmcpcall.h"
#include "pdfmcphost.h"
#include "pdfthreadannotations.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>

#include <cmath>

namespace pdf
{

// ----------------------------------------------------------------------------
// PDFMcpProtocol
// ----------------------------------------------------------------------------

QString PDFMcpProtocol::getVersion(PDFMcpEra era)
{
    switch (era)
    {
        case PDFMcpEra::Modern:
            return QString::fromLatin1(VERSION_2026_07_28);
        case PDFMcpEra::Legacy20251125:
            return QString::fromLatin1(VERSION_2025_11_25);
        case PDFMcpEra::Legacy20250618:
            return QString::fromLatin1(VERSION_2025_06_18);
    }
    return QString::fromLatin1(VERSION_2026_07_28);
}

std::optional<PDFMcpEra> PDFMcpProtocol::getEra(QStringView version)
{
    if (version == QLatin1String(VERSION_2026_07_28))
    {
        return PDFMcpEra::Modern;
    }
    if (version == QLatin1String(VERSION_2025_11_25))
    {
        return PDFMcpEra::Legacy20251125;
    }
    if (version == QLatin1String(VERSION_2025_06_18))
    {
        return PDFMcpEra::Legacy20250618;
    }
    return std::nullopt;
}

QStringList PDFMcpProtocol::getSupportedVersions()
{
    return { QString::fromLatin1(VERSION_2026_07_28), QString::fromLatin1(VERSION_2025_11_25), QString::fromLatin1(VERSION_2025_06_18) };
}

QStringList PDFMcpProtocol::getLogLevels()
{
    return { QStringLiteral("debug"), QStringLiteral("info"), QStringLiteral("notice"), QStringLiteral("warning"),
             QStringLiteral("error"), QStringLiteral("critical"), QStringLiteral("alert"), QStringLiteral("emergency") };
}

int PDFMcpProtocol::getLogLevelIndex(QStringView level)
{
    const QStringList levels = getLogLevels();
    for (int i = 0; i < levels.size(); ++i)
    {
        if (levels[i] == level)
        {
            return i;
        }
    }
    return -1;
}

// ----------------------------------------------------------------------------
// PDFMcpClientContext
// ----------------------------------------------------------------------------

bool PDFMcpClientContext::supportsElicitation() const
{
    const QJsonValue elicitation = clientCapabilities.value(QStringLiteral("elicitation"));
    if (!elicitation.isObject())
    {
        return false;
    }

    // An empty object means form; a client of URL mode only does not get forms
    const QJsonObject object = elicitation.toObject();
    return object.isEmpty() || object.contains(QStringLiteral("form"));
}

bool PDFMcpClientContext::supportsTasks() const
{
    if (era != PDFMcpEra::Modern)
    {
        return false;
    }

    const QJsonObject extensions = clientCapabilities.value(QStringLiteral("extensions")).toObject();
    return extensions.contains(QLatin1String(PDFMcpProtocol::TASKS_EXTENSION));
}

// ----------------------------------------------------------------------------
// PDFMcpCallResult
// ----------------------------------------------------------------------------

PDFMcpCallResult PDFMcpCallResult::createComplete(QByteArray result, bool isError)
{
    PDFMcpCallResult callResult;
    callResult.kind = Kind::Complete;
    callResult.result = std::move(result);
    callResult.isError = isError;
    return callResult;
}

PDFMcpCallResult PDFMcpCallResult::createToolError(const QString& text)
{
    PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.key(QLatin1StringView("content"));
    writer.beginArray();
    writer.beginObject();
    writer.writeMember(QLatin1StringView("type"), QLatin1StringView("text"));
    writer.writeMember(QLatin1StringView("text"), QStringView(text));
    writer.endObject();
    writer.endArray();
    writer.writeMember(QLatin1StringView("isError"), true);
    writer.endObject();
    return createComplete(writer.take(), true);
}

PDFMcpCallResult PDFMcpCallResult::createError(int code, const QString& message, QByteArray data)
{
    PDFMcpCallResult callResult;
    callResult.kind = Kind::Error;
    callResult.errorCode = code;
    callResult.errorMessage = message;
    callResult.errorData = std::move(data);
    return callResult;
}

PDFMcpCallResult PDFMcpCallResult::createInputRequired(PDFMcpInputRequests requests)
{
    PDFMcpCallResult callResult;
    callResult.kind = Kind::InputRequired;
    callResult.inputRequests = std::move(requests);
    return callResult;
}

QJsonObject PDFMcpCallResult::toQJson() const
{
    if (kind != Kind::Complete)
    {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(result).object();
}

QString PDFMcpCallResult::getFirstText() const
{
    const QJsonArray content = toQJson().value(QStringLiteral("content")).toArray();
    for (const QJsonValue& block : content)
    {
        const QJsonObject object = block.toObject();
        if (object.value(QStringLiteral("type")).toString() == QLatin1String("text"))
        {
            return object.value(QStringLiteral("text")).toString();
        }
    }
    return QString();
}

// ----------------------------------------------------------------------------
// PDFMcpCallSink
// ----------------------------------------------------------------------------

PDFMcpCallSink::PDFMcpCallSink(QObject* parent) :
    QObject(parent)
{

}

PDFMcpCallSink::~PDFMcpCallSink() = default;

void PDFMcpCallSink::processCall(const std::shared_ptr<PDFMcpCall>& call)
{
    PDFThreadAnnotations::happensAfter(call.get());
    // The default receiver drops the events (no transport)
    if (call)
    {
        call->takeEvents();
    }
}

// ----------------------------------------------------------------------------
// PDFMcpCall
// ----------------------------------------------------------------------------

/// Counter of the ids of the calls of the process
class PDFMcpCallCounter
{
public:
    PDFMcpCallCounter() = delete;

    static QString createId()
    {
        static std::atomic<quint64> s_counter = 0;
        return QStringLiteral("c%1").arg(++s_counter);
    }
};

PDFMcpCall::PDFMcpCall(PDFMcpCallRequest request) :
    m_request(std::move(request)),
    m_callId(PDFMcpCallCounter::createId())
{

}

PDFMcpCall::~PDFMcpCall() = default;

std::shared_ptr<PDFMcpCall> PDFMcpCall::create(PDFMcpCallRequest request)
{
    return std::make_shared<PDFMcpCall>(std::move(request));
}

void PDFMcpCall::reportProgress(double progress, double total, const QString& message)
{
    if (!std::isfinite(progress) || !std::isfinite(total))
    {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_result || progress <= m_lastProgress)
        {
            return;
        }
        m_lastProgress = progress;
    }

    if (!m_request.client.hasProgressToken)
    {
        return;
    }

    PDFMcpCallEvent event;
    event.type = PDFMcpCallEvent::Type::Progress;
    event.progress = progress;
    event.total = total;
    event.message = PDFMcpJson::sanitizeString(message, PDFMcpJson::SanitizeFlag::RemoveBidi, 512);
    postEvent(std::move(event));
}

void PDFMcpCall::log(const QString& level, const QString& text)
{
    const int levelIndex = PDFMcpProtocol::getLogLevelIndex(level);
    const int requestedIndex = PDFMcpProtocol::getLogLevelIndex(m_request.client.logLevel);
    if (levelIndex < 0 || requestedIndex < 0 || levelIndex < requestedIndex)
    {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        if (m_result)
        {
            return;
        }
    }

    PDFMcpCallEvent event;
    event.type = PDFMcpCallEvent::Type::Log;
    event.level = level;
    event.message = PDFMcpJson::sanitizeString(text, PDFMcpJson::SanitizeFlag::RemoveBidi | PDFMcpJson::SanitizeFlag::KeepNewlines, 4096);
    postEvent(std::move(event));
}

void PDFMcpCall::requestInput(PDFMcpInputRequests requests)
{
    finish(PDFMcpCallResult::createInputRequired(std::move(requests)));
}

bool PDFMcpCall::finish(PDFMcpCallResult result)
{
    {
        QMutexLocker locker(&m_mutex);
        if (m_result)
        {
            return false;
        }
        m_result = std::move(result);
    }

    PDFMcpCallEvent event;
    event.type = PDFMcpCallEvent::Type::Finished;
    postEvent(std::move(event));
    return true;
}

bool PDFMcpCall::isFinished() const
{
    QMutexLocker locker(&m_mutex);
    return m_result.has_value();
}

std::optional<PDFMcpCallResult> PDFMcpCall::getResult() const
{
    QMutexLocker locker(&m_mutex);
    return m_result;
}

void PDFMcpCall::cancel(PDFMcpCancelReason reason)
{
    if (reason == PDFMcpCancelReason::None)
    {
        return;
    }

    QPointer<PDFMcpHost> host;
    {
        QMutexLocker locker(&m_mutex);
        if (m_isCanceled.load())
        {
            return;
        }
        m_cancelReason = reason;
        m_isCanceled.store(true);
        host = m_host;
    }

    // The host is destroyed only after the server has stopped (plan 2.6, the
    // quit order), so the queued invocation never reaches a dead host
    if (host)
    {
        PDFThreadAnnotations::happensBefore(this);
        PDFThreadAnnotations::TransportScope transportScope;
        QMetaObject::invokeMethod(host.data(), &PDFMcpHost::callCanceled, Qt::QueuedConnection, shared_from_this());
    }
}

PDFMcpCancelReason PDFMcpCall::getCancelReason() const
{
    QMutexLocker locker(&m_mutex);
    return m_cancelReason;
}

void PDFMcpCall::setHost(PDFMcpHost* host)
{
    QMutexLocker locker(&m_mutex);
    m_host = host;
}

void PDFMcpCall::setSink(PDFMcpCallSink* sink)
{
    QMutexLocker locker(&m_mutex);
    m_sink = sink;
}

std::vector<PDFMcpCallEvent> PDFMcpCall::takeEvents()
{
    QMutexLocker locker(&m_mutex);
    std::vector<PDFMcpCallEvent> events;
    events.swap(m_events);
    m_isSinkNotified = false;
    return events;
}

double PDFMcpCall::getLastProgress() const
{
    QMutexLocker locker(&m_mutex);
    return m_lastProgress;
}

void PDFMcpCall::postEvent(PDFMcpCallEvent event)
{
    QMutexLocker locker(&m_mutex);

    // Bounded queue: nobody may take the events (a call without a transport)
    if (m_events.size() >= MAXIMUM_PENDING_EVENTS)
    {
        m_events.erase(m_events.begin());
    }
    m_events.push_back(std::move(event));
    if (m_isSinkNotified || !m_sink)
    {
        return;
    }
    m_isSinkNotified = true;

    // The event is posted while the mutex is held: the sink clears itself by
    // setSink(nullptr) under this mutex before it is destroyed (the dispatcher
    // at the end of a call and at the stop), so a worker reporting progress
    // never reaches a destroyed sink; an event posted to a sink, which is
    // destroyed later, is removed with it. Posting takes only the lock of the
    // event queue of Qt and never calls back into the call.
    PDFThreadAnnotations::happensBefore(this);
    PDFThreadAnnotations::TransportScope transportScope;
    QMetaObject::invokeMethod(m_sink.data(), &PDFMcpCallSink::processCall, Qt::QueuedConnection, shared_from_this());
}

}   // namespace pdf
