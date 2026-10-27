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

#ifndef APPMCPFIXTURE_H
#define APPMCPFIXTURE_H

#include "appdocumentfixture.h"
#include "mcptestclient.h"

#include "pdfappmcpconfirmation.h"
#include "pdfappmcphost.h"
#include "pdfappmcppermissions.h"
#include "pdfmcprepository.h"

#include "pdfmcpcall.h"

#include <QJsonObject>
#include <QPointer>

#include <memory>
#include <vector>

class QProcess;

namespace pdf
{
class PDFAppMcpController;
class PDFAppMcpHost;
}

namespace pdftest
{

/// Confirmation interface of the tests (until package C installs the dialogs):
/// it records the confirmations and answers them by the policy of the test -
/// at once (setAutoAnswer) or by answer()
class TestConfirmationProvider : public pdf::PDFAppMcpConfirmationProvider
{
public:
    explicit TestConfirmationProvider(pdf::PDFAppMcpHost* host) : m_host(host) { }

    virtual void showConfirmation(std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmation) override;
    virtual void withdrawConfirmation(quint64 confirmationId, pdf::PDFAppMcpDecision decision) override;

    /// Every confirmation is answered at once (from the event loop) by the decision
    void setAutoAnswer(std::optional<pdf::PDFAppMcpDecision> decision) { m_autoAnswer = decision; }

    /// Answers the oldest waiting confirmation. \returns false, when none waits.
    bool answer(pdf::PDFAppMcpDecision decision);

    std::vector<std::shared_ptr<pdf::PDFAppMcpConfirmation>> shown;     ///< Every shown confirmation
    std::vector<quint64> waiting;                                       ///< Ids of the waiting ones
    std::vector<std::pair<quint64, pdf::PDFAppMcpDecision>> withdrawn;

private:
    QPointer<pdf::PDFAppMcpHost> m_host;
    std::optional<pdf::PDFAppMcpDecision> m_autoAnswer;
};

/// How a tool is called by the fixture
enum class AppMcpTransport
{
    Direct,     ///< PDFAppMcpHost::handleCall of a PDFMcpCall (wave 1 runs without package A)
    Http        ///< The scripted client over the server (after package A)
};

/// Fixture of the MCP tests of the application (step 7): the fixture of the
/// document tab (a portable memory database, the real window) with its MCP
/// controller and host. Tools are called directly on the host (the default) or
/// over HTTP (startServer + client); the permissions are written as settings
/// and reach the host through the notifier; a test confirmation provider
/// answers the questions until package C replaces it.
class AppMcpFixture : public AppDocumentFixture
{
public:
    explicit AppMcpFixture(QSize size = QSize(1440, 960));
    ~AppMcpFixture();

    pdf::PDFAppMcpController* controller() const;
    pdf::PDFAppMcpHost* host() const;
    TestConfirmationProvider* confirmations() const { return m_provider.get(); }

    /// A free port of the loopback (the port of a closed listener)
    static quint16 findFreePort();

    /// Writes mcp.port of a free port and starts the server; waits until it runs
    bool startServer();

    /// Port of the running server (0 = none)
    quint16 port() const;

    /// The scripted client of the era with the token of the settings
    std::unique_ptr<McpTestClient> client(McpTestEra era = McpTestEra::Modern) const;

    /// Transport of callTool
    void setTransport(AppMcpTransport transport) { m_transport = transport; }

    /// Client of the direct calls (clientInfo.name, the era, the capabilities)
    void setClient(const QString& clientName, pdf::PDFMcpEra era = pdf::PDFMcpEra::Modern, const QJsonObject& capabilities = QJsonObject());

    /// The request of a direct call of a tool
    pdf::PDFMcpCallRequest createRequest(const QString& toolName, const QJsonObject& arguments) const;

    /// Starts a direct call and returns it (not waited for)
    std::shared_ptr<pdf::PDFMcpCall> startCall(const QString& toolName, const QJsonObject& arguments, bool hasProgressToken = false);

    /// Calls the tool and waits for its result (std::nullopt = no result in time).
    /// The structuredContent of a successful result is checked against the
    /// outputSchema of the tool (a violation fails the running test - review RP7)
    std::optional<pdf::PDFMcpCallResult> callTool(const QString& toolName, const QJsonObject& arguments);

    /// The violations of the outputSchema of the tool by the result (empty = none)
    QStringList checkOutputSchema(const QString& toolName, const pdf::PDFMcpCallResult& result) const;

    /// Records a failure of the running test, when the result violates the outputSchema
    void verifyOutputSchema(const QString& toolName, const pdf::PDFMcpCallResult& result) const;

    /// Waits for the result of a started call
    static std::optional<pdf::PDFMcpCallResult> waitResult(const std::shared_ptr<pdf::PDFMcpCall>& call, int timeout = 30000);

    /// Writes a permission (setting) and waits until the host has it
    bool setPermission(pdf::PDFAppMcpPermissionKind kind, pdf::PDFAppMcpPermission permission);
    bool setPermissions(pdf::PDFAppMcpPermission permission);

    /// The waiting confirmation (the oldest), waited for at most \p timeout
    std::shared_ptr<pdf::PDFAppMcpConfirmation> confirmDialog(int timeout = 10000);

    /// Answers the waiting confirmation
    bool answer(pdf::PDFAppMcpDecision decision);

    /// Waits until an activity row of the status is written (the log is flushed)
    bool waitActivity(pdf::PDFMcpActivityStatus status, int timeout = 10000);

    /// Activity rows (the log is flushed first)
    std::vector<pdf::PDFMcpActivityRow> activity();

    /// A second process of the test executable in the mode \p mode with the
    /// arguments (the database of a file); the caller owns the process. The
    /// test executable handles the modes by AppMcpFixture::runChild in main.
    static QProcess* secondInstance(const QStringList& arguments);

    /// Handles the child modes of secondInstance ("--mcp-child-*"); returns the
    /// exit code, or -1 when the arguments are not a child mode
    ///   --mcp-child-claim <database file> <instance>: claims mcp_server and
    ///       writes the heartbeat every 100 ms until killed (exit 0 when refused: 6)
    static int runChild(int argc, char* argv[]);

private:
    std::unique_ptr<TestConfirmationProvider> m_provider;
    AppMcpTransport m_transport = AppMcpTransport::Direct;
    QString m_clientName = QStringLiteral("claude-code");
    pdf::PDFMcpEra m_era = pdf::PDFMcpEra::Modern;
    QJsonObject m_capabilities;
};

}   // namespace pdftest

#endif // APPMCPFIXTURE_H
