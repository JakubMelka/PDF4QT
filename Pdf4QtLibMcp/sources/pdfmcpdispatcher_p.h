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

#ifndef PDFMCPDISPATCHER_P_H
#define PDFMCPDISPATCHER_P_H

// Private types of PDFMcpDispatcher, shared by its source files
// (pdfmcpdispatcher.cpp, _protocol.cpp, _calls.cpp, _streams.cpp)

#include "pdfmcpconnection.h"
#include "pdfmcpdispatcher.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"
#include "pdfmcplimits.h"
#include "pdfmcpserver.h"

#include <QJsonValue>
#include <QTimer>

namespace pdf
{

/// One JSON-RPC message of a POST being handled
class PDFMcpDispatcher::Request
{
public:
    std::shared_ptr<PDFMcpExchange> exchange;
    QJsonObject message;
    QString method;
    PDFMcpRequestId id;
    bool isNotification = false;
    QJsonObject params;
    QJsonObject meta;
    PDFMcpEra era = PDFMcpEra::Modern;
    QString sessionKey;                 ///< Legacy session of the request (empty in the modern era)
    PDFMcpClientContext client;
    QJsonValue progressToken;           ///< _meta.progressToken (undefined = none)
    std::vector<std::pair<QByteArray, QByteArray>> responseHeaders;     ///< Mcp-Session-Id of initialize
};

/// A call routed to the host (tools/call, prompts/get, document resources/read, completion/complete)
class PDFMcpDispatcher::PendingCall
{
public:
    enum class Mode
    {
        Waiting,    ///< No byte of the response yet (application/json while the result comes within 1 s)
        Stream,     ///< The response is an SSE stream
        Task        ///< The client got a task (the exchange is answered)
    };

    quint64 id = 0;
    std::shared_ptr<PDFMcpCall> call;
    std::shared_ptr<PDFMcpExchange> exchange;
    PDFMcpCallRequest request;          ///< The request (a retry after an elicitation is a new call of it)
    QJsonValue progressToken;
    Mode mode = Mode::Waiting;
    QString taskId;
    QPointer<QTimer> timer;             ///< 1 s: application/json -> SSE or a task
    bool isRetryable = false;           ///< MRTR may answer input_required (tools/call, prompts/get, resources/read)
    QString retryName;                  ///< Name bound by the sealed state
    QByteArray argumentsHash;           ///< Arguments bound by the sealed state
    bool isCounted = false;             ///< Counted as a running call of the assistant
    bool hasProgress = false;           ///< A progress notification was sent for the token
    double lastProgress = 0.0;          ///< Its last value (a retried call starts its progress again)

    // Legacy elicitation (elicitation/create on the stream of the call)
    int elicitationRounds = 0;
    std::vector<PDFMcpInputRequest> elicitations;
    size_t elicitationIndex = 0;
    QJsonObject elicitationAnswers;
    QJsonObject allowedValues;
    qint64 elicitationRequestId = 0;    ///< Id of the elicitation/create request of the server
    qint64 elicitationDeadline = 0;
    bool isEliciting = false;

    qint64 cancelDeadline = 0;          ///< A call canceled by the server is answered by an error after this time

    // Input of a task (tasks extension): "Each request key in inputRequests MUST
    // be unique over the lifetime of a single task" - the keys of the host get
    // the suffix of their round (follow-up 32)
    int taskInputRound = 0;
    std::map<QString, QString> taskInputKeys;   ///< Key of the client -> key of the host (the current round)
};

/// A listen stream of the modern era (subscriptions/listen)
class PDFMcpDispatcher::ListenStream
{
public:
    std::shared_ptr<PDFMcpExchange> exchange;
    PDFMcpRequestId requestId;          ///< io.modelcontextprotocol/subscriptionId
    QString assistantKey;
    PDFMcpSubscriptionFilter filter;
    PDFMcpSubscriptions subscriptions;
};

/// Helpers of the dispatcher
class PDFMcpDispatcherHelper
{
public:
    PDFMcpDispatcherHelper() = delete;

    /// A notification message {"jsonrpc":"2.0","method":...,"params":{...}};
    /// \p params are the serialized members of params (may be empty)
    static QByteArray createNotification(QLatin1StringView method, QByteArrayView params)
    {
        QByteArray message = QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"method\":");
        message += PDFMcpJsonWriter::quote(QString(method));
        if (!params.isEmpty())
        {
            message += ",\"params\":{";
            message += params;
            message += '}';
        }
        message += '}';
        return message;
    }

    /// Serialized member "_meta":{"io.modelcontextprotocol/subscriptionId":<id>}
    static QByteArray createSubscriptionMeta(const PDFMcpRequestId& id)
    {
        PDFMcpJsonWriter writer;
        writer.beginObject();
        writer.key(QLatin1StringView(PDFMcpProtocol::META_SUBSCRIPTION_ID));
        id.write(writer);
        writer.endObject();
        return QByteArrayLiteral("\"_meta\":") + writer.take();
    }

    /// A JSON-RPC error object {"code":..,"message":..,"data":..} (the error of a failed task)
    static QByteArray createErrorObject(int code, const QString& message, QByteArrayView data)
    {
        PDFMcpJsonWriter writer;
        writer.beginObject();
        writer.writeMember(QLatin1StringView("code"), code);
        writer.writeMember(QLatin1StringView("message"), QStringView(message));
        if (!data.isEmpty())
        {
            writer.key(QLatin1StringView("data"));
            writer.writeRaw(data);
        }
        writer.endObject();
        return writer.take();
    }

    /// The progress token of a request (a string or an integer)
    static void writeProgressToken(PDFMcpJsonWriter& writer, const QJsonValue& token)
    {
        if (token.isString())
        {
            writer.writeString(token.toString());
        }
        else
        {
            writer.writeInteger(token.toInteger());
        }
    }
};

}   // namespace pdf

#endif // PDFMCPDISPATCHER_P_H
