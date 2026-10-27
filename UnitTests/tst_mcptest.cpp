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

#include "fakemcphost.h"
#include "mcptestclient.h"

#include "pdfmcpcall.h"
#include "pdfmcpcatalog.h"
#include "pdfmcperrorcodes.h"
#include "pdfmcphttp.h"
#include "pdfmcpjson.h"
#include "pdfmcplimits.h"
#include "pdfmcprequeststate.h"
#include "pdfmcpschema.h"
#include "pdfmcpserver.h"
#include "pdfmcpuritemplate.h"
#include "pdfmcpassistants.h"
#include "pdfmcpconnection.h"
#include "pdfmcpdispatcher.h"
#include "pdfmcpratelimiter.h"
#include "pdfmcptasks.h"
#include "pdfmcplistener.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QtTest>

#include <atomic>
#include <cmath>
#include <deque>
#include <thread>
#include <limits>

// Wave 0 of step 7 (plan 8 item 7): the JSON layer with its differential fuzz
// against QJsonDocument, the schemas, the sealed request state, the URI
// templates, the calls, the catalog and the probe of a port in use. Package A
// (transport and protocol core): the HTTP parser and its fuzz, the JSON-RPC
// errors, the authorization, Host and Origin, the headers of the modern era,
// server/discover, the lists, the resources, the legacy sessions, the listen
// streams, progress and logging, cancellation, MRTR and the legacy
// elicitation, the tasks extension, the assistants [D2], the rate limits, a
// load of many clients, the order of the stop, and the MUST checklist of
// chapter 1 as the table of McpConformance::getRows (one data row each).

/// Helpers of the tests of the MCP library
class McpTestHelper
{
public:
    McpTestHelper() = delete;

    static pdf::PDFMcpJsonScanResult scan(const QByteArray& data, const pdf::PDFMcpJsonScanLimits& limits = pdf::PDFMcpJsonScanLimits())
    {
        return pdf::PDFMcpJsonScanner(limits).scan(data);
    }

    /// Statistics of a tree of Qt in the counting of the scanner
    class Statistics
    {
    public:
        int maximumDepth = 0;
        qsizetype objects = 0;
        qsizetype arrays = 0;
        qsizetype members = 0;
        qsizetype strings = 0;
        qsizetype numbers = 0;
        qsizetype literals = 0;
    };

    static void collect(const QJsonValue& value, int depth, Statistics* statistics)
    {
        switch (value.type())
        {
            case QJsonValue::Object:
            {
                ++statistics->objects;
                statistics->maximumDepth = qMax(statistics->maximumDepth, depth + 1);
                const QJsonObject object = value.toObject();
                statistics->members += object.size();
                for (auto it = object.constBegin(); it != object.constEnd(); ++it)
                {
                    collect(it.value(), depth + 1, statistics);
                }
                break;
            }
            case QJsonValue::Array:
            {
                ++statistics->arrays;
                statistics->maximumDepth = qMax(statistics->maximumDepth, depth + 1);
                const QJsonArray array = value.toArray();
                for (const QJsonValue& item : array)
                {
                    collect(item, depth + 1, statistics);
                }
                break;
            }
            case QJsonValue::String:
                ++statistics->strings;
                break;
            case QJsonValue::Double:
                ++statistics->numbers;
                break;
            default:
                ++statistics->literals;
                break;
        }
    }

    /// A random JSON text (valid), keys may repeat when \p allowDuplicates
    static QByteArray randomJson(QRandomGenerator& random, int depth)
    {
        const int kind = int(random.bounded(depth > 4 ? 6 : 8));
        switch (kind)
        {
            case 0:
                return QByteArray::number(qint64(random.generate64() >> random.bounded(64)) * (random.bounded(2) ? 1 : -1));
            case 1:
                return QByteArray::number(random.generateDouble() * 1000.0 - 500.0, 'g', 17);
            case 2:
            {
                static const char* const STRINGS[] = { "\"\"", "\"abc\"", "\"\\u00e9\"", "\"\\ud83d\\ude00\"", "\"\\n\\t\\\"\"", "\"\xc5\xa1\xc4\x8d\"", "\"\\u0000\"", "\"\\ud800\"" };
                return STRINGS[random.bounded(int(std::size(STRINGS)))];
            }
            case 3:
                return random.bounded(2) ? "true" : "false";
            case 4:
                return "null";
            case 5:
                return QByteArray::number(int(random.bounded(100000)));
            case 6:
            {
                QByteArray text = "[";
                const int count = int(random.bounded(4));
                for (int i = 0; i < count; ++i)
                {
                    text += (i ? "," : "") + randomJson(random, depth + 1);
                }
                return text + "]";
            }
            default:
            {
                QByteArray text = "{";
                const int count = int(random.bounded(4));
                for (int i = 0; i < count; ++i)
                {
                    text += (i ? "," : "") + QByteArray("\"k") + QByteArray::number(int(random.bounded(6))) + "\":" + randomJson(random, depth + 1);
                }
                return text + "}";
            }
        }
    }

    /// A mutation of a text: a flipped, inserted, removed or duplicated byte
    static QByteArray mutate(QRandomGenerator& random, QByteArray text)
    {
        const int count = 1 + int(random.bounded(3));
        static const char CHARACTERS[] = "{}[]\",:\\ 0123456789eE.-+tfnu\x80\xc3\xed\xf4\xff";
        for (int i = 0; i < count && !text.isEmpty(); ++i)
        {
            const qsizetype position = qsizetype(random.bounded(int(text.size())));
            switch (random.bounded(4))
            {
                case 0:
                    text[position] = CHARACTERS[random.bounded(int(sizeof(CHARACTERS) - 1))];
                    break;
                case 1:
                    text.insert(position, CHARACTERS[random.bounded(int(sizeof(CHARACTERS) - 1))]);
                    break;
                case 2:
                    text.remove(position, 1);
                    break;
                default:
                    text.insert(position, text.mid(position, 4));
                    break;
            }
        }
        return text;
    }

    static int factor()
    {
        return pdftest::McpTestClient::getTimeoutFactor();
    }

    static bool waitFor(const std::function<bool()>& condition, int timeout)
    {
        QElapsedTimer timer;
        timer.start();
        while (!condition())
        {
            if (timer.elapsed() > qint64(timeout) * factor())
            {
                return false;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
        }
        return true;
    }

    static pdf::PDFMcpServerConfig createConfig(quint16 port = 0)
    {
        pdf::PDFMcpServerConfig config;
        config.port = port;
        config.serverInfo.version = QStringLiteral("9.8.7.6");
        config.ioThreadCount = 2;
        return config;
    }
};

/// The test catalog, the behavior of the fake host and a running server
/// (package A of step 7)
class McpServerHelper
{
public:
    McpServerHelper() = delete;

    static QByteArray token() { return QByteArrayLiteral("test-token-0123456789abcdefghijklmnopqrstuvwxyz"); }

    static pdf::PDFMcpServerConfig createConfig()
    {
        pdf::PDFMcpServerConfig config = McpTestHelper::createConfig();
        config.token = token();
        config.requireToken = true;
        config.instructions = QStringLiteral("Test instructions of PDF4QT");
        config.serverInfo.description = QStringLiteral("PDF4QT test server");
        config.serverInfo.websiteUrl = QStringLiteral("https://github.com/JakubMelka/PDF4QT");
        pdf::PDFMcpIcon icon;
        icon.source = QStringLiteral("data:image/png;base64,AAAA");
        icon.sizes = QStringList({ QStringLiteral("48x48") });
        config.serverInfo.icons.push_back(icon);
        config.headerTimeout = 2000;
        return config;
    }

    static pdf::PDFMcpToolDefinition createTool(const QString& name)
    {
        pdf::PDFMcpToolDefinition tool;
        tool.name = name;
        tool.title = name.toUpper();
        tool.description = QStringLiteral("Test tool %1").arg(name);
        tool.inputSchema = R"({"type":"object","properties":{"value":{"type":"string"}},"additionalProperties":true})";
        tool.annotations.readOnlyHint = true;
        pdf::PDFMcpIcon icon;
        icon.source = QStringLiteral("data:image/png;base64,AAAA");
        tool.icons.push_back(icon);
        return tool;
    }

    static std::shared_ptr<const pdf::PDFMcpCatalog> createCatalog(quint64 generation = 1, int extraResources = 0, const QStringList& extraTools = {})
    {
        std::vector<pdf::PDFMcpToolDefinition> tools;
        for (const char* name : { "echo", "slow", "progress", "ask", "askslow", "big", "error" })
        {
            tools.push_back(createTool(QString::fromLatin1(name)));
        }
        for (const QString& name : extraTools)
        {
            tools.push_back(createTool(name));
        }

        std::vector<pdf::PDFMcpResourceDefinition> resources;
        pdf::PDFMcpResourceDefinition index;
        index.uri = QStringLiteral("pdf4qt://docs/index.md");
        index.name = QStringLiteral("index.md");
        index.mimeType = QStringLiteral("text/markdown");
        index.isStatic = true;
        index.content = "# Index \xc5\xa1";
        index.size = index.content.size();
        resources.push_back(index);

        pdf::PDFMcpResourceDefinition logo;
        logo.uri = QStringLiteral("pdf4qt://docs/logo.png");
        logo.name = QStringLiteral("logo.png");
        logo.mimeType = QStringLiteral("image/png");
        logo.isStatic = true;
        logo.content = QByteArray("\x89PNG\x00\x01", 6);
        resources.push_back(logo);

        pdf::PDFMcpResourceDefinition document;
        document.uri = QStringLiteral("pdf4qt://documents/ABC/info");
        document.name = QStringLiteral("info");
        document.mimeType = QStringLiteral("application/json");
        resources.push_back(document);

        for (int i = 0; i < extraResources; ++i)
        {
            pdf::PDFMcpResourceDefinition extra;
            extra.uri = QStringLiteral("pdf4qt://docs/extra-%1.md").arg(i, 4, 10, QLatin1Char('0'));
            extra.name = QStringLiteral("extra-%1.md").arg(i);
            extra.mimeType = QStringLiteral("text/markdown");
            extra.isStatic = true;
            extra.content = "x";
            resources.push_back(extra);
        }

        std::vector<pdf::PDFMcpResourceTemplateDefinition> templates;
        pdf::PDFMcpResourceTemplateDefinition pageText;
        pageText.uriTemplate = pdf::PDFMcpUriTemplate(QStringLiteral("pdf4qt://documents/{documentId}/pages/{page}/text"));
        pageText.name = QStringLiteral("page-text");
        templates.push_back(pageText);
        pdf::PDFMcpResourceTemplateDefinition help;
        help.uriTemplate = pdf::PDFMcpUriTemplate(QStringLiteral("pdf4qt://help/{topic}.md"));
        help.name = QStringLiteral("help");
        help.isStatic = true;
        templates.push_back(help);

        std::vector<pdf::PDFMcpPromptDefinition> prompts;
        pdf::PDFMcpPromptDefinition summarize;
        summarize.name = QStringLiteral("summarize-document");
        summarize.arguments.push_back({ QStringLiteral("document"), QString(), QStringLiteral("Document"), false });
        prompts.push_back(summarize);
        pdf::PDFMcpPromptDefinition choose;
        choose.name = QStringLiteral("choose-document");
        prompts.push_back(choose);

        return std::make_shared<const pdf::PDFMcpCatalog>(generation, std::move(tools), std::move(resources), std::move(templates), std::move(prompts));
    }

    /// The question of the tools "ask" / "askslow" and of the prompt "choose-document"
    static pdf::PDFMcpInputRequests createChoice()
    {
        pdf::PDFMcpInputRequest request;
        request.key = QStringLiteral("pdf4qt.choice");
        request.message = QStringLiteral("Choose a document");
        request.requestedSchema = R"({"type":"object","properties":{"documentId":{"type":"string","enum":["A","B"]}},"required":["documentId"]})";

        pdf::PDFMcpInputRequests requests;
        requests.requests.push_back(request);
        requests.allowedValues = QJsonDocument::fromJson(R"({"pdf4qt.choice":{"documentId":["A","B"]}})").object();
        return requests;
    }

    /// The chosen document of a retried call (empty = none)
    static QString getChoice(const pdf::PDFMcpCallRequest& request)
    {
        return request.inputResponses.value(QStringLiteral("pdf4qt.choice")).toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("documentId")).toString();
    }

    static pdf::PDFMcpCallResult createText(const QString& text)
    {
        pdf::PDFMcpContentBuilder builder;
        builder.addText(text);
        return builder.createCallResult(false);
    }

    /// The behavior of the fake host by the tool / the kind of the call
    static void handle(const std::shared_ptr<pdf::PDFMcpCall>& call)
    {
        const pdf::PDFMcpCallRequest& request = call->getRequest();
        switch (request.kind)
        {
            case pdf::PDFMcpCallKind::ToolCall:
            {
                const QString& name = request.name;
                if (name == QLatin1String("echo"))
                {
                    call->finish(createText(QString::fromUtf8(QJsonDocument(request.arguments).toJson(QJsonDocument::Compact))));
                }
                else if (name == QLatin1String("slow"))
                {
                    // Finished by the test
                }
                else if (name == QLatin1String("progress"))
                {
                    call->reportProgress(1, 3, QStringLiteral("one"));
                    call->reportProgress(2, 3, QStringLiteral("two"));
                    call->log(QStringLiteral("info"), QStringLiteral("info text"));
                    call->log(QStringLiteral("warning"), QStringLiteral("warning text"));
                    call->reportProgress(3, 3, QStringLiteral("three"));
                    call->finish(createText(QStringLiteral("done")));
                }
                else if (name == QLatin1String("ask"))
                {
                    const QString choice = getChoice(request);
                    if (choice.isEmpty())
                    {
                        call->requestInput(createChoice());
                    }
                    else
                    {
                        call->finish(createText(QStringLiteral("chosen ") + choice));
                    }
                }
                else if (name == QLatin1String("askslow"))
                {
                    const QString choice = getChoice(request);
                    if (choice.isEmpty())
                    {
                        QTimer::singleShot(400, qApp, [call]() { call->requestInput(createChoice()); });
                    }
                    else
                    {
                        call->finish(createText(QStringLiteral("chosen ") + choice));
                    }
                }
                else if (name == QLatin1String("big"))
                {
                    call->finish(createText(QString(17 * 1024 * 1024, QLatin1Char('x'))));
                }
                else if (name == QLatin1String("error"))
                {
                    call->finish(pdf::PDFMcpCallResult::createError(pdf::PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Bad argument")));
                }
                else
                {
                    call->finish(pdf::PDFMcpCallResult::createError(pdf::PDFMcpErrorCodes::INVALID_PARAMS, QStringLiteral("Unknown tool: ") + name));
                }
                break;
            }

            case pdf::PDFMcpCallKind::ResourceRead:
            {
                pdf::PDFMcpJsonWriter writer;
                writer.beginObject();
                writer.key(QLatin1StringView("contents"));
                writer.beginArray();
                writer.beginObject();
                writer.writeMember(QLatin1StringView("uri"), QStringView(request.uri));
                writer.writeMember(QLatin1StringView("mimeType"), QLatin1StringView("application/json"));
                writer.writeMember(QLatin1StringView("text"), QLatin1StringView("{}"));
                writer.endObject();
                writer.endArray();
                writer.endObject();
                call->finish(pdf::PDFMcpCallResult::createComplete(writer.take()));
                break;
            }

            case pdf::PDFMcpCallKind::PromptGet:
            {
                if (request.name == QLatin1String("choose-document") && getChoice(request).isEmpty())
                {
                    call->requestInput(createChoice());
                    break;
                }
                const QString text = QStringLiteral("Prompt ") + request.name + QLatin1Char(' ') + getChoice(request);
                call->finish(pdf::PDFMcpCallResult::createComplete(QJsonDocument(QJsonObject({ { QStringLiteral("messages"), QJsonArray({ QJsonObject({ { QStringLiteral("role"), QStringLiteral("user") },
                    { QStringLiteral("content"), QJsonObject({ { QStringLiteral("type"), QStringLiteral("text") }, { QStringLiteral("text"), text } }) } }) }) } })).toJson(QJsonDocument::Compact)));
                break;
            }

            case pdf::PDFMcpCallKind::Completion:
                call->finish(pdf::PDFMcpCallResult::createComplete(R"({"completion":{"values":["a"],"total":1,"hasMore":false}})"));
                break;
        }
    }

    /// The text of the first content block of a JSON-RPC response
    static QString getText(const QJsonObject& response)
    {
        const QJsonArray content = response.value(QStringLiteral("result")).toObject().value(QStringLiteral("content")).toArray();
        return content.isEmpty() ? QString() : content.first().toObject().value(QStringLiteral("text")).toString();
    }

    static int getErrorCode(const QJsonObject& response)
    {
        return response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt();
    }

    /// A raw POST of a JSON body with the headers of a modern request
    static QByteArray createRawPost(quint16 port, const QByteArray& body, const QList<QPair<QByteArray, QByteArray>>& extra = {}, bool withToken = true)
    {
        QByteArray request = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\nAccept: application/json, text/event-stream\r\nContent-Type: application/json\r\n";
        if (withToken)
        {
            request += "Authorization: Bearer " + token() + "\r\n";
        }
        for (const auto& header : extra)
        {
            request += header.first + ": " + header.second + "\r\n";
        }
        request += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        return request;
    }

    /// The modern _meta of a raw body
    static QByteArray modernMeta()
    {
        return R"("_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}})";
    }

    /// The headers of a modern raw request of the method
    static QList<QPair<QByteArray, QByteArray>> modernHeaders(const QByteArray& method, const QByteArray& name = QByteArray())
    {
        QList<QPair<QByteArray, QByteArray>> headers = { { "MCP-Protocol-Version", "2026-07-28" }, { "Mcp-Method", method } };
        if (!name.isEmpty())
        {
            headers.append(qMakePair(QByteArray("Mcp-Name"), QByteArray(name)));
        }
        return headers;
    }

    /// The loopback filter of this machine (wave 0 finding): an incomplete HTTP
    /// head does not reach a local server until the client closes. Detected by
    /// a plain listener: true = incomplete heads are held back.
    static bool hasLoopbackFilter()
    {
        static int s_result = -1;
        if (s_result >= 0)
        {
            return s_result == 1;
        }

        QTcpServer listener;
        if (!listener.listen(QHostAddress::LocalHost, 0))
        {
            s_result = 0;
            return false;
        }

        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, listener.serverPort());
        if (!client.waitForConnected(5000) || !listener.waitForNewConnection(5000))
        {
            s_result = 0;
            return false;
        }

        std::unique_ptr<QTcpSocket> server(listener.nextPendingConnection());
        client.write("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n");
        client.flush();
        QElapsedTimer timer;
        timer.start();
        while (server->bytesAvailable() == 0 && timer.elapsed() < 1500)
        {
            server->waitForReadyRead(100);
        }
        s_result = server->bytesAvailable() > 0 ? 0 : 1;
        return s_result == 1;
    }
};

/// A running server with the fake host of the test catalog
class McpServerFixture
{
public:
    explicit McpServerFixture(pdf::PDFMcpServerConfig config = McpServerHelper::createConfig(), std::shared_ptr<const pdf::PDFMcpCatalog> catalog = McpServerHelper::createCatalog())
    {
        host.setHandler(&McpServerHelper::handle);
        server = std::make_unique<pdf::PDFMcpServer>(&host);
        server->publishCatalog(std::move(catalog));
        isRunning = server->start(config) && McpTestHelper::waitFor([this]() { return server->getState() == pdf::PDFMcpServerState::Running; }, 5000);
    }

    ~McpServerFixture()
    {
        server.reset();
        pdf::PDFMcpClock::setOffset(0);
    }

    quint16 port() const { return server ? server->getPort() : 0; }

    pdftest::McpTestClient client(pdftest::McpTestEra era = pdftest::McpTestEra::Modern, const QString& name = QStringLiteral("test-client")) const
    {
        pdftest::McpTestClient result(port(), era);
        result.setToken(McpServerHelper::token());
        result.setClientInfo(name);
        return result;
    }

    /// Waits until the host has \p count calls
    bool waitCalls(size_t count, int timeout = 5000) const
    {
        return McpTestHelper::waitFor([this, count]() { return host.calls.size() >= count; }, timeout);
    }

    /// Waits until the host has \p count canceled calls
    bool waitCanceled(size_t count, int timeout = 5000) const
    {
        return McpTestHelper::waitFor([this, count]() { return host.canceledCalls.size() >= count; }, timeout);
    }

    /// Waits for an assistant event of the kind
    bool waitEvent(pdf::PDFMcpAssistantEvent::Kind kind, int timeout = 5000) const
    {
        return McpTestHelper::waitFor([this, kind]()
        {
            for (const pdf::PDFMcpAssistantEvent& event : host.events)
            {
                if (event.kind == kind)
                {
                    return true;
                }
            }
            return false;
        }, timeout);
    }

    int countEvents(pdf::PDFMcpAssistantEvent::Kind kind) const
    {
        int count = 0;
        for (const pdf::PDFMcpAssistantEvent& event : host.events)
        {
            count += event.kind == kind ? 1 : 0;
        }
        return count;
    }

    /// A raw request and its response
    pdftest::McpTestResponse raw(const QByteArray& request, int timeout = -1) const
    {
        pdftest::McpTestClient rawClient(port());
        return rawClient.sendRaw(request, timeout);
    }

    pdftest::FakeMcpHost host;
    std::unique_ptr<pdf::PDFMcpServer> server;
    bool isRunning = false;
};

/// Direct harness of the dispatcher without sockets (the Host rules of the
/// remote mode are tested without binding every interface - no firewall prompt)
class McpDispatcherHarness
{
public:
    explicit McpDispatcherHarness(const pdf::PDFMcpServerConfig& config, quint16 port = 4321) :
        shared(std::make_shared<pdf::PDFMcpServerShared>(config))
    {
        shared->host = &host;
        shared->setCatalog(McpServerHelper::createCatalog());
        dispatcher = std::make_unique<pdf::PDFMcpDispatcher>(shared, nullptr);
        dispatcher->setPort(port);
    }

    /// The response of a request (complete output of the exchange)
    pdftest::McpTestResponse request(const QByteArray& method, const QByteArray& hostHeader, const QList<QPair<QByteArray, QByteArray>>& headers = {}, const QByteArray& body = QByteArray())
    {
        pdf::PDFMcpHttpRequest httpRequest;
        httpRequest.method = method;
        httpRequest.target = "/mcp";
        httpRequest.version = "HTTP/1.1";
        httpRequest.headers.emplace_back("host", hostHeader);
        for (const auto& header : headers)
        {
            httpRequest.headers.emplace_back(header.first.toLower(), header.second);
        }
        httpRequest.body = body;

        std::shared_ptr<pdf::PDFMcpExchange> exchange = std::make_shared<pdf::PDFMcpExchange>(httpRequest, QStringLiteral("192.168.1.57"), true, QByteArray("1"));
        dispatcher->handleExchange(exchange);
        bool isFinished = false;
        bool closeConnection = false;
        return pdftest::McpTestClient::parseResponse(exchange->takeOutput(&isFinished, &closeConnection), true);
    }

    pdftest::FakeMcpHost host;
    std::shared_ptr<pdf::PDFMcpServerShared> shared;
    std::unique_ptr<pdf::PDFMcpDispatcher> dispatcher;
};

/// One row of the MUST checklist of chapter 1 of the plan (conformance)
class McpConformanceRow
{
public:
    const char* id;
    const char* requirement;
    void (*check)(McpServerFixture& fixture);
};

/// The checks of the MUST checklist (each one runs against a fresh server)
class McpConformance
{
public:
    McpConformance() = delete;

    static const std::vector<McpConformanceRow>& getRows();
};

/// Shapes of the messages of the server against the schema of their era
/// (review RP7: SCR/mcp/schema_*.ts of 2026-07-28 and 2025-11-25; 2025-06-18 is
/// 2025-11-25 without icons, Implementation.description / websiteUrl, the
/// elicitation mode, the experimental tasks and the extensions). A spec lists
/// the members "name:type" or "name?:type" (optional) with the types s string,
/// i integer, n number, b boolean, o object, a array, I request id (string or
/// integer), N number or null, x any. Every shape is closed: a member outside of
/// its spec is an error (the server never writes a member, which its era does
/// not know - resultType / ttlMs / serverInfo to a legacy client, icons to a
/// 2025-06-18 client).
class McpShape
{
public:
    McpShape() = delete;

    using Era = pdftest::McpTestEra;
    using Checker = void (*)(const QJsonValue& value, Era era, const QString& path, QStringList* errors);

    static bool isModern(Era era) { return era == Era::Modern; }

    static bool hasType(const QJsonValue& value, QStringView type)
    {
        if (type == u"s")
        {
            return value.isString();
        }
        if (type == u"i")
        {
            return value.isDouble() && std::isfinite(value.toDouble()) && std::floor(value.toDouble()) == value.toDouble();
        }
        if (type == u"n")
        {
            return value.isDouble();
        }
        if (type == u"b")
        {
            return value.isBool();
        }
        if (type == u"o")
        {
            return value.isObject();
        }
        if (type == u"a")
        {
            return value.isArray();
        }
        if (type == u"I")
        {
            return value.isString() || hasType(value, u"i");
        }
        if (type == u"N")
        {
            return value.isNull() || hasType(value, u"i");
        }
        return true;
    }

    /// Checks the members of an object against the spec (closed)
    static void check(const QJsonValue& value, const QString& spec, const QString& path, QStringList* errors)
    {
        if (!value.isObject())
        {
            errors->append(path + QStringLiteral(": expected an object"));
            return;
        }

        const QJsonObject object = value.toObject();
        QStringList known;
        for (const QString& member : spec.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        {
            const qsizetype colon = member.lastIndexOf(QLatin1Char(':'));
            QString name = member.left(colon);
            const QString type = member.mid(colon + 1);
            const bool isOptional = name.endsWith(QLatin1Char('?'));
            if (isOptional)
            {
                name.chop(1);
            }
            known.append(name);

            const QJsonValue item = object.value(name);
            if (item.isUndefined())
            {
                if (!isOptional)
                {
                    errors->append(QStringLiteral("%1.%2: missing").arg(path, name));
                }
                continue;
            }
            if (!hasType(item, type))
            {
                errors->append(QStringLiteral("%1.%2: expected the type %3").arg(path, name, type));
            }
        }

        for (auto it = object.begin(); it != object.end(); ++it)
        {
            if (!known.contains(it.key()))
            {
                errors->append(QStringLiteral("%1.%2: not a member of the schema of the era").arg(path, it.key()));
            }
        }
    }

    static void expectValue(const QJsonValue& value, const QJsonValue& expected, const QString& path, QStringList* errors)
    {
        if (value != expected)
        {
            errors->append(QStringLiteral("%1: %2 instead of %3").arg(path, QString::fromUtf8(QJsonDocument(QJsonArray({ value })).toJson(QJsonDocument::Compact)),
                                                                        QString::fromUtf8(QJsonDocument(QJsonArray({ expected })).toJson(QJsonDocument::Compact))));
        }
    }

    static void checkIcons(const QJsonObject& object, const QString& path, QStringList* errors)
    {
        const QJsonArray icons = object.value(QStringLiteral("icons")).toArray();
        for (qsizetype i = 0; i < icons.size(); ++i)
        {
            const QString iconPath = QStringLiteral("%1.icons[%2]").arg(path).arg(i);
            check(icons[i], QStringLiteral("src:s mimeType?:s sizes?:a theme?:s"), iconPath, errors);
            const QJsonObject icon = icons[i].toObject();
            const QString source = icon.value(QStringLiteral("src")).toString();
            if (!source.startsWith(QLatin1String("data:")) && !source.startsWith(QLatin1String("https:")))
            {
                errors->append(iconPath + QStringLiteral(".src: only https: or data: URIs"));
            }
            if (icon.contains(QStringLiteral("theme")) && icon.value(QStringLiteral("theme")) != QLatin1String("light") && icon.value(QStringLiteral("theme")) != QLatin1String("dark"))
            {
                errors->append(iconPath + QStringLiteral(".theme: light or dark"));
            }
        }
    }

    /// The members of icons: none for 2025-06-18
    static QString iconsMember(Era era)
    {
        return era == Era::Legacy20250618 ? QString() : QStringLiteral(" icons?:a");
    }

    static void checkImplementation(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        if (era == Era::Legacy20250618)
        {
            check(value, QStringLiteral("name:s title?:s version:s"), path, errors);
            return;
        }
        check(value, QStringLiteral("name:s title?:s version:s description?:s websiteUrl?:s icons?:a"), path, errors);
        checkIcons(value.toObject(), path, errors);
    }

    /// The spec of a result: its own members and the members of Result of the era
    static QString resultSpec(Era era, const QString& members, bool isCacheable)
    {
        if (!isModern(era))
        {
            return members + QStringLiteral(" _meta?:o");
        }
        return members + QStringLiteral(" resultType:s _meta:o") + (isCacheable ? QStringLiteral(" ttlMs:i cacheScope:s") : QString());
    }

    /// The members of Result: resultType, the serverInfo of _meta, the cache fields
    static void checkResultBase(const QJsonObject& result, Era era, const QString& path, QStringList* errors, const QString& resultType = QStringLiteral("complete"))
    {
        if (!isModern(era))
        {
            if (result.value(QStringLiteral("_meta")).toObject().contains(QStringLiteral("io.modelcontextprotocol/serverInfo")))
            {
                errors->append(path + QStringLiteral("._meta: serverInfo of a result is 2026-07-28"));
            }
            return;
        }

        expectValue(result.value(QStringLiteral("resultType")), resultType, path + QStringLiteral(".resultType"), errors);
        const QJsonObject meta = result.value(QStringLiteral("_meta")).toObject();
        checkImplementation(meta.value(QStringLiteral("io.modelcontextprotocol/serverInfo")), Era::Modern, path + QStringLiteral("._meta.serverInfo"), errors);
        if (result.contains(QStringLiteral("ttlMs")) && result.value(QStringLiteral("ttlMs")).toDouble() < 0.0)
        {
            errors->append(path + QStringLiteral(".ttlMs: MUST be >= 0"));
        }
        if (result.contains(QStringLiteral("cacheScope")) && result.value(QStringLiteral("cacheScope")) != QLatin1String("public") && result.value(QStringLiteral("cacheScope")) != QLatin1String("private"))
        {
            errors->append(path + QStringLiteral(".cacheScope: public or private"));
        }
    }

    /// A JSON-RPC response with a result; returns the result
    static QJsonObject checkResponse(const QJsonObject& response, const QString& path, QStringList* errors)
    {
        check(response, QStringLiteral("jsonrpc:s id:I result:o"), path, errors);
        expectValue(response.value(QStringLiteral("jsonrpc")), QStringLiteral("2.0"), path + QStringLiteral(".jsonrpc"), errors);
        return response.value(QStringLiteral("result")).toObject();
    }

    /// A JSON-RPC error response (the data checked by \p dataSpec, when given)
    static void checkError(const QJsonObject& response, int code, const QString& dataSpec, const QString& path, QStringList* errors)
    {
        // The id is written as null, when it could not be read (plan of step 7,
        // 1.5; the schemas of both eras say id?: RequestId - judgment of RP7)
        check(response, QStringLiteral("jsonrpc:s id?:x error:o"), path, errors);
        const QJsonValue id = response.value(QStringLiteral("id"));
        if (!id.isUndefined() && !id.isNull() && !hasType(id, u"I"))
        {
            errors->append(path + QStringLiteral(".id: string or integer"));
        }
        check(response.value(QStringLiteral("error")), dataSpec.isEmpty() ? QStringLiteral("code:i message:s data?:x") : QStringLiteral("code:i message:s data:o"),
              path + QStringLiteral(".error"), errors);
        const QJsonObject error = response.value(QStringLiteral("error")).toObject();
        expectValue(error.value(QStringLiteral("code")), code, path + QStringLiteral(".error.code"), errors);
        if (!dataSpec.isEmpty())
        {
            check(error.value(QStringLiteral("data")), dataSpec, path + QStringLiteral(".error.data"), errors);
        }
    }

    static void checkCapabilities(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        const QString extra = isModern(era) ? QStringLiteral(" extensions?:o") : (era == Era::Legacy20251125 ? QStringLiteral(" tasks?:o") : QString());
        check(value, QStringLiteral("experimental?:o logging?:o completions?:o prompts?:o resources?:o tools?:o") + extra, path, errors);
        const QJsonObject capabilities = value.toObject();
        if (capabilities.contains(QStringLiteral("prompts")))
        {
            check(capabilities.value(QStringLiteral("prompts")), QStringLiteral("listChanged?:b"), path + QStringLiteral(".prompts"), errors);
        }
        if (capabilities.contains(QStringLiteral("resources")))
        {
            check(capabilities.value(QStringLiteral("resources")), QStringLiteral("subscribe?:b listChanged?:b"), path + QStringLiteral(".resources"), errors);
        }
        if (capabilities.contains(QStringLiteral("tools")))
        {
            check(capabilities.value(QStringLiteral("tools")), QStringLiteral("listChanged?:b"), path + QStringLiteral(".tools"), errors);
        }
    }

    static void checkDiscover(const QJsonObject& response, QStringList* errors)
    {
        const QJsonObject result = checkResponse(response, QStringLiteral("server/discover"), errors);
        check(result, resultSpec(Era::Modern, QStringLiteral("supportedVersions:a capabilities:o instructions?:s"), true), QStringLiteral("DiscoverResult"), errors);
        checkResultBase(result, Era::Modern, QStringLiteral("DiscoverResult"), errors);
        checkCapabilities(result.value(QStringLiteral("capabilities")), Era::Modern, QStringLiteral("DiscoverResult.capabilities"), errors);
    }

    static void checkInitialize(const QJsonObject& response, Era era, QStringList* errors)
    {
        const QJsonObject result = checkResponse(response, QStringLiteral("initialize"), errors);
        check(result, QStringLiteral("protocolVersion:s capabilities:o serverInfo:o instructions?:s _meta?:o"), QStringLiteral("InitializeResult"), errors);
        checkResultBase(result, era, QStringLiteral("InitializeResult"), errors);
        checkCapabilities(result.value(QStringLiteral("capabilities")), era, QStringLiteral("InitializeResult.capabilities"), errors);
        checkImplementation(result.value(QStringLiteral("serverInfo")), era, QStringLiteral("InitializeResult.serverInfo"), errors);
    }

    /// EmptyResult (ping, logging/setLevel, resources/subscribe, tasks/update, tasks/cancel)
    static void checkEmpty(const QJsonObject& response, Era era, const QString& path, QStringList* errors)
    {
        const QJsonObject result = checkResponse(response, path, errors);
        check(result, resultSpec(era, QString(), false), path + QStringLiteral(".result"), errors);
        checkResultBase(result, era, path + QStringLiteral(".result"), errors);
    }

    static void checkToolAnnotations(const QJsonValue& value, const QString& path, QStringList* errors)
    {
        check(value, QStringLiteral("title?:s readOnlyHint?:b destructiveHint?:b idempotentHint?:b openWorldHint?:b"), path, errors);
    }

    static void checkTool(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        const QString execution = era == Era::Legacy20251125 ? QStringLiteral(" execution?:o") : QString();
        check(value, QStringLiteral("name:s title?:s description?:s inputSchema:o outputSchema?:o annotations?:o _meta?:o") + iconsMember(era) + execution, path, errors);
        const QJsonObject tool = value.toObject();
        checkIcons(tool, path, errors);
        expectValue(tool.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("type")), QStringLiteral("object"), path + QStringLiteral(".inputSchema.type"), errors);
        if (!isModern(era) && tool.contains(QStringLiteral("outputSchema")))
        {
            // 2025-11-25 and 2025-06-18: outputSchema of the type object
            expectValue(tool.value(QStringLiteral("outputSchema")).toObject().value(QStringLiteral("type")), QStringLiteral("object"), path + QStringLiteral(".outputSchema.type"), errors);
        }
        if (tool.contains(QStringLiteral("annotations")))
        {
            checkToolAnnotations(tool.value(QStringLiteral("annotations")), path + QStringLiteral(".annotations"), errors);
        }
    }

    static QString resourceMembers(Era era)
    {
        return QStringLiteral("uri:s name:s title?:s description?:s mimeType?:s annotations?:o size?:i _meta?:o") + iconsMember(era);
    }

    static void checkResource(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        check(value, resourceMembers(era), path, errors);
        checkIcons(value.toObject(), path, errors);
    }

    static void checkTemplate(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        check(value, QStringLiteral("uriTemplate:s name:s title?:s description?:s mimeType?:s annotations?:o _meta?:o") + iconsMember(era), path, errors);
        checkIcons(value.toObject(), path, errors);
    }

    static void checkPrompt(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        check(value, QStringLiteral("name:s title?:s description?:s arguments?:a _meta?:o") + iconsMember(era), path, errors);
        checkIcons(value.toObject(), path, errors);
        const QJsonArray arguments = value.toObject().value(QStringLiteral("arguments")).toArray();
        for (qsizetype i = 0; i < arguments.size(); ++i)
        {
            check(arguments[i], QStringLiteral("name:s title?:s description?:s required?:b"), QStringLiteral("%1.arguments[%2]").arg(path).arg(i), errors);
        }
    }

    /// A list result (tools, prompts, resources, resourceTemplates); returns its items
    static QJsonArray checkList(const QJsonObject& response, Era era, const QString& key, Checker checker, QStringList* errors)
    {
        const QString path = key + QStringLiteral("/list");
        const QJsonObject result = checkResponse(response, path, errors);
        check(result, resultSpec(era, key + QStringLiteral(":a nextCursor?:s"), true), path + QStringLiteral(".result"), errors);
        checkResultBase(result, era, path + QStringLiteral(".result"), errors);
        const QJsonArray items = result.value(key).toArray();
        for (qsizetype i = 0; i < items.size(); ++i)
        {
            checker(items[i], era, QStringLiteral("%1[%2]").arg(key).arg(i), errors);
        }
        return items;
    }

    static void checkResourceContents(const QJsonValue& value, const QString& path, QStringList* errors)
    {
        if (value.toObject().contains(QStringLiteral("blob")))
        {
            check(value, QStringLiteral("uri:s mimeType?:s blob:s _meta?:o"), path, errors);
            const QByteArray blob = value.toObject().value(QStringLiteral("blob")).toString().toLatin1();
            if (!QByteArray::fromBase64Encoding(blob, QByteArray::AbortOnBase64DecodingErrors))
            {
                errors->append(path + QStringLiteral(".blob: not base64"));
            }
            return;
        }
        check(value, QStringLiteral("uri:s mimeType?:s text:s _meta?:o"), path, errors);
    }

