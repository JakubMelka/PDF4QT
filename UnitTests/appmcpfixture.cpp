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

#include "appmcpfixture.h"

#include "pdfappmcpcontroller.h"
#include "pdfappmcphost.h"
#include "pdfsettingdefinitions.h"

#include "pdfmcpassistants.h"
#include "pdfmcpcatalog.h"
#include "pdfmcpschema.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTcpServer>
#include <QThread>
#include <QtTest>

namespace pdftest
{

void TestConfirmationProvider::showConfirmation(std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation)
{
    shown.push_back(confirmation);
    waiting.push_back(confirmation->id);
    if (m_autoAnswer && m_host)
    {
        QMetaObject::invokeMethod(m_host.data(), &pdf::PDFAppMcpHost::resolveConfirmation, Qt::QueuedConnection, confirmation->id, *m_autoAnswer);
    }
}

void TestConfirmationProvider::withdrawConfirmation(quint64 confirmationId, pdf::PDFAppMcpDecision decision)
{
    withdrawn.emplace_back(confirmationId, decision);
    waiting.erase(std::remove(waiting.begin(), waiting.end(), confirmationId), waiting.end());
}

bool TestConfirmationProvider::answer(pdf::PDFAppMcpDecision decision)
{
    // Ids answered by the host meanwhile (auto answers) are skipped
    while (!waiting.empty() && m_host)
    {
        const quint64 id = waiting.front();
        waiting.erase(waiting.begin());
        bool isPending = false;
        for (const auto& confirmation : m_host->getPendingConfirmations())
        {
            isPending = isPending || confirmation->id == id;
        }
        if (isPending)
        {
            m_host->resolveConfirmation(id, decision);
            return true;
        }
    }
    return false;
}

AppMcpFixture::AppMcpFixture(QSize size) :
    AppDocumentFixture(size)
{
    // The probe of a port in use asks the other server over the loopback: under
    // valgrind its answer takes longer than the 500 ms of the product
    if (controller())
    {
        controller()->setPortProbeTimeout(pdf::PDFMcpLimits::PORT_PROBE_TIMEOUT * getTimeoutFactor());
    }
    if (host())
    {
        m_provider = std::make_unique<TestConfirmationProvider>(host());
        host()->setConfirmationProvider(m_provider.get());
    }
}

AppMcpFixture::~AppMcpFixture()
{
    if (host())
    {
        host()->setConfirmationProvider(nullptr);
    }
}

pdf::PDFAppMcpController* AppMcpFixture::controller() const
{
    return context ? context->getMcpController() : nullptr;
}

pdf::PDFAppMcpHost* AppMcpFixture::host() const
{
    return controller() ? controller()->getHost() : nullptr;
}

quint16 AppMcpFixture::findFreePort()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
    {
        return 0;
    }
    const quint16 port = server.serverPort();
    server.close();
    return port;
}

bool AppMcpFixture::startServer()
{
    // The controller may be deleted by the application meanwhile (UnitTestsAppMcpLifetime)
    QPointer<pdf::PDFAppMcpController> mcp = controller();
    if (!mcp)
    {
        return false;
    }

    for (int attempt = 0; attempt < 5; ++attempt)
    {
        const quint16 freePort = findFreePort();
        if (freePort < 1024 || context->getSettings().setValue(pdf::PDFSettingKeys::mcpPort(), int(freePort)).isError())
        {
            continue;
        }

        if (!mcp)
        {
            return false;
        }
        mcp->stop();
        if (!mcp)
        {
            return false;
        }
        mcp->start();
        QElapsedTimer timer;
        timer.start();
        while (mcp && timer.elapsed() < 10000 * getTimeoutFactor())
        {
            const pdf::PDFAppMcpServerState state = mcp->getState();
            if (state == pdf::PDFAppMcpServerState::Running)
            {
                return true;
            }
            if (state != pdf::PDFAppMcpServerState::Starting)
            {
                break;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
        }
    }
    return false;
}

quint16 AppMcpFixture::port() const
{
    return controller() && controller()->isRunning() ? controller()->getPort() : 0;
}

std::unique_ptr<McpTestClient> AppMcpFixture::client(McpTestEra era) const
{
    std::unique_ptr<McpTestClient> result = std::make_unique<McpTestClient>(port(), era);
    if (controller())
    {
        result->setToken(controller()->getToken().toLatin1());
    }
    result->setClientInfo(m_clientName);
    return result;
}

void AppMcpFixture::setClient(const QString& clientName, pdf::PDFMcpEra era, const QJsonObject& capabilities)
{
    m_clientName = clientName;
    m_era = era;
    m_capabilities = capabilities;
}

pdf::PDFMcpCallRequest AppMcpFixture::createRequest(const QString& toolName, const QJsonObject& arguments) const
{
    pdf::PDFMcpCallRequest request;
    request.kind = pdf::PDFMcpCallKind::ToolCall;
    request.method = QStringLiteral("tools/call");
    request.name = toolName;
    request.arguments = arguments;
    request.client.era = m_era;
    request.client.protocolVersion = pdf::PDFMcpProtocol::getVersion(m_era);
    request.client.clientCapabilities = m_capabilities;
    request.client.clientName = m_clientName;
    request.client.remoteAddress = QStringLiteral("127.0.0.1");
    // The key of the server (pdfmcpdispatcher_protocol.cpp): the token only, when
    // it is required (not by default in the local mode - owner 2026-10-28)
    QByteArray keyToken;
    if (controller())
    {
        const pdf::PDFMcpServerConfig config = controller()->createConfig();
        keyToken = config.isTokenRequired() ? config.token : QByteArray();
    }
    request.client.assistantKey = pdf::PDFMcpAssistantRegistry::createModernKey(keyToken, m_clientName, request.client.remoteAddress);
    request.id = pdf::PDFMcpRequestId::createInteger(1);
    request.receivedAt = QDateTime::currentMSecsSinceEpoch();
    return request;
}

std::shared_ptr<pdf::PDFMcpCall> AppMcpFixture::startCall(const QString& toolName, const QJsonObject& arguments, bool hasProgressToken)
{
    pdf::PDFMcpCallRequest request = createRequest(toolName, arguments);
    request.client.hasProgressToken = hasProgressToken;
    std::shared_ptr<pdf::PDFMcpCall> call = pdf::PDFMcpCall::create(request);
    if (host())
    {
        host()->handleCall(call);
    }
    return call;
}

std::optional<pdf::PDFMcpCallResult> AppMcpFixture::waitResult(const std::shared_ptr<pdf::PDFMcpCall>& call, int timeout)
{
    QElapsedTimer timer;
    timer.start();
    while (call && !call->isFinished() && timer.elapsed() < qint64(timeout) * getTimeoutFactor())
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    }
    return call ? call->getResult() : std::nullopt;
}