    static void checkContentBlock(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        const QString type = value.toObject().value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text"))
        {
            check(value, QStringLiteral("type:s text:s annotations?:o _meta?:o"), path, errors);
        }
        else if (type == QLatin1String("image") || type == QLatin1String("audio"))
        {
            check(value, QStringLiteral("type:s data:s mimeType:s annotations?:o _meta?:o"), path, errors);
        }
        else if (type == QLatin1String("resource_link"))
        {
            check(value, QStringLiteral("type:s ") + resourceMembers(era), path, errors);
        }
        else if (type == QLatin1String("resource"))
        {
            check(value, QStringLiteral("type:s resource:o annotations?:o _meta?:o"), path, errors);
            checkResourceContents(value.toObject().value(QStringLiteral("resource")), path + QStringLiteral(".resource"), errors);
        }
        else
        {
            errors->append(path + QStringLiteral(".type: unknown content type ") + type);
        }
    }

    static void checkCallToolResult(const QJsonObject& result, Era era, const QString& path, QStringList* errors)
    {
        check(result, resultSpec(era, QStringLiteral("content:a structuredContent?:x isError?:b"), false), path, errors);
        checkResultBase(result, era, path, errors);
        if (!isModern(era) && result.contains(QStringLiteral("structuredContent")) && !result.value(QStringLiteral("structuredContent")).isObject())
        {
            errors->append(path + QStringLiteral(".structuredContent: an object of the legacy eras"));
        }
        const QJsonArray content = result.value(QStringLiteral("content")).toArray();
        for (qsizetype i = 0; i < content.size(); ++i)
        {
            checkContentBlock(content[i], era, QStringLiteral("%1.content[%2]").arg(path).arg(i), errors);
        }
    }

    static void checkReadResourceResult(const QJsonObject& result, Era era, const QString& path, QStringList* errors)
    {
        check(result, resultSpec(era, QStringLiteral("contents:a"), true), path, errors);
        checkResultBase(result, era, path, errors);
        const QJsonArray contents = result.value(QStringLiteral("contents")).toArray();
        if (contents.isEmpty())
        {
            errors->append(path + QStringLiteral(".contents: MUST NOT be empty"));
        }
        for (qsizetype i = 0; i < contents.size(); ++i)
        {
            checkResourceContents(contents[i], QStringLiteral("%1.contents[%2]").arg(path).arg(i), errors);
        }
    }

    static void checkGetPromptResult(const QJsonObject& result, Era era, const QString& path, QStringList* errors)
    {
        check(result, resultSpec(era, QStringLiteral("description?:s messages:a"), false), path, errors);
        checkResultBase(result, era, path, errors);
        const QJsonArray messages = result.value(QStringLiteral("messages")).toArray();
        for (qsizetype i = 0; i < messages.size(); ++i)
        {
            const QString messagePath = QStringLiteral("%1.messages[%2]").arg(path).arg(i);
            check(messages[i], QStringLiteral("role:s content:o"), messagePath, errors);
            const QString role = messages[i].toObject().value(QStringLiteral("role")).toString();
            if (role != QLatin1String("user") && role != QLatin1String("assistant"))
            {
                errors->append(messagePath + QStringLiteral(".role: user or assistant"));
            }
            checkContentBlock(messages[i].toObject().value(QStringLiteral("content")), era, messagePath + QStringLiteral(".content"), errors);
        }
    }

    static void checkCompleteResult(const QJsonObject& result, Era era, const QString& path, QStringList* errors)
    {
        check(result, resultSpec(era, QStringLiteral("completion:o"), false), path, errors);
        checkResultBase(result, era, path, errors);
        check(result.value(QStringLiteral("completion")), QStringLiteral("values:a total?:i hasMore?:b"), path + QStringLiteral(".completion"), errors);
        const QJsonArray values = result.value(QStringLiteral("completion")).toObject().value(QStringLiteral("values")).toArray();
        if (values.size() > 100)
        {
            errors->append(path + QStringLiteral(".completion.values: at most 100"));
        }
    }

    /// params of elicitation/create (a request of the legacy eras, an input request of MRTR)
    static void checkElicitParams(const QJsonValue& value, Era era, const QString& path, QStringList* errors)
    {
        if (era == Era::Legacy20250618)
        {
            check(value, QStringLiteral("message:s requestedSchema:o"), path, errors);
        }
        else
        {
            check(value, QStringLiteral("mode?:s message:s requestedSchema:o"), path, errors);
            if (value.toObject().contains(QStringLiteral("mode")))
            {
                expectValue(value.toObject().value(QStringLiteral("mode")), QStringLiteral("form"), path + QStringLiteral(".mode"), errors);
            }
        }

        const QJsonValue schema = value.toObject().value(QStringLiteral("requestedSchema"));
        check(schema, QStringLiteral("$schema?:s type:s properties:o required?:a"), path + QStringLiteral(".requestedSchema"), errors);
        expectValue(schema.toObject().value(QStringLiteral("type")), QStringLiteral("object"), path + QStringLiteral(".requestedSchema.type"), errors);
        const QJsonObject properties = schema.toObject().value(QStringLiteral("properties")).toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it)
        {
            // PrimitiveSchemaDefinition: flat, of a primitive type
            const QString type = it.value().toObject().value(QStringLiteral("type")).toString();
            if (type != QLatin1String("string") && type != QLatin1String("number") && type != QLatin1String("integer") && type != QLatin1String("boolean"))
            {
                errors->append(QStringLiteral("%1.requestedSchema.properties.%2: a primitive type").arg(path, it.key()));
            }
        }
    }

    static void checkInputRequiredResult(const QJsonObject& result, const QString& path, QStringList* errors)
    {
        check(result, QStringLiteral("resultType:s inputRequests?:o requestState?:s _meta:o"), path, errors);
        checkResultBase(result, Era::Modern, path, errors, QStringLiteral("input_required"));
        if (!result.contains(QStringLiteral("inputRequests")) && !result.contains(QStringLiteral("requestState")))
        {
            errors->append(path + QStringLiteral(": MUST include inputRequests or requestState"));
        }
        const QJsonObject inputRequests = result.value(QStringLiteral("inputRequests")).toObject();
        for (auto it = inputRequests.begin(); it != inputRequests.end(); ++it)
        {
            const QString requestPath = path + QStringLiteral(".inputRequests.") + it.key();
            check(it.value(), QStringLiteral("method:s params:o"), requestPath, errors);
            expectValue(it.value().toObject().value(QStringLiteral("method")), QStringLiteral("elicitation/create"), requestPath + QStringLiteral(".method"), errors);
            checkElicitParams(it.value().toObject().value(QStringLiteral("params")), Era::Modern, requestPath + QStringLiteral(".params"), errors);
        }
    }

    /// A notification; returns its params
    static QJsonObject checkNotification(const QJsonObject& message, const QString& method, const QString& paramsSpec, QStringList* errors)
    {
        check(message, paramsSpec.isNull() ? QStringLiteral("jsonrpc:s method:s params?:o") : QStringLiteral("jsonrpc:s method:s params:o"), method, errors);
        expectValue(message.value(QStringLiteral("method")), method, method + QStringLiteral(".method"), errors);
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        if (!paramsSpec.isNull())
        {
            check(params, paramsSpec, method + QStringLiteral(".params"), errors);
        }
        return params;
    }

    static QString taskMembers()
    {
        return QStringLiteral("taskId:s status:s statusMessage?:s createdAt:s lastUpdatedAt:s ttlMs:N pollIntervalMs?:i");
    }

    /// The members of a task by its status (tasks extension, DetailedTask)
    static QString detailedTaskMembers(const QJsonObject& task)
    {
        const QString status = task.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("completed"))
        {
            return taskMembers() + QStringLiteral(" result:o");
        }
        if (status == QLatin1String("failed"))
        {
            return taskMembers() + QStringLiteral(" error:o");
        }
        if (status == QLatin1String("input_required"))
        {
            return taskMembers() + QStringLiteral(" inputRequests:o");
        }
        return taskMembers();
    }

    static void checkTaskTimes(const QJsonObject& task, const QString& path, QStringList* errors)
    {
        for (const QString& key : { QStringLiteral("createdAt"), QStringLiteral("lastUpdatedAt") })
        {
            if (!QDateTime::fromString(task.value(key).toString(), Qt::ISODateWithMs).isValid())
            {
                errors->append(QStringLiteral("%1.%2: an ISO 8601 time").arg(path, key));
            }
        }
        const QString status = task.value(QStringLiteral("status")).toString();
        if (!QStringList({ QStringLiteral("working"), QStringLiteral("input_required"), QStringLiteral("completed"), QStringLiteral("failed"), QStringLiteral("cancelled") }).contains(status))
        {
            errors->append(path + QStringLiteral(".status: a TaskStatus"));
        }
    }

    /// Fails the test with the errors (all of them in the message)
    static bool report(const QStringList& errors)
    {
        if (errors.isEmpty())
        {
            return true;
        }
        qWarning("%s", qPrintable(errors.join(QLatin1Char('\n'))));
        return false;
    }
};

class McpTest : public QObject
{
    Q_OBJECT

private slots:
    void jsonLayer();
    void jsonLayerIds();
    void jsonLayerDifferentialFuzz();
    void jsonWriter();
    void jsonSanitizer();
    void jsonReader();
    void schemaBuilder();
    void schemaUniqueItems();
    void requestState();
    void uriTemplate();
    void callLifecycle();
    void catalogSerialization();
    void assistantKeys();
    void portInUseProbe();

    // Package A
    void serverStartStop();
    void httpParser();
    void httpPartialHeads();
    void httpFuzz();
    void jsonRpcErrors();
    void authHostOrigin();
    void remoteHosts();
    void modernHeaders();
    void discover();
    void listsAndPagination();
    void resourcesRead();
    void legacyLifecycle();
    void listenStreams();
    void progressAndLogging();
    void cancellation();
    void mrtrSealing();
    void legacyElicitation();
    void tasksExtension();
    void assistantsIdentity();
    void rateLimits();
    void loadManyClients();
    void shutdownOrder();
    void progressWhileStopping();
    void stateChangedSlots();
    void responseHeadersSanitized();
    void ipv6PortInUse();
    void unauthenticatedBodyNotRead();
    void assistantsLimit();

    // Review RP7 (protocol conformance)
    void legacyProgressAcrossElicitation();
    void schemaShapes_data();
    void schemaShapes();
    void schemaShapesModernStreams();

    // Review RC7-L (code of the library)
    void reviewStalledClientDropped();
    void reviewTaskInputTimeout();
    void reviewSchemaBounds();
    void reviewThrottleKeepsBlocks();
    void reviewProgressTokenExact();

    void conformance_data();
    void conformance();
};

void McpTest::jsonLayer()
{
    using Codes = pdf::PDFMcpErrorCodes;

    // Valid documents
    QVERIFY(McpTestHelper::scan(R"({"jsonrpc":"2.0","method":"x"})").isValid());
    QVERIFY(McpTestHelper::scan("  [1, 2.5e3, -0, true, false, null, \"a\"]  ").isValid());
    QCOMPARE(McpTestHelper::scan("[1]").topLevel, pdf::PDFMcpJsonScanResult::TopLevel::Array);
    QCOMPARE(McpTestHelper::scan("\"x\"").topLevel, pdf::PDFMcpJsonScanResult::TopLevel::Other);

    // Syntax errors: -32700
    for (const char* text : { "", "   ", "{", "{\"a\":}", "{\"a\" 1}", "[1,]", "[01]", "[1.]", "[.5]", "[1e]", "[+1]", "{a:1}", "{\"a\":1,}",
                              "[tru]", "[nul]", "\"\\x\"", "\"\\u12G4\"", "\"abc", "[1] [2]", "{\"a\":1}x", "\xef\xbb\xbf{}", "/*c*/{}", "{'a':1}" })
    {
        const pdf::PDFMcpJsonScanResult result = McpTestHelper::scan(text);
        QVERIFY2(!result.isValid(), text);
        QVERIFY2(result.errorCode == Codes::PARSE_ERROR, text);
    }

    // Control characters and invalid UTF-8 inside strings
    for (const QByteArray& text : { QByteArray("\"a\x01\""), QByteArray("\"\t\""), QByteArray("\"\xc0\x80\""), QByteArray("\"\xed\xa0\x80\""),
                                    QByteArray("\"\xf5\x80\x80\x80\""), QByteArray("\"\xe2\x82\""), QByteArray("\"\x80\""), QByteArray("\"\xf4\x90\x80\x80\""),
                                    QByteArray("\"\xe0\x80\xaf\"") })
    {
        const pdf::PDFMcpJsonScanResult result = McpTestHelper::scan(text);
        QVERIFY2(result.errorCode == Codes::PARSE_ERROR, text.toHex().constData());
    }
    QVERIFY(McpTestHelper::scan("\"\xc5\xa1\xe2\x82\xac\xf0\x9f\x98\x80\"").isValid());

    // Duplicate keys (also after the escapes are decoded): -32600 "duplicate key"
    for (const char* text : { R"({"a":1,"a":2})", R"({"a":1,"\u0061":2})", R"({"x":{"b":1,"b":1}})", R"([{"id":1,"id":2}])" })
    {
        const pdf::PDFMcpJsonScanResult result = McpTestHelper::scan(text);
        QCOMPARE(result.errorCode, Codes::INVALID_REQUEST);
        QCOMPARE(result.errorMessage, QStringLiteral("duplicate key"));
    }
    QVERIFY(McpTestHelper::scan(R"({"a":{"a":1},"b":{"a":2}})").isValid());

    // Depth: 64 levels pass, 65 are refused
    QVERIFY(McpTestHelper::scan(QByteArray(64, '[') + QByteArray(64, ']')).isValid());
    QCOMPARE(McpTestHelper::scan(QByteArray(65, '[') + QByteArray(65, ']')).errorCode, Codes::INVALID_REQUEST);
    QCOMPARE(McpTestHelper::scan(QByteArray(100000, '[')).errorCode, Codes::INVALID_REQUEST);

    // Members, items, strings, numbers and the size over the limits
    pdf::PDFMcpJsonScanLimits limits;
    limits.maximumObjectMembers = 3;
    limits.maximumArrayItems = 4;
    limits.maximumStringSize = 8;
    limits.maximumSize = 64;
    QVERIFY(McpTestHelper::scan(R"({"a":1,"b":2,"c":3})", limits).isValid());
    QCOMPARE(McpTestHelper::scan(R"({"a":1,"b":2,"c":3,"d":4})", limits).errorCode, Codes::INVALID_REQUEST);
    QVERIFY(McpTestHelper::scan("[1,2,3,4]", limits).isValid());
    QCOMPARE(McpTestHelper::scan("[1,2,3,4,5]", limits).errorCode, Codes::INVALID_REQUEST);
    QVERIFY(McpTestHelper::scan("\"12345678\"", limits).isValid());
    QCOMPARE(McpTestHelper::scan("\"123456789\"", limits).errorCode, Codes::INVALID_REQUEST);
    QCOMPARE(McpTestHelper::scan("[" + QByteArray(80, '1') + "]", limits).errorCode, Codes::INVALID_REQUEST);
    QCOMPARE(McpTestHelper::scan("[1" + QByteArray(64, '0') + "]").errorCode, Codes::INVALID_REQUEST);
    QVERIFY(McpTestHelper::scan("[1" + QByteArray(63, '0') + "]").isValid());

    // Numbers out of the range of a double are refused, an underflow is zero
    QCOMPARE(McpTestHelper::scan("[1e400]").errorCode, Codes::INVALID_REQUEST);
    QCOMPARE(McpTestHelper::scan("[-1.5e309]").errorCode, Codes::INVALID_REQUEST);
    QVERIFY(McpTestHelper::scan("[1e-400]").isValid());
    QVERIFY(McpTestHelper::scan("[0.000001e-320]").isValid());

    // Lone surrogates are recorded, not refused
    const pdf::PDFMcpJsonScanResult lone = McpTestHelper::scan(R"(["\ud800", "\udc00x", "\ud800\u0041"])");
    QVERIFY(lone.isValid());
    QVERIFY(lone.hasLoneSurrogate);
    const pdf::PDFMcpJsonScanResult pair = McpTestHelper::scan(R"(["\ud83d\ude00"])");
    QVERIFY(pair.isValid());
    QVERIFY(!pair.hasLoneSurrogate);

    // The default limits are the plan
    const pdf::PDFMcpJsonScanLimits defaults;
    QCOMPARE(defaults.maximumSize, qsizetype(8) * 1024 * 1024);
    QCOMPARE(defaults.maximumDepth, 64);
    QCOMPARE(defaults.maximumObjectMembers, 10000);
    QCOMPARE(defaults.maximumArrayItems, 100000);
    QCOMPARE(defaults.maximumStringSize, qsizetype(4) * 1024 * 1024);
    QCOMPARE(defaults.maximumNumberLength, 64);
    QCOMPARE(defaults.maximumIdLength, 256);
}

void McpTest::jsonLayerIds()
{
    using Codes = pdf::PDFMcpErrorCodes;

    auto id = [](const QByteArray& value)
    {
        return McpTestHelper::scan(R"({"jsonrpc":"2.0","method":"m","id":)" + value + "}");
    };

    // A string of at most 256 characters is echoed exactly
    pdf::PDFMcpJsonScanResult result = id("\"abc\"");
    QVERIFY(result.isValid() && result.hasId);
    QCOMPARE(result.id.getKind(), pdf::PDFMcpRequestIdKind::String);
    QCOMPARE(result.id.getString(), QStringLiteral("abc"));
    QCOMPARE(id("\"" + QByteArray(256, 'x') + "\"").id.getString().size(), 256);
    QCOMPARE(id("\"" + QByteArray(257, 'x') + "\"").errorCode, Codes::INVALID_REQUEST);
    QCOMPARE(id(R"("\u00e9\ud800")").id.getString(), QString(QChar(0xE9)) + QChar(0xD800));

    // Integers through qint64, never a double
    QCOMPARE(id("0").id.getInteger(), qint64(0));
    QCOMPARE(id("-0").id.getInteger(), qint64(0));
    QCOMPARE(id("9223372036854775807").id.getInteger(), std::numeric_limits<qint64>::max());
    QCOMPARE(id("-9223372036854775808").id.getInteger(), std::numeric_limits<qint64>::min());
    QCOMPARE(id("9007199254740993").id.getInteger(), qint64(9007199254740993));
    for (const char* invalid : { "9223372036854775808", "-9223372036854775809", "12345678901234567890", "1.0", "1.5", "1e3", "1E0", "-1e-1",
                                 "null", "true", "false", "{}", "[]", "[1]", "{\"a\":1}" })
    {
        const pdf::PDFMcpJsonScanResult invalidResult = id(invalid);
        QVERIFY2(invalidResult.errorCode == Codes::INVALID_REQUEST, invalid);
        QVERIFY2(!invalidResult.id.isValid(), invalid);
    }

    // The id of a nested object or of an array is no id; a notification has none
    QVERIFY(!McpTestHelper::scan(R"({"params":{"id":1}})").hasId);
    QVERIFY(!McpTestHelper::scan(R"([{"id":1}])").hasId);
    QVERIFY(!McpTestHelper::scan(R"({"method":"notifications/initialized"})").hasId);

    // A syntax error later in the document wins (no id is reported)
    const pdf::PDFMcpJsonScanResult broken = McpTestHelper::scan(R"({"id":5,"x":})");
    QCOMPARE(broken.errorCode, Codes::PARSE_ERROR);
    QVERIFY(!broken.id.isValid());

    // The writer echoes the ids
    pdf::PDFMcpJsonWriter writer;
    writer.beginArray();
    pdf::PDFMcpRequestId::createString(QString(QChar(0xD800)) + QStringLiteral("\"x")).write(writer);
    pdf::PDFMcpRequestId::createInteger(std::numeric_limits<qint64>::min()).write(writer);
    pdf::PDFMcpRequestId::createInvalid().write(writer);
    writer.endArray();
    QCOMPARE(writer.getData(), QByteArray("[\"\\ud800\\\"x\",-9223372036854775808,null]"));

    // Ids of the responses of a client
    QCOMPARE(pdf::PDFMcpRequestId::fromJson(QJsonValue(qint64(42))).getInteger(), qint64(42));
    QVERIFY(!pdf::PDFMcpRequestId::fromJson(QJsonValue(1.5)).isValid());
    QVERIFY(!pdf::PDFMcpRequestId::fromJson(QJsonValue(QJsonValue::Null)).isValid());
    QVERIFY(pdf::PDFMcpRequestId::fromJson(QJsonValue(QJsonValue::Undefined)).isNone());
}

void McpTest::jsonLayerDifferentialFuzz()
{
    // Whatever the scanner accepts, QJsonDocument must parse to the same
    // structure (plan 2.2). Documents with a lone surrogate are accepted by the
    // scanner; when Qt refuses them, the dispatcher answers -32700 by the result
    // of Qt (counted and printed).
    QRandomGenerator random(20261026);
    int accepted = 0;
    int refused = 0;
    int qtRefusedSurrogates = 0;
    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i < 100000; ++i)
    {
        // Qt parses only an object or an array at the top level
        QByteArray text = (i % 2) ? "[" + McpTestHelper::randomJson(random, 1) + "]" : "{\"id\":" + McpTestHelper::randomJson(random, 1) + "}";
        if (random.bounded(2))
        {
            text = McpTestHelper::mutate(random, text);
        }

        const pdf::PDFMcpJsonScanResult result = McpTestHelper::scan(text);
        if (!result.isValid())
        {
            ++refused;
            continue;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(text, &error);
        if (error.error != QJsonParseError::NoError)
        {
            QVERIFY2(result.hasLoneSurrogate || result.topLevel == pdf::PDFMcpJsonScanResult::TopLevel::Other || (text.contains("\u0000") && false),
                     qPrintable(QStringLiteral("Qt refused an accepted document: %1 (%2)").arg(QString::fromLatin1(text.toHex()), error.errorString())));
            ++qtRefusedSurrogates;
            continue;
        }

        ++accepted;
        McpTestHelper::Statistics statistics;
        const QJsonValue root = document.isObject() ? QJsonValue(document.object()) : QJsonValue(document.array());
        if (!document.isObject() && !document.isArray())
        {
            continue;
        }
        McpTestHelper::collect(root, 0, &statistics);
        QCOMPARE(statistics.objects, result.objectCount);
        QCOMPARE(statistics.arrays, result.arrayCount);
        QCOMPARE(statistics.members, result.memberCount);
        QCOMPARE(statistics.strings, result.stringCount);
        QCOMPARE(statistics.numbers, result.numberCount);
        QCOMPARE(statistics.literals, result.literalCount);
        QCOMPARE(statistics.maximumDepth, result.maximumDepth);
    }

    qInfo("Differential fuzz: %d accepted and equal, %d refused by the scanner, %d accepted by the scanner but refused by Qt, %lld ms",
          accepted, refused, qtRefusedSurrogates, timer.elapsed());
    QVERIFY(accepted > 1000);
    QVERIFY(refused > 1000);
}

void McpTest::jsonWriter()
{
    pdf::PDFMcpJsonWriter writer;
    writer.beginObject();
    writer.writeMember(QLatin1StringView("z"), 1);
    writer.writeMember(QLatin1StringView("a"), QLatin1StringView("x"));
    writer.key(QLatin1StringView("list"));
    writer.beginArray();
    writer.writeInteger(std::numeric_limits<qint64>::max());
    writer.writeNumber(0.1);
    writer.writeNumber(std::numeric_limits<double>::quiet_NaN());
    writer.writeNumber(1e300);
    writer.writeBool(false);
    writer.writeNull();
    writer.writeString(QString::fromUtf8("\"\\\n\x01\x1f/\xc5\xa1") + QChar(0xDC00));
    writer.endArray();
    writer.writeRawMembers("\"raw\":[1]");
    writer.key(QLatin1StringView("qt"));
    writer.writeQJson(QJsonObject({ { QStringLiteral("b"), 2 }, { QStringLiteral("a"), 1.5 }, { QStringLiteral("c"), QJsonValue(qint64(9007199254740993)) } }));
    writer.endObject();
    QVERIFY(writer.isComplete());

    // Insertion order kept, exact integers, shortest doubles, RFC 8259 escapes
    QCOMPARE(writer.getData(), QByteArray("{\"z\":1,\"a\":\"x\",\"list\":[9223372036854775807,0.1,null,1e+300,false,null,\"\\\"\\\\\\n\\u0001\\u001f/\xc5\xa1\\udc00\"],"
                                          "\"raw\":[1],\"qt\":{\"a\":1.5,\"b\":2,\"c\":9007199254740993}}"));
    QVERIFY(QJsonDocument::fromJson(writer.getData()).isObject());

    // Doubles round trip
    QRandomGenerator random(7);
    for (int i = 0; i < 2000; ++i)
    {
        const double value = (random.generateDouble() - 0.5) * std::pow(10.0, int(random.bounded(40)) - 20);
        bool ok = false;
        QCOMPARE(QString::fromLatin1(pdf::PDFMcpJsonWriter::formatNumber(value)).toDouble(&ok), value);
        QVERIFY(ok);
    }

    // Base64 encoded directly into the output equals Qt
    for (int size = 0; size < 300; ++size)
    {
        QByteArray data(size, '\0');
        for (int i = 0; i < size; ++i)
        {
            data[i] = char(random.bounded(256));
        }
        pdf::PDFMcpJsonWriter base64;
        base64.writeBase64(data);
        QCOMPARE(base64.getData(), "\"" + data.toBase64() + "\"");
    }

    // Base64url of the cursors and the sealed states
    const QByteArray bytes("\xfb\xff\x01", 3);
    QCOMPARE(pdf::PDFMcpJson::toBase64Url(bytes), QByteArray("-_8B"));
    QCOMPARE(*pdf::PDFMcpJson::fromBase64Url("-_8B"), bytes);
    QVERIFY(!pdf::PDFMcpJson::fromBase64Url("a+b/").has_value());
    QVERIFY(!pdf::PDFMcpJson::fromBase64Url("abcde").has_value());
    QVERIFY(!pdf::PDFMcpJson::fromBase64Url("ab=").has_value());
}

void McpTest::jsonSanitizer()
{
    using Flag = pdf::PDFMcpJson::SanitizeFlag;

    const QString hostile = QStringLiteral("a") + QChar(0) + QChar(0xD800) + QStringLiteral("b") + QChar(0x202E) + QStringLiteral("c\r\nd\te") + QChar(0x85) + QChar(0x2066) + QStringLiteral("f");
    QCOMPARE(pdf::PDFMcpJson::sanitizeString(hostile, Flag::RemoveBidi), QStringLiteral("a\uFFFDbc  d ef"));
    QCOMPARE(pdf::PDFMcpJson::sanitizeString(hostile, Flag::RemoveBidi | Flag::KeepNewlines), QStringLiteral("a\uFFFDbc\nd\tef"));
    QCOMPARE(pdf::PDFMcpJson::sanitizeString(hostile, Flag::None), QStringLiteral("a\uFFFDb") + QChar(0x202E) + QStringLiteral("c  d e") + QChar(0x2066) + QStringLiteral("f"));
    QCOMPARE(pdf::PDFMcpJson::sanitizeString(QStringLiteral("  x  "), Flag::Trim), QStringLiteral("x"));

    // A pair is never cut by the limit; a lone low surrogate is replaced
    const QString emoji = QStringLiteral("ab") + QString::fromUcs4(U"\U0001F600");
    QCOMPARE(pdf::PDFMcpJson::sanitizeString(emoji, Flag::None, 3), QStringLiteral("ab"));
    QCOMPARE(pdf::PDFMcpJson::sanitizeString(emoji, Flag::None, 4), emoji);
    QCOMPARE(pdf::PDFMcpJson::sanitizeString(QString(QChar(0xDC00)), Flag::None), QStringLiteral("\uFFFD"));
    QVERIFY(pdf::PDFMcpJson::isBidiControl(0x200F));
    QVERIFY(!pdf::PDFMcpJson::isBidiControl(0x200D));

    // The canonical arguments sort the keys
    QCOMPARE(pdf::PDFMcpJson::toCompactJson(QJsonObject({ { QStringLiteral("b"), 1 }, { QStringLiteral("a"), 2 } })), QByteArray(R"({"a":2,"b":1})"));
}

void McpTest::jsonReader()
{
    const QJsonObject object = QJsonDocument::fromJson(R"({"s":"a\u202eb","n":1.5,"i":2.0,"big":9007199254740993,"f":3,"b":true,"e":"x",
                                                         "arr":[1,2,3],"r":{"left":1,"bottom":2,"right":0,"top":4},"bad":{"left":1},
                                                         "q":[1,2,3,4,5,6,7,8],"o":{"x":1,"y":"no"}})").object();
    QVERIFY(!object.isEmpty());

    pdf::PDFMcpJsonReader reader(object, QStringLiteral("arguments"));
    QCOMPARE(*reader.getString(QLatin1StringView("s"), 10, true), QStringLiteral("ab"));
    QVERIFY(!reader.getInteger(QLatin1StringView("n"), 0, 10, true));
    QCOMPARE(*reader.getInteger(QLatin1StringView("i"), 0, 10, true), qint64(2));
    QCOMPARE(*reader.getInteger(QLatin1StringView("big"), 0, std::numeric_limits<qint64>::max(), true), qint64(9007199254740993));
    QVERIFY(!reader.getInteger(QLatin1StringView("f"), 4, 10, true));
    QCOMPARE(*reader.getNumber(QLatin1StringView("n"), 0.0, 2.0, true), 1.5);
    QCOMPARE(*reader.getBool(QLatin1StringView("b"), true), true);
    QVERIFY(!reader.getEnum(QLatin1StringView("e"), { QStringLiteral("y") }, true));
    QVERIFY(!reader.getArray(QLatin1StringView("arr"), 2, true));
    QCOMPARE(reader.getArray(QLatin1StringView("arr"), 3, true)->size(), 3);
    QCOMPARE(*reader.getRect(QLatin1StringView("r"), true), QRectF(0, 2, 1, 2));
    QVERIFY(!reader.getRect(QLatin1StringView("bad"), true));
    QVERIFY(reader.getQuad(object.value(QStringLiteral("q")), QStringLiteral("arguments.q")).has_value());
    QVERIFY(!reader.getQuad(object.value(QStringLiteral("arr")), QStringLiteral("arguments.arr")).has_value());
    QVERIFY(!reader.getString(QLatin1StringView("missing"), 10, false));
    QVERIFY(!reader.getString(QLatin1StringView("required"), 10, true));

    pdf::PDFMcpJsonReader child = reader.child(QLatin1StringView("o"), true);
    QVERIFY(!child.getInteger(QLatin1StringView("y"), 0, 1, true));
    child.checkUnknownKeys({ QStringLiteral("x") });

    QVERIFY(reader.hasErrors());
    const QString errors = reader.getErrorText();
    QVERIFY2(errors.contains(QStringLiteral("arguments.n: expected an integer")), qPrintable(errors));
    QVERIFY2(errors.contains(QStringLiteral("arguments.required: is required")), qPrintable(errors));
    QVERIFY2(errors.contains(QStringLiteral("arguments.bad.bottom")), qPrintable(errors));
    QVERIFY2(errors.contains(QStringLiteral("arguments.o.y: expected an integer")), qPrintable(errors));
    QVERIFY2(errors.contains(QStringLiteral("arguments.o.y: unknown property")), qPrintable(errors));
    QVERIFY(reader.getErrors().size() <= pdf::PDFMcpJsonReader::MAXIMUM_ERRORS);
}

void McpTest::schemaBuilder()
{
    using B = pdf::PDFMcpSchemaBuilder;

    pdf::PDFMcpSchema schema = B::object(QStringLiteral("Root"));
    schema.addProperty(QStringLiteral("action"), B::enumeration(QStringLiteral("The action"), { QStringLiteral("b"), QStringLiteral("a") }), true);
    schema.addProperty(QStringLiteral("count"), B::integer(QStringLiteral("Count"), 1, 10).setDefault(5));
    schema.addProperty(QStringLiteral("ratio"), B::number(QString(), 0.0, 1.0));
    schema.addProperty(QStringLiteral("items"), B::array(QString(), B::string(QString(), 3), 1, 2));
    pdf::PDFMcpSchema nested = B::object();
    nested.addProperty(QStringLiteral("flag"), B::boolean(QString()), true);
    schema.addProperty(QStringLiteral("nested"), nested);

    const QByteArray json = schema.toJson();
    QCOMPARE(json, QByteArray(R"({"type":"object","description":"Root","properties":{"action":{"type":"string","description":"The action","enum":["b","a"]},)"
                              R"("count":{"type":"integer","description":"Count","minimum":1,"maximum":10,"default":5},"ratio":{"type":"number","minimum":0,"maximum":1},)"
                              R"("items":{"type":"array","items":{"type":"string","maxLength":3},"minItems":1,"maxItems":2},)"
                              R"("nested":{"type":"object","properties":{"flag":{"type":"boolean"}},"required":["flag"],"additionalProperties":false}},)"
                              R"("required":["action"],"additionalProperties":false})"));
    QCOMPARE(schema.toJson(), json);

    // Validation with the paths of the errors
    QStringList errors;
    QVERIFY(schema.validate(QJsonDocument::fromJson(R"({"action":"a","count":2.0,"items":["x"],"nested":{"flag":true}})").object(), QStringLiteral("arguments"), &errors));
    QVERIFY(errors.isEmpty());
    QVERIFY(!schema.validate(QJsonDocument::fromJson(R"({"action":"c","count":1.5,"ratio":2,"items":["long",1,"x"],"nested":{},"extra":1})").object(), QStringLiteral("arguments"), &errors, 16));
    const QString text = errors.join(QLatin1Char('\n'));
    QVERIFY2(text.contains(QStringLiteral("arguments.action: must be one of: b, a")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("arguments.count: expected an integer")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("arguments.ratio: must be from 0 to 1")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("arguments.items: has more than 2 items")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("arguments.nested.flag: is required")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("arguments.extra: unknown property")), qPrintable(text));
    QVERIFY(!schema.validate(QJsonValue(QStringLiteral("x")), QString(), &errors));

    // Code points count for maxLength; uniqueItems and patterns of the core schemas
    errors.clear();
    QVERIFY(B::string(QString(), 2).validate(QJsonValue(QString::fromUcs4(U"\U0001F600\U0001F600")), QStringLiteral("s"), &errors));
    pdf::PDFMcpSchema unique = B::array(QString(), B::string(QString()));
    unique.setUniqueItems(true);
    QVERIFY(!unique.validate(QJsonArray({ QStringLiteral("a"), QStringLiteral("a") }), QStringLiteral("u"), &errors));
    pdf::PDFMcpSchema color = B::string(QString());
    color.setPattern(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    QVERIFY(color.validate(QJsonValue(QStringLiteral("#00ff00")), QString(), &errors));
    QVERIFY(!color.validate(QJsonValue(QStringLiteral("green")), QString(), &errors));

    // fromQJson: only the allowed keywords and a single type
    QString error;
    QVERIFY(pdf::PDFMcpSchema::fromQJson(schema.toQJson(), &error).has_value());
    QCOMPARE(pdf::PDFMcpSchema::fromQJson(schema.toQJson(), &error)->toQJson(), schema.toQJson());
    for (const char* bad : { R"({"type":"object","oneOf":[]})", R"({"type":["string","null"]})", R"({"type":"object","$ref":"#"})", R"({"type":"string","const":"x"})",
                             R"({"type":"widget"})", R"({"type":"object","properties":{"a":{"type":"string","anyOf":[]}}})" })
    {
        QVERIFY2(!pdf::PDFMcpSchema::fromQJson(QJsonDocument::fromJson(bad).object(), &error).has_value(), bad);
    }

    const QStringList allowed = pdf::PDFMcpSchema::getAllowedKeywords();
    for (const char* keyword : { "oneOf", "anyOf", "allOf", "$ref", "const", "not", "if", "$defs" })
    {
        QVERIFY(!allowed.contains(QString::fromLatin1(keyword)));
    }
}

void McpTest::schemaUniqueItems()
{
    // Review RW7-0: uniqueItems of an array without maxItems (the page filters of
    // the core schemas) was a pairwise comparison in the GUI thread of the host -
    // 100 000 items of a request of 1 MB took minutes
    using B = pdf::PDFMcpSchemaBuilder;
    pdf::PDFMcpSchema unique = B::array(QString(), B::string(QString()));
    unique.setUniqueItems(true);

    QJsonArray distinct;
    QJsonArray invalid;
    for (int i = 0; i < 100000; ++i)
    {
        distinct.append(QStringLiteral("s%1").arg(i));
        invalid.append(i);
    }

    QElapsedTimer timer;
    timer.start();
    QStringList errors;
    QVERIFY(unique.validate(distinct, QStringLiteral("u"), &errors));
    QVERIFY(errors.isEmpty());
    QVERIFY(!unique.validate(invalid, QStringLiteral("u"), &errors, 4));
    QCOMPARE(errors.size(), 4);
    const qint64 elapsed = timer.elapsed();
    qInfo("uniqueItems of 2 x 100 000 items: %lld ms", elapsed);
    QVERIFY2(elapsed < qint64(5000) * pdftest::McpTestClient::getTimeoutFactor(), qPrintable(QString::number(elapsed)));

    // Equality of JSON Schema: the last item equal to the first one, numbers by
    // their value, objects without the order of the keys
    distinct.append(QStringLiteral("s0"));
    errors.clear();
    QVERIFY(!unique.validate(distinct, QStringLiteral("u"), &errors));
    QCOMPARE(errors, QStringList({ QStringLiteral("u: has duplicate items") }));

    pdf::PDFMcpSchema any = B::array(QString(), B::object(QString(), false));
    any.setUniqueItems(true);
    QVERIFY(!any.validate(QJsonDocument::fromJson(R"([{"a":1,"b":[2,3]},{"c":1},{"b":[2,3],"a":1.0}])").array(), QStringLiteral("o"), &errors));
    QVERIFY(any.validate(QJsonDocument::fromJson(R"([{"a":1,"b":[2,3]},{"a":1,"b":[3,2]},{"a":"1"}])").array(), QStringLiteral("o"), &errors));
    pdf::PDFMcpSchema numbers = B::array(QString(), B::number(QString(), -10.0, 10.0));
    numbers.setUniqueItems(true);
    QVERIFY(!numbers.validate(QJsonDocument::fromJson("[1,2,1.0]").array(), QStringLiteral("n"), &errors));
}

void McpTest::requestState()
{
    const pdf::PDFMcpRequestState state(QByteArray("0123456789abcdef0123456789abcdef"));
    const QJsonObject arguments({ { QStringLiteral("action"), QStringLiteral("get_text") }, { QStringLiteral("pages"), QStringLiteral("1-3") } });

    pdf::PDFMcpRequestStateData data;
    data.assistantKey = QStringLiteral("a:0123456789abcdef0123456789abcdef");
    data.method = QStringLiteral("tools/call");
    data.name = QStringLiteral("text");
    data.argumentsHash = pdf::PDFMcpRequestState::hashArguments(arguments);
    data.allowedValues = QJsonObject({ { QStringLiteral("documentId"), QJsonArray({ QStringLiteral("x"), QStringLiteral("y") }) } });
    data.expiresAt = 1000000;
    data.nonce = pdf::PDFMcpRequestState::createNonce();
    QCOMPARE(data.nonce.size(), 16);

    const QByteArray sealed = state.seal(data);
    QVERIFY(!sealed.isEmpty());
    QVERIFY(sealed.size() <= pdf::PDFMcpLimits::MAXIMUM_REQUEST_STATE_SIZE);

    pdf::PDFMcpRequestStateData unsealed;
    QCOMPARE(state.unseal(sealed, 999999, data.assistantKey, data.method, data.name, data.argumentsHash, &unsealed), pdf::PDFMcpRequestStateResult::Valid);
    QCOMPARE(unsealed.allowedValues, data.allowedValues);
    QCOMPARE(unsealed.nonce, data.nonce);

    // The order of the keys does not change the hash of the arguments
    QCOMPARE(pdf::PDFMcpRequestState::hashArguments(QJsonDocument::fromJson(R"({"pages":"1-3","action":"get_text"})").object()), data.argumentsHash);

    using Result = pdf::PDFMcpRequestStateResult;
    QCOMPARE(state.unseal(sealed, 1000001, data.assistantKey, data.method, data.name, data.argumentsHash, nullptr), Result::Expired);
    QCOMPARE(state.unseal(sealed, 0, QStringLiteral("a:ffff"), data.method, data.name, data.argumentsHash, nullptr), Result::WrongBinding);
    QCOMPARE(state.unseal(sealed, 0, data.assistantKey, QStringLiteral("prompts/get"), data.name, data.argumentsHash, nullptr), Result::WrongBinding);
    QCOMPARE(state.unseal(sealed, 0, data.assistantKey, data.method, QStringLiteral("render"), data.argumentsHash, nullptr), Result::WrongBinding);
    QCOMPARE(state.unseal(sealed, 0, data.assistantKey, data.method, data.name, pdf::PDFMcpRequestState::hashArguments(QJsonObject()), nullptr), Result::WrongBinding);

    // Tampering: every changed character of the payload or the signature, another key
    static const QByteArray ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    for (qsizetype i = 0; i < sealed.size(); i += 7)
    {
        // The highest bit of the 6 bits of a character is always used
        const qsizetype value = ALPHABET.indexOf(sealed[i]);
        if (value < 0)
        {
            continue;
        }
        QByteArray tampered = sealed;
        tampered[i] = ALPHABET[value ^ 0x20];
        QVERIFY(state.unseal(tampered, 0, data.assistantKey, data.method, data.name, data.argumentsHash, nullptr) != Result::Valid);
    }
    const pdf::PDFMcpRequestState other;
    QCOMPARE(other.unseal(sealed, 0, data.assistantKey, data.method, data.name, data.argumentsHash, nullptr), Result::BadSignature);
    for (const char* malformed : { "", ".", "abc", "a.b.c", "!!!.???", "abc." })
    {
        QVERIFY(state.unseal(QByteArray(malformed), 0, data.assistantKey, data.method, data.name, data.argumentsHash, nullptr) != Result::Valid);
    }
    QCOMPARE(state.unseal(QByteArray(5000, 'a'), 0, data.assistantKey, data.method, data.name, data.argumentsHash, nullptr), Result::Malformed);

    // Too big data cannot be sealed
    pdf::PDFMcpRequestStateData big = data;
    big.allowedValues.insert(QStringLiteral("x"), QString(4000, QLatin1Char('x')));
    QVERIFY(state.seal(big).isEmpty());

    QVERIFY(pdf::PDFMcpRequestState::isEqualConstantTime("abc", "abc"));
    QVERIFY(!pdf::PDFMcpRequestState::isEqualConstantTime("abc", "abd"));
    QVERIFY(!pdf::PDFMcpRequestState::isEqualConstantTime("abc", "ab"));
}

void McpTest::uriTemplate()
{
    const pdf::PDFMcpUriTemplate image(QStringLiteral("pdf4qt://documents/{documentId}/pages/{page}/image/{dpi}"));
    QVERIFY(image.isValid());
    QCOMPARE(image.getVariables(), QStringList({ QStringLiteral("documentId"), QStringLiteral("page"), QStringLiteral("dpi") }));

    const std::optional<QMap<QString, QString>> values = image.match(QStringLiteral("pdf4qt://documents/abc/pages/12/image/150"));
    QVERIFY(values.has_value());
    QCOMPARE(values->value(QStringLiteral("documentId")), QStringLiteral("abc"));
    QCOMPARE(values->value(QStringLiteral("page")), QStringLiteral("12"));
    QCOMPARE(values->value(QStringLiteral("dpi")), QStringLiteral("150"));
    QCOMPARE(image.expand(*values), QStringLiteral("pdf4qt://documents/abc/pages/12/image/150"));

    for (const char* uri : { "pdf4qt://documents//pages/1/image/72", "pdf4qt://documents/a/b/pages/1/image/72", "pdf4qt://documents/a/pages/1/image/72/x",
                             "pdf4qt://documents/a/pages/1/image/", "pdf4qt://documents/a%2Fb/pages/1/image/72", "pdf4qt://documents/a%00/pages/1/image/72",
                             "pdf4qt://documents/a%G1/pages/1/image/72", "pdf4qt://documents/%ff/pages/1/image/72", "other://documents/a/pages/1/image/72" })
    {
        QVERIFY2(!image.match(QString::fromLatin1(uri)).has_value(), uri);
    }
    QCOMPARE(image.match(QStringLiteral("pdf4qt://documents/a%20b/pages/1/image/72"))->value(QStringLiteral("documentId")), QStringLiteral("a b"));
    QVERIFY(!image.match(QString(3000, QLatin1Char('a'))).has_value());

    const pdf::PDFMcpUriTemplate help(QStringLiteral("pdf4qt://help/{topic}.md"));
    QCOMPARE(help.match(QStringLiteral("pdf4qt://help/ai-tools.md"))->value(QStringLiteral("topic")), QStringLiteral("ai-tools"));
    QCOMPARE(help.expand({ { QStringLiteral("topic"), QStringLiteral("a b/c") } }), QStringLiteral("pdf4qt://help/a%20b%2Fc.md"));

    for (const char* invalid : { "a{", "a}", "{}", "{a}{b}", "{+a}", "{a b}", "" })
    {
        QVERIFY2(!pdf::PDFMcpUriTemplate(QString::fromLatin1(invalid)).isValid(), invalid);
    }
}

void McpTest::callLifecycle()
{
    pdftest::FakeMcpHost host;

    pdf::PDFMcpCallRequest request;
    request.kind = pdf::PDFMcpCallKind::ToolCall;
    request.name = QStringLiteral("text");
    request.client.hasProgressToken = true;
    request.client.logLevel = QStringLiteral("warning");
    std::shared_ptr<pdf::PDFMcpCall> call = pdf::PDFMcpCall::create(request);
    call->setHost(&host);
    QVERIFY(call->getCallId().startsWith(QLatin1Char('c')));
    QVERIFY(!call->isOperationCancelled());

    // Progress grows strictly; logs below the requested level are dropped
    call->reportProgress(1, 10, QStringLiteral("one"));
    call->reportProgress(1, 10, QStringLiteral("again"));
    call->reportProgress(0.5, 10, QStringLiteral("back"));
    call->reportProgress(std::numeric_limits<double>::quiet_NaN(), 10, QString());
    call->reportProgress(2, 0, QStringLiteral("two"));
    call->log(QStringLiteral("info"), QStringLiteral("hidden"));
    call->log(QStringLiteral("error"), QStringLiteral("shown"));
    call->log(QStringLiteral("unknown"), QStringLiteral("hidden"));

    std::vector<pdf::PDFMcpCallEvent> events = call->takeEvents();
    QCOMPARE(events.size(), size_t(3));
    QCOMPARE(events[0].progress, 1.0);
    QCOMPARE(events[1].progress, 2.0);
    QCOMPARE(events[2].type, pdf::PDFMcpCallEvent::Type::Log);
    QCOMPARE(events[2].message, QStringLiteral("shown"));

    // The first finish wins; nothing is sent after it
    QVERIFY(call->finish(pdf::PDFMcpCallResult::createToolError(QStringLiteral("first"))));
    QVERIFY(!call->finish(pdf::PDFMcpCallResult::createToolError(QStringLiteral("second"))));
    call->reportProgress(5, 10, QString());
    events = call->takeEvents();
    QCOMPARE(events.size(), size_t(1));
    QCOMPARE(events[0].type, pdf::PDFMcpCallEvent::Type::Finished);
    QVERIFY(call->getResult()->isError);
    QCOMPARE(call->getResult()->getFirstText(), QStringLiteral("first"));

    // Without a progress token nothing is sent; the queue is bounded
    pdf::PDFMcpCallRequest plain;
    std::shared_ptr<pdf::PDFMcpCall> quiet = pdf::PDFMcpCall::create(plain);
    quiet->reportProgress(1, 2, QString());
    QVERIFY(quiet->takeEvents().empty());
    QCOMPARE(quiet->getLastProgress(), 1.0);

    std::shared_ptr<pdf::PDFMcpCall> busy = pdf::PDFMcpCall::create(request);
    for (int i = 1; i <= 10000; ++i)
    {
        busy->reportProgress(i, 0, QString());
    }
    QCOMPARE(busy->takeEvents().size(), pdf::PDFMcpCall::MAXIMUM_PENDING_EVENTS);

    // Cancellation: the first reason wins, the host is told from the event loop
    std::shared_ptr<pdf::PDFMcpCall> canceled = pdf::PDFMcpCall::create(request);
    canceled->setHost(&host);
    canceled->cancel(pdf::PDFMcpCancelReason::StreamClosed);
    canceled->cancel(pdf::PDFMcpCancelReason::ServerStopped);
    QVERIFY(canceled->isCanceled());
    QVERIFY(pdf::PDFOperationControl::isOperationCancelled(canceled.get()));
    QCOMPARE(canceled->getCancelReason(), pdf::PDFMcpCancelReason::StreamClosed);
    QVERIFY(host.canceledCalls.empty());
    QTRY_COMPARE(host.canceledCalls.size(), size_t(1));

    // Calls from many threads at once
    std::shared_ptr<pdf::PDFMcpCall> shared = pdf::PDFMcpCall::create(request);
    std::vector<std::unique_ptr<QThread>> threads;
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back(QThread::create([shared, t]()
        {
            for (int i = 0; i < 500; ++i)
            {
                shared->reportProgress(t * 1000 + i, 0, QString());
                shared->log(QStringLiteral("error"), QStringLiteral("x"));
            }
            shared->finish(pdf::PDFMcpCallResult::createComplete("{}"));
        }));
        threads.back()->start();
    }
    for (auto& thread : threads)
    {
        thread->wait();
    }
    QVERIFY(shared->isFinished());

    // The client capabilities
    pdf::PDFMcpClientContext client;
    QVERIFY(!client.supportsElicitation());
    client.clientCapabilities = QJsonDocument::fromJson(R"({"elicitation":{},"extensions":{"io.modelcontextprotocol/tasks":{}}})").object();
    QVERIFY(client.supportsElicitation());
    QVERIFY(client.supportsTasks());
    client.era = pdf::PDFMcpEra::Legacy20251125;
    QVERIFY(!client.supportsTasks());
    client.clientCapabilities = QJsonDocument::fromJson(R"({"elicitation":{"url":{}}})").object();
    QVERIFY(!client.supportsElicitation());

    QCOMPARE(pdf::PDFMcpProtocol::getEra(u"2025-06-18"), std::optional<pdf::PDFMcpEra>(pdf::PDFMcpEra::Legacy20250618));
    QVERIFY(!pdf::PDFMcpProtocol::getEra(u"2024-11-05").has_value());
    QCOMPARE(pdf::PDFMcpProtocol::getSupportedVersions().front(), QStringLiteral("2026-07-28"));
}

void McpTest::catalogSerialization()
{
    pdf::PDFMcpIcon icon;
    icon.source = QStringLiteral("data:image/png;base64,AAAA");
    icon.sizes = QStringList({ QStringLiteral("48x48") });
    icon.theme = QStringLiteral("light");

    pdf::PDFMcpToolDefinition tool;
    tool.name = QStringLiteral("text");
    tool.title = QStringLiteral("Document text");
    tool.description = QStringLiteral("Read text");
    tool.inputSchema = R"({"type":"object","properties":{},"additionalProperties":false})";
    tool.outputSchema = R"({"type":"object","properties":{}})";
    tool.annotations.readOnlyHint = true;
    tool.icons.push_back(icon);

    pdf::PDFMcpResourceDefinition resource;
    resource.uri = QStringLiteral("pdf4qt://docs/index.md");
    resource.name = QStringLiteral("index.md");
    resource.mimeType = QStringLiteral("text/markdown");
    resource.size = 10;
    resource.isStatic = true;
    resource.content = "# Index";

    pdf::PDFMcpResourceTemplateDefinition resourceTemplate;
    resourceTemplate.uriTemplate = pdf::PDFMcpUriTemplate(QStringLiteral("pdf4qt://help/{topic}.md"));
    resourceTemplate.name = QStringLiteral("help");

    pdf::PDFMcpPromptDefinition prompt;
    prompt.name = QStringLiteral("summarize-document");
    prompt.arguments.push_back({ QStringLiteral("document"), QString(), QStringLiteral("Document"), false });

    const pdf::PDFMcpCatalog catalog(7, { tool }, { resource }, { resourceTemplate }, { prompt });
    QCOMPARE(catalog.getGeneration(), quint64(7));
    QVERIFY(catalog.findTool(u"text"));
    QVERIFY(!catalog.findTool(u"other"));
    QVERIFY(catalog.findResource(u"pdf4qt://docs/index.md"));
    QVERIFY(catalog.findPrompt(u"summarize-document"));
    QMap<QString, QString> values;
    QVERIFY(catalog.matchTemplate(QStringLiteral("pdf4qt://help/x.md"), &values));
    QCOMPARE(values.value(QStringLiteral("topic")), QStringLiteral("x"));

    // Icons only for 2025-11-25 and newer; the lists are valid JSON in every era
    QVERIFY(catalog.getToolsJson(pdf::PDFMcpEra::Modern).contains("\"icons\""));
    QVERIFY(catalog.getToolsJson(pdf::PDFMcpEra::Legacy20251125).contains("\"icons\""));
    QVERIFY(!catalog.getToolsJson(pdf::PDFMcpEra::Legacy20250618).contains("\"icons\""));
    for (pdf::PDFMcpEra era : { pdf::PDFMcpEra::Modern, pdf::PDFMcpEra::Legacy20251125, pdf::PDFMcpEra::Legacy20250618 })
    {
        for (const QByteArray& json : { catalog.getToolsJson(era), catalog.getResourcesJson(era), catalog.getTemplatesJson(era), catalog.getPromptsJson(era) })
        {
            QVERIFY(QJsonDocument::fromJson(json).isArray());
            QCOMPARE(QJsonDocument::fromJson(json).array().size(), 1);
        }
    }
    const QJsonObject toolJson = QJsonDocument::fromJson(catalog.getToolsJson(pdf::PDFMcpEra::Modern)).array().first().toObject();
    QCOMPARE(toolJson.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("readOnlyHint")).toBool(), true);
    QCOMPARE(toolJson.value(QStringLiteral("annotations")).toObject().value(QStringLiteral("openWorldHint")).toBool(), false);
    QCOMPARE(toolJson.value(QStringLiteral("inputSchema")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("object"));

    // The content of a tool result
    pdf::PDFMcpContentBuilder builder;
    builder.addText(QStringLiteral("hello"));
    builder.addImage(QByteArray("\x89PNG", 4), QStringLiteral("image/png"));
    builder.addResourceLink(QStringLiteral("pdf4qt://documents/x/info"), QStringLiteral("info"), QString(), QStringLiteral("application/json"));
    builder.addEmbeddedText(QStringLiteral("pdf4qt://a"), QStringLiteral("text/plain"), QStringLiteral("t"));
    builder.addEmbeddedBlob(QStringLiteral("pdf4qt://b"), QStringLiteral("application/octet-stream"), QByteArray("\x00\x01", 2));
    builder.setStructuredContent(R"({"action":"x"})");
    const pdf::PDFMcpCallResult result = builder.createCallResult(false);
    const QJsonObject object = result.toQJson();
    QCOMPARE(object.value(QStringLiteral("content")).toArray().size(), 5);
    QCOMPARE(object.value(QStringLiteral("content")).toArray().at(1).toObject().value(QStringLiteral("data")).toString(), QStringLiteral("iVBORw=="));
    QCOMPARE(object.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("action")).toString(), QStringLiteral("x"));
    QCOMPARE(object.value(QStringLiteral("isError")).toBool(), false);
    QCOMPARE(result.getFirstText(), QStringLiteral("hello"));

    // The builder is reusable
    builder.addText(QStringLiteral("again"));
    QCOMPARE(QJsonDocument::fromJson(builder.createResult(true)).object().value(QStringLiteral("content")).toArray().size(), 1);
}

void McpTest::assistantKeys()
{
    using Registry = pdf::PDFMcpAssistantRegistry;

    // [D2]: token + name + address; never the raw token
    const QString key = Registry::createModernKey("secret-token", QStringLiteral("claude-code"), QStringLiteral("127.0.0.1"));
    QVERIFY(Registry::isValidKey(key));
    QVERIFY(key.startsWith(QStringLiteral("a:")));
    QVERIFY(!key.contains(QStringLiteral("secret")));
    QCOMPARE(Registry::createModernKey("secret-token", QStringLiteral("claude-code"), QStringLiteral("127.0.0.1")), key);
    QVERIFY(Registry::createModernKey("other-token", QStringLiteral("claude-code"), QStringLiteral("127.0.0.1")) != key);
    QVERIFY(Registry::createModernKey("secret-token", QStringLiteral("codex-mcp-client"), QStringLiteral("127.0.0.1")) != key);
    QVERIFY(Registry::createModernKey("secret-token", QStringLiteral("claude-code"), QStringLiteral("::1")) != key);
    QCOMPARE(Registry::createModernKey(QByteArray(), QString(), QStringLiteral("::1")), Registry::createModernKey(QByteArray(), Registry::getDefaultClientName(), QStringLiteral("::1")));
    // The separators keep the parts apart
    QVERIFY(Registry::createModernKey("ab", QStringLiteral("c"), QString()) != Registry::createModernKey("a", QStringLiteral("bc"), QString()));

    const QString legacy = Registry::createLegacyKey("SESSIONID");
    QVERIFY(Registry::isValidKey(legacy));
    QVERIFY(legacy.startsWith(QStringLiteral("s:")));
    for (const char* invalid : { "", "a:", "x:0123456789abcdef0123456789abcdef", "a:0123456789ABCDEF0123456789abcdef", "a:0123456789abcdef0123456789abcde" })
    {
        QVERIFY2(!Registry::isValidKey(QString::fromLatin1(invalid)), invalid);
    }
}

void McpTest::portInUseProbe()
{
    pdftest::FakeMcpHost host;

    // A port of another PDF4QT: its answer has Server: PDF4QT/…
    pdf::PDFMcpServer first(&host);
    QVERIFY(first.start(McpTestHelper::createConfig()));
    QTRY_COMPARE_WITH_TIMEOUT(first.getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());

    pdf::PDFMcpServer second(&host);
    QVERIFY(second.start(McpTestHelper::createConfig(first.getPort())));
    QTRY_COMPARE_WITH_TIMEOUT(second.getState(), pdf::PDFMcpServerState::PortInUseByPdf4Qt, 5000 * McpTestHelper::factor());
    QVERIFY(!second.getErrorMessage().isEmpty());

    // The probe itself
    pdf::PDFMcpPortProbe probe;
    QSignalSpy probeSpy(&probe, &pdf::PDFMcpPortProbe::finished);
    probe.start(first.getPort());
    QTRY_COMPARE_WITH_TIMEOUT(probeSpy.count(), 1, 5000 * McpTestHelper::factor());
    QCOMPARE(probe.getResult(), pdf::PDFMcpPortProbe::Result::Pdf4Qt);
    QCOMPARE(probe.getServerHeader(), QByteArray("PDF4QT/9.8.7.6"));
    first.stop();

    // A foreign HTTP server
    QTcpServer foreign;
    QVERIFY(foreign.listen(QHostAddress::LocalHost, 0));
    QObject::connect(&foreign, &QTcpServer::newConnection, &foreign, [&foreign]()
    {
        while (QTcpSocket* socket = foreign.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]()
            {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nServer: nginx/1.25\r\nContent-Length: 0\r\n\r\n");
            });
        }
    });
    pdf::PDFMcpServer third(&host);
    QVERIFY(third.start(McpTestHelper::createConfig(foreign.serverPort())));
    QTRY_COMPARE_WITH_TIMEOUT(third.getState(), pdf::PDFMcpServerState::PortInUseByOtherProgram, 5000 * McpTestHelper::factor());

    // A listener, which never answers: the timeout of the probe
    QTcpServer silent;
    QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
    pdf::PDFMcpServerConfig config = McpTestHelper::createConfig(silent.serverPort());
    config.portProbeTimeout = 200;
    pdf::PDFMcpServer fourth(&host);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(fourth.start(config));
    QTRY_COMPARE_WITH_TIMEOUT(fourth.getState(), pdf::PDFMcpServerState::PortInUseByOtherProgram, 5000 * McpTestHelper::factor());
    QVERIFY(timer.elapsed() >= 150);

    // A port, where nothing listens on the loopback: Unreachable
    pdf::PDFMcpPortProbe unreachable;
    QSignalSpy unreachableSpy(&unreachable, &pdf::PDFMcpPortProbe::finished);
    QTcpServer closed;
    QVERIFY(closed.listen(QHostAddress::LocalHost, 0));
    const quint16 closedPort = closed.serverPort();
    closed.close();
    unreachable.start(closedPort, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(unreachableSpy.count(), 1, 5000 * McpTestHelper::factor());
    QVERIFY(unreachable.getResult() == pdf::PDFMcpPortProbe::Result::Unreachable || unreachable.getResult() == pdf::PDFMcpPortProbe::Result::OtherProgram);

    // A server in the state "port in use" starts again after the stop
    third.stop();
    QCOMPARE(third.getState(), pdf::PDFMcpServerState::Stopped);
    QVERIFY(third.start(McpTestHelper::createConfig()));
    QTRY_COMPARE_WITH_TIMEOUT(third.getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());
    third.stop();

    // The value of the Server header of PDF4QT
    QCOMPARE(pdf::PDFMcpHttpResponse::getServerHeader("1.0"), QByteArray("PDF4QT/1.0"));
}

// ----------------------------------------------------------------------------
// Package A: transport and protocol
// ----------------------------------------------------------------------------

void McpTest::serverStartStop()
{
    pdftest::FakeMcpHost host;
    pdf::PDFMcpServer server(&host);
    QSignalSpy stateSpy(&server, &pdf::PDFMcpServer::stateChanged);

    QVERIFY(server.start(McpServerHelper::createConfig()));
    QCOMPARE(server.getState(), pdf::PDFMcpServerState::Starting);
    QVERIFY(!server.start(McpServerHelper::createConfig()));
    QTRY_COMPARE_WITH_TIMEOUT(server.getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());
    QVERIFY(server.getPort() != 0);
    QVERIFY(stateSpy.count() >= 2);

    // Every response has the headers of PDF4QT
    pdftest::McpTestClient client(server.getPort());
    client.setToken(McpServerHelper::token());
    const QJsonObject discover = client.discover();
    const pdftest::McpTestResponse response = client.lastResponse();
    QCOMPARE(response.status, 200);
    QCOMPARE(response.header("Server"), QByteArray("PDF4QT/9.8.7.6"));
    QCOMPARE(response.header("Cache-Control"), QByteArray("no-store"));
    QCOMPARE(response.header("X-Content-Type-Options"), QByteArray("nosniff"));
    QVERIFY(discover.contains(QStringLiteral("supportedVersions")));

    // IPv6 loopback too (when the system has it)
    pdftest::McpTestClient ipv6(server.getPort(), pdftest::McpTestEra::Modern, QStringLiteral("::1"));
    ipv6.setToken(McpServerHelper::token());
    ipv6.discover();
    QVERIFY(ipv6.lastResponse().status == 200 || ipv6.lastResponse().status == 0);

    // Open connections do not block the stop
    QTcpSocket idle;
    idle.connectToHost(QHostAddress::LocalHost, server.getPort());
    QVERIFY(idle.waitForConnected(5000 * McpTestHelper::factor()));
    const quint16 port = server.getPort();
    server.stop();
    QCOMPARE(server.getState(), pdf::PDFMcpServerState::Stopped);
    QCOMPARE(server.getPort(), quint16(0));
    QCOMPARE(pdftest::McpTestClient(port).get().status, 0);

    // Start and stop again
    for (int i = 0; i < 3; ++i)
    {
        QVERIFY(server.start(McpServerHelper::createConfig()));
        QTRY_COMPARE_WITH_TIMEOUT(server.getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());
        server.stop();
    }

    // A stop while starting; a destruction while running
    QVERIFY(server.start(McpServerHelper::createConfig()));
    server.stop();
    QCOMPARE(server.getState(), pdf::PDFMcpServerState::Stopped);
    {
        pdf::PDFMcpServer running(&host);
        QVERIFY(running.start(McpServerHelper::createConfig()));
        QTRY_COMPARE_WITH_TIMEOUT(running.getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());
    }

    // Catalogs are published also to a stopped server; the stubs of a stopped server do nothing
    std::shared_ptr<const pdf::PDFMcpCatalog> catalog = std::make_shared<const pdf::PDFMcpCatalog>();
    server.publishCatalog(catalog);
    QCOMPARE(server.getCatalog(), catalog);
    server.publishCatalog(nullptr);
    QCOMPARE(server.getCatalog(), catalog);
    QVERIFY(server.getAssistants().empty());
    server.notifyResourceUpdated(QStringLiteral("pdf4qt://documents/X"));
    server.disconnectAssistant(QStringLiteral("a:0123456789abcdef0123456789abcdef"));
    server.disconnectAll();
    server.checkTimeouts();
}