std::optional<pdf::PDFMcpCallResult> AppMcpFixture::callTool(const QString& toolName, const QJsonObject& arguments)
{
    if (m_transport == AppMcpTransport::Http)
    {
        std::unique_ptr<McpTestClient> httpClient = client(m_era == pdf::PDFMcpEra::Modern ? McpTestEra::Modern : McpTestEra::Legacy20251125);
        httpClient->setCapabilities(m_capabilities);
        if (httpClient->getEra() != McpTestEra::Modern)
        {
            httpClient->initialize();
        }

        const QJsonObject response = httpClient->callTool(toolName, arguments);
        if (response.contains(QStringLiteral("error")))
        {
            const QJsonObject error = response.value(QStringLiteral("error")).toObject();
            return pdf::PDFMcpCallResult::createError(error.value(QStringLiteral("code")).toInt(), error.value(QStringLiteral("message")).toString());
        }
        if (!response.contains(QStringLiteral("result")))
        {
            return std::nullopt;
        }
        const QJsonObject result = response.value(QStringLiteral("result")).toObject();
        const pdf::PDFMcpCallResult callResult = pdf::PDFMcpCallResult::createComplete(QJsonDocument(result).toJson(QJsonDocument::Compact), result.value(QStringLiteral("isError")).toBool());
        verifyOutputSchema(toolName, callResult);
        return callResult;
    }

    const std::optional<pdf::PDFMcpCallResult> result = waitResult(startCall(toolName, arguments));
    if (result)
    {
        verifyOutputSchema(toolName, *result);
    }
    return result;
}

QStringList AppMcpFixture::checkOutputSchema(const QString& toolName, const pdf::PDFMcpCallResult& result) const
{
    // Review RP7: "If an output schema is provided: Servers MUST provide
    // structured results that conform to this schema" (clients of the SDKs
    // refuse a result, whose structuredContent does not match)
    QStringList errors;
    const std::shared_ptr<const pdf::PDFMcpCatalog> catalog = host() ? host()->getCatalog() : nullptr;
    const pdf::PDFMcpToolDefinition* tool = catalog ? catalog->findTool(toolName) : nullptr;
    if (result.kind != pdf::PDFMcpCallResult::Kind::Complete || !tool || tool->outputSchema.isEmpty())
    {
        return errors;
    }

    const QJsonObject object = QJsonDocument::fromJson(result.result).object();
    if (object.value(QStringLiteral("isError")).toBool())
    {
        // An execution error carries a text for the model only
        return errors;
    }

    QString message;
    const std::optional<pdf::PDFMcpSchema> schema = pdf::PDFMcpSchema::fromQJson(QJsonDocument::fromJson(tool->outputSchema).object(), &message);
    if (!schema)
    {
        errors.append(QStringLiteral("outputSchema of %1: %2").arg(toolName, message));
    }
    else if (!object.contains(QStringLiteral("structuredContent")))
    {
        errors.append(QStringLiteral("%1: no structuredContent of a tool with an outputSchema").arg(toolName));
    }
    else
    {
        schema->validate(object.value(QStringLiteral("structuredContent")), QStringLiteral("structuredContent"), &errors, 16);
    }
    return errors;
}