void McpTest::httpParser()
{
    using Parser = pdf::PDFMcpHttpParser;

    // A complete request; names in lower case, values trimmed
    {
        Parser parser;
        QCOMPARE(parser.feed("\r\n\r\nPOST /mcp HTTP/1.1\r\nHost:  127.0.0.1:1 \r\nX-Test:\tvalue\t\r\nContent-Length: 4\r\n\r\nbody"), Parser::Status::RequestComplete);
        QCOMPARE(parser.getRequest().method, QByteArray("POST"));
        QCOMPARE(parser.getRequest().target, QByteArray("/mcp"));
        QCOMPARE(parser.getRequest().getHeader("host"), QByteArray("127.0.0.1:1"));
        QCOMPARE(parser.getRequest().getHeader("x-test"), QByteArray("value"));
        QCOMPARE(parser.getRequest().body, QByteArray("body"));
        QVERIFY(!parser.getRequest().isConnectionClose());
    }

    // Incremental, byte by byte, and two pipelined requests
    {
        const QByteArray request = "POST /mcp HTTP/1.1\r\nHost: h\r\nConnection: keep-alive, Close\r\nContent-Length: 2\r\n\r\n{}";
        Parser parser;
        for (qsizetype i = 0; i < request.size() - 1; ++i)
        {
            QCOMPARE(parser.feed(request.mid(i, 1)), Parser::Status::NeedMoreData);
        }
        QCOMPARE(parser.feed(request.right(1)), Parser::Status::RequestComplete);
        QVERIFY(parser.getRequest().isConnectionClose());

        Parser pipelined;
        QCOMPARE(pipelined.feed(request + "GET /mcp HTTP/1.1\r\nHost: h\r\n\r\n"), Parser::Status::RequestComplete);
        QCOMPARE(pipelined.takeRequest().method, QByteArray("POST"));
        pipelined.reset();
        QCOMPARE(pipelined.parse(), Parser::Status::RequestComplete);
        QCOMPARE(pipelined.getRequest().method, QByteArray("GET"));
        QVERIFY(pipelined.getRequest().body.isEmpty());
        pipelined.reset();
        QCOMPARE(pipelined.getBufferedSize(), qsizetype(0));
        QVERIFY(!pipelined.hasStarted());
    }

    // Every limit and status of plan 1.5
    struct Case
    {
        const char* name;
        QByteArray data;
        int status;     ///< 0 = complete, -1 = need more data
    };

    const QByteArray host = "Host: h\r\n";
    QByteArray longHead = "GET /mcp HTTP/1.1\r\n" + host;
    while (longHead.size() <= pdf::PDFMcpLimits::MAXIMUM_HEADER_SIZE)
    {
        longHead += "X-Filler: " + QByteArray(100, 'a') + "\r\n";
    }
    QByteArray manyHeaders = "GET /mcp HTTP/1.1\r\n" + host;
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_HEADER_COUNT; ++i)
    {
        manyHeaders += "X-H" + QByteArray::number(i) + ": 1\r\n";
    }
    QByteArray maximumHeaders = "GET /mcp HTTP/1.1\r\n" + host;
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_HEADER_COUNT - 1; ++i)
    {
        maximumHeaders += "X-H" + QByteArray::number(i) + ": 1\r\n";
    }

    const std::vector<Case> cases = {
        { "get", "GET /mcp HTTP/1.1\r\n" + host + "\r\n", 0 },
        { "maximum headers", maximumHeaders + "\r\n", 0 },
        { "too many headers", manyHeaders + "\r\n", 431 },
        { "head too large", longHead + "\r\n", 431 },
        { "head too large, unfinished", longHead, 431 },
        { "target too long", "GET /" + QByteArray(3000, 'a') + " HTTP/1.1\r\n" + host + "\r\n", 431 },
        { "bare LF request line", "GET /mcp HTTP/1.1\n" + host + "\r\n", 400 },
        { "bare LF header", "GET /mcp HTTP/1.1\r\nHost: h\n\r\n", 400 },
        { "bare CR", "GET /mcp HTTP/1.1\r\nHost: h\rX: y\r\n\r\n", 400 },
        { "NUL", QByteArray("GET /mcp HTTP/1.1\r\nHost: h\0\r\n\r\n", 32), 400 },
        { "control in value", "GET /mcp HTTP/1.1\r\nHost: h\x01\r\n\r\n", 400 },
        { "DEL in value", "GET /mcp HTTP/1.1\r\nHost: h\x7f\r\n\r\n", 400 },
        { "obs-fold", "GET /mcp HTTP/1.1\r\nHost: h\r\nX: a\r\n b\r\n\r\n", 400 },
        { "space before colon", "GET /mcp HTTP/1.1\r\nHost : h\r\n\r\n", 400 },
        { "invalid name", "GET /mcp HTTP/1.1\r\nHo(st: h\r\n\r\n", 400 },
        { "no colon", "GET /mcp HTTP/1.1\r\nHost\r\n\r\n", 400 },
        { "double space", "GET  /mcp HTTP/1.1\r\n" + host + "\r\n", 400 },
        { "two parts", "GET /mcp\r\n" + host + "\r\n", 400 },
        { "four parts", "GET /mcp x HTTP/1.1\r\n" + host + "\r\n", 400 },
        { "invalid method", "G(T /mcp HTTP/1.1\r\n" + host + "\r\n", 400 },
        { "invalid target", "GET /m\x7f HTTP/1.1\r\n" + host + "\r\n", 400 },
        { "invalid version", "GET /mcp HTTPS/1.1\r\n" + host + "\r\n", 400 },
        { "HTTP/1.0", "GET /mcp HTTP/1.0\r\n" + host + "\r\n", 505 },
        { "HTTP/2 preface", "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 505 },
        { "no host", "GET /mcp HTTP/1.1\r\n\r\n", 400 },
        { "two hosts", "GET /mcp HTTP/1.1\r\n" + host + host + "\r\n", 400 },
        { "transfer-encoding", "POST /mcp HTTP/1.1\r\n" + host + "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n", 411 },
        { "smuggling CL + TE", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", 411 },
        { "POST without length", "POST /mcp HTTP/1.1\r\n" + host + "\r\n", 411 },
        { "repeated length", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}", 400 },
        { "list of lengths", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: 2, 2\r\n\r\n{}", 400 },
        { "signed length", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: +2\r\n\r\n{}", 400 },
        { "negative length", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: -1\r\n\r\n", 400 },
        { "hex length", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: 0x2\r\n\r\n{}", 400 },
        { "huge length digits", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: 1234567890123456789\r\n\r\n", 400 },
        { "too large body", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: " + QByteArray::number(pdf::PDFMcpLimits::MAXIMUM_BODY_SIZE + 1) + "\r\n\r\n", 413 },
        { "maximum body announced", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: " + QByteArray::number(pdf::PDFMcpLimits::MAXIMUM_BODY_SIZE) + "\r\n\r\n", -1 },
        { "expect", "POST /mcp HTTP/1.1\r\n" + host + "Expect: 100-continue\r\nContent-Length: 2\r\n\r\n", 417 },
        { "partial head", "GET /mcp HTTP/1.1\r\nHost: h\r\n", -1 },
        { "partial body", "POST /mcp HTTP/1.1\r\n" + host + "Content-Length: 5\r\n\r\n{}", -1 }
    };

    for (const Case& testCase : cases)
    {
        Parser parser;
        const Parser::Status status = parser.feed(testCase.data);
        if (testCase.status == 0)
        {
            QVERIFY2(status == Parser::Status::RequestComplete, testCase.name);
        }
        else if (testCase.status < 0)
        {
            QVERIFY2(status == Parser::Status::NeedMoreData, testCase.name);
        }
        else
        {
            QVERIFY2(status == Parser::Status::Error, testCase.name);
            QVERIFY2(parser.getErrorStatus() == testCase.status, qPrintable(QStringLiteral("%1: %2").arg(QString::fromLatin1(testCase.name)).arg(parser.getErrorStatus())));
            QVERIFY(!parser.getErrorMessage().isEmpty());

            // The error is final
            QCOMPARE(parser.feed("GET /mcp HTTP/1.1\r\nHost: h\r\n\r\n"), Parser::Status::Error);
            parser.clear();
            QCOMPARE(parser.feed("GET /mcp HTTP/1.1\r\nHost: h\r\n\r\n"), Parser::Status::RequestComplete);
        }
    }

    // Lowered limits of the tests
    pdf::PDFMcpHttpParserLimits limits;
    limits.maximumBodySize = 10;
    limits.maximumHeaderCount = 2;
    Parser limited(limits);
    QCOMPARE(limited.feed("POST /mcp HTTP/1.1\r\nHost: h\r\nContent-Length: 11\r\n\r\n"), Parser::Status::Error);
    QCOMPARE(limited.getErrorStatus(), 413);

    // The head started (408 decided by the connection)
    Parser started;
    QVERIFY(!started.hasStarted());
    started.feed("G");
    QVERIFY(started.hasStarted());
    QVERIFY(!started.isHeadComplete());
}

void McpTest::httpPartialHeads()
{
    if (McpServerHelper::hasLoopbackFilter())
    {
        QSKIP("An HTTP filter of the loopback holds an incomplete HTTP head until the client closes (asserted on Linux)");
    }

    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.headerTimeout = 300;
    config.bodyTimeout = 300;
    McpServerFixture fixture(config);
    QVERIFY(fixture.isRunning);

    // Slowloris: a head, which never ends, gets 408 after the time of the head
    QElapsedTimer timer;
    timer.start();
    QTcpSocket slow;
    slow.connectToHost(QHostAddress::LocalHost, fixture.port());
    QVERIFY(slow.waitForConnected(5000));
    slow.write("POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n");
    QByteArray data;
    for (int i = 0; i < 20 && !data.contains("\r\n\r\n"); ++i)
    {
        slow.write("X-Slow: 1\r\n");
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        slow.waitForReadyRead(50);
        data += slow.readAll();
    }
    McpTestHelper::waitFor([&]() { data += slow.readAll(); return data.contains("\r\n\r\n"); }, 5000);
    QVERIFY2(data.startsWith("HTTP/1.1 408"), data.constData());

    // A body, which never ends, gets 408 too
    QTcpSocket body;
    body.connectToHost(QHostAddress::LocalHost, fixture.port());
    QVERIFY(body.waitForConnected(5000));
    body.write(McpServerHelper::createRawPost(fixture.port(), "{}").replace("Content-Length: 2", "Content-Length: 50"));
    QByteArray bodyData;
    QVERIFY(McpTestHelper::waitFor([&]() { bodyData += body.readAll(); return bodyData.contains("\r\n\r\n"); }, 5000));
    QVERIFY2(bodyData.startsWith("HTTP/1.1 408"), bodyData.constData());

    // An unfinished head over the limit gets 431 at once
    QTcpSocket large;
    large.connectToHost(QHostAddress::LocalHost, fixture.port());
    QVERIFY(large.waitForConnected(5000));
    large.write("GET /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nX: " + QByteArray(20000, 'a'));
    QByteArray largeData;
    QVERIFY(McpTestHelper::waitFor([&]() { largeData += large.readAll(); return largeData.contains("\r\n\r\n"); }, 5000));
    QVERIFY2(largeData.startsWith("HTTP/1.1 431"), largeData.constData());
}

void McpTest::httpFuzz()
{
    using Parser = pdf::PDFMcpHttpParser;

    const QByteArray base[] = {
        McpServerHelper::createRawPost(8765, R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})", McpServerHelper::modernHeaders("tools/list")),
        "GET /mcp HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nAccept: text/event-stream\r\nMcp-Session-Id: ABC\r\n\r\n",
        "DELETE /mcp HTTP/1.1\r\nHost: 127.0.0.1:8765\r\nMcp-Session-Id: ABC\r\n\r\n"
    };

    // 10^5 seeded mutations: never a crash, always a final state, the same
    // result whole and in random pieces
    QRandomGenerator random(20261026);
    QElapsedTimer timer;
    timer.start();
    int count = 0;
    int complete = 0;
    int errors = 0;
    for (; count < 100000 && timer.elapsed() < 30000 * McpTestHelper::factor(); ++count)
    {
        const QByteArray data = McpTestHelper::mutate(random, base[random.bounded(3)]);

        Parser whole;
        const Parser::Status wholeStatus = whole.feed(data);

        Parser pieces;
        Parser::Status pieceStatus = Parser::Status::NeedMoreData;
        qsizetype position = 0;
        while (position < data.size() && pieceStatus == Parser::Status::NeedMoreData)
        {
            const qsizetype size = 1 + random.bounded(int(qMin<qsizetype>(64, data.size() - position)));
            pieceStatus = pieces.feed(data.mid(position, size));
            position += size;
        }

        QCOMPARE(int(pieceStatus), int(wholeStatus));
        if (wholeStatus == Parser::Status::Error)
        {
            ++errors;
            QCOMPARE(pieces.getErrorStatus(), whole.getErrorStatus());
            const int status = whole.getErrorStatus();
            QVERIFY2(status == 400 || status == 411 || status == 413 || status == 417 || status == 431 || status == 505, qPrintable(QString::number(status)));
        }
        else if (wholeStatus == Parser::Status::RequestComplete)
        {
            ++complete;
            QCOMPARE(pieces.getRequest().method, whole.getRequest().method);
            QCOMPARE(pieces.getRequest().target, whole.getRequest().target);
            QCOMPARE(pieces.getRequest().headers, whole.getRequest().headers);
            QCOMPARE(pieces.getRequest().body, whole.getRequest().body);
        }
    }
    qInfo("httpFuzz: %d mutations, %d complete, %d errors, %lld ms", count, complete, errors, timer.elapsed());
    QVERIFY(count >= 1000);

    // Over the socket: every mutated request gets a status (408 for an
    // unfinished head), the server keeps running
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.headerTimeout = 200;
    config.bodyTimeout = 200;
    McpServerFixture fixture(config);
    QVERIFY(fixture.isRunning);
    for (int i = 0; i < 60; ++i)
    {
        QByteArray data = base[random.bounded(3)];
        data.replace("8765", QByteArray::number(fixture.port()));
        data = McpTestHelper::mutate(random, data);
        const pdftest::McpTestResponse response = fixture.raw(data, 5000);
        QVERIFY2(response.status >= 200, data.left(200).constData());
        QCOMPARE(response.header("Server"), QByteArray("PDF4QT/9.8.7.6"));
    }
    QCOMPARE(fixture.client().discover().value(QStringLiteral("supportedVersions")).toArray().size(), 3);
}

void McpTest::jsonRpcErrors()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);

    // The ids are echoed exactly
    struct IdCase
    {
        QByteArray id;
        bool isValid;
    };
    const IdCase idCases[] = {
        { "\"" + QByteArray(256, 'x') + "\"", true },
        { "\"\\u00e9\\\"x\"", true },
        { "9223372036854775807", true },
        { "-9223372036854775808", true },
        { "0", true },
        { "\"" + QByteArray(257, 'x') + "\"", false },
        { "9223372036854775808", false },
        { "1.5", false },
        { "1e3", false },
        { "null", false },
        { "true", false },
        { "{}", false },
        { "[1]", false }
    };

    for (const IdCase& idCase : idCases)
    {
        const QByteArray body = "{\"jsonrpc\":\"2.0\",\"id\":" + idCase.id + ",\"method\":\"tools/list\",\"params\":{" + McpServerHelper::modernMeta() + "}}";
        const pdftest::McpTestResponse response = fixture.raw(McpServerHelper::createRawPost(fixture.port(), body, McpServerHelper::modernHeaders("tools/list")));
        if (idCase.isValid)
        {
            QCOMPARE(response.status, 200);
            const QJsonObject message = response.json();
            QVERIFY2(message.contains(QStringLiteral("result")), idCase.id.constData());

            // The id token of the response equals the id of the request
            const QJsonDocument requestDocument = QJsonDocument::fromJson(body);
            QCOMPARE(message.value(QStringLiteral("id")), requestDocument.object().value(QStringLiteral("id")));
            if (!idCase.id.startsWith('"'))
            {
                QVERIFY2(response.body.contains("\"id\":" + idCase.id + ","), response.body.left(100).constData());
            }
        }
        else
        {
            QCOMPARE(response.status, 400);
            QCOMPARE(McpServerHelper::getErrorCode(response.json()), pdf::PDFMcpErrorCodes::INVALID_REQUEST);
            QVERIFY(response.json().value(QStringLiteral("id")).isNull());
        }
    }

    struct BodyCase
    {
        const char* name;
        QByteArray body;
        int status;
        int code;
    };

    QByteArray deep = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"x":)";
    for (int i = 0; i < 64; ++i)
    {
        deep += '[';
    }
    deep += QByteArray(64, ']') + "}}";

    const BodyCase bodyCases[] = {
        { "duplicate keys", R"({"jsonrpc":"2.0","id":1,"id":2,"method":"tools/list"})", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "duplicate escaped keys", R"({"jsonrpc":"2.0","id":1,"method":"x","method":"y"})", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "depth 65", deep, 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "invalid UTF-8", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"\xc3\x28\"}", 400, pdf::PDFMcpErrorCodes::PARSE_ERROR },
        { "overlong UTF-8", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"\xc0\xaf\"}", 400, pdf::PDFMcpErrorCodes::PARSE_ERROR },
        { "batch", R"([{"jsonrpc":"2.0","id":1,"method":"tools/list"}])", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "string", R"("x")", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "trailing garbage", R"({"jsonrpc":"2.0","id":1,"method":"tools/list"}x)", 400, pdf::PDFMcpErrorCodes::PARSE_ERROR },
        { "empty", "", 400, pdf::PDFMcpErrorCodes::PARSE_ERROR },
        { "jsonrpc 1.0", R"({"jsonrpc":"1.0","id":1,"method":"tools/list"})", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "no jsonrpc", R"({"id":1,"method":"tools/list"})", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "method number", R"({"jsonrpc":"2.0","id":1,"method":5})", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "empty method", R"({"jsonrpc":"2.0","id":1,"method":""})", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "params array", R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":[]})", 400, pdf::PDFMcpErrorCodes::INVALID_PARAMS },
        { "meta array", R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":[]}})", 400, pdf::PDFMcpErrorCodes::INVALID_PARAMS },
        { "no method", R"({"jsonrpc":"2.0","id":1})", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST },
        { "response without a session", R"({"jsonrpc":"2.0","id":1,"result":{}})", 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST }
    };

    for (const BodyCase& bodyCase : bodyCases)
    {
        const pdftest::McpTestResponse response = fixture.raw(McpServerHelper::createRawPost(fixture.port(), bodyCase.body, McpServerHelper::modernHeaders("tools/list")));
        QVERIFY2(response.status == bodyCase.status, qPrintable(QStringLiteral("%1: %2").arg(QString::fromLatin1(bodyCase.name)).arg(response.status)));
        QVERIFY2(McpServerHelper::getErrorCode(response.json()) == bodyCase.code, bodyCase.name);
    }

    // Lone surrogates: the scanner records them; a document, which QJsonDocument
    // refuses after a valid scan, is a parse error (wave 0 fuzz)
    int refusedByQt = 0;
    for (const char* surrogate : { "\\ud800", "\\udc00", "\\ud800\\u0041", "\\ud800x", "\\ud800\\ud800", "\\udfff\\ud800", "\\ud83d", "\\ud800\\uDBFF" })
    {
        const QByteArray body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"echo\",\"arguments\":{\"value\":\"a" + QByteArray(surrogate) + "b\"}," + McpServerHelper::modernMeta() + "}}";
        QVERIFY(pdf::PDFMcpJsonScanner().scan(body).isValid());
        const bool isRefusedByQt = QJsonDocument::fromJson(body).isNull();
        const pdftest::McpTestResponse response = fixture.raw(McpServerHelper::createRawPost(fixture.port(), body, McpServerHelper::modernHeaders("tools/call", "echo")));
        if (isRefusedByQt)
        {
            ++refusedByQt;
            QCOMPARE(response.status, 400);
            QCOMPARE(McpServerHelper::getErrorCode(response.json()), pdf::PDFMcpErrorCodes::PARSE_ERROR);
        }
        else
        {
            QCOMPARE(response.status, 200);
            QVERIFY(response.json().contains(QStringLiteral("result")));
        }
    }
    qInfo("jsonRpcErrors: %d lone-surrogate documents refused by QJsonDocument", refusedByQt);

    // The same rules in a legacy session
    pdftest::McpTestClient legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
    QVERIFY(!legacy.initialize().isEmpty());
    const pdftest::McpTestResponse legacyBatch = legacy.post(R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])");
    QCOMPARE(legacyBatch.status, 400);
    QCOMPARE(McpServerHelper::getErrorCode(legacyBatch.json()), pdf::PDFMcpErrorCodes::INVALID_REQUEST);
    const pdftest::McpTestResponse legacyDuplicate = legacy.post(R"({"jsonrpc":"2.0","id":1,"id":1,"method":"ping"})");
    QCOMPARE(legacyDuplicate.status, 400);
}

void McpTest::authHostOrigin()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    const QByteArray discover = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\",\"params\":{" + McpServerHelper::modernMeta() + "}}";
    const QList<QPair<QByteArray, QByteArray>> headers = McpServerHelper::modernHeaders("server/discover");
    const quint16 port = fixture.port();

    // The token: missing, wrong, another scheme, repeated
    pdftest::McpTestResponse response = fixture.raw(McpServerHelper::createRawPost(port, discover, headers, false));
    QCOMPARE(response.status, 401);
    QCOMPARE(response.header("WWW-Authenticate"), QByteArray("Bearer realm=\"pdf4qt\""));
    QVERIFY(!response.header("WWW-Authenticate").contains("resource_metadata"));

    QList<QPair<QByteArray, QByteArray>> wrong = headers;
    wrong.append(qMakePair(QByteArray("Authorization"), QByteArray("Bearer wrong")));
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, wrong, false)).status, 401);

    QList<QPair<QByteArray, QByteArray>> basic = headers;
    basic.append(qMakePair(QByteArray("Authorization"), QByteArray("Basic " + McpServerHelper::token())));
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, basic, false)).status, 401);

    QList<QPair<QByteArray, QByteArray>> lowerCase = headers;
    lowerCase.append(qMakePair(QByteArray("Authorization"), QByteArray("bearer " + McpServerHelper::token())));
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, lowerCase, false)).status, 200);

    QList<QPair<QByteArray, QByteArray>> repeated = headers;
    repeated.append(qMakePair(QByteArray("Authorization"), QByteArray("Bearer " + McpServerHelper::token())));
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, repeated, true)).status, 401);

    // A query string is refused, other paths are unknown (also /.well-known)
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, headers).replace("POST /mcp", "POST /mcp?token=x")).status, 400);
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, headers).replace("POST /mcp", "POST /.well-known/oauth-protected-resource")).status, 404);
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, headers).replace("POST /mcp", "POST /mcp/")).status, 404);

    // No CORS: OPTIONS is 405 before the token
    const pdftest::McpTestResponse options = fixture.raw("OPTIONS /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\nOrigin: http://evil.example\r\nConnection: close\r\n\r\n");
    QCOMPARE(options.status, 403);
    const pdftest::McpTestResponse optionsLocal = fixture.raw("OPTIONS /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\nOrigin: http://127.0.0.1:3000\r\nConnection: close\r\n\r\n");
    QCOMPARE(optionsLocal.status, 405);
    const pdftest::McpTestResponse optionsWithoutOrigin = fixture.raw("OPTIONS /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\nConnection: close\r\n\r\n");
    QCOMPARE(optionsWithoutOrigin.status, 405);
    QCOMPARE(optionsWithoutOrigin.header("Allow"), QByteArray("GET, POST, DELETE"));
    QCOMPARE(fixture.raw("PUT /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").status, 405);

    // Host allowlist of the local mode
    for (const QByteArray& host : { QByteArray("127.0.0.1:") + QByteArray::number(port), QByteArray("localhost:") + QByteArray::number(port), QByteArray("LOCALHOST:") + QByteArray::number(port),
                                    QByteArray("[::1]:") + QByteArray::number(port) })
    {
        QByteArray request = McpServerHelper::createRawPost(port, discover, headers);
        request.replace("Host: 127.0.0.1:" + QByteArray::number(port), "Host: " + host);
        QVERIFY2(fixture.raw(request).status == 200, host.constData());
    }
    for (const QByteArray& host : { QByteArray("evil.example:") + QByteArray::number(port), QByteArray("127.0.0.1"), QByteArray("127.0.0.1:1"), QByteArray("192.168.1.24:") + QByteArray::number(port) })
    {
        QByteArray request = McpServerHelper::createRawPost(port, discover, headers);
        request.replace("Host: 127.0.0.1:" + QByteArray::number(port), "Host: " + host);
        QVERIFY2(fixture.raw(request).status == 403, host.constData());
    }

    // Origin: absent = OK; a loopback origin only with the token; others 403
    for (const QByteArray& origin : { QByteArray("http://127.0.0.1:3000"), QByteArray("http://localhost"), QByteArray("https://[::1]:8443") })
    {
        QList<QPair<QByteArray, QByteArray>> withOrigin = headers;
        withOrigin.append(qMakePair(QByteArray("Origin"), QByteArray(origin)));
        QVERIFY2(fixture.raw(McpServerHelper::createRawPost(port, discover, withOrigin)).status == 200, origin.constData());
    }
    for (const QByteArray& origin : { QByteArray("http://evil.example"), QByteArray("null"), QByteArray("http://127.0.0.1.evil.example"), QByteArray("file://"), QByteArray("http://user@127.0.0.1") })
    {
        QList<QPair<QByteArray, QByteArray>> withOrigin = headers;
        withOrigin.append(qMakePair(QByteArray("Origin"), QByteArray(origin)));
        QVERIFY2(fixture.raw(McpServerHelper::createRawPost(port, discover, withOrigin)).status == 403, origin.constData());
    }
    QList<QPair<QByteArray, QByteArray>> twoOrigins = headers;
    twoOrigins.append(qMakePair(QByteArray("Origin"), QByteArray("http://localhost")));
    twoOrigins.append(qMakePair(QByteArray("Origin"), QByteArray("http://localhost")));
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, twoOrigins)).status, 403);

    // Throttle: 20 failures of an address in a minute - 429 for a minute, also with the token
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_AUTHORIZATION_FAILURES - 4; ++i)
    {
        QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, headers, false)).status, 401);
    }
    const pdftest::McpTestResponse blocked = fixture.raw(McpServerHelper::createRawPost(port, discover, headers));
    QCOMPARE(blocked.status, 429);
    QCOMPARE(McpServerHelper::getErrorCode(blocked.json()), pdf::PDFMcpErrorCodes::RATE_LIMITED);
    QVERIFY(blocked.json().value(QStringLiteral("error")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("retryAfterMs")).toInteger() > 0);
    QVERIFY(!blocked.header("Retry-After").isEmpty());
    pdf::PDFMcpClock::setOffset(61000);
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, headers)).status, 200);
    pdf::PDFMcpClock::setOffset(0);

    // A server without the token: no Authorization needed, a loopback origin is refused
    pdf::PDFMcpServerConfig open = McpServerHelper::createConfig();
    open.requireToken = false;
    McpServerFixture openFixture(open);
    QVERIFY(openFixture.isRunning);
    QCOMPARE(openFixture.raw(McpServerHelper::createRawPost(openFixture.port(), discover, headers, false)).status, 200);
    QList<QPair<QByteArray, QByteArray>> localOrigin = headers;
    localOrigin.append(qMakePair(QByteArray("Origin"), QByteArray("http://127.0.0.1:3000")));
    QCOMPARE(openFixture.raw(McpServerHelper::createRawPost(openFixture.port(), discover, localOrigin, false)).status, 403);
}

void McpTest::remoteHosts()
{
    // The remote mode (the dispatcher without sockets: no listener on every
    // interface in the tests); the token is always required
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.bindMode = pdf::PDFMcpBindMode::Remote;
    config.requireToken = false;
    config.additionalHosts = QStringList({ QStringLiteral("My-Host.test:4321") });
    QVERIFY(config.isTokenRequired());
    McpDispatcherHarness harness(config);

    const QByteArray body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\",\"params\":{" + McpServerHelper::modernMeta() + "}}";
    QList<QPair<QByteArray, QByteArray>> headers = McpServerHelper::modernHeaders("server/discover");
    headers.append(qMakePair(QByteArray("Accept"), QByteArray("application/json, text/event-stream")));
    headers.append(qMakePair(QByteArray("Content-Type"), QByteArray("application/json")));
    headers.append(qMakePair(QByteArray("Authorization"), QByteArray("Bearer " + McpServerHelper::token())));

    QCOMPARE(harness.request("POST", "127.0.0.1:4321", headers, body).status, 200);
    QCOMPARE(harness.request("POST", "my-host.test:4321", headers, body).status, 200);
    QCOMPARE(harness.request("POST", "evil.example:4321", headers, body).status, 403);

    // Every address of an interface, which is up
    QStringList interfaceHosts;
    for (const QNetworkInterface& networkInterface : QNetworkInterface::allInterfaces())
    {
        if (!networkInterface.flags().testFlag(QNetworkInterface::IsUp))
        {
            continue;
        }
        for (const QNetworkAddressEntry& entry : networkInterface.addressEntries())
        {
            QHostAddress address = entry.ip();
            address.setScopeId(QString());
            interfaceHosts.append(address.protocol() == QAbstractSocket::IPv6Protocol ? QStringLiteral("[%1]:4321").arg(address.toString()) : QStringLiteral("%1:4321").arg(address.toString()));
        }
    }
    for (const QString& host : interfaceHosts)
    {
        QVERIFY2(harness.request("POST", host.toLatin1(), headers, body).status == 200, qPrintable(host));
    }

    const QString hostName = QHostInfo::localHostName().toLower();
    if (!hostName.isEmpty())
    {
        QCOMPARE(harness.request("POST", (hostName + QStringLiteral(".local:4321")).toLatin1(), headers, body).status, 200);
    }

    // The token is required also with requireToken off
    QList<QPair<QByteArray, QByteArray>> withoutToken = headers;
    withoutToken.removeLast();
    QCOMPARE(harness.request("POST", "127.0.0.1:4321", withoutToken, body).status, 401);

    // A miss refreshes the list at most once per second (no crash, still refused)
    for (int i = 0; i < 5; ++i)
    {
        // TEST-NET-1 of RFC 5737 is never an address of an interface (WSL owns 10.255.255.254)
        QCOMPARE(harness.request("POST", "192.0.2.1:4321", headers, body).status, 403);
    }
}

void McpTest::modernHeaders()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    const quint16 port = fixture.port();

    auto post = [&](const QByteArray& body, const QList<QPair<QByteArray, QByteArray>>& headers) { return fixture.raw(McpServerHelper::createRawPost(port, body, headers)); };
    const QByteArray list = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"params\":{" + McpServerHelper::modernMeta() + "}}";
    const QByteArray call = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"echo\",\"arguments\":{}," + McpServerHelper::modernMeta() + "}}";

    auto expect = [&](const pdftest::McpTestResponse& response, int status, int code, const char* name)
    {
        QVERIFY2(response.status == status, qPrintable(QStringLiteral("%1: status %2").arg(QString::fromLatin1(name)).arg(response.status)));
        QVERIFY2(McpServerHelper::getErrorCode(response.json()) == code, qPrintable(QStringLiteral("%1: code %2").arg(QString::fromLatin1(name)).arg(McpServerHelper::getErrorCode(response.json()))));
    };

    // -32020: MCP-Protocol-Version, Mcp-Method, Mcp-Name
    expect(post(list, { { "Mcp-Method", "tools/list" } }), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "no version header");
    expect(post(list, { { "MCP-Protocol-Version", "2025-11-25" }, { "Mcp-Method", "tools/list" } }), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "version mismatch");
    expect(post(list, { { "MCP-Protocol-Version", "2026-07-28" } }), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "no method header");
    expect(post(list, { { "MCP-Protocol-Version", "2026-07-28" }, { "Mcp-Method", "tools/call" } }), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "method mismatch");
    expect(post(list, { { "MCP-Protocol-Version", "2026-07-28" }, { "Mcp-Method", "TOOLS/LIST" } }), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "method case");
    expect(post(call, McpServerHelper::modernHeaders("tools/call")), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "no name header");
    expect(post(call, McpServerHelper::modernHeaders("tools/call", "other")), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "name mismatch");
    expect(post(call, McpServerHelper::modernHeaders("tools/call", "=?base64?!!!?=")), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "invalid base64");
    expect(post(call, McpServerHelper::modernHeaders("tools/call", "=?base64?" + QByteArray("\xff\xfe").toBase64() + "?=")), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "base64 of invalid UTF-8");
    expect(post(call, McpServerHelper::modernHeaders("tools/call", "ech\xc3\xb3")), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "non-ASCII name");
    QList<QPair<QByteArray, QByteArray>> twoNames = McpServerHelper::modernHeaders("tools/call", "echo");
    twoNames.append(qMakePair(QByteArray("Mcp-Name"), QByteArray("echo")));
    expect(post(call, twoNames), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "two name headers");

    // The base64 sentinel is decoded before the comparison
    const pdftest::McpTestResponse sentinel = post(call, McpServerHelper::modernHeaders("tools/call", "=?base64?" + QByteArray("echo").toBase64() + "?="));
    QCOMPARE(sentinel.status, 200);
    QVERIFY(sentinel.json().contains(QStringLiteral("result")));
    const QByteArray unicodeCall = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"\xc5\xa1\xc4\x8d\",\"arguments\":{}," + McpServerHelper::modernMeta() + "}}";
    const pdftest::McpTestResponse unicode = post(unicodeCall, McpServerHelper::modernHeaders("tools/call", "=?base64?" + QByteArray("\xc5\xa1\xc4\x8d").toBase64() + "?="));
    QCOMPARE(unicode.status, 200);
    QCOMPARE(McpServerHelper::getErrorCode(unicode.json()), pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    // A control character is what the sentinel exists for ("line1\nline2" of
    // Value Encoding): the decoded value matches the body, the tool is unknown
    const QByteArray controlCall = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"line1\\nline2\",\"arguments\":{}," + McpServerHelper::modernMeta() + "}}";
    const pdftest::McpTestResponse control = post(controlCall, McpServerHelper::modernHeaders("tools/call", "=?base64?" + QByteArray("line1\nline2").toBase64() + "?="));
    QCOMPARE(control.status, 200);
    QCOMPARE(McpServerHelper::getErrorCode(control.json()), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    expect(post(controlCall, McpServerHelper::modernHeaders("tools/call", "=?base64?" + QByteArray("line1\nline3").toBase64() + "?=")), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "control name mismatch");

    // resources/read: Mcp-Name = uri; tasks/*: Mcp-Name = taskId
    const QByteArray read = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/read\",\"params\":{\"uri\":\"pdf4qt://docs/index.md\"," + McpServerHelper::modernMeta() + "}}";
    QCOMPARE(post(read, McpServerHelper::modernHeaders("resources/read", "pdf4qt://docs/index.md")).status, 200);
    expect(post(read, McpServerHelper::modernHeaders("resources/read", "pdf4qt://docs/other.md")), 400, pdf::PDFMcpErrorCodes::HEADER_MISMATCH, "uri mismatch");

    // -32022 with the supported versions; a legacy version in _meta needs initialize
    const QByteArray future = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"params\":{\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2030-01-01\",\"io.modelcontextprotocol/clientCapabilities\":{}}}}";
    const pdftest::McpTestResponse unsupported = post(future, { { "MCP-Protocol-Version", "2030-01-01" }, { "Mcp-Method", "tools/list" } });
    expect(unsupported, 400, pdf::PDFMcpErrorCodes::UNSUPPORTED_PROTOCOL_VERSION, "unsupported version");
    const QJsonObject data = unsupported.json().value(QStringLiteral("error")).toObject().value(QStringLiteral("data")).toObject();
    QCOMPARE(data.value(QStringLiteral("requested")).toString(), QStringLiteral("2030-01-01"));
    QCOMPARE(data.value(QStringLiteral("supported")).toArray(), QJsonArray({ QStringLiteral("2026-07-28") }));
    QByteArray legacyMeta = future;
    legacyMeta.replace("2030-01-01", "2025-11-25");
    expect(post(legacyMeta, { { "MCP-Protocol-Version", "2025-11-25" }, { "Mcp-Method", "tools/list" } }), 400, pdf::PDFMcpErrorCodes::UNSUPPORTED_PROTOCOL_VERSION, "legacy version in _meta");

    // -32602 of the required _meta fields (400)
    expect(post(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}}})", McpServerHelper::modernHeaders("tools/list")),
           400, pdf::PDFMcpErrorCodes::INVALID_PARAMS, "no capabilities");
    expect(post(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":5,"io.modelcontextprotocol/clientCapabilities":{}}}})", McpServerHelper::modernHeaders("tools/list")),
           400, pdf::PDFMcpErrorCodes::INVALID_PARAMS, "version number");
    expect(post(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{},"io.modelcontextprotocol/logLevel":"loud"}}})", McpServerHelper::modernHeaders("tools/list")),
           400, pdf::PDFMcpErrorCodes::INVALID_PARAMS, "invalid log level");
    expect(post(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{},"progressToken":{}}}})", McpServerHelper::modernHeaders("tools/list")),
           400, pdf::PDFMcpErrorCodes::INVALID_PARAMS, "invalid progress token");
    expect(post(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})", McpServerHelper::modernHeaders("tools/list")), 400, pdf::PDFMcpErrorCodes::INVALID_PARAMS, "no _meta with the modern header");
    expect(post(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})", {}), 400, pdf::PDFMcpErrorCodes::INVALID_REQUEST, "no session");

    // 404 -32601: unknown methods, ping and initialize of the modern era
    for (const char* method : { "unknown/method", "ping", "initialize", "logging/setLevel", "resources/subscribe" })
    {
        const QByteArray body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"" + QByteArray(method) + "\",\"params\":{" + McpServerHelper::modernMeta() + "}}";
        expect(post(body, McpServerHelper::modernHeaders(method)), 404, pdf::PDFMcpErrorCodes::METHOD_NOT_FOUND, method);
    }

    // 405 of GET / DELETE without a session; 406; 415
    pdftest::McpTestClient client = fixture.client();
    const pdftest::McpTestResponse get = client.get();
    QCOMPARE(get.status, 405);
    QCOMPARE(get.header("Allow"), QByteArray("POST"));
    QCOMPARE(client.deleteSession().status, 405);
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, list, McpServerHelper::modernHeaders("tools/list")).replace("Accept: application/json, text/event-stream", "Accept: application/json")).status, 406);
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, list, McpServerHelper::modernHeaders("tools/list")).replace("Accept: application/json, text/event-stream", "Accept: text/html")).status, 406);
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, list, McpServerHelper::modernHeaders("tools/list")).replace("Accept: application/json, text/event-stream", "Accept: */*")).status, 200);
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, list, McpServerHelper::modernHeaders("tools/list")).replace("Content-Type: application/json", "Content-Type: text/plain")).status, 415);
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, list, McpServerHelper::modernHeaders("tools/list")).replace("Content-Type: application/json", "Content-Type: application/json; charset=latin1")).status, 415);
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, list, McpServerHelper::modernHeaders("tools/list")).replace("Content-Type: application/json", "Content-Type: Application/JSON; charset=\"UTF-8\"")).status, 200);

    // A modern notification is accepted; Mcp-Session-Id of a modern request is ignored and not echoed
    QCOMPARE(post("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":1," + McpServerHelper::modernMeta() + "}}", McpServerHelper::modernHeaders("notifications/cancelled")).status, 202);
    QCOMPARE(post(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1}})", {}).status, 202);
    QList<QPair<QByteArray, QByteArray>> withSession = McpServerHelper::modernHeaders("tools/list");
    withSession.append(qMakePair(QByteArray("Mcp-Session-Id"), QByteArray("UNKNOWNSESSIONUNKNOWNSESSI")));
    const pdftest::McpTestResponse ignored = post(list, withSession);
    QCOMPARE(ignored.status, 200);
    QVERIFY(ignored.header("Mcp-Session-Id").isEmpty());

    // Trace context is accepted and ignored
    const QByteArray traced = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\",\"params\":{\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\",\"io.modelcontextprotocol/clientCapabilities\":{},\"traceparent\":\"" + QByteArray(600, 'a') + "\",\"baggage\":5}}}";
    QCOMPARE(post(traced, McpServerHelper::modernHeaders("tools/list")).status, 200);
}

void McpTest::discover()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);

    pdftest::McpTestClient client = fixture.client();
    const QJsonObject result = client.discover();
    QCOMPARE(result.value(QStringLiteral("supportedVersions")).toArray(), QJsonArray({ QStringLiteral("2026-07-28"), QStringLiteral("2025-11-25"), QStringLiteral("2025-06-18") }));
    const QJsonObject capabilities = result.value(QStringLiteral("capabilities")).toObject();
    QCOMPARE(capabilities.value(QStringLiteral("tools")).toObject().value(QStringLiteral("listChanged")).toBool(), true);
    QCOMPARE(capabilities.value(QStringLiteral("resources")).toObject().value(QStringLiteral("subscribe")).toBool(), true);
    QCOMPARE(capabilities.value(QStringLiteral("resources")).toObject().value(QStringLiteral("listChanged")).toBool(), true);
    QCOMPARE(capabilities.value(QStringLiteral("prompts")).toObject().value(QStringLiteral("listChanged")).toBool(), true);
    QVERIFY(capabilities.value(QStringLiteral("completions")).isObject());
    QVERIFY(capabilities.value(QStringLiteral("logging")).isObject());
    QVERIFY(capabilities.value(QStringLiteral("extensions")).toObject().value(QStringLiteral("io.modelcontextprotocol/tasks")).isObject());
    QVERIFY(!capabilities.contains(QStringLiteral("tasks")));
    QCOMPARE(result.value(QStringLiteral("instructions")).toString(), QStringLiteral("Test instructions of PDF4QT"));
    QCOMPARE(result.value(QStringLiteral("ttlMs")).toInteger(), pdf::PDFMcpLimits::DISCOVER_TTL);
    QCOMPARE(result.value(QStringLiteral("cacheScope")).toString(), QStringLiteral("private"));
    QCOMPARE(result.value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
    const QJsonObject serverInfo = result.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/serverInfo")).toObject();
    QCOMPARE(serverInfo.value(QStringLiteral("name")).toString(), QStringLiteral("pdf4qt"));
    QCOMPARE(serverInfo.value(QStringLiteral("title")).toString(), QStringLiteral("PDF4QT"));
    QCOMPARE(serverInfo.value(QStringLiteral("version")).toString(), QStringLiteral("9.8.7.6"));
    QCOMPARE(serverInfo.value(QStringLiteral("websiteUrl")).toString(), QStringLiteral("https://github.com/JakubMelka/PDF4QT"));
    QCOMPARE(serverInfo.value(QStringLiteral("icons")).toArray().size(), 1);

    // The bytes are deterministic
    client.discover();
    const QByteArray first = client.lastResponse().body;
    client.discover();
    QCOMPARE(QByteArray(client.lastResponse().body).replace("\"id\":3", "\"id\":2"), first);

    // A legacy session has no server/discover (-32601 with 200)
    pdftest::McpTestClient legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
    legacy.initialize();
    const QJsonObject legacyResponse = legacy.request(QStringLiteral("server/discover"));
    QCOMPARE(legacy.lastResponse().status, 200);
    QCOMPARE(McpServerHelper::getErrorCode(legacyResponse), pdf::PDFMcpErrorCodes::METHOD_NOT_FOUND);
}

void McpTest::listsAndPagination()
{
    McpServerFixture fixture(McpServerHelper::createConfig(), McpServerHelper::createCatalog(5, 150));
    QVERIFY(fixture.isRunning);
    pdftest::McpTestClient client = fixture.client();

    // tools/list: one page, ttl 60 s, private, deterministic
    const QJsonObject tools = client.listTools().value(QStringLiteral("result")).toObject();
    QCOMPARE(tools.value(QStringLiteral("tools")).toArray().size(), 7);
    QCOMPARE(tools.value(QStringLiteral("ttlMs")).toInteger(), pdf::PDFMcpLimits::TOOLS_LIST_TTL);
    QCOMPARE(tools.value(QStringLiteral("cacheScope")).toString(), QStringLiteral("private"));
    QVERIFY(!tools.contains(QStringLiteral("nextCursor")));
    const QByteArray toolsBody = client.lastResponse().body;
    client.listTools();
    QCOMPARE(client.lastResponse().body.mid(client.lastResponse().body.indexOf("\"result\"")), toolsBody.mid(toolsBody.indexOf("\"result\"")));

    // resources/list: pages of 100 with an opaque cursor, ttl 0
    const QJsonObject page1 = client.request(QStringLiteral("resources/list")).value(QStringLiteral("result")).toObject();
    QCOMPARE(page1.value(QStringLiteral("resources")).toArray().size(), 100);
    QCOMPARE(page1.value(QStringLiteral("ttlMs")).toInteger(), 0);
    const QString cursor = page1.value(QStringLiteral("nextCursor")).toString();
    QVERIFY(!cursor.isEmpty());
    QVERIFY(cursor.size() <= pdf::PDFMcpLimits::MAXIMUM_CURSOR_LENGTH);
    const QJsonObject page2 = client.request(QStringLiteral("resources/list"), QJsonObject({ { QStringLiteral("cursor"), cursor } })).value(QStringLiteral("result")).toObject();
    QCOMPARE(page2.value(QStringLiteral("resources")).toArray().size(), 53);
    QVERIFY(!page2.contains(QStringLiteral("nextCursor")));
    QCOMPARE(page2.value(QStringLiteral("resources")).toArray().first().toObject().value(QStringLiteral("uri")).toString(), QStringLiteral("pdf4qt://docs/extra-0097.md"));

    // Invalid cursors; a stale generation restarts from its offset
    for (const QJsonValue& invalid : { QJsonValue(QStringLiteral("!!")), QJsonValue(QStringLiteral("")), QJsonValue(QString(80, QLatin1Char('A'))), QJsonValue(5),
                                       QJsonValue(QString::fromLatin1(pdf::PDFMcpJson::toBase64Url("1:x"))), QJsonValue(QString::fromLatin1(pdf::PDFMcpJson::toBase64Url("1:2:3"))) })
    {
        const QJsonObject response = client.request(QStringLiteral("resources/list"), QJsonObject({ { QStringLiteral("cursor"), invalid } }));
        QCOMPARE(McpServerHelper::getErrorCode(response), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    }
    fixture.server->publishCatalog(McpServerHelper::createCatalog(6, 150));
    const QJsonObject stale = client.request(QStringLiteral("resources/list"), QJsonObject({ { QStringLiteral("cursor"), cursor } })).value(QStringLiteral("result")).toObject();
    QCOMPARE(stale.value(QStringLiteral("resources")).toArray().size(), 53);
    const QJsonObject beyond = client.request(QStringLiteral("resources/list"), QJsonObject({ { QStringLiteral("cursor"), pdf::PDFMcpDispatcher::createCursor(6, 100000) } })).value(QStringLiteral("result")).toObject();
    QCOMPARE(beyond.value(QStringLiteral("resources")).toArray().size(), 0);

    // templates and prompts: one hour, private
    const QJsonObject templates = client.request(QStringLiteral("resources/templates/list")).value(QStringLiteral("result")).toObject();
    QCOMPARE(templates.value(QStringLiteral("resourceTemplates")).toArray().size(), 2);
    QCOMPARE(templates.value(QStringLiteral("ttlMs")).toInteger(), pdf::PDFMcpLimits::STATIC_LIST_TTL);
    const QJsonObject prompts = client.request(QStringLiteral("prompts/list")).value(QStringLiteral("result")).toObject();
    QCOMPARE(prompts.value(QStringLiteral("prompts")).toArray().size(), 2);
    QCOMPARE(prompts.value(QStringLiteral("cacheScope")).toString(), QStringLiteral("private"));

    // The legacy eras: no resultType / ttlMs / cacheScope, no icons for 2025-06-18
    for (pdftest::McpTestEra era : { pdftest::McpTestEra::Legacy20251125, pdftest::McpTestEra::Legacy20250618 })
    {
        pdftest::McpTestClient legacy = fixture.client(era);
        legacy.initialize();
        const QJsonObject legacyTools = legacy.listTools().value(QStringLiteral("result")).toObject();
        QCOMPARE(legacyTools.value(QStringLiteral("tools")).toArray().size(), 7);
        QVERIFY(!legacyTools.contains(QStringLiteral("resultType")));
        QVERIFY(!legacyTools.contains(QStringLiteral("ttlMs")));
        QVERIFY(!legacyTools.contains(QStringLiteral("cacheScope")));
        QVERIFY(!legacyTools.contains(QStringLiteral("_meta")));
        QCOMPARE(legacyTools.value(QStringLiteral("tools")).toArray().first().toObject().contains(QStringLiteral("icons")), era == pdftest::McpTestEra::Legacy20251125);
        const QJsonObject legacyPage = legacy.request(QStringLiteral("resources/list")).value(QStringLiteral("result")).toObject();
        QCOMPARE(legacyPage.value(QStringLiteral("resources")).toArray().size(), 100);
        QVERIFY(legacyPage.contains(QStringLiteral("nextCursor")));
    }
}

void McpTest::resourcesRead()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    pdftest::McpTestClient client = fixture.client();

    // A static text: public, one hour, answered without the host
    const QJsonObject text = client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://docs/index.md") } })).value(QStringLiteral("result")).toObject();
    QCOMPARE(text.value(QStringLiteral("contents")).toArray().first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("# Index š"));
    QCOMPARE(text.value(QStringLiteral("cacheScope")).toString(), QStringLiteral("public"));
    QCOMPARE(text.value(QStringLiteral("ttlMs")).toInteger(), pdf::PDFMcpLimits::STATIC_LIST_TTL);
    const QJsonObject blob = client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://docs/logo.png") } })).value(QStringLiteral("result")).toObject();
    QCOMPARE(QByteArray::fromBase64(blob.value(QStringLiteral("contents")).toArray().first().toObject().value(QStringLiteral("blob")).toString().toLatin1()), QByteArray("\x89PNG\x00\x01", 6));
    QVERIFY(fixture.host.calls.empty());

    // A document resource (listed or of a template) goes to the host: private, ttl 0
    const QJsonObject document = client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://documents/ABC/info") } })).value(QStringLiteral("result")).toObject();
    QCOMPARE(document.value(QStringLiteral("cacheScope")).toString(), QStringLiteral("private"));
    QCOMPARE(document.value(QStringLiteral("ttlMs")).toInteger(), 0);
    QCOMPARE(fixture.host.calls.size(), size_t(1));
    QCOMPARE(fixture.host.calls.back()->getRequest().kind, pdf::PDFMcpCallKind::ResourceRead);
    client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://documents/XYZ/pages/3/text") } }));
    QCOMPARE(fixture.host.calls.size(), size_t(2));

    // Not found: -32602 (modern), -32002 (legacy), with data.uri; a static template without a file too
    const QJsonObject missing = client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://help/none.md") } }));
    QCOMPARE(McpServerHelper::getErrorCode(missing), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    QCOMPARE(missing.value(QStringLiteral("error")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("uri")).toString(), QStringLiteral("pdf4qt://help/none.md"));
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("file:///etc/passwd") } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QString(3000, QLatin1Char('a')) } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    pdftest::McpTestClient legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
    legacy.initialize();
    const QJsonObject legacyMissing = legacy.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://docs/none.md") } }));
    QCOMPARE(McpServerHelper::getErrorCode(legacyMissing), pdf::PDFMcpErrorCodes::RESOURCE_NOT_FOUND_LEGACY);
    QCOMPARE(legacy.lastResponse().status, 200);

    // prompts/get and completion/complete go to the host; their parameters are checked
    const QJsonObject prompt = client.getPrompt(QStringLiteral("summarize-document"), QJsonObject({ { QStringLiteral("document"), QStringLiteral("x") } }));
    QVERIFY(prompt.value(QStringLiteral("result")).toObject().value(QStringLiteral("messages")).isArray());
    QCOMPARE(McpServerHelper::getErrorCode(client.getPrompt(QStringLiteral("unknown"))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    QCOMPARE(McpServerHelper::getErrorCode(client.getPrompt(QStringLiteral("summarize-document"), QJsonObject({ { QStringLiteral("document"), 5 } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    const QJsonObject completion = client.request(QStringLiteral("completion/complete"), QJsonObject({ { QStringLiteral("ref"), QJsonObject({ { QStringLiteral("type"), QStringLiteral("ref/prompt") }, { QStringLiteral("name"), QStringLiteral("summarize-document") } }) },
                                                                                                     { QStringLiteral("argument"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("document") }, { QStringLiteral("value"), QStringLiteral("") } }) } }));
    QCOMPARE(completion.value(QStringLiteral("result")).toObject().value(QStringLiteral("completion")).toObject().value(QStringLiteral("values")).toArray().size(), 1);
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("completion/complete"))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    // tools/call parameters; an error of the host; a result over 16 MiB
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), 5 } }))), pdf::PDFMcpErrorCodes::HEADER_MISMATCH);
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), 5 } }), { qMakePair(QByteArray("Mcp-Name"), QByteArray("5")) })), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("echo") }, { QStringLiteral("arguments"), 5 } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    const QJsonObject error = client.callTool(QStringLiteral("error"), QJsonObject());
    QCOMPARE(McpServerHelper::getErrorCode(error), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    QCOMPARE(client.lastResponse().status, 200);
    const QJsonObject big = client.callTool(QStringLiteral("big"), QJsonObject());
    QVERIFY(big.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
    QVERIFY(McpServerHelper::getText(big).contains(QStringLiteral("16 MiB")));
}

void McpTest::legacyLifecycle()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);

    // initialize echoes a supported version, other versions get 2025-11-25
    struct VersionCase
    {
        const char* requested;
        const char* negotiated;
    };
    for (const VersionCase& versionCase : { VersionCase{ "2025-11-25", "2025-11-25" }, VersionCase{ "2025-06-18", "2025-06-18" }, VersionCase{ "2024-11-05", "2025-11-25" }, VersionCase{ "2026-07-28", "2025-11-25" } })
    {
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20251125);
        const QJsonObject params({ { QStringLiteral("protocolVersion"), QString::fromLatin1(versionCase.requested) }, { QStringLiteral("capabilities"), QJsonObject() },
                                   { QStringLiteral("clientInfo"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("legacy") }, { QStringLiteral("version"), QStringLiteral("1") } }) } });
        const QJsonObject result = client.request(QStringLiteral("initialize"), params).value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("protocolVersion")).toString(), QString::fromLatin1(versionCase.negotiated));
        const QByteArray sessionId = client.lastResponse().header("Mcp-Session-Id");
        QCOMPARE(sessionId.size(), 26);
        for (char c : sessionId)
        {
            QVERIFY(c >= 0x21 && c <= 0x7E);
        }

        const QJsonObject serverInfo = result.value(QStringLiteral("serverInfo")).toObject();
        QCOMPARE(serverInfo.value(QStringLiteral("name")).toString(), QStringLiteral("pdf4qt"));
        QCOMPARE(serverInfo.contains(QStringLiteral("icons")), QByteArray(versionCase.negotiated) == "2025-11-25");
        QCOMPARE(serverInfo.contains(QStringLiteral("websiteUrl")), QByteArray(versionCase.negotiated) == "2025-11-25");
        QVERIFY(!result.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("extensions")));
        QCOMPARE(result.value(QStringLiteral("instructions")).toString(), QStringLiteral("Test instructions of PDF4QT"));
        QVERIFY(!result.contains(QStringLiteral("resultType")));
    }
    QCOMPARE(McpServerHelper::getErrorCode(fixture.client(pdftest::McpTestEra::Legacy20251125).request(QStringLiteral("initialize"), QJsonObject())), pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20251125);
    QVERIFY(!client.initialize().isEmpty());
    QCOMPARE(client.lastResponse().status, 202);
    QVERIFY(fixture.waitEvent(pdf::PDFMcpAssistantEvent::Kind::Connected));

    // ping, lists, subscriptions, log level; unknown method -32601 with 200
    QVERIFY(client.request(QStringLiteral("ping")).value(QStringLiteral("result")).toObject().isEmpty());
    QCOMPARE(client.lastResponse().status, 200);
    QCOMPARE(client.listTools().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray().size(), 7);
    QVERIFY(client.request(QStringLiteral("resources/subscribe"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://documents/ABC/info") } })).contains(QStringLiteral("result")));
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("resources/subscribe"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("http://x") } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    QVERIFY(client.request(QStringLiteral("resources/unsubscribe"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://documents/ABC/info") } })).contains(QStringLiteral("result")));
    QVERIFY(client.request(QStringLiteral("logging/setLevel"), QJsonObject({ { QStringLiteral("level"), QStringLiteral("warning") } })).contains(QStringLiteral("result")));
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("logging/setLevel"), QJsonObject({ { QStringLiteral("level"), QStringLiteral("loud") } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("unknown/method"))), pdf::PDFMcpErrorCodes::METHOD_NOT_FOUND);
    QCOMPARE(client.lastResponse().status, 200);
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("subscriptions/listen"))), pdf::PDFMcpErrorCodes::METHOD_NOT_FOUND);

    // MCP-Protocol-Version: absent accepted, another refused
    QCOMPARE(client.post(QJsonDocument(client.createRequest(QStringLiteral("ping"))).toJson(QJsonDocument::Compact), { { "MCP-Protocol-Version", QByteArray() } }).status, 200);
    QCOMPARE(client.post(QJsonDocument(client.createRequest(QStringLiteral("ping"))).toJson(QJsonDocument::Compact), { { "MCP-Protocol-Version", "2025-06-18" } }).status, 400);

    // The GET stream: one per session, a new GET replaces the old one
    std::unique_ptr<pdftest::McpTestStream> first = client.openGetStream();
    QVERIFY(first);
    QVERIFY(first->waitForComments(1, 5000));
    QCOMPARE(first->response().status, 200);
    QVERIFY(first->response().isSse);
    std::unique_ptr<pdftest::McpTestStream> second = client.openGetStream();
    QVERIFY(second->waitForComments(1, 5000));
    QVERIFY(first->waitForEnd(5000));
    fixture.server->publishCatalog(McpServerHelper::createCatalog(2, 0, { QStringLiteral("new-tool") }));
    QVERIFY(second->waitForMessages(1, 5000));
    QCOMPARE(second->messages().front().value(QStringLiteral("method")).toString(), QStringLiteral("notifications/tools/list_changed"));
    QVERIFY(!second->messages().front().contains(QStringLiteral("params")));
    QCOMPARE(int(first->messages().size()), 0);
    pdftest::McpTestClient noAccept = client;
    noAccept.setExtraHeaders({ { "Accept", "application/json" } });
    QCOMPARE(noAccept.get().status, 406);

    // DELETE ends the session; a later request gets 404
    QCOMPARE(client.deleteSession().status, 200);
    QVERIFY(second->waitForEnd(5000));
    client.setSessionId(QByteArray());
    pdftest::McpTestClient ended = fixture.client(pdftest::McpTestEra::Legacy20251125);
    ended.initialize();
    const QByteArray endedId = ended.getSessionId();
    QCOMPARE(ended.deleteSession().status, 200);
    ended.setSessionId(endedId);
    ended.request(QStringLiteral("ping"));
    QCOMPARE(ended.lastResponse().status, 404);
    QCOMPARE(ended.get().status, 404);
    QCOMPARE(ended.deleteSession().status, 404);
    QVERIFY(fixture.waitEvent(pdf::PDFMcpAssistantEvent::Kind::SessionEnded));

    // Unknown and malformed session ids
    for (const QByteArray& id : { QByteArray("AAAAAAAAAAAAAAAAAAAAAAAAAA"), QByteArray("x"), QByteArray(200, 'A') })
    {
        pdftest::McpTestClient unknown = fixture.client(pdftest::McpTestEra::Legacy20251125);
        unknown.setSessionId(id);
        unknown.request(QStringLiteral("ping"));
        QCOMPARE(unknown.lastResponse().status, 404);
    }

    // Expiry after 30 minutes without a request
    pdftest::McpTestClient idle = fixture.client(pdftest::McpTestEra::Legacy20251125);
    idle.initialize();
    const int sessionEnded = fixture.countEvents(pdf::PDFMcpAssistantEvent::Kind::SessionEnded);
    pdf::PDFMcpClock::setOffset(pdf::PDFMcpLimits::SESSION_IDLE_TIME + 1000);
    fixture.server->checkTimeouts();
    QVERIFY(McpTestHelper::waitFor([&]() { return fixture.countEvents(pdf::PDFMcpAssistantEvent::Kind::SessionEnded) > sessionEnded; }, 5000));
    idle.request(QStringLiteral("ping"));
    QCOMPARE(idle.lastResponse().status, 404);
    pdf::PDFMcpClock::setOffset(0);

    // Disconnect of the user ends the session
    pdftest::McpTestClient disconnected = fixture.client(pdftest::McpTestEra::Legacy20250618);
    disconnected.initialize();
    const QString key = pdf::PDFMcpAssistantRegistry::createLegacyKey(disconnected.getSessionId());
    QVERIFY(McpTestHelper::waitFor([&]() { for (const auto& info : fixture.server->getAssistants()) { if (info.key == key) { return true; } } return false; }, 5000));
    fixture.server->disconnectAssistant(key);
    QVERIFY(fixture.waitEvent(pdf::PDFMcpAssistantEvent::Kind::Disconnected));
    disconnected.request(QStringLiteral("ping"));
    QCOMPARE(disconnected.lastResponse().status, 404);

    // At most 64 sessions
    std::vector<pdftest::McpTestClient> clients;
    int refused = 0;
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_LEGACY_SESSIONS + 2; ++i)
    {
        pdftest::McpTestClient many = fixture.client(pdftest::McpTestEra::Legacy20251125);
        many.request(QStringLiteral("initialize"), QJsonObject({ { QStringLiteral("protocolVersion"), QStringLiteral("2025-11-25") } }));
        if (many.lastResponse().status == 503)
        {
            ++refused;
        }
    }
    QVERIFY(refused >= 2);
}

void McpTest::listenStreams()
{
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.keepAliveInterval = 200;
    config.resourceUpdateInterval = 300;
    McpServerFixture fixture(config);
    QVERIFY(fixture.isRunning);
    pdftest::McpTestClient client = fixture.client();

    // taskIds without the extension: -32021
    const QJsonObject withTasks = client.request(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({ { QStringLiteral("taskIds"), QJsonArray({ QStringLiteral("x") }) } }) } }));
    QCOMPARE(McpServerHelper::getErrorCode(withTasks), pdf::PDFMcpErrorCodes::MISSING_CLIENT_CAPABILITY);
    QCOMPARE(client.lastResponse().status, 400);
    QVERIFY(withTasks.value(QStringLiteral("error")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("requiredCapabilities")).toObject().value(QStringLiteral("extensions")).toObject().contains(QStringLiteral("io.modelcontextprotocol/tasks")));
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), 5 } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({ { QStringLiteral("toolsListChanged"), 1 } }) } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    // The acknowledgment first, with the accepted subset and the subscription id
    QJsonObject message = client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({
        { QStringLiteral("toolsListChanged"), true },
        { QStringLiteral("promptsListChanged"), false },
        { QStringLiteral("resourceSubscriptions"), QJsonArray({ QStringLiteral("pdf4qt://documents/ABC/info"), QStringLiteral("http://invalid"), QStringLiteral("pdf4qt://documents/ABC/info"), 5, QStringLiteral("pdf4qt://documents/ABCD/info") }) } }) } }));
    message.insert(QStringLiteral("id"), QStringLiteral("listen-1"));
    std::unique_ptr<pdftest::McpTestStream> stream = client.openStream(message);
    QVERIFY(stream);
    QVERIFY(stream->waitForMessages(1, 5000));
    const QJsonObject ack = stream->messages().front();
    QCOMPARE(ack.value(QStringLiteral("method")).toString(), QStringLiteral("notifications/subscriptions/acknowledged"));
    QCOMPARE(ack.value(QStringLiteral("params")).toObject().value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/subscriptionId")).toString(), QStringLiteral("listen-1"));
    const QJsonObject accepted = ack.value(QStringLiteral("params")).toObject().value(QStringLiteral("notifications")).toObject();
    QCOMPARE(accepted.value(QStringLiteral("toolsListChanged")).toBool(), true);
    QVERIFY(!accepted.contains(QStringLiteral("promptsListChanged")));
    QCOMPARE(accepted.value(QStringLiteral("resourceSubscriptions")).toArray(), QJsonArray({ QStringLiteral("pdf4qt://documents/ABC/info"), QStringLiteral("pdf4qt://documents/ABCD/info") }));
    QCOMPARE(stream->response().header("X-Accel-Buffering"), QByteArray("no"));

    // Keep-alive comments
    QVERIFY(stream->waitForComments(2, 5000));

    // list_changed only for the requested types (prompts were not asked)
    fixture.server->publishCatalog(McpServerHelper::createCatalog(2, 0, { QStringLiteral("extra") }));
    QVERIFY(stream->waitForMessages(2, 5000));
    const QJsonObject changed = stream->messages()[1];
    QCOMPARE(changed.value(QStringLiteral("method")).toString(), QStringLiteral("notifications/tools/list_changed"));
    QCOMPARE(changed.value(QStringLiteral("params")).toObject().value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/subscriptionId")).toString(), QStringLiteral("listen-1"));
    fixture.server->publishCatalog(McpServerHelper::createCatalog(3, 0, { QStringLiteral("extra") }));

    // A document prefix updates the subscribed URIs below it (not ABCD); updates are coalesced
    for (int i = 0; i < 5; ++i)
    {
        fixture.server->notifyResourceUpdated(QStringLiteral("pdf4qt://documents/ABC"));
    }
    QVERIFY(stream->waitForMessages(4, 5000));
    QTest::qWait(600);
    std::vector<QJsonObject> messages = stream->messages();
    int updates = 0;
    for (const QJsonObject& item : messages)
    {
        if (item.value(QStringLiteral("method")).toString() == QLatin1String("notifications/resources/updated"))
        {
            ++updates;
            QCOMPARE(item.value(QStringLiteral("params")).toObject().value(QStringLiteral("uri")).toString(), QStringLiteral("pdf4qt://documents/ABC/info"));
            QCOMPARE(item.value(QStringLiteral("params")).toObject().value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/subscriptionId")).toString(), QStringLiteral("listen-1"));
        }
        QVERIFY(item.value(QStringLiteral("method")).toString() != QLatin1String("notifications/prompts/list_changed"));
        QVERIFY(item.value(QStringLiteral("method")).toString() != QLatin1String("notifications/progress"));
    }
    QCOMPARE(updates, 2);
    fixture.server->notifyResourceUpdated(QStringLiteral("pdf4qt://documents/ABCDE"));
    fixture.server->notifyResourceUpdated(QStringLiteral("pdf4qt://other"));

    // A legacy session subscribed by resources/subscribe gets it on its GET stream
    pdftest::McpTestClient legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
    legacy.initialize();
    legacy.request(QStringLiteral("resources/subscribe"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://documents/ABC/info") } }));
    std::unique_ptr<pdftest::McpTestStream> getStream = legacy.openGetStream();
    QVERIFY(getStream->waitForComments(1, 5000));
    fixture.server->notifyResourceUpdated(QStringLiteral("pdf4qt://documents/ABC/info"));
    QVERIFY(getStream->waitForMessages(1, 5000));
    QCOMPARE(getStream->messages().front().value(QStringLiteral("params")).toObject().value(QStringLiteral("uri")).toString(), QStringLiteral("pdf4qt://documents/ABC/info"));
    QVERIFY(!getStream->messages().front().value(QStringLiteral("params")).toObject().contains(QStringLiteral("_meta")));

    // At most 8 streams of an assistant; a closed stream frees its place
    std::vector<std::unique_ptr<pdftest::McpTestStream>> streams;
    for (int i = 1; i < pdf::PDFMcpLimits::MAXIMUM_LISTEN_STREAMS_PER_ASSISTANT; ++i)
    {
        streams.push_back(client.openStream(client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject() } }))));
        QVERIFY(streams.back()->waitForMessages(1, 5000));
    }
    const QJsonObject limited = client.request(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject() } }));
    QCOMPARE(client.lastResponse().status, 429);
    QCOMPARE(McpServerHelper::getErrorCode(limited), pdf::PDFMcpErrorCodes::RATE_LIMITED);
    streams.back()->close();
    QVERIFY(McpTestHelper::waitFor([&]()
    {
        std::unique_ptr<pdftest::McpTestStream> again = client.openStream(client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject() } })));
        const bool isOpen = again && again->waitForMessages(1, 1000);
        if (isOpen)
        {
            streams.back() = std::move(again);
        }
        return isOpen;
    }, 5000));

    // The stop ends the stream gracefully: the final result with the subscription id
    fixture.server->stop();
    QVERIFY(stream->waitForEnd(5000));
    const QJsonObject final = stream->response().json();
    QCOMPARE(final.value(QStringLiteral("id")).toString(), QStringLiteral("listen-1"));
    QCOMPARE(final.value(QStringLiteral("result")).toObject().value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
    QCOMPARE(final.value(QStringLiteral("result")).toObject().value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/subscriptionId")).toString(), QStringLiteral("listen-1"));
    QVERIFY(getStream->waitForEnd(5000));
}

void McpTest::progressAndLogging()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    pdftest::McpTestClient client = fixture.client();

    // With a progress token and a log level: an SSE stream, progress strictly
    // growing, messages at the level and above, the result last
    QJsonObject meta;
    meta.insert(QStringLiteral("progressToken"), QStringLiteral("tok"));
    meta.insert(QStringLiteral("io.modelcontextprotocol/logLevel"), QStringLiteral("warning"));
    const QJsonObject response = client.callTool(QStringLiteral("progress"), QJsonObject(), meta);
    const pdftest::McpTestResponse& last = client.lastResponse();
    QVERIFY(last.isSse);
    QCOMPARE(last.header("Content-Type"), QByteArray("text/event-stream"));
    QCOMPARE(last.header("Transfer-Encoding"), QByteArray("chunked"));
    QCOMPARE(McpServerHelper::getText(response), QStringLiteral("done"));
    const std::vector<QJsonObject> events = last.messages();
    double previous = 0.0;
    int progressCount = 0;
    int messageCount = 0;
    for (size_t i = 0; i < events.size(); ++i)
    {
        const QString method = events[i].value(QStringLiteral("method")).toString();
        const QJsonObject params = events[i].value(QStringLiteral("params")).toObject();
        if (method == QLatin1String("notifications/progress"))
        {
            ++progressCount;
            QCOMPARE(params.value(QStringLiteral("progressToken")).toString(), QStringLiteral("tok"));
            QVERIFY(params.value(QStringLiteral("progress")).toDouble() > previous);
            previous = params.value(QStringLiteral("progress")).toDouble();
            QCOMPARE(params.value(QStringLiteral("total")).toDouble(), 3.0);
        }
        else if (method == QLatin1String("notifications/message"))
        {
            ++messageCount;
            QCOMPARE(params.value(QStringLiteral("level")).toString(), QStringLiteral("warning"));
            QCOMPARE(params.value(QStringLiteral("logger")).toString(), QStringLiteral("pdf4qt"));
            QCOMPARE(params.value(QStringLiteral("data")).toString(), QStringLiteral("warning text"));
        }
        else
        {
            // "MUST stop after completion": the response is the last message
            QCOMPARE(i, events.size() - 1);
        }
    }
    QCOMPARE(progressCount, 3);
    QCOMPARE(messageCount, 1);

    // An integer token; the level info gives both messages
    QJsonObject integerMeta;
    integerMeta.insert(QStringLiteral("progressToken"), 7);
    integerMeta.insert(QStringLiteral("io.modelcontextprotocol/logLevel"), QStringLiteral("info"));
    client.callTool(QStringLiteral("progress"), QJsonObject(), integerMeta);
    int infoMessages = 0;
    for (const QJsonObject& event : client.lastResponse().messages())
    {
        if (event.value(QStringLiteral("method")).toString() == QLatin1String("notifications/progress"))
        {
            QCOMPARE(event.value(QStringLiteral("params")).toObject().value(QStringLiteral("progressToken")).toInt(), 7);
            QVERIFY(client.lastResponse().body.contains("\"progressToken\":7"));
        }
        infoMessages += event.value(QStringLiteral("method")).toString() == QLatin1String("notifications/message") ? 1 : 0;
    }
    QCOMPARE(infoMessages, 2);

    // Without a token and a level: nothing but the result (JSON)
    const QJsonObject plain = client.callTool(QStringLiteral("progress"), QJsonObject());
    QVERIFY(!client.lastResponse().isSse);
    QCOMPARE(McpServerHelper::getText(plain), QStringLiteral("done"));

    // Never on a listen stream
    std::unique_ptr<pdftest::McpTestStream> listen = client.openStream(client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({ { QStringLiteral("toolsListChanged"), true } }) } })));
    QVERIFY(listen->waitForMessages(1, 5000));
    client.callTool(QStringLiteral("progress"), QJsonObject(), meta);
    QTest::qWait(200);
    QCOMPARE(int(listen->messages().size()), 1);

    // Legacy: the token of params._meta, the level of logging/setLevel
    pdftest::McpTestClient legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
    legacy.initialize();
    legacy.request(QStringLiteral("logging/setLevel"), QJsonObject({ { QStringLiteral("level"), QStringLiteral("info") } }));
    legacy.request(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("progress") }, { QStringLiteral("arguments"), QJsonObject() },
                                                              { QStringLiteral("_meta"), QJsonObject({ { QStringLiteral("progressToken"), QStringLiteral("legacy") } }) } }));
    int legacyProgress = 0;
    int legacyMessages = 0;
    for (const QJsonObject& event : legacy.lastResponse().messages())
    {
        legacyProgress += event.value(QStringLiteral("method")).toString() == QLatin1String("notifications/progress") ? 1 : 0;
        legacyMessages += event.value(QStringLiteral("method")).toString() == QLatin1String("notifications/message") ? 1 : 0;
    }
    QCOMPARE(legacyProgress, 3);
    QCOMPARE(legacyMessages, 2);
}

void McpTest::cancellation()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    pdftest::McpTestClient client = fixture.client();

    // A closed stream cancels the call (before and after the switch to SSE)
    for (int wait : { 0, 1500 })
    {
        const size_t calls = fixture.host.calls.size();
        const size_t canceled = fixture.host.canceledCalls.size();
        std::unique_ptr<pdftest::McpTestStream> stream = client.openStream(client.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() } })));
        QVERIFY(fixture.waitCalls(calls + 1));
        if (wait > 0)
        {
            QVERIFY(stream->waitForComments(1, 5000));
        }
        stream->close();
        QVERIFY(fixture.waitCanceled(canceled + 1));
        const std::shared_ptr<pdf::PDFMcpCall> call = fixture.host.canceledCalls.back();
        QCOMPARE(call->getCancelReason(), pdf::PDFMcpCancelReason::StreamClosed);
        QVERIFY(call->isOperationCancelled());

        // A late result is dropped
        call->finish(McpServerHelper::createText(QStringLiteral("late")));
    }

    // Legacy notifications/cancelled: the call is canceled, no response is sent for it
    pdftest::McpTestClient legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
    legacy.initialize();
    const size_t calls = fixture.host.calls.size();
    QJsonObject request = legacy.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() } }));
    request.insert(QStringLiteral("id"), QStringLiteral("req-7"));
    std::unique_ptr<pdftest::McpTestStream> legacyStream = legacy.openStream(request);
    QVERIFY(fixture.waitCalls(calls + 1));
    QCOMPARE(legacy.notify(QStringLiteral("notifications/cancelled"), QJsonObject({ { QStringLiteral("requestId"), QStringLiteral("other") } })), 202);
    QCOMPARE(legacy.notify(QStringLiteral("notifications/cancelled"), QJsonObject({ { QStringLiteral("requestId"), QStringLiteral("req-7") }, { QStringLiteral("reason"), QStringLiteral("user") } })), 202);
    QVERIFY(fixture.waitCanceled(3));
    QCOMPARE(fixture.host.canceledCalls.back()->getCancelReason(), pdf::PDFMcpCancelReason::ClientCanceled);
    QVERIFY(legacyStream->waitForEnd(5000));
    QVERIFY(legacyStream->response().json().isEmpty());

    // A legacy POST stream closed by the client cancels too (deviation documented)
    const size_t legacyCalls = fixture.host.calls.size();
    std::unique_ptr<pdftest::McpTestStream> closed = legacy.openStream(legacy.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() } })));
    QVERIFY(fixture.waitCalls(legacyCalls + 1));
    closed->close();
    QVERIFY(fixture.waitCanceled(4));

    // The stop of the server in the middle of a call
    const size_t stopCalls = fixture.host.calls.size();
    std::unique_ptr<pdftest::McpTestStream> stopped = client.openStream(client.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() } })));
    QVERIFY(fixture.waitCalls(stopCalls + 1));
    fixture.server->stop();
    QVERIFY(fixture.waitCanceled(5));
    QCOMPARE(fixture.host.canceledCalls.back()->getCancelReason(), pdf::PDFMcpCancelReason::ServerStopped);
    QVERIFY(stopped->waitForEnd(5000));
    const pdftest::McpTestResponse stoppedResponse = stopped->response();
    QVERIFY(stoppedResponse.status == 503 || stoppedResponse.status == 0 || McpServerHelper::getErrorCode(stoppedResponse.json()) == pdf::PDFMcpErrorCodes::SERVER_STOPPING);
}

void McpTest::mrtrSealing()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    pdftest::McpTestClient client = fixture.client();
    client.setCapabilities(QJsonObject({ { QStringLiteral("elicitation"), QJsonObject() } }));
    client.setInputRetryRounds(0);

    // The first call answers input_required with the sealed state
    const QJsonObject arguments({ { QStringLiteral("value"), QStringLiteral("x") } });
    const QJsonObject first = client.callTool(QStringLiteral("ask"), arguments).value(QStringLiteral("result")).toObject();
    QCOMPARE(first.value(QStringLiteral("resultType")).toString(), QStringLiteral("input_required"));
    const QJsonObject inputRequest = first.value(QStringLiteral("inputRequests")).toObject().value(QStringLiteral("pdf4qt.choice")).toObject();
    QCOMPARE(inputRequest.value(QStringLiteral("method")).toString(), QStringLiteral("elicitation/create"));
    QCOMPARE(inputRequest.value(QStringLiteral("params")).toObject().value(QStringLiteral("mode")).toString(), QStringLiteral("form"));
    QCOMPARE(inputRequest.value(QStringLiteral("params")).toObject().value(QStringLiteral("message")).toString(), QStringLiteral("Choose a document"));
    QVERIFY(inputRequest.value(QStringLiteral("params")).toObject().value(QStringLiteral("requestedSchema")).isObject());
    const QString state = first.value(QStringLiteral("requestState")).toString();
    QVERIFY(!state.isEmpty());
    QVERIFY(state.size() <= pdf::PDFMcpLimits::MAXIMUM_REQUEST_STATE_SIZE);
    QVERIFY(first.value(QStringLiteral("_meta")).toObject().contains(QStringLiteral("io.modelcontextprotocol/serverInfo")));

    auto retry = [&](pdftest::McpTestClient& retryClient, const QJsonObject& retryArguments, const QJsonObject& responses, const QJsonValue& requestState)
    {
        QJsonObject params({ { QStringLiteral("name"), QStringLiteral("ask") }, { QStringLiteral("arguments"), retryArguments }, { QStringLiteral("inputResponses"), responses } });
        if (!requestState.isUndefined())
        {
            params.insert(QStringLiteral("requestState"), requestState);
        }
        return retryClient.request(QStringLiteral("tools/call"), params);
    };
    auto accept = [](const QJsonValue& value)
    {
        return QJsonObject({ { QStringLiteral("pdf4qt.choice"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("documentId"), value } }) } }) } });
    };

    // A valid answer
    QCOMPARE(McpServerHelper::getText(retry(client, arguments, accept(QStringLiteral("B")), state)), QStringLiteral("chosen B"));

    // Unknown keys are ignored
    QJsonObject withUnknown = accept(QStringLiteral("A"));
    withUnknown.insert(QStringLiteral("unknown"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") } }));
    QCOMPARE(McpServerHelper::getText(retry(client, arguments, withUnknown, state)), QStringLiteral("chosen A"));

    // Refused states: tampered, of another assistant, of other arguments, expired, malformed
    const QString expired = QStringLiteral("expired");
    QString tampered = state;
    tampered[tampered.size() / 3] = tampered[tampered.size() / 3] == QLatin1Char('A') ? QLatin1Char('B') : QLatin1Char('A');
    for (const QJsonValue& badState : { QJsonValue(tampered), QJsonValue(QStringLiteral("x.y")), QJsonValue(5), QJsonValue(QString(5000, QLatin1Char('a'))) })
    {
        const QJsonObject response = retry(client, arguments, accept(QStringLiteral("A")), badState);
        QVERIFY(response.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
        QVERIFY(McpServerHelper::getText(response).contains(expired));
    }
    pdftest::McpTestClient other = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("other-client"));
    other.setCapabilities(QJsonObject({ { QStringLiteral("elicitation"), QJsonObject() } }));
    QVERIFY(McpServerHelper::getText(retry(other, arguments, accept(QStringLiteral("A")), state)).contains(expired));
    QVERIFY(McpServerHelper::getText(retry(client, QJsonObject({ { QStringLiteral("value"), QStringLiteral("y") } }), accept(QStringLiteral("A")), state)).contains(expired));
    pdf::PDFMcpClock::setOffset(pdf::PDFMcpLimits::REQUEST_STATE_LIFETIME + 1000);
    QVERIFY(McpServerHelper::getText(retry(client, arguments, accept(QStringLiteral("A")), state)).contains(expired));
    pdf::PDFMcpClock::setOffset(0);

    // A missing answer or a value outside of the allowed set: a new input_required
    for (const QJsonObject& responses : { QJsonObject(), accept(QStringLiteral("Z")), accept(5), QJsonObject({ { QStringLiteral("pdf4qt.choice"), 5 } }) })
    {
        const QJsonObject response = retry(client, arguments, responses, state);
        QCOMPARE(response.value(QStringLiteral("result")).toObject().value(QStringLiteral("resultType")).toString(), QStringLiteral("input_required"));
    }

    // Answers without a state are not verified: the host asks again
    QCOMPARE(retry(client, arguments, accept(QStringLiteral("A")), QJsonValue(QJsonValue::Undefined)).value(QStringLiteral("result")).toObject().value(QStringLiteral("resultType")).toString(), QStringLiteral("input_required"));

    // Declined / canceled
    for (const char* action : { "decline", "cancel" })
    {
        const QJsonObject declined = retry(client, arguments, QJsonObject({ { QStringLiteral("pdf4qt.choice"), QJsonObject({ { QStringLiteral("action"), QString::fromLatin1(action) } }) } }), state);
        QVERIFY(declined.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
        QVERIFY(McpServerHelper::getText(declined).contains(QStringLiteral("declined")));
    }

    // The limit of 64 KiB of the answers; inputResponses of a wrong type
    const QJsonObject huge = accept(QString(70000, QLatin1Char('a')));
    QCOMPARE(McpServerHelper::getErrorCode(retry(client, arguments, huge, state)), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("ask") }, { QStringLiteral("inputResponses"), 5 } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    // A client without elicitation never gets inputRequests
    pdftest::McpTestClient plain = fixture.client();
    const QJsonObject fallback = plain.callTool(QStringLiteral("ask"), arguments);
    QVERIFY(fallback.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
    QVERIFY(!fallback.value(QStringLiteral("result")).toObject().contains(QStringLiteral("inputRequests")));
    QVERIFY(McpServerHelper::getText(fallback).contains(QStringLiteral("Choose a document")));

    // The scripted client answers and retries by itself; prompts/get too
    client.setInputRetryRounds(4);
    client.setElicitationResponder([](const QJsonObject& params)
    {
        Q_UNUSED(params);
        return QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("documentId"), QStringLiteral("A") } }) } });
    });
    QCOMPARE(McpServerHelper::getText(client.callTool(QStringLiteral("ask"), arguments)), QStringLiteral("chosen A"));
    const QJsonObject prompt = client.getPrompt(QStringLiteral("choose-document"));
    QVERIFY(prompt.value(QStringLiteral("result")).toObject().value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("content")).toObject().value(QStringLiteral("text")).toString().endsWith(QStringLiteral(" A")));

    // The host gets the verified answers and the allowed values
    const pdf::PDFMcpCallRequest& lastRequest = fixture.host.calls.back()->getRequest();
    QCOMPARE(McpServerHelper::getChoice(lastRequest), QStringLiteral("A"));
    QVERIFY(lastRequest.requestState.value(QStringLiteral("allowedValues")).isObject());
}

void McpTest::legacyElicitation()
{
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.elicitationTimeout = 500;
    McpServerFixture fixture(config);
    QVERIFY(fixture.isRunning);

    // A round trip of each legacy version (mode form only for 2025-11-25)
    for (pdftest::McpTestEra era : { pdftest::McpTestEra::Legacy20251125, pdftest::McpTestEra::Legacy20250618 })
    {
        pdftest::McpTestClient client = fixture.client(era);
        client.setCapabilities(QJsonObject({ { QStringLiteral("elicitation"), QJsonObject() } }));
        QJsonObject asked;
        client.setElicitationResponder([&asked](const QJsonObject& params)
        {
            asked = params;
            return QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("documentId"), QStringLiteral("B") } }) } });
        });
        client.initialize();
        QCOMPARE(McpServerHelper::getText(client.callTool(QStringLiteral("ask"), QJsonObject())), QStringLiteral("chosen B"));
        QCOMPARE(asked.value(QStringLiteral("message")).toString(), QStringLiteral("Choose a document"));
        QCOMPARE(asked.contains(QStringLiteral("mode")), era == pdftest::McpTestEra::Legacy20251125);
        QVERIFY(asked.value(QStringLiteral("requestedSchema")).isObject());
    }

    // Declined, an error answer, a value outside of the set (asked again until the rounds end)
    pdftest::McpTestClient declining = fixture.client(pdftest::McpTestEra::Legacy20251125);
    declining.setCapabilities(QJsonObject({ { QStringLiteral("elicitation"), QJsonObject() } }));
    declining.setElicitationResponder([](const QJsonObject&) { return QJsonObject({ { QStringLiteral("action"), QStringLiteral("decline") } }); });
    declining.initialize();
    QVERIFY(McpServerHelper::getText(declining.callTool(QStringLiteral("ask"), QJsonObject())).contains(QStringLiteral("declined")));
    declining.setElicitationResponder([](const QJsonObject&) { return QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("documentId"), QStringLiteral("Z") } }) } }); });
    const QJsonObject tooMany = declining.callTool(QStringLiteral("ask"), QJsonObject());
    QVERIFY(tooMany.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
    QVERIFY(McpServerHelper::getText(tooMany).contains(QStringLiteral("too many")));

    // No answer within the time: an execution error; a wrong id or a foreign session is ignored
    pdftest::McpTestClient silent = fixture.client(pdftest::McpTestEra::Legacy20251125);
    silent.setCapabilities(QJsonObject({ { QStringLiteral("elicitation"), QJsonObject() } }));
    silent.initialize();
    std::unique_ptr<pdftest::McpTestStream> stream = silent.openStream(silent.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("ask") }, { QStringLiteral("arguments"), QJsonObject() } })));
    QVERIFY(stream->waitForMessages(1, 5000));
    const QJsonObject elicitation = stream->messages().front();
    QCOMPARE(elicitation.value(QStringLiteral("method")).toString(), QStringLiteral("elicitation/create"));
    pdftest::McpTestClient foreign = fixture.client(pdftest::McpTestEra::Legacy20251125);
    foreign.initialize();
    QJsonObject wrongAnswer({ { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("id"), elicitation.value(QStringLiteral("id")) },
                              { QStringLiteral("result"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("decline") } }) } });
    QCOMPARE(foreign.post(QJsonDocument(wrongAnswer).toJson(QJsonDocument::Compact)).status, 202);
    wrongAnswer.insert(QStringLiteral("id"), 99999);
    QCOMPARE(silent.post(QJsonDocument(wrongAnswer).toJson(QJsonDocument::Compact)).status, 202);
    QVERIFY(stream->waitForEnd(5000));
    const QJsonObject timedOut = stream->response().json();
    QVERIFY(timedOut.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
    QVERIFY(McpServerHelper::getText(timedOut).contains(QStringLiteral("in time")));

    // An error response of the client is a canceled choice
    std::unique_ptr<pdftest::McpTestStream> errorStream = silent.openStream(silent.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("ask") }, { QStringLiteral("arguments"), QJsonObject() } })));
    QVERIFY(errorStream->waitForMessages(1, 5000));
    const QJsonObject errorAnswer({ { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("id"), errorStream->messages().front().value(QStringLiteral("id")) },
                                    { QStringLiteral("error"), QJsonObject({ { QStringLiteral("code"), -1 }, { QStringLiteral("message"), QStringLiteral("no") } }) } });
    QCOMPARE(silent.post(QJsonDocument(errorAnswer).toJson(QJsonDocument::Compact)).status, 202);
    QVERIFY(errorStream->waitForEnd(5000));
    QVERIFY(McpServerHelper::getText(errorStream->response().json()).contains(QStringLiteral("declined")));

    // A client without the capability gets the fallback text
    pdftest::McpTestClient plain = fixture.client(pdftest::McpTestEra::Legacy20251125);
    plain.initialize();
    QVERIFY(McpServerHelper::getText(plain.callTool(QStringLiteral("ask"), QJsonObject())).contains(QStringLiteral("cannot ask")));
}