void AppMcpFixture::verifyOutputSchema(const QString& toolName, const pdf::PDFMcpCallResult& result) const
{
    const QStringList errors = checkOutputSchema(toolName, result);
    if (!errors.isEmpty())
    {
        // Every call of callTool is checked: the failure is recorded in the running test
        QTest::qFail(qPrintable(QStringLiteral("The result of %1 does not conform to its outputSchema: %2").arg(toolName, errors.join(QStringLiteral("; ")))), __FILE__, __LINE__);
    }
}

bool AppMcpFixture::setPermission(pdf::PDFAppMcpPermissionKind kind, pdf::PDFAppMcpPermission permission)
{
    QString key;
    switch (kind)
    {
        case pdf::PDFAppMcpPermissionKind::Read:
            key = pdf::PDFSettingKeys::mcpPermissionRead();
            break;
        case pdf::PDFAppMcpPermissionKind::Edit:
            key = pdf::PDFSettingKeys::mcpPermissionEdit();
            break;
        case pdf::PDFAppMcpPermissionKind::Files:
            key = pdf::PDFSettingKeys::mcpPermissionFiles();
            break;
        case pdf::PDFAppMcpPermissionKind::None:
            return false;
    }

    if (context->getSettings().setValue(key, pdf::PDFAppMcpPermissions::getName(permission)).isError())
    {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (host() && host()->getPermissions().get(kind) != permission)
    {
        if (timer.elapsed() > 10000 * getTimeoutFactor())
        {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    }
    return host() != nullptr;
}

bool AppMcpFixture::setPermissions(pdf::PDFAppMcpPermission permission)
{
    return setPermission(pdf::PDFAppMcpPermissionKind::Read, permission) &&
           setPermission(pdf::PDFAppMcpPermissionKind::Edit, permission) &&
           setPermission(pdf::PDFAppMcpPermissionKind::Files, permission);
}

std::shared_ptr<pdf::PDFAppMcpConfirmation> AppMcpFixture::confirmDialog(int timeout)
{
    QElapsedTimer timer;
    timer.start();
    while (host() && timer.elapsed() < qint64(timeout) * getTimeoutFactor())
    {
        const std::vector<std::shared_ptr<pdf::PDFAppMcpConfirmation>> pending = host()->getPendingConfirmations();
        if (!pending.empty())
        {
            return pending.front();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    }
    return nullptr;
}

bool AppMcpFixture::answer(pdf::PDFAppMcpDecision decision)
{
    return m_provider && m_provider->answer(decision);
}

std::vector<pdf::PDFMcpActivityRow> AppMcpFixture::activity()
{
    if (host() && host()->getActivityLog())
    {
        host()->getActivityLog()->flush();
    }
    return pdf::PDFMcpRepository(context->getDatabase()).getActivity(pdf::PDFMcpRepository::MAXIMUM_ACTIVITY_ROWS);
}

bool AppMcpFixture::waitActivity(pdf::PDFMcpActivityStatus status, int timeout)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < qint64(timeout) * getTimeoutFactor())
    {
        for (const pdf::PDFMcpActivityRow& row : activity())
        {
            if (row.status == status)
            {
                return true;
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 10);
    }
    return false;
}

QProcess* AppMcpFixture::secondInstance(const QStringList& arguments)
{
    QProcess* process = new QProcess();
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process->start(QCoreApplication::applicationFilePath(), arguments);
    return process;
}

int AppMcpFixture::runChild(int argc, char* argv[])
{
    if (argc < 2 || !QByteArray(argv[1]).startsWith("--mcp-child-"))
    {
        return -1;
    }

    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() >= 4 && arguments[1] == QLatin1String("--mcp-child-claim"))
    {
        pdf::PDFDatabaseOptions options;
        options.filePath = arguments[2];
        pdf::PDFDatabase database;
        database.open(options);
        if (database.getState().mode != pdf::PDFDatabaseStorageMode::Disk)
        {
            return 3;
        }

        pdf::PDFMcpRepository repository(&database);
        bool claimed = false;
        repository.claimServer(arguments[3], QCoreApplication::applicationPid(), 8765, pdf::PDFMcpServerRowState::Running, QStringLiteral("http://127.0.0.1:8765/mcp"), &claimed);
        if (!claimed)
        {
            database.close();
            return 6;
        }

        // Heartbeat until the test kills the process
        for (int i = 0; i < 1200; ++i)
        {
            repository.heartbeat(arguments[3]);
            QThread::msleep(100);
        }
        database.close();
        return 0;
    }
    return 2;
}

}   // namespace pdftest