void McpTest::tasksExtension()
{
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.taskCreationDelay = 100;
    McpServerFixture fixture(config);
    QVERIFY(fixture.isRunning);

    const QJsonObject tasksCapabilities({ { QStringLiteral("elicitation"), QJsonObject() }, { QStringLiteral("extensions"), QJsonObject({ { QStringLiteral("io.modelcontextprotocol/tasks"), QJsonObject() } }) } });
    pdftest::McpTestClient client = fixture.client();
    client.setCapabilities(tasksCapabilities);
    client.setInputRetryRounds(0);

    // A fast call is answered directly
    QCOMPARE(McpServerHelper::getText(client.callTool(QStringLiteral("echo"), QJsonObject())), QStringLiteral("{}"));

    // A slow call becomes a task
    const QJsonObject created = client.callTool(QStringLiteral("slow"), QJsonObject()).value(QStringLiteral("result")).toObject();
    QCOMPARE(created.value(QStringLiteral("resultType")).toString(), QStringLiteral("task"));
    QCOMPARE(created.value(QStringLiteral("status")).toString(), QStringLiteral("working"));
    QCOMPARE(created.value(QStringLiteral("pollIntervalMs")).toInt(), pdf::PDFMcpLimits::TASK_POLL_INTERVAL);
    QVERIFY(created.value(QStringLiteral("ttlMs")).isNull());
    QVERIFY(!created.value(QStringLiteral("createdAt")).toString().isEmpty());
    const QString taskId = created.value(QStringLiteral("taskId")).toString();
    QVERIFY(!taskId.isEmpty());

    // "durably created": tasks/get resolves at once
    const QJsonObject working = client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), taskId } })).value(QStringLiteral("result")).toObject();
    QCOMPARE(working.value(QStringLiteral("status")).toString(), QStringLiteral("working"));
    QCOMPARE(working.value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));

    // Bound to the assistant; without the extension -32021
    pdftest::McpTestClient other = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("other"));
    other.setCapabilities(tasksCapabilities);
    QCOMPARE(McpServerHelper::getErrorCode(other.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), taskId } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    pdftest::McpTestClient withoutExtension = fixture.client();
    QCOMPARE(McpServerHelper::getErrorCode(withoutExtension.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), taskId } }))), pdf::PDFMcpErrorCodes::MISSING_CLIENT_CAPABILITY);
    QCOMPARE(withoutExtension.lastResponse().status, 400);
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), QStringLiteral("unknown") } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    // notifications/tasks on a listen stream with the task id
    std::unique_ptr<pdftest::McpTestStream> listen = client.openStream(client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({ { QStringLiteral("taskIds"), QJsonArray({ taskId, QStringLiteral("foreign") }) } }) } })));
    QVERIFY(listen->waitForMessages(1, 5000));
    QCOMPARE(listen->messages().front().value(QStringLiteral("params")).toObject().value(QStringLiteral("notifications")).toObject().value(QStringLiteral("taskIds")).toArray(), QJsonArray({ taskId }));

    // tasks/update of a task, which does not wait for input
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("tasks/update"), QJsonObject({ { QStringLiteral("taskId"), taskId }, { QStringLiteral("inputResponses"), QJsonObject() } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    // The host finishes the call: the task is completed with the result
    std::shared_ptr<pdf::PDFMcpCall> slowCall;
    for (const auto& call : fixture.host.calls)
    {
        if (call->getRequest().name == QLatin1String("slow"))
        {
            slowCall = call;
        }
    }
    QVERIFY(slowCall);
    slowCall->reportProgress(1, 2, QStringLiteral("not sent"));
    slowCall->finish(McpServerHelper::createText(QStringLiteral("slow result")));
    QVERIFY(listen->waitForMessages(2, 5000));
    const QJsonObject notification = listen->messages()[1];
    QCOMPARE(notification.value(QStringLiteral("method")).toString(), QStringLiteral("notifications/tasks"));
    QCOMPARE(notification.value(QStringLiteral("params")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
    QCOMPARE(notification.value(QStringLiteral("params")).toObject().value(QStringLiteral("_meta")).toObject().contains(QStringLiteral("io.modelcontextprotocol/subscriptionId")), true);
    const QJsonObject completed = client.pollTask(taskId).value(QStringLiteral("result")).toObject();
    QCOMPARE(completed.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
    QCOMPARE(completed.value(QStringLiteral("result")).toObject().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("slow result"));
    QVERIFY(completed.value(QStringLiteral("ttlMs")).toInteger() >= pdf::PDFMcpLimits::TASK_TTL);

    // tasks/cancel cancels the call
    const QString canceledId = client.callTool(QStringLiteral("slow"), QJsonObject()).value(QStringLiteral("result")).toObject().value(QStringLiteral("taskId")).toString();
    QVERIFY(!canceledId.isEmpty());
    const size_t canceled = fixture.host.canceledCalls.size();
    QVERIFY(client.request(QStringLiteral("tasks/cancel"), QJsonObject({ { QStringLiteral("taskId"), canceledId } })).contains(QStringLiteral("result")));
    QVERIFY(fixture.waitCanceled(canceled + 1));
    QCOMPARE(fixture.host.canceledCalls.back()->getCancelReason(), pdf::PDFMcpCancelReason::TaskCanceled);
    QCOMPARE(client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), canceledId } })).value(QStringLiteral("result")).toObject().value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));

    // A task, which needs input after its creation: input_required, tasks/update, completed
    const QString askId = client.callTool(QStringLiteral("askslow"), QJsonObject()).value(QStringLiteral("result")).toObject().value(QStringLiteral("taskId")).toString();
    QVERIFY(!askId.isEmpty());
    QJsonObject waiting;
    QVERIFY(McpTestHelper::waitFor([&]()
    {
        // Polled as the poll interval asks (the rate limit of the assistant)
        QTest::qWait(100);
        waiting = client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), askId } })).value(QStringLiteral("result")).toObject();
        return waiting.value(QStringLiteral("status")).toString() == QLatin1String("input_required");
    }, 5000));
    QVERIFY(waiting.value(QStringLiteral("inputRequests")).toObject().contains(QStringLiteral("pdf4qt.choice")));
    const QJsonObject answer({ { QStringLiteral("pdf4qt.choice"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("documentId"), QStringLiteral("A") } }) } }) } });
    QVERIFY(client.request(QStringLiteral("tasks/update"), QJsonObject({ { QStringLiteral("taskId"), askId }, { QStringLiteral("inputResponses"), answer } })).contains(QStringLiteral("result")));
    const QJsonObject answered = client.pollTask(askId).value(QStringLiteral("result")).toObject();
    QCOMPARE(answered.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
    QCOMPARE(answered.value(QStringLiteral("result")).toObject().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("chosen A"));

    // A finished task is kept for its TTL
    pdf::PDFMcpClock::setOffset(pdf::PDFMcpLimits::TASK_TTL + 60000);
    fixture.server->checkTimeouts();
    QVERIFY(McpTestHelper::waitFor([&]() { return McpServerHelper::getErrorCode(client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), taskId } }))) == pdf::PDFMcpErrorCodes::INVALID_PARAMS; }, 5000));
    pdf::PDFMcpClock::setOffset(0);

    // A legacy client never gets a task
    pdftest::McpTestClient legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
    legacy.setCapabilities(tasksCapabilities);
    legacy.initialize();
    std::unique_ptr<pdftest::McpTestStream> legacyCall = legacy.openStream(legacy.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() } })));
    QVERIFY(legacyCall->waitForComments(1, 5000));
    QVERIFY(legacyCall->messages().empty());

    // The limits of the store
    pdf::PDFMcpTaskStore store;
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_TASKS_PER_ASSISTANT; ++i)
    {
        QVERIFY(store.create(QStringLiteral("a:1"), 0));
    }
    QVERIFY(!store.create(QStringLiteral("a:1"), 0));
    for (int i = pdf::PDFMcpLimits::MAXIMUM_TASKS_PER_ASSISTANT; i < pdf::PDFMcpLimits::MAXIMUM_TASKS; ++i)
    {
        QVERIFY(store.create(QStringLiteral("a:%1").arg(i % 3 + 2), 0));
    }
    QVERIFY(!store.create(QStringLiteral("a:9"), 0));
    QCOMPARE(store.getTaskCount(), pdf::PDFMcpLimits::MAXIMUM_TASKS);
    QCOMPARE(store.purge(1000000, 1), 0);
    QVERIFY(!store.findOwned(store.getTaskIds().front(), QStringLiteral("a:none")));
}

void McpTest::assistantsIdentity()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    QSignalSpy changedSpy(fixture.server.get(), &pdf::PDFMcpServer::assistantsChanged);

    // [D2]: the key of the token, the name and the address; one Connected event
    pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("claude-code"));
    client.listTools();
    client.listTools();
    const QString key = pdf::PDFMcpAssistantRegistry::createModernKey(McpServerHelper::token(), QStringLiteral("claude-code"), QStringLiteral("127.0.0.1"));
    QVERIFY(fixture.waitEvent(pdf::PDFMcpAssistantEvent::Kind::Connected));
    QCOMPARE(fixture.countEvents(pdf::PDFMcpAssistantEvent::Kind::Connected), 1);
    QCOMPARE(fixture.host.events.front().key, key);
    QCOMPARE(fixture.host.events.front().name, QStringLiteral("claude-code"));
    QTRY_VERIFY_WITH_TIMEOUT(changedSpy.count() >= 1, 5000);
    std::vector<pdf::PDFMcpAssistantInfo> assistants = fixture.server->getAssistants();
    QCOMPARE(assistants.size(), size_t(1));
    QCOMPARE(assistants.front().key, key);
    QVERIFY(assistants.front().connectedAt > 0);
    QCOMPARE(assistants.front().address, QStringLiteral("127.0.0.1"));
    QVERIFY(!assistants.front().isRemote);

    // Another name is another assistant; a missing clientInfo is "MCP client"
    fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("codex-mcp-client")).listTools();
    QTRY_COMPARE_WITH_TIMEOUT(fixture.server->getAssistants().size(), size_t(2), 5000);
    QCOMPARE(fixture.host.calls.size(), size_t(0));

    // Idle after 30 minutes without a request; a listen stream keeps an assistant
    pdftest::McpTestClient listener = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("listener"));
    std::unique_ptr<pdftest::McpTestStream> stream = listener.openStream(listener.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject() } })));
    QVERIFY(stream->waitForMessages(1, 5000));
    pdf::PDFMcpClock::setOffset(pdf::PDFMcpLimits::ASSISTANT_IDLE_TIME + 1000);
    fixture.server->checkTimeouts();
    QVERIFY(McpTestHelper::waitFor([&]() { return fixture.countEvents(pdf::PDFMcpAssistantEvent::Kind::Idle) == 2; }, 5000));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.server->getAssistants().size(), size_t(1), 5000);
    QCOMPARE(fixture.server->getAssistants().front().name, QStringLiteral("listener"));
    stream->close();
    QTest::qWait(200);
    pdf::PDFMcpClock::setOffset(2 * (pdf::PDFMcpLimits::ASSISTANT_IDLE_TIME + 1000));
    fixture.server->checkTimeouts();
    QVERIFY(McpTestHelper::waitFor([&]() { return fixture.countEvents(pdf::PDFMcpAssistantEvent::Kind::Idle) == 3; }, 5000));
    pdf::PDFMcpClock::setOffset(0);

    // The next request connects it again
    client.listTools();
    QVERIFY(McpTestHelper::waitFor([&]() { return fixture.countEvents(pdf::PDFMcpAssistantEvent::Kind::Connected) == 4; }, 5000));

    // Disconnect: the calls canceled (denied_disconnected), the listen streams ended gracefully
    std::unique_ptr<pdftest::McpTestStream> listen = client.openStream(client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject() } })));
    QVERIFY(listen->waitForMessages(1, 5000));
    std::unique_ptr<pdftest::McpTestStream> call = client.openStream(client.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() } })));
    QVERIFY(fixture.waitCalls(1));
    fixture.server->disconnectAssistant(key);
    QVERIFY(fixture.waitCanceled(1));
    QCOMPARE(fixture.host.canceledCalls.back()->getCancelReason(), pdf::PDFMcpCancelReason::Disconnected);
    QVERIFY(fixture.waitEvent(pdf::PDFMcpAssistantEvent::Kind::Disconnected));
    QVERIFY(listen->waitForMessages(2, 5000));
    QCOMPARE(listen->messages().back().value(QStringLiteral("result")).toObject().value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));

    // The host answers the canceled call (its confirmation ends)
    fixture.host.canceledCalls.back()->finish(pdf::PDFMcpCallResult::createToolError(QStringLiteral("denied")));
    QVERIFY(call->waitForEnd(5000));
    QVERIFY(McpServerHelper::getText(call->response().json()).contains(QStringLiteral("denied")));

    // A host, which does not answer a canceled call: the grace time
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.cancelGraceTime = 200;
    McpServerFixture graceFixture(config);
    pdftest::McpTestClient graceClient = graceFixture.client();
    std::unique_ptr<pdftest::McpTestStream> graceCall = graceClient.openStream(graceClient.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() } })));
    QVERIFY(graceFixture.waitCalls(1));
    graceFixture.server->disconnectAll();
    QVERIFY(graceCall->waitForEnd(5000));
    QCOMPARE(McpServerHelper::getErrorCode(graceCall->response().json()), pdf::PDFMcpErrorCodes::INTERNAL_ERROR);

    // disconnectAll ends the legacy sessions too; invalid keys are ignored
    pdftest::McpTestClient legacy = graceFixture.client(pdftest::McpTestEra::Legacy20251125);
    legacy.initialize();
    graceFixture.server->disconnectAssistant(QStringLiteral("not a key"));
    graceFixture.server->disconnectAll();
    QVERIFY(McpTestHelper::waitFor([&]() { legacy.request(QStringLiteral("ping")); return legacy.lastResponse().status == 404; }, 5000));
    QTRY_VERIFY_WITH_TIMEOUT(graceFixture.server->getAssistants().empty(), 5000);
}

void McpTest::rateLimits()
{
    // The token bucket: 50 at once, then 20 per second
    pdf::PDFMcpRateLimiter limiter;
    qint64 retryAfter = 0;
    for (int i = 0; i < pdf::PDFMcpLimits::RATE_BURST; ++i)
    {
        QVERIFY(limiter.allow(QStringLiteral("a"), 1000, &retryAfter));
    }
    QVERIFY(!limiter.allow(QStringLiteral("a"), 1000, &retryAfter));
    QCOMPARE(retryAfter, qint64(50));
    QVERIFY(limiter.allow(QStringLiteral("b"), 1000, &retryAfter));
    QVERIFY(limiter.allow(QStringLiteral("a"), 1050, &retryAfter));
    QVERIFY(!limiter.allow(QStringLiteral("a"), 1050, &retryAfter));
    int allowed = 0;
    for (int i = 0; i < 100; ++i)
    {
        allowed += limiter.allow(QStringLiteral("a"), 2050, &retryAfter) ? 1 : 0;
    }
    QCOMPARE(allowed, pdf::PDFMcpLimits::RATE_SUSTAINED);
    limiter.remove(QStringLiteral("a"));
    QVERIFY(limiter.allow(QStringLiteral("a"), 2050, &retryAfter));

    // Bounded keys
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS * 2; ++i)
    {
        limiter.allow(QString::number(i), 3000 + i, &retryAfter);
    }
    QVERIFY(limiter.getKeyCount() <= pdf::PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS);

    // The throttle of the authorization
    pdf::PDFMcpAuthorizationThrottle throttle;
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_AUTHORIZATION_FAILURES - 1; ++i)
    {
        throttle.recordFailure(QStringLiteral("1.2.3.4"), 1000);
    }
    QVERIFY(!throttle.isBlocked(QStringLiteral("1.2.3.4"), 1000, &retryAfter));
    throttle.recordFailure(QStringLiteral("1.2.3.4"), 1000);
    QVERIFY(throttle.isBlocked(QStringLiteral("1.2.3.4"), 1000, &retryAfter));
    QCOMPARE(retryAfter, pdf::PDFMcpLimits::AUTHORIZATION_BLOCK_TIME);
    QVERIFY(!throttle.isBlocked(QStringLiteral("5.6.7.8"), 1000, &retryAfter));
    QVERIFY(!throttle.isBlocked(QStringLiteral("1.2.3.4"), 1000 + pdf::PDFMcpLimits::AUTHORIZATION_BLOCK_TIME, &retryAfter));
    for (int i = 0; i < 19; ++i)
    {
        throttle.recordFailure(QStringLiteral("9.9.9.9"), qint64(i) * 10000);
    }
    QVERIFY(!throttle.isBlocked(QStringLiteral("9.9.9.9"), 190000, &retryAfter));
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS * 2; ++i)
    {
        throttle.recordFailure(QString::number(i), 5000);
    }
    QVERIFY(throttle.getAddressCount() <= pdf::PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS);

    // Over HTTP: 429 with -31001, retryAfterMs and Retry-After
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    pdftest::McpTestClient client = fixture.client();
    bool isLimited = false;
    for (int i = 0; i < 300 && !isLimited; ++i)
    {
        const QJsonObject response = client.listTools();
        if (client.lastResponse().status == 429)
        {
            isLimited = true;
            QCOMPARE(McpServerHelper::getErrorCode(response), pdf::PDFMcpErrorCodes::RATE_LIMITED);
            QVERIFY(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("retryAfterMs")).toInteger() > 0);
            QVERIFY(!client.lastResponse().header("Retry-After").isEmpty());
        }
    }
    QVERIFY(isLimited);

    // Another assistant is not limited; at most 16 calls in flight per assistant
    pdftest::McpTestClient calls = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("calls"));
    std::vector<std::unique_ptr<pdftest::McpTestStream>> streams;
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_CALLS_PER_ASSISTANT; ++i)
    {
        streams.push_back(calls.openStream(calls.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() } }))));
    }
    QVERIFY(fixture.waitCalls(size_t(pdf::PDFMcpLimits::MAXIMUM_CALLS_PER_ASSISTANT)));
    QCOMPARE(McpServerHelper::getErrorCode(calls.callTool(QStringLiteral("echo"), QJsonObject())), pdf::PDFMcpErrorCodes::RATE_LIMITED);
    QCOMPARE(calls.lastResponse().status, 429);

    // completion/complete: 20 per second
    pdftest::McpTestClient completion = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("completion"));
    const QJsonObject params({ { QStringLiteral("ref"), QJsonObject({ { QStringLiteral("type"), QStringLiteral("ref/prompt") }, { QStringLiteral("name"), QStringLiteral("summarize-document") } }) },
                               { QStringLiteral("argument"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("document") }, { QStringLiteral("value"), QStringLiteral("") } }) } });
    bool isCompletionLimited = false;
    for (int i = 0; i < 60 && !isCompletionLimited; ++i)
    {
        completion.request(QStringLiteral("completion/complete"), params);
        isCompletionLimited = completion.lastResponse().status == 429;
    }
    QVERIFY(isCompletionLimited);
}

/// A worker of loadManyClients: up to 3 raw requests in flight on its own
/// sockets (16 workers use 48 of the 64 connections - the closing connections
/// are counted too); a request limited by the rate (429) or by the connection
/// limit (503 before the stop) is sent again after a while
class McpLoadWorker
{
public:
    McpLoadWorker(quint16 port, int index, int calls) :
        m_port(port),
        m_index(index),
        m_calls(calls)
    {

    }

    void run()
    {
        // Legacy workers initialize their sessions first (synchronously)
        const pdftest::McpTestEra era = (m_index % 3 == 0) ? pdftest::McpTestEra::Modern : (m_index % 3 == 1 ? pdftest::McpTestEra::Legacy20251125 : pdftest::McpTestEra::Legacy20250618);
        pdftest::McpTestClient client(m_port, era);
        client.setToken(McpServerHelper::token());
        client.setClientInfo(QStringLiteral("load-%1").arg(m_index));
        if (era != pdftest::McpTestEra::Modern)
        {
            client.initialize();
            if (client.getSessionId().isEmpty())
            {
                isStoppedEarly = true;
                return;
            }
        }

        class Item
        {
        public:
            qint64 id = 0;
            int kind = 0;
            qint64 notBefore = 0;
        };

        class InFlight
        {
        public:
            Item item;
            std::unique_ptr<QTcpSocket> socket;
            QByteArray data;
        };

        std::deque<Item> queue;
        for (int i = 0; i < m_calls; ++i)
        {
            Item item;
            item.id = qint64(m_index) * 100000 + i;
            item.kind = i % 10;
            queue.push_back(item);
        }

        QElapsedTimer timer;
        timer.start();
        std::vector<InFlight> inFlight;
        while ((!queue.empty() || !inFlight.empty()) && timer.elapsed() < qint64(120000) * pdftest::McpTestClient::getTimeoutFactor())
        {
            while (!queue.empty() && inFlight.size() < 3 && queue.front().notBefore <= timer.elapsed())
            {
                // Fast, slow (finished later by the host) and canceled calls
                InFlight request;
                request.item = queue.front();
                queue.pop_front();
                const QString tool = request.item.kind == 7 ? QStringLiteral("slowfinish") : QStringLiteral("echo");
                QJsonObject message = client.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), tool }, { QStringLiteral("arguments"), QJsonObject({ { QStringLiteral("value"), QString::number(request.item.id) } }) } }));
                message.insert(QStringLiteral("id"), request.item.id);
                request.socket = std::make_unique<QTcpSocket>();
                request.socket->connectToHost(QHostAddress::LocalHost, m_port);
                request.socket->write(client.createHttpRequest("POST", message));
                inFlight.push_back(std::move(request));
            }

            QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 5);
            for (size_t i = 0; i < inFlight.size();)
            {
                InFlight& request = inFlight[i];
                if (request.socket->isOpen())
                {
                    request.data += request.socket->readAll();
                }
                const bool isClosed = request.socket->state() == QAbstractSocket::UnconnectedState;
                const pdftest::McpTestResponse response = pdftest::McpTestClient::parseResponse(request.data, isClosed);
                bool isDone = true;
                if (request.item.kind == 9 && request.socket->state() == QAbstractSocket::ConnectedState && request.socket->bytesToWrite() == 0)
                {
                    // The client gives up its request (the server cancels the call)
                    request.socket->abort();
                    ++canceled;
                }
                else if (response.status == 429)
                {
                    // Limited by the rate: sent again after retryAfterMs
                    ++limited;
                    Item retry = request.item;
                    retry.notBefore = timer.elapsed() + qMax<qint64>(20, response.json().value(QStringLiteral("error")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("retryAfterMs")).toInteger());
                    queue.push_back(retry);
                }
                else if (response.status != 0 && response.isComplete)
                {
                    const QJsonObject json = response.json();
                    if (response.status == 503 && !m_isStopping.load())
                    {
                        // The limit of the connections (64): sent again a bit later
                        ++busy;
                        Item retry = request.item;
                        retry.notBefore = timer.elapsed() + 20;
                        queue.push_back(retry);
                    }
                    else if (response.status == 503)
                    {
                        ++stopped;
                    }
                    else if (json.value(QStringLiteral("id")).toInteger() == request.item.id && json.contains(QStringLiteral("result")))
                    {
                        ++matched;
                    }
                    else
                    {
                        ++failures;
                    }
                }
                else if (isClosed)
                {
                    ++closed;
                }
                else
                {
                    isDone = false;
                }

                if (isDone)
                {
                    inFlight.erase(inFlight.begin() + qsizetype(i));

                    // After the stop of the server the rest is not sent (a refused
                    // connection to the loopback takes seconds on Windows)
                    if (m_isStopping.load() && (stopped > 0 || closed > 0))
                    {
                        abandoned += int(queue.size());
                        queue.clear();
                    }
                }
                else
                {
                    ++i;
                }
            }
        }
        unfinished = int(inFlight.size() + queue.size());
    }

    std::atomic<int> matched = 0;
    std::atomic<int> canceled = 0;
    std::atomic<int> stopped = 0;
    std::atomic<int> closed = 0;
    std::atomic<int> limited = 0;
    std::atomic<int> failures = 0;
    std::atomic<int> unfinished = 0;
    std::atomic<bool> isStoppedEarly = false;
    std::atomic<int> busy = 0;
    std::atomic<int> abandoned = 0;

    /// The test stops the server (503 is final then)
    void setStopping() { m_isStopping.store(true); }

private:
    std::atomic<bool> m_isStopping = false;
    quint16 m_port = 0;
    int m_index = 0;
    int m_calls = 0;
};

void McpTest::loadManyClients()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);

    // "slowfinish" is finished by the host after a while
    fixture.host.setHandler([](const std::shared_ptr<pdf::PDFMcpCall>& call)
    {
        if (call->getRequest().name == QLatin1String("slowfinish"))
        {
            QTimer::singleShot(50, qApp, [call]() { call->finish(McpServerHelper::createText(QStringLiteral("slow"))); });
            return;
        }
        McpServerHelper::handle(call);
    });

    for (int phase = 0; phase < 2; ++phase)
    {
        constexpr int CLIENTS = 16;
        const int calls = phase == 0 ? 200 : 100;
        std::vector<std::unique_ptr<McpLoadWorker>> workers;
        std::vector<std::unique_ptr<QThread>> threads;
        for (int i = 0; i < CLIENTS; ++i)
        {
            workers.push_back(std::make_unique<McpLoadWorker>(fixture.port(), i, calls));
            McpLoadWorker* worker = workers.back().get();
            threads.emplace_back(QThread::create([worker]() { worker->run(); }));
            threads.back()->start();
        }

        QElapsedTimer timer;
        timer.start();
        if (phase == 1)
        {
            // The server stops in the middle of the run
            QTest::qWait(300);
            for (const auto& worker : workers)
            {
                worker->setStopping();
            }
            fixture.server->stop();
        }

        bool isFinished = false;
        while (!isFinished && timer.elapsed() < qint64(150000) * McpTestHelper::factor())
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
            isFinished = true;
            for (const auto& thread : threads)
            {
                isFinished = isFinished && thread->isFinished();
            }
        }
        QVERIFY(isFinished);
        for (const auto& thread : threads)
        {
            thread->wait();
        }

        int matched = 0;
        int canceled = 0;
        int stopped = 0;
        int closed = 0;
        int limited = 0;
        int failures = 0;
        int unfinished = 0;
        int stoppedEarly = 0;
        int busy = 0;
        for (const auto& worker : workers)
        {
            matched += worker->matched;
            canceled += worker->canceled;
            stopped += worker->stopped;
            closed += worker->closed;
            limited += worker->limited;
            failures += worker->failures;
            unfinished += worker->unfinished;
            stoppedEarly += worker->isStoppedEarly ? 1 : 0;
            busy += worker->busy;
        }
        qInfo("loadManyClients phase %d: %d matched, %d canceled, %d stopped, %d closed, %d limited, %d busy, %d failures, %d unfinished, %lld ms",
              phase, matched, canceled, stopped, closed, limited, busy, failures, unfinished, timer.elapsed());
        QCOMPARE(failures, 0);
        QCOMPARE(unfinished, 0);
        if (phase == 0)
        {
            // Every call got its own response (ids matched) or was canceled by the client
            QCOMPARE(stoppedEarly, 0);
            QCOMPARE(matched + canceled, CLIENTS * calls);
            QVERIFY(matched > CLIENTS * calls / 2);
        }
        else
        {
            QVERIFY(stopped + closed + stoppedEarly > 0);
            QCOMPARE(fixture.server->getState(), pdf::PDFMcpServerState::Stopped);
        }
    }
}

void McpTest::shutdownOrder()
{
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);

    // A listen stream, a GET stream, a pending call and a task
    pdftest::McpTestClient client = fixture.client();
    std::unique_ptr<pdftest::McpTestStream> listen = client.openStream(client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject() } })));
    QVERIFY(listen->waitForMessages(1, 5000));

    pdftest::McpTestClient legacy = fixture.client(pdftest::McpTestEra::Legacy20251125);
    legacy.initialize();
    legacy.request(QStringLiteral("logging/setLevel"), QJsonObject({ { QStringLiteral("level"), QStringLiteral("info") } }));
    std::unique_ptr<pdftest::McpTestStream> getStream = legacy.openGetStream();
    QVERIFY(getStream->waitForComments(1, 5000));

    std::unique_ptr<pdftest::McpTestStream> call = client.openStream(client.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() } })));
    QVERIFY(fixture.waitCalls(1));

    pdftest::McpTestClient tasks = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("tasks"));
    tasks.setCapabilities(QJsonObject({ { QStringLiteral("extensions"), QJsonObject({ { QStringLiteral("io.modelcontextprotocol/tasks"), QJsonObject() } }) } }));
    QCOMPARE(tasks.callTool(QStringLiteral("slow"), QJsonObject()).value(QStringLiteral("result")).toObject().value(QStringLiteral("resultType")).toString(), QStringLiteral("task"));

    // The stop: synchronous, every call canceled (ServerStopped), the streams ended
    QElapsedTimer timer;
    timer.start();
    fixture.server->stop();
    QVERIFY(timer.elapsed() < 5000 * McpTestHelper::factor());
    QCOMPARE(fixture.server->getState(), pdf::PDFMcpServerState::Stopped);
    QVERIFY(fixture.server->getAssistants().empty());

    QVERIFY(fixture.waitCanceled(2));
    for (const auto& canceled : fixture.host.canceledCalls)
    {
        QCOMPARE(canceled->getCancelReason(), pdf::PDFMcpCancelReason::ServerStopped);
    }
    QVERIFY(fixture.waitEvent(pdf::PDFMcpAssistantEvent::Kind::SessionEnded));

    QVERIFY(listen->waitForEnd(5000));
    QCOMPARE(listen->response().json().value(QStringLiteral("result")).toObject().value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
    QVERIFY(getStream->waitForEnd(5000));
    bool hasStopMessage = false;
    for (const QJsonObject& message : getStream->messages())
    {
        hasStopMessage = hasStopMessage || message.value(QStringLiteral("method")).toString() == QLatin1String("notifications/message");
    }
    QVERIFY(hasStopMessage);
    QVERIFY(call->waitForEnd(5000));
    QCOMPARE(McpServerHelper::getErrorCode(call->response().json()), pdf::PDFMcpErrorCodes::SERVER_STOPPING);

    // A finish after the stop is dropped; the server starts again
    for (const auto& pending : fixture.host.calls)
    {
        pending->finish(McpServerHelper::createText(QStringLiteral("late")));
    }
    QVERIFY(fixture.server->start(McpServerHelper::createConfig()));
    QTRY_COMPARE_WITH_TIMEOUT(fixture.server->getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());
    QCOMPARE(fixture.client().discover().value(QStringLiteral("supportedVersions")).toArray().size(), 3);

    // The destruction of a running server with streams open
    std::unique_ptr<pdftest::McpTestStream> open = fixture.client().openStream(fixture.client().createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject() } })));
    QVERIFY(open->waitForMessages(1, 5000));
    fixture.server.reset();
    QVERIFY(open->waitForEnd(5000));
}

void McpTest::progressWhileStopping()
{
    // Review RW7-0: workers report progress in a loop while the server stops and
    // the dispatcher (the sink of the calls) is destroyed - no event reaches it
    // (run it under ASan with --repeat)
    for (int round = 0; round < 3; ++round)
    {
        std::atomic<bool> isStopped = false;
        std::vector<std::thread> workers;

        // The workers end also when a check fails
        class JoinGuard
        {
        public:
            JoinGuard(std::atomic<bool>& stopped, std::vector<std::thread>& threads) : m_stopped(stopped), m_threads(threads) { }
            ~JoinGuard()
            {
                m_stopped.store(true);
                for (std::thread& thread : m_threads)
                {
                    if (thread.joinable())
                    {
                        thread.join();
                    }
                }
            }

        private:
            std::atomic<bool>& m_stopped;
            std::vector<std::thread>& m_threads;
        };
        JoinGuard joinGuard(isStopped, workers);
        McpServerFixture fixture;
        QVERIFY(fixture.isRunning);
        fixture.host.setHandler([&workers, &isStopped](const std::shared_ptr<pdf::PDFMcpCall>& call)
        {
            workers.emplace_back([call, &isStopped]()
            {
                double progress = 0.0;
                while (!isStopped.load())
                {
                    call->reportProgress(progress += 1.0, 0.0, QStringLiteral("working"));
                    call->log(QStringLiteral("error"), QStringLiteral("log"));
                }
            });
        });

        pdftest::McpTestClient client = fixture.client();
        std::vector<std::unique_ptr<pdftest::McpTestStream>> streams;
        for (int i = 0; i < 4; ++i)
        {
            QJsonObject request = client.createRequest(QStringLiteral("tools/call"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("slow") }, { QStringLiteral("arguments"), QJsonObject() },
                { QStringLiteral("_meta"), QJsonObject({ { QStringLiteral("progressToken"), i }, { QStringLiteral("io.modelcontextprotocol/logLevel"), QStringLiteral("error") } }) } }));
            streams.push_back(client.openStream(request));
        }
        QVERIFY(fixture.waitCalls(4));
        QVERIFY(streams.front()->waitForMessages(2, 5000));

        // The stop while the workers report; they go on after it
        fixture.server->stop();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 200)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }

        // A new run, the old calls still reporting
        QVERIFY(fixture.server->start(McpServerHelper::createConfig()));
        QTRY_COMPARE_WITH_TIMEOUT(fixture.server->getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());
        fixture.server.reset();

        isStopped.store(true);
        for (std::thread& worker : workers)
        {
            worker.join();
        }
        for (const auto& call : fixture.host.calls)
        {
            QVERIFY(call->isCanceled());
        }
    }

    // The sink cleared: nothing is posted any more
    pdf::PDFMcpCallRequest request;
    request.client.hasProgressToken = true;
    std::shared_ptr<pdf::PDFMcpCall> call = pdf::PDFMcpCall::create(request);
    {
        pdf::PDFMcpCallSink sink;
        call->setSink(&sink);
        call->setSink(nullptr);
    }
    call->reportProgress(1, 2, QString());
    QCoreApplication::processEvents();
    QCOMPARE(call->takeEvents().size(), size_t(1));
}

void McpTest::stateChangedSlots()
{
    // Review RW7-0: a slot of stateChanged stops or deletes the server
    pdftest::FakeMcpHost host;

    // Stopped from the announcement of Starting
    pdf::PDFMcpServer stopped(&host);
    QMetaObject::Connection connection = QObject::connect(&stopped, &pdf::PDFMcpServer::stateChanged, &stopped, [&stopped]()
    {
        if (stopped.getState() == pdf::PDFMcpServerState::Starting)
        {
            stopped.stop();
        }
    });
    QVERIFY(stopped.start(McpServerHelper::createConfig()));
    QCOMPARE(stopped.getState(), pdf::PDFMcpServerState::Stopped);
    QObject::disconnect(connection);
    QTest::qWait(100);
    QCOMPARE(stopped.getState(), pdf::PDFMcpServerState::Stopped);
    QVERIFY(stopped.start(McpServerHelper::createConfig()));
    QTRY_COMPARE_WITH_TIMEOUT(stopped.getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());
    stopped.stop();

    // Deleted from the announcements of Starting, Running and Stopping
    for (pdf::PDFMcpServerState deleteState : { pdf::PDFMcpServerState::Starting, pdf::PDFMcpServerState::Running, pdf::PDFMcpServerState::Stopping })
    {
        QPointer<pdf::PDFMcpServer> server = new pdf::PDFMcpServer(&host);
        QObject::connect(server.data(), &pdf::PDFMcpServer::stateChanged, server.data(), [server, deleteState]()
        {
            if (server && server->getState() == deleteState)
            {
                delete server.data();
            }
        });
        QVERIFY(server->start(McpServerHelper::createConfig()));
        if (server && deleteState == pdf::PDFMcpServerState::Stopping)
        {
            QTRY_COMPARE_WITH_TIMEOUT(server->getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());
            server->stop();
        }
        QTRY_VERIFY_WITH_TIMEOUT(server.isNull(), 5000 * McpTestHelper::factor());
    }
}

void McpTest::responseHeadersSanitized()
{
    // Review RW7-0: no header of the server splits a response (defense in depth)
    pdf::PDFMcpHttpResponse response;
    response.status = 200;
    response.headers.emplace_back(QByteArray("X-Test"), QByteArray("a\r\nInjected: b\0c", 16));
    response.headers.emplace_back(QByteArray("X-\nBad"), QByteArray("v"));
    response.headers.emplace_back(QByteArray("\r\n"), QByteArray("dropped"));
    for (const QByteArray& data : { response.serialize("1"), response.serializeStreamHead("1") })
    {
        const QByteArray head = data.left(data.indexOf("\r\n\r\n"));
        QVERIFY(head.contains("X-Test: aInjected: bc\r\n"));
        QVERIFY(head.contains("X-Bad: v\r\n"));
        QVERIFY(!head.contains("dropped"));
        QVERIFY(!head.contains('\0'));
        for (const QByteArray& line : head.split('\n'))
        {
            QVERIFY(line.endsWith('\r') || line == head.split('\n').back());
            QVERIFY(!line.startsWith("Injected"));
        }
    }
    QCOMPARE(pdf::PDFMcpHttpResponse::removeLineBreaks(QByteArray("a\rb\nc\0d", 7)), QByteArray("abcd"));
}

void McpTest::ipv6PortInUse()
{
    // Coordinator (security): another program on [::1]:port would get the
    // requests and the token of a client resolving localhost to ::1 first - the
    // server does not run then (port in use); only a computer without IPv6 is accepted
    QVERIFY(!pdf::PDFMcpControl::isIgnorableIPv6Error(QAbstractSocket::AddressInUseError));
    QVERIFY(!pdf::PDFMcpControl::isIgnorableIPv6Error(QAbstractSocket::SocketAccessError));
    QVERIFY(pdf::PDFMcpControl::isIgnorableIPv6Error(QAbstractSocket::UnsupportedSocketOperationError));
    QVERIFY(pdf::PDFMcpControl::isIgnorableIPv6Error(QAbstractSocket::SocketAddressNotAvailableError));

    QTcpServer foreign;
    if (!foreign.listen(QHostAddress::LocalHostIPv6, 0))
    {
        QSKIP("No IPv6 loopback on this computer (the ignored case of the rule)");
    }

    // The IPv4 port is free, only ::1 is taken
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, foreign.serverPort()));
    probe.close();

    pdftest::FakeMcpHost host;
    pdf::PDFMcpServer server(&host);
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.port = foreign.serverPort();
    config.portProbeTimeout = 300;
    QVERIFY(server.start(config));
    QTRY_COMPARE_WITH_TIMEOUT(server.getState(), pdf::PDFMcpServerState::PortInUseByOtherProgram, 5000 * McpTestHelper::factor());
    QCOMPARE(server.getPort(), quint16(0));
    QVERIFY(!server.isRunning());

    // Nothing of PDF4QT listens on the IPv4 port
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, foreign.serverPort());
    QVERIFY(!socket.waitForConnected(1000));

    // A free port (0) takes another port, when ::1 of the first one is taken
    server.stop();
    QVERIFY(server.start(McpServerHelper::createConfig()));
    QTRY_COMPARE_WITH_TIMEOUT(server.getState(), pdf::PDFMcpServerState::Running, 5000 * McpTestHelper::factor());
    pdftest::McpTestClient ipv6(server.getPort(), pdftest::McpTestEra::Modern, QStringLiteral("::1"));
    ipv6.setToken(McpServerHelper::token());
    QVERIFY(ipv6.discover().contains(QStringLiteral("supportedVersions")));
    server.stop();
}

void McpTest::unauthenticatedBodyNotRead()
{
    // Review RS7: a request without the token, whose large body has not
    // arrived, is answered after its head and the connection closes - a client
    // without the token cannot make the server buffer 8 MiB x 64 connections
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.bodyTimeout = pdf::PDFMcpLimits::BODY_TIMEOUT;
    McpServerFixture fixture(config);
    QVERIFY(fixture.isRunning);
    const quint16 port = fixture.port();
    const QByteArray discover = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\",\"params\":{" + McpServerHelper::modernMeta() + "}}";
    const QList<QPair<QByteArray, QByteArray>> headers = McpServerHelper::modernHeaders("server/discover");

    const auto sendHead = [&](const QByteArray& authorization, QByteArray* answer) -> bool
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (!socket.waitForConnected(5000))
        {
            return false;
        }

        QByteArray head = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
                          "\r\nAccept: application/json, text/event-stream\r\nContent-Type: application/json\r\nMCP-Protocol-Version: 2026-07-28\r\nMcp-Method: server/discover\r\n";
        if (!authorization.isEmpty())
        {
            head += "Authorization: " + authorization + "\r\n";
        }
        head += "Content-Length: " + QByteArray::number(pdf::PDFMcpLimits::MAXIMUM_BODY_SIZE) + "\r\n\r\n";
        socket.write(head + QByteArray(1024, ' '));
        QByteArray data;
        const bool isAnswered = McpTestHelper::waitFor([&]() { data += socket.readAll(); return data.contains("\r\n\r\n"); }, 5000);
        *answer = data;
        return isAnswered;
    };

    // (the loopback filter of Windows holds only an incomplete head, not a body)
    QByteArray answer;
    QVERIFY(sendHead("Bearer wrong", &answer));
    QVERIFY2(answer.startsWith("HTTP/1.1 401"), answer.constData());
    QVERIFY(answer.contains("WWW-Authenticate: Bearer realm=\"pdf4qt\""));
    QVERIFY(answer.contains("Connection: close"));

    // Without Authorization too; each early answer counts as a failure of the address
    QVERIFY(sendHead(QByteArray(), &answer));
    QVERIFY2(answer.startsWith("HTTP/1.1 401"), answer.constData());
    for (int i = 2; i < pdf::PDFMcpLimits::MAXIMUM_AUTHORIZATION_FAILURES; ++i)
    {
        QVERIFY(sendHead("Bearer wrong", &answer));
        QVERIFY2(answer.startsWith("HTTP/1.1 401"), answer.constData());
    }
    QVERIFY(sendHead("Bearer wrong", &answer));
    QVERIFY2(answer.startsWith("HTTP/1.1 429"), answer.constData());
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, headers)).status, 429);
    pdf::PDFMcpClock::setOffset(pdf::PDFMcpLimits::AUTHORIZATION_BLOCK_TIME + 1000);

    // The token: a large body is read as before (white space after the message)
    const QByteArray largeBody = discover + QByteArray(pdf::PDFMcpConnection::EARLY_REJECTION_SIZE * 2, ' ');
    const pdftest::McpTestResponse large = fixture.raw(McpServerHelper::createRawPost(port, largeBody, headers));
    QCOMPARE(large.status, 200);
    QVERIFY(large.json().value(QStringLiteral("result")).toObject().contains(QStringLiteral("supportedVersions")));

    // A small body without the token is read and answered as before
    QCOMPARE(fixture.raw(McpServerHelper::createRawPost(port, discover, headers, false)).status, 401);

    // A server without the token reads the large body
    pdf::PDFMcpServerConfig open = McpServerHelper::createConfig();
    open.requireToken = false;
    McpServerFixture openFixture(open);
    QVERIFY(openFixture.isRunning);
    QCOMPARE(openFixture.raw(McpServerHelper::createRawPost(openFixture.port(), largeBody, headers, false)).status, 200);
    pdf::PDFMcpClock::setOffset(0);

    // The parser: the head without the body, nothing more is parsed
    pdf::PDFMcpHttpParser parser;
    QCOMPARE(parser.feed("POST /mcp HTTP/1.1\r\nHost: a\r\nContent-Length: 100\r\n\r\n0123456789"), pdf::PDFMcpHttpParser::Status::NeedMoreData);
    QCOMPARE(parser.getMissingBodySize(), qsizetype(90));
    const pdf::PDFMcpHttpRequest head = parser.takeHead();
    QCOMPARE(head.method, QByteArray("POST"));
    QVERIFY(head.body.isEmpty());
    QCOMPARE(parser.getMissingBodySize(), qsizetype(0));
    QCOMPARE(parser.feed(QByteArray(90, 'x')), pdf::PDFMcpHttpParser::Status::Error);
}

void McpTest::assistantsLimit()
{
    // Review RS7: clientInfo.name is chosen by the client - every new name is a
    // new assistant with its own limits (calls, streams, tasks, rates) and a row
    // of the page AI, so their number is bounded
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_ASSISTANTS; ++i)
    {
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("forged-%1").arg(i));
        client.discover();
        QVERIFY2(client.lastResponse().status == 200, qPrintable(QString::number(i)));
    }
    QTRY_COMPARE_WITH_TIMEOUT(fixture.server->getAssistants().size(), size_t(pdf::PDFMcpLimits::MAXIMUM_ASSISTANTS), 5000 * McpTestHelper::factor());

    pdftest::McpTestClient refused = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("one-more"));
    refused.discover();
    const QJsonObject response = refused.lastResponse().json();
    QCOMPARE(refused.lastResponse().status, 429);
    QCOMPARE(McpServerHelper::getErrorCode(response), pdf::PDFMcpErrorCodes::RATE_LIMITED);
    QVERIFY(!refused.lastResponse().header("Retry-After").isEmpty());
    QCOMPARE(fixture.server->getAssistants().size(), size_t(pdf::PDFMcpLimits::MAXIMUM_ASSISTANTS));

    // A connected assistant goes on; an idle one frees its place
    pdftest::McpTestClient known = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("forged-7"));
    known.discover();
    QCOMPARE(known.lastResponse().status, 200);
    pdf::PDFMcpClock::setOffset(pdf::PDFMcpLimits::ASSISTANT_IDLE_TIME + 1000);
    fixture.server->checkTimeouts();
    QTRY_VERIFY_WITH_TIMEOUT(fixture.server->getAssistants().empty(), 5000 * McpTestHelper::factor());
    refused.discover();
    QCOMPARE(refused.lastResponse().status, 200);
    pdf::PDFMcpClock::setOffset(0);
}

void McpTest::legacyProgressAcrossElicitation()
{
    // Review RP7: "The progress value MUST increase with each notification". A
    // legacy elicitation/create launches the call of the request again, whose
    // own progress starts from the beginning - the stream of the request never
    // sends a lower value
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    fixture.host.setHandler([](const std::shared_ptr<pdf::PDFMcpCall>& call)
    {
        if (McpServerHelper::getChoice(call->getRequest()).isEmpty())
        {
            call->reportProgress(2, 4, QStringLiteral("before the question"));
            call->requestInput(McpServerHelper::createChoice());
            return;
        }
        call->reportProgress(1, 4, QStringLiteral("again from the start"));
        call->reportProgress(3, 4, QStringLiteral("after the question"));
        call->finish(McpServerHelper::createText(QStringLiteral("done")));
    });

    pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20251125);
    client.setCapabilities(QJsonObject({ { QStringLiteral("elicitation"), QJsonObject() } }));
    client.setElicitationResponder([](const QJsonObject&)
    {
        return QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("documentId"), QStringLiteral("A") } }) } });
    });
    client.initialize();
    QCOMPARE(McpServerHelper::getText(client.callTool(QStringLiteral("progress-ask"), QJsonObject(), QJsonObject({ { QStringLiteral("progressToken"), 7 } }))), QStringLiteral("done"));

    QList<double> values;
    for (const QJsonObject& message : client.lastResponse().messages())
    {
        if (message.value(QStringLiteral("method")).toString() == QLatin1String("notifications/progress"))
        {
            values.append(message.value(QStringLiteral("params")).toObject().value(QStringLiteral("progress")).toDouble());
        }
    }
    QCOMPARE(values, QList<double>({ 2.0, 3.0 }));
}

void McpTest::schemaShapes_data()
{
    QTest::addColumn<int>("era");
    QTest::newRow("2026-07-28") << int(pdftest::McpTestEra::Modern);
    QTest::newRow("2025-11-25") << int(pdftest::McpTestEra::Legacy20251125);
    QTest::newRow("2025-06-18") << int(pdftest::McpTestEra::Legacy20250618);
}

void McpTest::schemaShapes()
{
    // Review RP7: the real responses of every method of the era (also the
    // requests and notifications of the server on their streams) against the
    // fields of schema.ts of the era (McpShape) - closed shapes, so a member of
    // another era is found too
    QFETCH(int, era);
    const pdftest::McpTestEra testEra = pdftest::McpTestEra(era);
    const bool isModern = testEra == pdftest::McpTestEra::Modern;

    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    const QJsonObject elicitation({ { QStringLiteral("elicitation"), QJsonObject() } });
    pdftest::McpTestClient client = fixture.client(testEra, QStringLiteral("shape-client"));
    client.setCapabilities(elicitation);
    client.setInputRetryRounds(0);
    QStringList errors;

    // The opening: server/discover or initialize, then the empty results of the legacy session
    if (isModern)
    {
        McpShape::checkDiscover(client.request(QStringLiteral("server/discover")), &errors);
    }
    else
    {
        const QJsonObject clientInfo({ { QStringLiteral("name"), QStringLiteral("shape-client") }, { QStringLiteral("version"), QStringLiteral("1.0") } });
        McpShape::checkInitialize(client.request(QStringLiteral("initialize"), QJsonObject({ { QStringLiteral("protocolVersion"), client.getProtocolVersion() },
                                                                                             { QStringLiteral("capabilities"), elicitation },
                                                                                             { QStringLiteral("clientInfo"), clientInfo } })), testEra, &errors);
        QVERIFY(!client.getSessionId().isEmpty());
        QCOMPARE(client.notify(QStringLiteral("notifications/initialized")), 202);

        const QJsonObject uri({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://documents/ABC/info") } });
        McpShape::checkEmpty(client.request(QStringLiteral("ping")), testEra, QStringLiteral("ping"), &errors);
        McpShape::checkEmpty(client.request(QStringLiteral("logging/setLevel"), QJsonObject({ { QStringLiteral("level"), QStringLiteral("debug") } })), testEra, QStringLiteral("logging/setLevel"), &errors);
        McpShape::checkEmpty(client.request(QStringLiteral("resources/subscribe"), uri), testEra, QStringLiteral("resources/subscribe"), &errors);
        McpShape::checkEmpty(client.request(QStringLiteral("resources/unsubscribe"), uri), testEra, QStringLiteral("resources/unsubscribe"), &errors);
    }

    // The lists (icons of the test catalog: 2025-11-25 and 2026-07-28 only)
    const QJsonArray tools = McpShape::checkList(client.request(QStringLiteral("tools/list")), testEra, QStringLiteral("tools"), &McpShape::checkTool, &errors);
    QVERIFY(!tools.isEmpty());
    QCOMPARE(tools.first().toObject().contains(QStringLiteral("icons")), testEra != pdftest::McpTestEra::Legacy20250618);
    McpShape::checkList(client.request(QStringLiteral("prompts/list")), testEra, QStringLiteral("prompts"), &McpShape::checkPrompt, &errors);
    McpShape::checkList(client.request(QStringLiteral("resources/list")), testEra, QStringLiteral("resources"), &McpShape::checkResource, &errors);
    McpShape::checkList(client.request(QStringLiteral("resources/templates/list")), testEra, QStringLiteral("resourceTemplates"), &McpShape::checkTemplate, &errors);

    // resources/read: a static text, a static blob, a document resource of the host
    for (const QString& uri : { QStringLiteral("pdf4qt://docs/index.md"), QStringLiteral("pdf4qt://docs/logo.png"), QStringLiteral("pdf4qt://documents/ABC/info") })
    {
        const QJsonObject result = McpShape::checkResponse(client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), uri } })), uri, &errors);
        McpShape::checkReadResourceResult(result, testEra, uri, &errors);
    }

    // tools/call: a text result, the progress and log notifications of its stream
    McpShape::checkCallToolResult(McpShape::checkResponse(client.callTool(QStringLiteral("echo"), QJsonObject({ { QStringLiteral("value"), QStringLiteral("x") } })),
                                                          QStringLiteral("tools/call echo"), &errors), testEra, QStringLiteral("CallToolResult"), &errors);

    QJsonObject progressMeta({ { QStringLiteral("progressToken"), QStringLiteral("p") } });
    if (isModern)
    {
        progressMeta.insert(QStringLiteral("io.modelcontextprotocol/logLevel"), QStringLiteral("debug"));
    }
    client.callTool(QStringLiteral("progress"), QJsonObject(), progressMeta);
    int progressCount = 0;
    int logCount = 0;
    for (const QJsonObject& message : client.lastResponse().messages())
    {
        const QString method = message.value(QStringLiteral("method")).toString();
        if (method == QLatin1String("notifications/progress"))
        {
            ++progressCount;
            McpShape::checkNotification(message, method, QStringLiteral("progressToken:I progress:n total?:n message?:s _meta?:o"), &errors);
        }
        else if (method == QLatin1String("notifications/message"))
        {
            ++logCount;
            const QJsonObject params = McpShape::checkNotification(message, method, QStringLiteral("level:s logger?:s data:x _meta?:o"), &errors);
            QVERIFY(pdf::PDFMcpProtocol::getLogLevelIndex(params.value(QStringLiteral("level")).toString()) >= 0);
        }
        else
        {
            McpShape::checkCallToolResult(McpShape::checkResponse(message, QStringLiteral("tools/call progress"), &errors), testEra, QStringLiteral("CallToolResult"), &errors);
        }
    }
    QCOMPARE(progressCount, 3);
    QCOMPARE(logCount, 2);

    // An execution error (isError) of a client without elicitation
    pdftest::McpTestClient plain = fixture.client(testEra, QStringLiteral("shape-plain"));
    if (!isModern)
    {
        plain.initialize();
    }
    const QJsonObject declined = McpShape::checkResponse(plain.callTool(QStringLiteral("ask"), QJsonObject()), QStringLiteral("tools/call ask"), &errors);
    McpShape::checkCallToolResult(declined, testEra, QStringLiteral("CallToolResult(isError)"), &errors);
    QVERIFY(declined.value(QStringLiteral("isError")).toBool());

    // A question of the server: InputRequiredResult (modern) or elicitation/create
    // on the stream of the call (legacy), of tools/call and of prompts/get
    client.setElicitationResponder([](const QJsonObject&)
    {
        return QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("documentId"), QStringLiteral("A") } }) } });
    });
    for (const QString& method : { QStringLiteral("tools/call"), QStringLiteral("prompts/get") })
    {
        const QString name = method == QLatin1String("tools/call") ? QStringLiteral("ask") : QStringLiteral("choose-document");
        const QJsonObject response = method == QLatin1String("tools/call") ? client.callTool(name, QJsonObject()) : client.getPrompt(name);
        if (isModern)
        {
            McpShape::checkInputRequiredResult(McpShape::checkResponse(response, method, &errors), QStringLiteral("InputRequiredResult"), &errors);
            continue;
        }

        int questions = 0;
        for (const QJsonObject& message : client.lastResponse().messages())
        {
            if (message.value(QStringLiteral("method")).toString() == QLatin1String("elicitation/create"))
            {
                ++questions;
                McpShape::check(message, QStringLiteral("jsonrpc:s id:I method:s params:o"), QStringLiteral("elicitation/create"), &errors);
                McpShape::checkElicitParams(message.value(QStringLiteral("params")), testEra, QStringLiteral("elicitation/create.params"), &errors);
                continue;
            }

            const QJsonObject result = McpShape::checkResponse(message, method, &errors);
            if (method == QLatin1String("tools/call"))
            {
                McpShape::checkCallToolResult(result, testEra, QStringLiteral("CallToolResult"), &errors);
            }
            else
            {
                McpShape::checkGetPromptResult(result, testEra, QStringLiteral("GetPromptResult"), &errors);
            }
        }
        QCOMPARE(questions, 1);
    }

    // prompts/get without a question, completion/complete
    McpShape::checkGetPromptResult(McpShape::checkResponse(client.getPrompt(QStringLiteral("summarize-document")), QStringLiteral("prompts/get"), &errors),
                                   testEra, QStringLiteral("GetPromptResult"), &errors);
    const QJsonObject completion({ { QStringLiteral("ref"), QJsonObject({ { QStringLiteral("type"), QStringLiteral("ref/prompt") }, { QStringLiteral("name"), QStringLiteral("summarize-document") } }) },
                                   { QStringLiteral("argument"), QJsonObject({ { QStringLiteral("name"), QStringLiteral("document") }, { QStringLiteral("value"), QString() } }) } });
    McpShape::checkCompleteResult(McpShape::checkResponse(client.request(QStringLiteral("completion/complete"), completion), QStringLiteral("completion/complete"), &errors),
                                  testEra, QStringLiteral("CompleteResult"), &errors);

    // Errors: of the host, of an unknown resource (data.uri), of an unknown method
    McpShape::checkError(client.callTool(QStringLiteral("error"), QJsonObject()), pdf::PDFMcpErrorCodes::INVALID_PARAMS, QString(), QStringLiteral("tools/call error"), &errors);
    McpShape::checkError(client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://docs/none.md") } })),
                         isModern ? pdf::PDFMcpErrorCodes::INVALID_PARAMS : pdf::PDFMcpErrorCodes::RESOURCE_NOT_FOUND_LEGACY, QStringLiteral("uri:s"), QStringLiteral("resources/read none"), &errors);
    McpShape::checkError(client.request(QStringLiteral("unknown/method")), pdf::PDFMcpErrorCodes::METHOD_NOT_FOUND, QString(), QStringLiteral("unknown/method"), &errors);
    QCOMPARE(client.lastResponse().status, isModern ? 404 : 200);

    if (isModern)
    {
        // The errors of the modern era with their data
        const QByteArray future = R"({"jsonrpc":"2.0","id":5,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2030-01-01","io.modelcontextprotocol/clientCapabilities":{}}}})";
        McpShape::checkError(fixture.raw(McpServerHelper::createRawPost(fixture.port(), future, { { "MCP-Protocol-Version", "2030-01-01" }, { "Mcp-Method", "tools/list" } })).json(),
                             pdf::PDFMcpErrorCodes::UNSUPPORTED_PROTOCOL_VERSION, QStringLiteral("supported:a requested:s"), QStringLiteral("-32022"), &errors);
        McpShape::checkError(client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), QStringLiteral("x") } })),
                             pdf::PDFMcpErrorCodes::MISSING_CLIENT_CAPABILITY, QStringLiteral("requiredCapabilities:o"), QStringLiteral("-32021"), &errors);
        McpShape::checkError(client.request(QStringLiteral("tools/list"), QJsonObject(), { { "Mcp-Method", "prompts/list" } }), pdf::PDFMcpErrorCodes::HEADER_MISMATCH,
                             QString(), QStringLiteral("-32020"), &errors);
    }
    else
    {
        // The notifications of the GET stream of the session
        QVERIFY(client.request(QStringLiteral("resources/subscribe"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://documents/ABC/info") } })).contains(QStringLiteral("result")));
        std::unique_ptr<pdftest::McpTestStream> stream = client.openGetStream();
        QVERIFY(stream && stream->waitForComments(1, 5000));
        fixture.server->publishCatalog(McpServerHelper::createCatalog(2, 3, { QStringLiteral("extra") }));
        fixture.server->notifyResourceUpdated(QStringLiteral("pdf4qt://documents/ABC/info"));
        QVERIFY(stream->waitForMessages(3, 5000));
        for (const QJsonObject& message : stream->messages())
        {
            const QString method = message.value(QStringLiteral("method")).toString();
            if (method == QLatin1String("notifications/resources/updated"))
            {
                McpShape::checkNotification(message, method, QStringLiteral("uri:s _meta?:o"), &errors);
            }
            else
            {
                QVERIFY2(method.endsWith(QLatin1String("/list_changed")), qPrintable(method));
                McpShape::checkNotification(message, method, QString(), &errors);
                McpShape::check(message.value(QStringLiteral("params")).toObject(), QStringLiteral("_meta?:o"), method + QStringLiteral(".params"), &errors);
            }
        }
    }

    QVERIFY(McpShape::report(errors));
}

void McpTest::schemaShapesModernStreams()
{
    // Review RP7: the notifications of a listen stream, its graceful end and the
    // results of the tasks extension against the schema of 2026-07-28
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.taskCreationDelay = 100;
    McpServerFixture fixture(config);
    QVERIFY(fixture.isRunning);
    QStringList errors;

    pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Modern, QStringLiteral("shape-streams"));
    client.setCapabilities(QJsonObject({ { QStringLiteral("extensions"), QJsonObject({ { QStringLiteral("io.modelcontextprotocol/tasks"), QJsonObject() } }) } }));

    // A task: CreateTaskResult, tasks/get, notifications/tasks, tasks/cancel
    const QJsonObject created = McpShape::checkResponse(client.callTool(QStringLiteral("slow"), QJsonObject()), QStringLiteral("tools/call slow"), &errors);
    McpShape::check(created, McpShape::taskMembers() + QStringLiteral(" resultType:s _meta:o"), QStringLiteral("CreateTaskResult"), &errors);
    McpShape::checkResultBase(created, McpShape::Era::Modern, QStringLiteral("CreateTaskResult"), &errors, QStringLiteral("task"));
    McpShape::checkTaskTimes(created, QStringLiteral("CreateTaskResult"), &errors);
    const QString taskId = created.value(QStringLiteral("taskId")).toString();
    QVERIFY(!taskId.isEmpty());

    QJsonObject listenRequest = client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({
        { QStringLiteral("toolsListChanged"), true }, { QStringLiteral("promptsListChanged"), true }, { QStringLiteral("resourcesListChanged"), true },
        { QStringLiteral("resourceSubscriptions"), QJsonArray({ QStringLiteral("pdf4qt://documents/ABC/info") }) }, { QStringLiteral("taskIds"), QJsonArray({ taskId }) } }) } }));
    listenRequest.insert(QStringLiteral("id"), QStringLiteral("listen-1"));
    std::unique_ptr<pdftest::McpTestStream> listen = client.openStream(listenRequest);
    QVERIFY(listen && listen->waitForMessages(1, 5000));

    std::shared_ptr<pdf::PDFMcpCall> slowCall;
    for (const std::shared_ptr<pdf::PDFMcpCall>& call : fixture.host.calls)
    {
        if (call->getRequest().name == QLatin1String("slow"))
        {
            slowCall = call;
        }
    }
    QVERIFY(slowCall);
    slowCall->finish(McpServerHelper::createText(QStringLiteral("slow result")));
    fixture.server->publishCatalog(McpServerHelper::createCatalog(2, 3, { QStringLiteral("extra") }));
    fixture.server->notifyResourceUpdated(QStringLiteral("pdf4qt://documents/ABC/info"));
    QVERIFY(listen->waitForMessages(5, 5000));

    const QString subscriptionMeta = QStringLiteral("io.modelcontextprotocol/subscriptionId:I");
    for (const QJsonObject& message : listen->messages())
    {
        const QString method = message.value(QStringLiteral("method")).toString();
        QJsonObject params;
        if (method == QLatin1String("notifications/subscriptions/acknowledged"))
        {
            params = McpShape::checkNotification(message, method, QStringLiteral("_meta:o notifications:o"), &errors);
            McpShape::check(params.value(QStringLiteral("notifications")), QStringLiteral("toolsListChanged?:b promptsListChanged?:b resourcesListChanged?:b resourceSubscriptions?:a taskIds?:a"),
                            method + QStringLiteral(".params.notifications"), &errors);
        }
        else if (method == QLatin1String("notifications/resources/updated"))
        {
            params = McpShape::checkNotification(message, method, QStringLiteral("_meta:o uri:s"), &errors);
        }
        else if (method == QLatin1String("notifications/tasks"))
        {
            params = message.value(QStringLiteral("params")).toObject();
            McpShape::checkNotification(message, method, McpShape::detailedTaskMembers(params) + QStringLiteral(" _meta:o"), &errors);
            McpShape::checkTaskTimes(params, method, &errors);
            McpShape::checkCallToolResult(params.value(QStringLiteral("result")).toObject(), McpShape::Era::Legacy20251125, method + QStringLiteral(".params.result"), &errors);
        }
        else
        {
            QVERIFY2(method.endsWith(QLatin1String("/list_changed")), qPrintable(method));
            params = McpShape::checkNotification(message, method, QStringLiteral("_meta:o"), &errors);
        }
        McpShape::check(params.value(QStringLiteral("_meta")), subscriptionMeta, method + QStringLiteral(".params._meta"), &errors);
        McpShape::expectValue(params.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/subscriptionId")), QStringLiteral("listen-1"),
                              method + QStringLiteral(".params._meta.subscriptionId"), &errors);
    }

    // tasks/get of the completed task (the result of the original request without
    // the members of Result), tasks/cancel of another one
    const QJsonObject completed = McpShape::checkResponse(client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), taskId } })), QStringLiteral("tasks/get"), &errors);
    McpShape::check(completed, McpShape::detailedTaskMembers(completed) + QStringLiteral(" resultType:s _meta:o"), QStringLiteral("GetTaskResult"), &errors);
    McpShape::checkResultBase(completed, McpShape::Era::Modern, QStringLiteral("GetTaskResult"), &errors);
    McpShape::expectValue(completed.value(QStringLiteral("status")), QStringLiteral("completed"), QStringLiteral("GetTaskResult.status"), &errors);

    const QString canceledId = client.callTool(QStringLiteral("slow"), QJsonObject()).value(QStringLiteral("result")).toObject().value(QStringLiteral("taskId")).toString();
    QVERIFY(!canceledId.isEmpty());
    McpShape::checkEmpty(client.request(QStringLiteral("tasks/cancel"), QJsonObject({ { QStringLiteral("taskId"), canceledId } })), McpShape::Era::Modern, QStringLiteral("tasks/cancel"), &errors);
    const QJsonObject canceled = McpShape::checkResponse(client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), canceledId } })), QStringLiteral("tasks/get"), &errors);
    McpShape::check(canceled, McpShape::detailedTaskMembers(canceled) + QStringLiteral(" resultType:s _meta:o"), QStringLiteral("GetTaskResult(cancelled)"), &errors);

    // The graceful end of the listen stream at the stop: SubscriptionsListenResult
    fixture.server->stop();
    QVERIFY(listen->waitForEnd(5000));
    const QJsonObject end = McpShape::checkResponse(listen->response().json(), QStringLiteral("subscriptions/listen"), &errors);
    McpShape::check(end, QStringLiteral("resultType:s _meta:o"), QStringLiteral("SubscriptionsListenResult"), &errors);
    McpShape::check(end.value(QStringLiteral("_meta")), subscriptionMeta + QStringLiteral(" io.modelcontextprotocol/serverInfo?:o"), QStringLiteral("SubscriptionsListenResult._meta"), &errors);
    McpShape::expectValue(listen->response().json().value(QStringLiteral("id")), QStringLiteral("listen-1"), QStringLiteral("SubscriptionsListenResult.id"), &errors);

    QVERIFY(McpShape::report(errors));
}

void McpTest::reviewStalledClientDropped()
{
    // A client, which stops reading the last response of its connection, held
    // one of the 64 connection slots for ever: the connection waited in its
    // closing state for a bytesWritten, which never came. The writing of the
    // rest has the time of a body now (restarted by every progress).
    pdf::PDFMcpResourceDefinition big;
    big.uri = QStringLiteral("pdf4qt://docs/big.md");
    big.name = QStringLiteral("big.md");
    big.mimeType = QStringLiteral("text/markdown");
    big.isStatic = true;
    big.content = QByteArray(31 * 1024 * 1024, 'x');   // the response stays below the 32 MiB of the backpressure
    big.size = big.content.size();
    std::vector<pdf::PDFMcpResourceDefinition> resources;
    resources.push_back(big);
    std::shared_ptr<const pdf::PDFMcpCatalog> catalog = std::make_shared<const pdf::PDFMcpCatalog>(1, std::vector<pdf::PDFMcpToolDefinition>(), std::move(resources),
                                                                                                   std::vector<pdf::PDFMcpResourceTemplateDefinition>(), std::vector<pdf::PDFMcpPromptDefinition>());

    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.bodyTimeout = 500;

    // The parts of a run of the server (PDFMcpServer keeps its shared state
    // private): the dispatcher in this thread, one I/O thread, a listener;
    // destroyed in the order of PDFMcpServer::stop
    pdftest::FakeMcpHost host;
    std::shared_ptr<pdf::PDFMcpServerShared> shared = std::make_shared<pdf::PDFMcpServerShared>(config);
    shared->host = &host;
    shared->setCatalog(catalog);
    pdf::PDFMcpIoThreads ioThreads;
    ioThreads.start(1, shared);
    std::unique_ptr<pdf::PDFMcpDispatcher> dispatcher = std::make_unique<pdf::PDFMcpDispatcher>(shared, nullptr);
    pdf::PDFMcpListener listener(&ioThreads, nullptr);
    QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
    const quint16 port = listener.serverPort();
    dispatcher->setPort(port);

    // The client sends its request and never reads: a thread without an event
    // loop (nothing takes the data from the system) with a small receive buffer
    const QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"pdf4qt://docs/big.md",)" + McpServerHelper::modernMeta() + "}}";
    const QByteArray request = McpServerHelper::createRawPost(port, body, McpServerHelper::modernHeaders("resources/read", "pdf4qt://docs/big.md"));
    std::atomic<bool> isDone = false;
    std::atomic<bool> isSent = false;
    std::thread stalledClient([&]()
    {
        // The receive buffer is set before the connection (bound first): a later
        // change does not stop the automatic tuning of the window (Windows took
        // the whole response of 31 MiB on the loopback)
        QTcpSocket socket;
        socket.bind(QHostAddress::LocalHost, 0);
        socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4096);
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (socket.waitForConnected(5000))
        {
            socket.write(request);
            isSent = socket.waitForBytesWritten(5000);
        }
        while (!isDone)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        socket.abort();
    });
    auto joinClient = qScopeGuard([&]() { isDone = true; stalledClient.join(); });
    QVERIFY(McpTestHelper::waitFor([&]() { return isSent.load(); }, 5000));
    QVERIFY(McpTestHelper::waitFor([&]() { return shared->connectionCount.load() == 1; }, 5000));

    // The connection is dropped although the client keeps its socket open
    QVERIFY2(McpTestHelper::waitFor([&]() { return shared->connectionCount.load() == 0; }, 15000), "A client, which does not read, holds its connection");

    // A client, which reads, gets the whole response
    pdftest::McpTestClient client(port);
    client.setToken(McpServerHelper::token());
    client.setClientInfo(QStringLiteral("reader"));
    const QJsonObject read = client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://docs/big.md") } }));
    QCOMPARE(read.value(QStringLiteral("result")).toObject().value(QStringLiteral("contents")).toArray().first().toObject().value(QStringLiteral("text")).toString().size(), big.content.size());

    listener.close();
    dispatcher->shutdown();
    ioThreads.stop();
}

void McpTest::reviewTaskInputTimeout()
{
    // A task waiting for the input of the client had no deadline: its pending
    // call stayed for ever (the limits of the calls of the assistant and of the
    // server) and kept its assistant connected. It ends after the time of an
    // elicitation as a legacy elicitation does.
    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.taskCreationDelay = 100;
    config.elicitationTimeout = 1500;
    McpServerFixture fixture(config);
    QVERIFY(fixture.isRunning);

    const QJsonObject tasksCapabilities({ { QStringLiteral("elicitation"), QJsonObject() }, { QStringLiteral("extensions"), QJsonObject({ { QStringLiteral("io.modelcontextprotocol/tasks"), QJsonObject() } }) } });
    pdftest::McpTestClient client = fixture.client();
    client.setCapabilities(tasksCapabilities);
    client.setInputRetryRounds(0);

    const QString taskId = client.callTool(QStringLiteral("askslow"), QJsonObject()).value(QStringLiteral("result")).toObject().value(QStringLiteral("taskId")).toString();
    QVERIFY(!taskId.isEmpty());

    QString status;
    QJsonObject task;
    QVERIFY(McpTestHelper::waitFor([&]()
    {
        // Polled as the poll interval asks (the rate limit of the assistant)
        QTest::qWait(100);
        task = client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), taskId } })).value(QStringLiteral("result")).toObject();
        status = task.value(QStringLiteral("status")).toString();
        return status == QLatin1String("input_required") || status == QLatin1String("completed");
    }, 5000));
    QCOMPARE(status, QStringLiteral("input_required"));

    // Nobody answers: the task ends with the text of an unanswered question
    QVERIFY2(McpTestHelper::waitFor([&]()
    {
        QTest::qWait(100);
        task = client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), taskId } })).value(QStringLiteral("result")).toObject();
        return task.value(QStringLiteral("status")).toString() == QLatin1String("completed");
    }, 10000), "A task waiting for input never ends");
    const QJsonObject result = task.value(QStringLiteral("result")).toObject();
    QVERIFY(result.value(QStringLiteral("isError")).toBool());
    QVERIFY(result.value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("did not answer")));
    QVERIFY(task.value(QStringLiteral("ttlMs")).toInteger() >= pdf::PDFMcpLimits::TASK_TTL);

    // The answer comes too late: the task does not wait any more
    const QJsonObject answer({ { QStringLiteral("pdf4qt.choice"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("documentId"), QStringLiteral("A") } }) } }) } });
    QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("tasks/update"), QJsonObject({ { QStringLiteral("taskId"), taskId }, { QStringLiteral("inputResponses"), answer } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);

    // An answer within the time still runs the call
    const QString answeredId = client.callTool(QStringLiteral("askslow"), QJsonObject()).value(QStringLiteral("result")).toObject().value(QStringLiteral("taskId")).toString();
    QVERIFY(McpTestHelper::waitFor([&]()
    {
        QTest::qWait(100);
        return client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), answeredId } })).value(QStringLiteral("result")).toObject().value(QStringLiteral("status")).toString() == QLatin1String("input_required");
    }, 5000));
    QVERIFY(client.request(QStringLiteral("tasks/update"), QJsonObject({ { QStringLiteral("taskId"), answeredId }, { QStringLiteral("inputResponses"), answer } })).contains(QStringLiteral("result")));
    const QJsonObject answered = client.pollTask(answeredId).value(QStringLiteral("result")).toObject();
    QCOMPARE(answered.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
    QCOMPARE(answered.value(QStringLiteral("result")).toObject().value(QStringLiteral("content")).toArray().first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("chosen A"));
}

void McpTest::reviewSchemaBounds()
{
    QString error;
    QStringList errors;

    // A schema of a tool of Pdf4QtLibCore with the limits of qint64: its maximum
    // rounds to 2^63, which was converted to qint64 (undefined; INT64_MIN on
    // x64 - every value was refused)
    const QJsonObject wide = QJsonDocument::fromJson(R"({"type":"integer","minimum":-9223372036854775808,"maximum":9223372036854775807})").object();
    std::optional<pdf::PDFMcpSchema> wideSchema = pdf::PDFMcpSchema::fromQJson(wide, &error);
    QVERIFY2(wideSchema, qPrintable(error));
    QVERIFY(wideSchema->validate(QJsonValue(5), QStringLiteral("a"), &errors));
    QVERIFY(wideSchema->validate(QJsonValue(qint64(-9000000000000000000LL)), QStringLiteral("a"), &errors));
    QVERIFY(wideSchema->validate(QJsonValue(std::numeric_limits<qint64>::max()), QStringLiteral("a"), &errors));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QStringLiteral("; "))));

    const QJsonObject beyond = QJsonDocument::fromJson(R"({"type":"integer","minimum":1e30,"maximum":1e300})").object();
    std::optional<pdf::PDFMcpSchema> beyondSchema = pdf::PDFMcpSchema::fromQJson(beyond, &error);
    QVERIFY(beyondSchema);
    QVERIFY(!beyondSchema->validate(QJsonValue(std::numeric_limits<qint64>::max()), QStringLiteral("a"), &errors));

    // One bound only: the message names only that bound (was "from 1 to 0")
    errors.clear();
    std::optional<pdf::PDFMcpSchema> minimumOnly = pdf::PDFMcpSchema::fromQJson(QJsonDocument::fromJson(R"({"type":"integer","minimum":1})").object(), &error);
    QVERIFY(minimumOnly);
    QVERIFY(minimumOnly->validate(QJsonValue(7), QStringLiteral("page"), &errors));
    QVERIFY(!minimumOnly->validate(QJsonValue(0), QStringLiteral("page"), &errors));
    QCOMPARE(errors, QStringList({ QStringLiteral("page: must be at least 1") }));

    errors.clear();
    std::optional<pdf::PDFMcpSchema> maximumOnly = pdf::PDFMcpSchema::fromQJson(QJsonDocument::fromJson(R"({"type":"number","maximum":2.5})").object(), &error);
    QVERIFY(maximumOnly);
    QVERIFY(!maximumOnly->validate(QJsonValue(3), QStringLiteral("ratio"), &errors));
    QCOMPARE(errors, QStringList({ QStringLiteral("ratio: must be at most 2.5") }));

    // A fractional bound of an integer: exact (0.5 = at least 1), no exponent in the text
    errors.clear();
    std::optional<pdf::PDFMcpSchema> fractional = pdf::PDFMcpSchema::fromQJson(QJsonDocument::fromJson(R"({"type":"integer","minimum":0.5,"maximum":1000000000})").object(), &error);
    QVERIFY(fractional);
    QVERIFY(fractional->validate(QJsonValue(1), QStringLiteral("n"), &errors));
    QVERIFY(fractional->validate(QJsonValue(1000000000), QStringLiteral("n"), &errors));
    QVERIFY(!fractional->validate(QJsonValue(0), QStringLiteral("n"), &errors));
    QVERIFY(!fractional->validate(QJsonValue(1000000001), QStringLiteral("n"), &errors));
    QCOMPARE(errors, QStringList({ QStringLiteral("n: must be from 0.5 to 1000000000"), QStringLiteral("n: must be from 0.5 to 1000000000") }));
}

void McpTest::reviewThrottleKeepsBlocks()
{
    // A blocked address has no failures (cleared at the block): the eviction of
    // a full throttle took it as the oldest and an attacker of many addresses
    // ended its block at once
    pdf::PDFMcpAuthorizationThrottle throttle;
    for (int i = 0; i < pdf::PDFMcpLimits::MAXIMUM_AUTHORIZATION_FAILURES; ++i)
    {
        throttle.recordFailure(QStringLiteral("10.0.0.1"), 0);
    }
    qint64 retryAfter = 0;
    QVERIFY(throttle.isBlocked(QStringLiteral("10.0.0.1"), 1000, &retryAfter));

    for (int i = 0; i < 2 * pdf::PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS; ++i)
    {
        throttle.recordFailure(QStringLiteral("10.1.%1.%2").arg(i / 256).arg(i % 256), 1000 + i / 1000);
    }
    QVERIFY(throttle.getAddressCount() <= pdf::PDFMcpLimits::MAXIMUM_RATE_LIMIT_KEYS);
    QVERIFY2(throttle.isBlocked(QStringLiteral("10.0.0.1"), 5000, &retryAfter), "The block of an address ended by the failures of other addresses");
    QCOMPARE(retryAfter, pdf::PDFMcpLimits::AUTHORIZATION_BLOCK_TIME - 5000);

    // The block ends at its time
    QVERIFY(!throttle.isBlocked(QStringLiteral("10.0.0.1"), pdf::PDFMcpLimits::AUTHORIZATION_BLOCK_TIME, &retryAfter));
}

void McpTest::reviewProgressTokenExact()
{
    // QJsonDocument of Qt 6 keeps an integer as qint64: a progress token above
    // 2^53 is echoed exactly (a reported finding, which is not a defect)
    McpServerFixture fixture;
    QVERIFY(fixture.isRunning);
    pdftest::McpTestClient client = fixture.client();

    QJsonObject meta;
    meta.insert(QStringLiteral("progressToken"), qint64(9007199254740993LL));
    client.callTool(QStringLiteral("progress"), QJsonObject(), meta);
    QVERIFY(client.lastResponse().isSse);
    QVERIFY(client.lastResponse().body.contains("\"progressToken\":9007199254740993"));
    QVERIFY(!client.lastResponse().body.contains("9007199254740992"));
}

void McpTest::conformance_data()
{
    QTest::addColumn<int>("row");
    const std::vector<McpConformanceRow>& rows = McpConformance::getRows();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        QTest::newRow(rows[i].id) << int(i);
    }
}

void McpTest::conformance()
{
    QFETCH(int, row);
    const McpConformanceRow& conformanceRow = McpConformance::getRows()[size_t(row)];
    qInfo("%s: %s", conformanceRow.id, conformanceRow.requirement);

    pdf::PDFMcpServerConfig config = McpServerHelper::createConfig();
    config.taskCreationDelay = 100;
    config.jsonResponseDelay = 300;
    McpServerFixture fixture(config);
    QVERIFY(fixture.isRunning);
    conformanceRow.check(fixture);
}

// ----------------------------------------------------------------------------
// The MUST checklist of chapter 1 of the plan (conformance, both eras). Each
// row names the requirement of the specification and checks it against a
// fresh server by the scripted client (no external tool - owner decision Q2).
// ----------------------------------------------------------------------------

/// The checks of the rows
class McpConformanceChecks
{
public:
    McpConformanceChecks() = delete;

    static QJsonObject callParams(const QString& name)
    {
        return QJsonObject({ { QStringLiteral("name"), name }, { QStringLiteral("arguments"), QJsonObject() } });
    }

    static void originInvalid(McpServerFixture& fixture)
    {
        // "If the Origin header is present and invalid, servers MUST respond with HTTP 403"
        pdftest::McpTestClient client = fixture.client();
        client.setExtraHeaders({ { "Origin", "http://evil.example" } });
        client.discover();
        QCOMPARE(client.lastResponse().status, 403);
    }

    static void singleMessage(McpServerFixture& fixture)
    {
        // "The body of the HTTP POST MUST be a single JSON-RPC request or notification"
        pdftest::McpTestClient client = fixture.client();
        const QByteArray batch = "[" + QJsonDocument(client.createRequest(QStringLiteral("tools/list"))).toJson(QJsonDocument::Compact) + "]";
        QCOMPARE(client.post(batch, McpServerHelper::modernHeaders("tools/list")).status, 400);
    }

    static void acceptBoth(McpServerFixture& fixture)
    {
        // "The client MUST include an Accept header, listing both ..." - refused by 406
        pdftest::McpTestClient client = fixture.client();
        client.setExtraHeaders({ { "Accept", "text/event-stream" } });
        client.discover();
        QCOMPARE(client.lastResponse().status, 406);
    }

    static void notificationAccepted(McpServerFixture& fixture)
    {
        // "If the server accepts it, the server MUST return HTTP status code 202 Accepted with no body"
        pdftest::McpTestClient client = fixture.client();
        QCOMPARE(client.notify(QStringLiteral("notifications/cancelled"), QJsonObject({ { QStringLiteral("requestId"), 1 } })), 202);
        QVERIFY(client.lastResponse().body.isEmpty());
    }

    static void responseTypes(McpServerFixture& fixture)
    {
        // "the server MUST return either Content-Type: application/json ... or text/event-stream"
        pdftest::McpTestClient client = fixture.client();
        client.discover();
        QCOMPARE(client.lastResponse().header("Content-Type"), QByteArray("application/json"));
        client.callTool(QStringLiteral("progress"), QJsonObject(), QJsonObject({ { QStringLiteral("progressToken"), 1 } }));
        QCOMPARE(client.lastResponse().header("Content-Type"), QByteArray("text/event-stream"));
    }

    static void versionHeaderMatches(McpServerFixture& fixture)
    {
        // "The header value MUST match ... If the values do not match, the server MUST reject the request with 400 and HeaderMismatch"
        pdftest::McpTestClient client = fixture.client();
        const QJsonObject response = client.request(QStringLiteral("tools/list"), QJsonObject(), { { "MCP-Protocol-Version", "2025-11-25" } });
        QCOMPARE(client.lastResponse().status, 400);
        QCOMPARE(McpServerHelper::getErrorCode(response), pdf::PDFMcpErrorCodes::HEADER_MISMATCH);
    }

    static void unsupportedVersion(McpServerFixture& fixture)
    {
        // "it MUST respond with 400 Bad Request and an UnsupportedProtocolVersionError listing its supported versions"
        const QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"1900-01-01","io.modelcontextprotocol/clientCapabilities":{}}}})";
        const pdftest::McpTestResponse response = fixture.raw(McpServerHelper::createRawPost(fixture.port(), body, { { "MCP-Protocol-Version", "1900-01-01" }, { "Mcp-Method", "tools/list" } }));
        QCOMPARE(response.status, 400);
        QCOMPARE(McpServerHelper::getErrorCode(response.json()), pdf::PDFMcpErrorCodes::UNSUPPORTED_PROTOCOL_VERSION);
        QVERIFY(response.json().value(QStringLiteral("error")).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("supported")).isArray());
    }

    static void unknownMethod(McpServerFixture& fixture)
    {
        // "If the server does not implement the requested RPC method, it MUST respond with 404 Not Found and ... -32601"
        pdftest::McpTestClient client = fixture.client();
        const QJsonObject response = client.request(QStringLiteral("unknown/method"));
        QCOMPARE(client.lastResponse().status, 404);
        QCOMPARE(McpServerHelper::getErrorCode(response), pdf::PDFMcpErrorCodes::METHOD_NOT_FOUND);
    }

    static void methodHeader(McpServerFixture& fixture)
    {
        // Mcp-Method is REQUIRED for all requests and must equal the method
        pdftest::McpTestClient client = fixture.client();
        client.request(QStringLiteral("tools/list"), QJsonObject(), { { "Mcp-Method", QByteArray() } });
        QCOMPARE(client.lastResponse().status, 400);
        client.request(QStringLiteral("tools/list"), QJsonObject(), { { "Mcp-Method", "prompts/list" } });
        QCOMPARE(client.lastResponse().status, 400);
    }

    static void nameHeader(McpServerFixture& fixture)
    {
        // Mcp-Name is REQUIRED for tools/call, resources/read and prompts/get
        pdftest::McpTestClient client = fixture.client();
        for (const QString& method : { QStringLiteral("tools/call"), QStringLiteral("prompts/get"), QStringLiteral("resources/read") })
        {
            const QJsonObject params = method == QLatin1String("resources/read") ? QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://docs/index.md") } })
                                                                                   : QJsonObject({ { QStringLiteral("name"), QStringLiteral("echo") } });
            const QJsonObject response = client.request(method, params, { { "Mcp-Name", QByteArray() } });
            QCOMPARE(client.lastResponse().status, 400);
            QCOMPARE(McpServerHelper::getErrorCode(response), pdf::PDFMcpErrorCodes::HEADER_MISMATCH);
        }
    }

    static void nameSentinel(McpServerFixture& fixture)
    {
        // "servers MUST decode an encoded Mcp-Name ... value before comparing it to the corresponding request body value"
        pdftest::McpTestClient client = fixture.client();
        const QJsonObject response = client.request(QStringLiteral("tools/call"), callParams(QStringLiteral("echo")), { { "Mcp-Name", "=?base64?ZWNobw==?=" } });
        QCOMPARE(client.lastResponse().status, 200);
        QCOMPARE(McpServerHelper::getText(response), QStringLiteral("{}"));
    }

    static void requiredMeta(McpServerFixture& fixture)
    {
        // "A request missing any required field is malformed; the server MUST reject it with -32602 ... status MUST be 400"
        const QByteArray body = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}}})";
        const pdftest::McpTestResponse response = fixture.raw(McpServerHelper::createRawPost(fixture.port(), body, McpServerHelper::modernHeaders("tools/list")));
        QCOMPARE(response.status, 400);
        QCOMPARE(McpServerHelper::getErrorCode(response.json()), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    }

    static void resultType(McpServerFixture& fixture)
    {
        // "The result MUST include a resultType field"
        pdftest::McpTestClient client = fixture.client();
        for (const QString& method : { QStringLiteral("server/discover"), QStringLiteral("tools/list"), QStringLiteral("prompts/list"), QStringLiteral("resources/list"), QStringLiteral("resources/templates/list") })
        {
            QCOMPARE(client.request(method).value(QStringLiteral("result")).toObject().value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
        }
        QCOMPARE(client.callTool(QStringLiteral("echo"), QJsonObject()).value(QStringLiteral("result")).toObject().value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
    }

    static void serverInfoMeta(McpServerFixture& fixture)
    {
        // Servers SHOULD include io.modelcontextprotocol/serverInfo in every result's _meta
        pdftest::McpTestClient client = fixture.client();
        const QJsonObject result = client.callTool(QStringLiteral("echo"), QJsonObject()).value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/serverInfo")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("pdf4qt"));
    }

    static void sessionIdIgnored(McpServerFixture& fixture)
    {
        // "An Mcp-Session-Id header on a request: ignore it, and do not mint or echo session IDs"
        pdftest::McpTestClient client = fixture.client();
        client.request(QStringLiteral("tools/list"), QJsonObject(), { { "Mcp-Session-Id", "ABCDEFGHIJKLMNOPQRSTUVWXYZ" } });
        QCOMPARE(client.lastResponse().status, 200);
        QVERIFY(client.lastResponse().header("Mcp-Session-Id").isEmpty());
    }

    static void getDelete405(McpServerFixture& fixture)
    {
        // "HTTP GET or DELETE to the MCP endpoint: respond with 405 Method Not Allowed"
        pdftest::McpTestClient client = fixture.client();
        QCOMPARE(client.get().status, 405);
        QCOMPARE(client.deleteSession().status, 405);
    }

    static void listenAcknowledgedFirst(McpServerFixture& fixture)
    {
        // "The server MUST send notifications/subscriptions/acknowledged as the first message carrying the subscription's ID"
        pdftest::McpTestClient client = fixture.client();
        std::unique_ptr<pdftest::McpTestStream> stream = client.openStream(client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({ { QStringLiteral("toolsListChanged"), true } }) } })));
        QVERIFY(stream && stream->waitForMessages(1, 5000));
        QCOMPARE(stream->messages().front().value(QStringLiteral("method")).toString(), QStringLiteral("notifications/subscriptions/acknowledged"));
        QVERIFY(stream->messages().front().value(QStringLiteral("params")).toObject().value(QStringLiteral("_meta")).toObject().contains(QStringLiteral("io.modelcontextprotocol/subscriptionId")));
    }

    static void listenRequestedOnly(McpServerFixture& fixture)
    {
        // "The server MUST NOT send notification types the client has not explicitly requested"
        pdftest::McpTestClient client = fixture.client();
        std::unique_ptr<pdftest::McpTestStream> stream = client.openStream(client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({ { QStringLiteral("promptsListChanged"), true } }) } })));
        QVERIFY(stream && stream->waitForMessages(1, 5000));
        fixture.server->publishCatalog(McpServerHelper::createCatalog(2, 3, { QStringLiteral("extra") }));
        fixture.server->notifyResourceUpdated(QStringLiteral("pdf4qt://documents/ABC"));
        QTest::qWait(300);
        QCOMPARE(int(stream->messages().size()), 1);
    }

    static void listenSubscriptionId(McpServerFixture& fixture)
    {
        // "All notifications delivered on the stream carry io.modelcontextprotocol/subscriptionId in _meta"
        pdftest::McpTestClient client = fixture.client();
        QJsonObject request = client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject({ { QStringLiteral("toolsListChanged"), true },
            { QStringLiteral("resourcesListChanged"), true }, { QStringLiteral("resourceSubscriptions"), QJsonArray({ QStringLiteral("pdf4qt://documents/ABC/info") }) } }) } }));
        request.insert(QStringLiteral("id"), 77);
        std::unique_ptr<pdftest::McpTestStream> stream = client.openStream(request);
        QVERIFY(stream && stream->waitForMessages(1, 5000));
        fixture.server->publishCatalog(McpServerHelper::createCatalog(2, 3, { QStringLiteral("extra") }));
        fixture.server->notifyResourceUpdated(QStringLiteral("pdf4qt://documents/ABC/info"));
        QVERIFY(stream->waitForMessages(4, 5000));
        for (const QJsonObject& message : stream->messages())
        {
            QCOMPARE(message.value(QStringLiteral("params")).toObject().value(QStringLiteral("_meta")).toObject().value(QStringLiteral("io.modelcontextprotocol/subscriptionId")).toInt(), 77);
        }
    }

    static void listenGracefulEnd(McpServerFixture& fixture)
    {
        // "it SHOULD respond to the original subscriptions/listen request with a completion result before closing the stream"
        pdftest::McpTestClient client = fixture.client();
        std::unique_ptr<pdftest::McpTestStream> stream = client.openStream(client.createRequest(QStringLiteral("subscriptions/listen"), QJsonObject({ { QStringLiteral("notifications"), QJsonObject() } })));
        QVERIFY(stream && stream->waitForMessages(1, 5000));
        fixture.server->stop();
        QVERIFY(stream->waitForEnd(5000));
        const QJsonObject result = stream->response().json().value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("resultType")).toString(), QStringLiteral("complete"));
        QVERIFY(result.value(QStringLiteral("_meta")).toObject().contains(QStringLiteral("io.modelcontextprotocol/subscriptionId")));
    }

    static void closeCancels(McpServerFixture& fixture)
    {
        // "Closing the SSE response stream MUST be treated by the server as cancellation of that request"
        pdftest::McpTestClient client = fixture.client();
        std::unique_ptr<pdftest::McpTestStream> stream = client.openStream(client.createRequest(QStringLiteral("tools/call"), callParams(QStringLiteral("slow"))));
        QVERIFY(fixture.waitCalls(1));
        stream->close();
        QVERIFY(fixture.waitCanceled(1));
    }

    static void progressRules(McpServerFixture& fixture)
    {
        // Progress only with a token, strictly increasing, nothing after the result
        pdftest::McpTestClient client = fixture.client();
        client.callTool(QStringLiteral("progress"), QJsonObject());
        QVERIFY(!client.lastResponse().isSse);
        client.callTool(QStringLiteral("progress"), QJsonObject(), QJsonObject({ { QStringLiteral("progressToken"), QStringLiteral("p") } }));
        const std::vector<QJsonObject> messages = client.lastResponse().messages();
        QVERIFY(!messages.empty());
        QVERIFY(messages.back().contains(QStringLiteral("result")));

        // Review RP7: the values really increase, each one carries the token
        double last = -1.0;
        int count = 0;
        for (const QJsonObject& message : messages)
        {
            if (message.value(QStringLiteral("method")).toString() == QLatin1String("notifications/progress"))
            {
                const QJsonObject params = message.value(QStringLiteral("params")).toObject();
                QCOMPARE(params.value(QStringLiteral("progressToken")).toString(), QStringLiteral("p"));
                QVERIFY(params.value(QStringLiteral("progress")).toDouble() > last);
                last = params.value(QStringLiteral("progress")).toDouble();
                ++count;
            }
        }
        QCOMPARE(count, 3);
    }

    static void loggingRules(McpServerFixture& fixture)
    {
        // "MUST NOT emit notifications/message for requests that did not include this field"
        pdftest::McpTestClient client = fixture.client();
        client.callTool(QStringLiteral("progress"), QJsonObject(), QJsonObject({ { QStringLiteral("progressToken"), QStringLiteral("p") } }));
        for (const QJsonObject& message : client.lastResponse().messages())
        {
            QVERIFY(message.value(QStringLiteral("method")).toString() != QLatin1String("notifications/message"));
        }
    }

    static void resourceNotFound(McpServerFixture& fixture)
    {
        // Resource not found: -32602 (2026-07-28) with data.uri, -32002 of the legacy eras
        pdftest::McpTestClient client = fixture.client();
        QCOMPARE(McpServerHelper::getErrorCode(client.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://docs/none.md") } }))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
        pdftest::McpTestClient legacy = fixture.client(pdftest::McpTestEra::Legacy20250618);
        legacy.initialize();
        QCOMPARE(McpServerHelper::getErrorCode(legacy.request(QStringLiteral("resources/read"), QJsonObject({ { QStringLiteral("uri"), QStringLiteral("pdf4qt://docs/none.md") } }))), pdf::PDFMcpErrorCodes::RESOURCE_NOT_FOUND_LEGACY);
    }

    static void inputRequestsDeclared(McpServerFixture& fixture)
    {
        // "Servers MUST NOT send an inputRequests that the client has not declared support for"
        pdftest::McpTestClient client = fixture.client();
        const QJsonObject result = client.callTool(QStringLiteral("ask"), QJsonObject()).value(QStringLiteral("result")).toObject();
        QVERIFY(!result.contains(QStringLiteral("inputRequests")));
        QVERIFY(result.value(QStringLiteral("isError")).toBool());
    }

    static void requestStateIntegrity(McpServerFixture& fixture)
    {
        // "servers MUST protect its integrity ... and MUST reject state that fails verification"
        pdftest::McpTestClient client = fixture.client();
        client.setCapabilities(QJsonObject({ { QStringLiteral("elicitation"), QJsonObject() } }));
        client.setInputRetryRounds(0);
        QString state = client.callTool(QStringLiteral("ask"), QJsonObject()).value(QStringLiteral("result")).toObject().value(QStringLiteral("requestState")).toString();
        QVERIFY(!state.isEmpty());
        state.chop(2);
        state += QStringLiteral("AA");
        QJsonObject params = callParams(QStringLiteral("ask"));
        params.insert(QStringLiteral("requestState"), state);
        params.insert(QStringLiteral("inputResponses"), QJsonObject({ { QStringLiteral("pdf4qt.choice"), QJsonObject({ { QStringLiteral("action"), QStringLiteral("accept") }, { QStringLiteral("content"), QJsonObject({ { QStringLiteral("documentId"), QStringLiteral("A") } }) } }) } }));
        const QJsonObject response = client.request(QStringLiteral("tools/call"), params);
        QVERIFY(response.value(QStringLiteral("result")).toObject().value(QStringLiteral("isError")).toBool());
    }

    static void taskOnlyWithExtension(McpServerFixture& fixture)
    {
        // "A server MUST NOT return CreateTaskResult to a client that did not include the extension capability on its request"
        pdftest::McpTestClient client = fixture.client();
        std::unique_ptr<pdftest::McpTestStream> stream = client.openStream(client.createRequest(QStringLiteral("tools/call"), callParams(QStringLiteral("slow"))));
        QVERIFY(stream->waitForComments(1, 5000));
        QVERIFY(stream->messages().empty());
    }

    static void tasksCapability(McpServerFixture& fixture)
    {
        // tasks/* of a client without the extension: -32021 with requiredCapabilities (400)
        pdftest::McpTestClient client = fixture.client();
        const QJsonObject response = client.request(QStringLiteral("tasks/get"), QJsonObject({ { QStringLiteral("taskId"), QStringLiteral("x") } }));
        QCOMPARE(client.lastResponse().status, 400);
        QCOMPARE(McpServerHelper::getErrorCode(response), pdf::PDFMcpErrorCodes::MISSING_CLIENT_CAPABILITY);
    }

    static void initializeVersion(McpServerFixture& fixture)
    {
        // "If the server supports the requested protocol version, it MUST respond with the same version. Otherwise ... another protocol version it supports"
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20250618);
        QCOMPARE(client.initialize().value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-06-18"));
        const QJsonObject other = client.request(QStringLiteral("initialize"), QJsonObject({ { QStringLiteral("protocolVersion"), QStringLiteral("2024-01-01") } }));
        QCOMPARE(other.value(QStringLiteral("result")).toObject().value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-11-25"));
    }

    static void sessionNotFound(McpServerFixture& fixture)
    {
        // "The server MAY terminate the session at any time, after which it MUST respond to requests containing that session ID with HTTP 404"
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20251125);
        client.initialize();
        const QByteArray sessionId = client.getSessionId();
        QCOMPARE(client.deleteSession().status, 200);
        client.setSessionId(sessionId);
        client.request(QStringLiteral("ping"));
        QCOMPARE(client.lastResponse().status, 404);
    }

    static void getStreamNoBroadcast(McpServerFixture& fixture)
    {
        // "The server MUST send each of its JSON-RPC messages on only one of the connected streams"
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20251125);
        client.initialize();
        std::unique_ptr<pdftest::McpTestStream> first = client.openGetStream();
        QVERIFY(first->waitForComments(1, 5000));
        std::unique_ptr<pdftest::McpTestStream> second = client.openGetStream();
        QVERIFY(second->waitForComments(1, 5000));
        fixture.server->publishCatalog(McpServerHelper::createCatalog(2, 0, { QStringLiteral("extra") }));
        QVERIFY(second->waitForMessages(1, 5000));
        QVERIFY(first->messages().empty());
    }

    static void idsEchoed(McpServerFixture& fixture)
    {
        // "Result responses MUST include the same ID as the request they correspond to"
        pdftest::McpTestClient client = fixture.client();
        QJsonObject request = client.createRequest(QStringLiteral("tools/list"));
        request.insert(QStringLiteral("id"), QStringLiteral("id-š"));
        const pdftest::McpTestResponse response = client.post(QJsonDocument(request).toJson(QJsonDocument::Compact));
        QCOMPARE(response.json().value(QStringLiteral("id")).toString(), QStringLiteral("id-š"));
    }

    static void idNotNull(McpServerFixture& fixture)
    {
        // "Unlike base JSON-RPC, the ID MUST NOT be null"
        pdftest::McpTestClient client = fixture.client();
        QJsonObject request = client.createRequest(QStringLiteral("tools/list"));
        request.insert(QStringLiteral("id"), QJsonValue::Null);
        const pdftest::McpTestResponse response = client.post(QJsonDocument(request).toJson(QJsonDocument::Compact));
        QCOMPARE(response.status, 400);
        QCOMPARE(McpServerHelper::getErrorCode(response.json()), pdf::PDFMcpErrorCodes::INVALID_REQUEST);
    }

    static void discoverImplemented(McpServerFixture& fixture)
    {
        // "Servers MUST implement server/discover"
        pdftest::McpTestClient client = fixture.client();
        QVERIFY(client.discover().value(QStringLiteral("supportedVersions")).toArray().contains(QStringLiteral("2026-07-28")));
    }

    static void keepAliveComments(McpServerFixture& fixture)
    {
        // Keep-alive comments of a call kept open (any line beginning with a colon is a comment)
        pdftest::McpTestClient client = fixture.client();
        std::unique_ptr<pdftest::McpTestStream> stream = client.openStream(client.createRequest(QStringLiteral("tools/call"), callParams(QStringLiteral("slow"))));
        QVERIFY(stream->waitForComments(1, 5000));
        QCOMPARE(stream->response().header("X-Accel-Buffering"), QByteArray("no"));
    }

    static void legacyListChanged(McpServerFixture& fixture)
    {
        // Legacy capabilities listChanged: notifications on the GET stream of the session
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20251125);
        client.initialize();
        std::unique_ptr<pdftest::McpTestStream> stream = client.openGetStream();
        QVERIFY(stream->waitForComments(1, 5000));
        fixture.server->publishCatalog(McpServerHelper::createCatalog(2, 3));
        QVERIFY(stream->waitForMessages(1, 5000));
        QCOMPARE(stream->messages().front().value(QStringLiteral("method")).toString(), QStringLiteral("notifications/resources/list_changed"));
    }

    // Rows of the review RP7 ------------------------------------------------

    static void modernNoClientResponses(McpServerFixture& fixture)
    {
        // "The client MUST NOT send JSON-RPC responses" (modern Streamable HTTP): refused
        pdftest::McpTestClient client = fixture.client();
        const QJsonObject response({ { QStringLiteral("jsonrpc"), QStringLiteral("2.0") }, { QStringLiteral("id"), 5 }, { QStringLiteral("result"), QJsonObject() } });
        QCOMPARE(client.post(QJsonDocument(response).toJson(QJsonDocument::Compact)).status, 400);
    }

    static void modernNoServerRequests(McpServerFixture& fixture)
    {
        // "The server MUST NOT send independent JSON-RPC requests on this stream":
        // a question is an InputRequiredResult (MRTR), never an elicitation/create
        pdftest::McpTestClient client = fixture.client();
        client.setCapabilities(QJsonObject({ { QStringLiteral("elicitation"), QJsonObject() } }));
        client.setInputRetryRounds(0);
        const QJsonObject result = client.callTool(QStringLiteral("askslow"), QJsonObject()).value(QStringLiteral("result")).toObject();
        QCOMPARE(result.value(QStringLiteral("resultType")).toString(), QStringLiteral("input_required"));
        for (const QJsonObject& message : client.lastResponse().messages())
        {
            QVERIFY(!(message.contains(QStringLiteral("method")) && message.contains(QStringLiteral("id"))));
        }
    }

    static void cacheFieldsOfPages(McpServerFixture& fixture)
    {
        // "Servers MUST provide a ttlMs value that is >= 0" and "MUST apply the same
        // cacheScope to all response pages for a given list"
        fixture.server->publishCatalog(McpServerHelper::createCatalog(2, 150));
        pdftest::McpTestClient client = fixture.client();
        QJsonObject first;
        QVERIFY(McpTestHelper::waitFor([&]()
        {
            first = client.request(QStringLiteral("resources/list")).value(QStringLiteral("result")).toObject();
            return first.contains(QStringLiteral("nextCursor"));
        }, 5000));
        const QJsonObject second = client.request(QStringLiteral("resources/list"), QJsonObject({ { QStringLiteral("cursor"), first.value(QStringLiteral("nextCursor")) } })).value(QStringLiteral("result")).toObject();
        QVERIFY(second.value(QStringLiteral("resources")).isArray());
        QCOMPARE(second.value(QStringLiteral("cacheScope")), first.value(QStringLiteral("cacheScope")));
        for (const QJsonObject& page : { first, second })
        {
            QVERIFY(page.value(QStringLiteral("ttlMs")).isDouble() && page.value(QStringLiteral("ttlMs")).toDouble() >= 0.0);
        }
    }

    static void invalidCursor(McpServerFixture& fixture)
    {
        // "Invalid cursors SHOULD result in an error with code -32602"
        pdftest::McpTestClient client = fixture.client();
        QCOMPARE(McpServerHelper::getErrorCode(client.listTools(QStringLiteral("!!not-a-cursor!!"))), pdf::PDFMcpErrorCodes::INVALID_PARAMS);
    }

    static void legacyNoSession(McpServerFixture& fixture)
    {
        // "Servers that require a session ID SHOULD respond to requests without an
        // MCP-Session-Id header (other than initialization) with HTTP 400"
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20251125);
        client.request(QStringLiteral("tools/list"));
        QCOMPARE(client.lastResponse().status, 400);
    }

    static void legacyVersionHeader(McpServerFixture& fixture)
    {
        // "If the server receives a request with an invalid or unsupported
        // MCP-Protocol-Version, it MUST respond with 400 Bad Request"
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20250618);
        client.initialize();
        client.request(QStringLiteral("tools/list"), QJsonObject(), { { "MCP-Protocol-Version", "1999-01-01" } });
        QCOMPARE(client.lastResponse().status, 400);
        client.request(QStringLiteral("tools/list"));
        QCOMPARE(client.lastResponse().status, 200);
    }

    static void legacySessionIdAscii(McpServerFixture& fixture)
    {
        // "The session ID MUST only contain visible ASCII characters (0x21 to 0x7E)"
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20251125);
        client.initialize();
        const QByteArray sessionId = client.getSessionId();
        QVERIFY(sessionId.size() >= 16);
        for (char c : sessionId)
        {
            QVERIFY(uchar(c) >= 0x21 && uchar(c) <= 0x7E);
        }
    }

    static void elicitationModeSupported(McpServerFixture& fixture)
    {
        // "Servers MUST NOT send elicitation requests with modes that are not
        // supported by the client" - a client of the URL mode only gets no form
        const QJsonObject urlOnly({ { QStringLiteral("elicitation"), QJsonObject({ { QStringLiteral("url"), QJsonObject() } }) } });
        for (pdftest::McpTestEra era : { pdftest::McpTestEra::Modern, pdftest::McpTestEra::Legacy20251125 })
        {
            pdftest::McpTestClient client = fixture.client(era);
            client.setCapabilities(urlOnly);
            client.setInputRetryRounds(0);
            if (era != pdftest::McpTestEra::Modern)
            {
                client.initialize();
            }
            const QJsonObject result = client.callTool(QStringLiteral("ask"), QJsonObject()).value(QStringLiteral("result")).toObject();
            QVERIFY(result.value(QStringLiteral("isError")).toBool());
            QVERIFY(!result.contains(QStringLiteral("inputRequests")));
            for (const QJsonObject& message : client.lastResponse().messages())
            {
                QVERIFY(message.value(QStringLiteral("method")).toString() != QLatin1String("elicitation/create"));
            }
        }
    }

    static void legacyTaskMetadataIgnored(McpServerFixture& fixture)
    {
        // 2025-11-25 tasks: "Receivers that do not declare the task capability for a
        // request type MUST process requests of that type normally, ignoring any
        // task-augmentation metadata" (VS Code declares the experimental tasks)
        pdftest::McpTestClient client = fixture.client(pdftest::McpTestEra::Legacy20251125);
        client.setCapabilities(QJsonObject({ { QStringLiteral("tasks"), QJsonObject({ { QStringLiteral("requests"), QJsonObject() } }) } }));
        QVERIFY(!client.initialize().value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("tasks")));
        const QJsonObject params({ { QStringLiteral("name"), QStringLiteral("echo") }, { QStringLiteral("arguments"), QJsonObject() },
                                   { QStringLiteral("task"), QJsonObject({ { QStringLiteral("ttl"), 60000 } }) } });
        const QJsonObject result = client.request(QStringLiteral("tools/call"), params).value(QStringLiteral("result")).toObject();
        QVERIFY(result.contains(QStringLiteral("content")));
        QVERIFY(!result.contains(QStringLiteral("task")));
    }
};

const std::vector<McpConformanceRow>& McpConformance::getRows()
{
    static const std::vector<McpConformanceRow> s_rows = {
        { "http-origin-403", "Origin present and invalid: 403 (Streamable HTTP, Security)", &McpConformanceChecks::originInvalid },
        { "http-single-message", "The POST body is a single JSON-RPC message (no batch)", &McpConformanceChecks::singleMessage },
        { "http-accept-406", "Accept lists application/json and text/event-stream (406)", &McpConformanceChecks::acceptBoth },
        { "http-notification-202", "A notification is answered by 202 without a body", &McpConformanceChecks::notificationAccepted },
        { "http-response-types", "A request is answered by application/json or text/event-stream", &McpConformanceChecks::responseTypes },
        { "meta-version-header", "MCP-Protocol-Version equals _meta protocolVersion (400, -32020)", &McpConformanceChecks::versionHeaderMatches },
        { "meta-unsupported-version", "An unsupported version: 400, -32022 with data.supported", &McpConformanceChecks::unsupportedVersion },
        { "meta-unknown-method", "An unknown method: 404, -32601", &McpConformanceChecks::unknownMethod },
        { "meta-method-header", "Mcp-Method required and equal to the method (-32020)", &McpConformanceChecks::methodHeader },
        { "meta-name-header", "Mcp-Name required for tools/call, resources/read, prompts/get (-32020)", &McpConformanceChecks::nameHeader },
        { "meta-name-sentinel", "The base64 sentinel of Mcp-Name is decoded before the comparison", &McpConformanceChecks::nameSentinel },
        { "meta-required-fields", "A missing required _meta field: -32602 with 400", &McpConformanceChecks::requiredMeta },
        { "result-type", "Every result carries resultType", &McpConformanceChecks::resultType },
        { "result-server-info", "serverInfo in the _meta of every result", &McpConformanceChecks::serverInfoMeta },
        { "modern-session-ignored", "Mcp-Session-Id of a modern request is ignored, never minted or echoed", &McpConformanceChecks::sessionIdIgnored },
        { "modern-get-delete-405", "GET and DELETE without a session: 405", &McpConformanceChecks::getDelete405 },
        { "listen-ack-first", "subscriptions/listen: acknowledged first with the subscription id", &McpConformanceChecks::listenAcknowledgedFirst },
        { "listen-requested-only", "subscriptions/listen: only the requested notification types", &McpConformanceChecks::listenRequestedOnly },
        { "listen-subscription-id", "subscriptions/listen: every message carries the subscription id", &McpConformanceChecks::listenSubscriptionId },
        { "listen-graceful-end", "subscriptions/listen: the server ends by a complete result (HTTP rule of the Subscriptions page)", &McpConformanceChecks::listenGracefulEnd },
        { "cancel-stream-close", "Closing the response stream cancels the request", &McpConformanceChecks::closeCancels },
        { "progress-rules", "Progress only with a progressToken, strictly increasing, stopped after completion", &McpConformanceChecks::progressRules },
        { "logging-rules", "notifications/message only for requests with a logLevel", &McpConformanceChecks::loggingRules },
        { "resource-not-found", "resources/read of an unknown URI: -32602 (modern), -32002 (legacy) with data.uri", &McpConformanceChecks::resourceNotFound },
        { "mrtr-declared-only", "inputRequests only for a client, which declared elicitation", &McpConformanceChecks::inputRequestsDeclared },
        { "mrtr-state-integrity", "requestState integrity protected; a failed verification refused", &McpConformanceChecks::requestStateIntegrity },
        { "tasks-extension-only", "CreateTaskResult only with the tasks extension on the request", &McpConformanceChecks::taskOnlyWithExtension },
        { "tasks-capability", "tasks/* without the extension: -32021 with requiredCapabilities", &McpConformanceChecks::tasksCapability },
        { "legacy-initialize-version", "initialize echoes a supported version, otherwise another supported one", &McpConformanceChecks::initializeVersion },
        { "legacy-session-404", "A terminated session: 404 to its requests", &McpConformanceChecks::sessionNotFound },
        { "legacy-get-no-broadcast", "One message on only one stream (a new GET replaces the old one)", &McpConformanceChecks::getStreamNoBroadcast },
        { "legacy-list-changed", "Legacy list_changed on the GET stream of the session", &McpConformanceChecks::legacyListChanged },
        { "jsonrpc-id-echoed", "The response id equals the request id", &McpConformanceChecks::idsEchoed },
        { "jsonrpc-id-not-null", "A null id is refused (-32600)", &McpConformanceChecks::idNotNull },
        { "discover-implemented", "server/discover is implemented", &McpConformanceChecks::discoverImplemented },
        { "sse-keep-alive", "Keep-alive comments and X-Accel-Buffering: no on a stream", &McpConformanceChecks::keepAliveComments },
        { "modern-no-client-responses", "A JSON-RPC response of a modern client is refused (400)", &McpConformanceChecks::modernNoClientResponses },
        { "modern-no-server-requests", "No server request on a modern stream: a question is an InputRequiredResult", &McpConformanceChecks::modernNoServerRequests },
        { "cache-fields-of-pages", "ttlMs >= 0 and the same cacheScope on every page of a list", &McpConformanceChecks::cacheFieldsOfPages },
        { "pagination-invalid-cursor", "An invalid cursor: -32602", &McpConformanceChecks::invalidCursor },
        { "legacy-no-session-400", "A legacy request without a session (not initialize): 400", &McpConformanceChecks::legacyNoSession },
        { "legacy-version-header-400", "A legacy MCP-Protocol-Version, which is invalid or unsupported: 400", &McpConformanceChecks::legacyVersionHeader },
        { "legacy-session-id-ascii", "The session id has visible ASCII characters only", &McpConformanceChecks::legacySessionIdAscii },
        { "elicitation-mode-supported", "No elicitation of a mode, which the client did not declare (form vs url)", &McpConformanceChecks::elicitationModeSupported },
        { "legacy-task-metadata-ignored", "2025-11-25: task metadata of a request is ignored (no tasks capability declared)", &McpConformanceChecks::legacyTaskMetadataIgnored }
    };
    return s_rows;
}

QTEST_GUILESS_MAIN(McpTest)

#include "tst_mcptest.moc"
