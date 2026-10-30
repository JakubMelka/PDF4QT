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

#include "pdfappmcpclientsetup.h"

#include <QRegularExpression>
#include <QStringList>

namespace pdf
{

/// Building blocks of the setup texts
class PDFAppMcpClientSetupHelper
{
public:
    PDFAppMcpClientSetupHelper() = delete;

    /// Longest token, which is put into a text
    static constexpr int MAXIMUM_TOKEN_LENGTH = 256;

    /// Longest address, which is put into a text
    static constexpr int MAXIMUM_ADDRESS_LENGTH = 256;

    static QString getHeaderValue(const QString& token)
    {
        return QStringLiteral("Bearer ") + token;
    }

    /// The address is a loopback address (mcp-remote accepts plain http only there)
    static bool isLoopbackAddress(const QString& address)
    {
        return address.startsWith(QLatin1String("http://127.0.0.1:")) || address.startsWith(QLatin1String("http://localhost:")) ||
               address.startsWith(QLatin1String("http://[::1]:"));
    }

    static PDFAppMcpSetupStep step(const QString& text, const QString& code, PDFAppMcpSetupCodeKind codeKind)
    {
        PDFAppMcpSetupStep result;
        result.text = text;
        result.code = code;
        result.codeKind = code.isEmpty() ? PDFAppMcpSetupCodeKind::None : codeKind;
        return result;
    }

    /// The last step of the setups of the assistants (Figma 653:5774)
    static PDFAppMcpSetupStep promptStep(const QString& assistant)
    {
        return step(PDFAppMcpClientSetup::tr("Open a document in PDF4QT and ask %1 about it, for example:").arg(assistant),
                    PDFAppMcpClientSetup::tr("Summarize the document that is open in PDF4QT."), PDFAppMcpSetupCodeKind::Prompt);
    }

    /// JSON of a server with "url" and "headers" ("type": "http" when \p type is set),
    /// indented by two spaces as Figma 955:149432
    static QString createJsonHttpServer(const QString& rootKey, bool hasType, const QString& address, const QString& token)
    {
        QStringList lines;
        lines << QStringLiteral("{");
        lines << QStringLiteral("  %1: {").arg(PDFAppMcpClientSetup::quoteJson(rootKey));
        lines << QStringLiteral("    %1: {").arg(PDFAppMcpClientSetup::quoteJson(PDFAppMcpClientSetup::getServerName()));
        if (hasType)
        {
            lines << QStringLiteral("      \"type\": \"http\",");
        }
        if (token.isEmpty())
        {
            lines << QStringLiteral("      \"url\": %1").arg(PDFAppMcpClientSetup::quoteJson(address));
        }
        else
        {
            lines << QStringLiteral("      \"url\": %1,").arg(PDFAppMcpClientSetup::quoteJson(address));
            lines << QStringLiteral("      \"headers\": {");
            lines << QStringLiteral("        \"Authorization\": %1").arg(PDFAppMcpClientSetup::quoteJson(getHeaderValue(token)));
            lines << QStringLiteral("      }");
        }
        lines << QStringLiteral("    }");
        lines << QStringLiteral("  }");
        lines << QStringLiteral("}");
        return lines.join(QLatin1Char('\n'));
    }

    /// Claude Desktop: the bridge mcp-remote (plan 5.2). "Authorization:${…}"
    /// without a space works around the Windows argument bug described in the
    /// README of mcp-remote; plain http to another computer needs --allow-http.
    static QString createClaudeDesktop(const QString& address, const QString& token)
    {
        QStringList arguments = { QStringLiteral("-y"), QStringLiteral("mcp-remote"), address, QStringLiteral("--transport"), QStringLiteral("http-only") };
        if (!isLoopbackAddress(address))
        {
            arguments << QStringLiteral("--allow-http");
        }
        if (!token.isEmpty())
        {
            arguments << QStringLiteral("--header") << QStringLiteral("Authorization:${PDF4QT_AUTH}");
        }

        QStringList lines;
        lines << QStringLiteral("{");
        lines << QStringLiteral("  \"mcpServers\": {");
        lines << QStringLiteral("    %1: {").arg(PDFAppMcpClientSetup::quoteJson(PDFAppMcpClientSetup::getServerName()));
        lines << QStringLiteral("      \"command\": \"npx\",");
        lines << QStringLiteral("      \"args\": [");
        for (qsizetype i = 0; i < arguments.size(); ++i)
        {
            lines << QStringLiteral("        %1%2").arg(PDFAppMcpClientSetup::quoteJson(arguments[i]), i + 1 < arguments.size() ? QStringLiteral(",") : QString());
        }
        if (token.isEmpty())
        {
            lines << QStringLiteral("      ]");
        }
        else
        {
            lines << QStringLiteral("      ],");
            lines << QStringLiteral("      \"env\": {");
            lines << QStringLiteral("        \"PDF4QT_AUTH\": %1").arg(PDFAppMcpClientSetup::quoteJson(getHeaderValue(token)));
            lines << QStringLiteral("      }");
        }
        lines << QStringLiteral("    }");
        lines << QStringLiteral("  }");
        lines << QStringLiteral("}");
        return lines.join(QLatin1Char('\n'));
    }

    /// Codex: ~/.codex/config.toml. tool_timeout_sec = 180, because a
    /// confirmation in PDF4QT waits up to 2 minutes (Codex waits 60 s by default)
    static QString createCodex(const QString& address, const QString& token)
    {
        QStringList lines;
        lines << QStringLiteral("[mcp_servers.%1]").arg(PDFAppMcpClientSetup::getServerName());
        lines << QStringLiteral("url = %1").arg(PDFAppMcpClientSetup::quoteToml(address));
        if (!token.isEmpty())
        {
            lines << QStringLiteral("http_headers = { \"Authorization\" = %1 }").arg(PDFAppMcpClientSetup::quoteToml(getHeaderValue(token)));
        }
        lines << QStringLiteral("tool_timeout_sec = 180");
        return lines.join(QLatin1Char('\n'));
    }
};

std::vector<PDFAppMcpSetupClient> PDFAppMcpClientSetup::getTileClients()
{
    return { PDFAppMcpSetupClient::ClaudeCode, PDFAppMcpSetupClient::Codex, PDFAppMcpSetupClient::Gemini,
             PDFAppMcpSetupClient::Copilot, PDFAppMcpSetupClient::Cursor, PDFAppMcpSetupClient::Other };
}

std::vector<PDFAppMcpSetupClient> PDFAppMcpClientSetup::getVariants(PDFAppMcpSetupClient client)
{
    if (client == PDFAppMcpSetupClient::ClaudeCode || client == PDFAppMcpSetupClient::ClaudeDesktop)
    {
        return { PDFAppMcpSetupClient::ClaudeCode, PDFAppMcpSetupClient::ClaudeDesktop };
    }
    return { client };
}

QString PDFAppMcpClientSetup::getTileName(PDFAppMcpSetupClient client)
{
    switch (client)
    {
        case PDFAppMcpSetupClient::ClaudeCode:
        case PDFAppMcpSetupClient::ClaudeDesktop:
            return QStringLiteral("Claude");
        case PDFAppMcpSetupClient::Codex:
            return QStringLiteral("Codex");
        case PDFAppMcpSetupClient::Gemini:
            return QStringLiteral("Gemini");
        case PDFAppMcpSetupClient::Copilot:
            return QStringLiteral("Copilot");
        case PDFAppMcpSetupClient::Cursor:
            return QStringLiteral("Cursor");
        case PDFAppMcpSetupClient::Other:
            return tr("Other client");
    }
    return QString();
}

QString PDFAppMcpClientSetup::getVendor(PDFAppMcpSetupClient client)
{
    switch (client)
    {
        case PDFAppMcpSetupClient::ClaudeCode:
        case PDFAppMcpSetupClient::ClaudeDesktop:
            return QStringLiteral("Anthropic");
        case PDFAppMcpSetupClient::Codex:
            return QStringLiteral("OpenAI");
        case PDFAppMcpSetupClient::Gemini:
            return QStringLiteral("Google");
        case PDFAppMcpSetupClient::Copilot:
            return QStringLiteral("GitHub");
        case PDFAppMcpSetupClient::Cursor:
            return QStringLiteral("Anysphere");
        case PDFAppMcpSetupClient::Other:
            return tr("Any MCP client");
    }
    return QString();
}

QString PDFAppMcpClientSetup::getVariantName(PDFAppMcpSetupClient client)
{
    switch (client)
    {
        case PDFAppMcpSetupClient::ClaudeCode:
            return QStringLiteral("Claude Code");
        case PDFAppMcpSetupClient::ClaudeDesktop:
            return QStringLiteral("Claude Desktop");
        case PDFAppMcpSetupClient::Codex:
            return QStringLiteral("Codex");
        case PDFAppMcpSetupClient::Gemini:
            return QStringLiteral("Gemini CLI");
        case PDFAppMcpSetupClient::Copilot:
            return QStringLiteral("GitHub Copilot");
        case PDFAppMcpSetupClient::Cursor:
            return QStringLiteral("Cursor");
        case PDFAppMcpSetupClient::Other:
            return tr("Other client");
    }
    return QString();
}

QString PDFAppMcpClientSetup::getClientInfoName(PDFAppMcpSetupClient client)
{
    // The names of the table of PDFAppMcpClientNames. Wave 0 verified only
    // gemini-cli-mcp-client and "Visual Studio Code"; claude-code, claude-ai,
    // codex-mcp-client and cursor-vscode are UNVERIFIED (no local client source).
    switch (client)
    {
        case PDFAppMcpSetupClient::ClaudeCode:
            return QStringLiteral("claude-code");
        case PDFAppMcpSetupClient::ClaudeDesktop:
            return QStringLiteral("claude-ai");
        case PDFAppMcpSetupClient::Codex:
            return QStringLiteral("codex-mcp-client");
        case PDFAppMcpSetupClient::Gemini:
            return QStringLiteral("gemini-cli-mcp-client");
        case PDFAppMcpSetupClient::Copilot:
            return QStringLiteral("Visual Studio Code");
        case PDFAppMcpSetupClient::Cursor:
            return QStringLiteral("cursor-vscode");
        case PDFAppMcpSetupClient::Other:
            return QString();
    }
    return QString();
}

bool PDFAppMcpClientSetup::isSafeToken(const QString& token)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9._~+/=-]+$"));
    return !token.isEmpty() && token.size() <= PDFAppMcpClientSetupHelper::MAXIMUM_TOKEN_LENGTH && expression.match(token).hasMatch();
}

bool PDFAppMcpClientSetup::isSafeAddress(const QString& address)
{
    static const QRegularExpression expression(QStringLiteral("^http://([0-9]{1,3}(\\.[0-9]{1,3}){3}|\\[[0-9A-Fa-f:.]+\\]|[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?):[0-9]{1,5}/mcp$"));
    return address.size() <= PDFAppMcpClientSetupHelper::MAXIMUM_ADDRESS_LENGTH && expression.match(address).hasMatch();
}

QString PDFAppMcpClientSetup::quoteJson(const QString& text)
{
    QString result;
    result.reserve(text.size() + 2);
    result += QLatin1Char('"');
    for (const QChar character : text)
    {
        switch (character.unicode())
        {
            case u'"':
                result += QStringLiteral("\\\"");
                break;
            case u'\\':
                result += QStringLiteral("\\\\");
                break;
            case u'\n':
                result += QStringLiteral("\\n");
                break;
            case u'\r':
                result += QStringLiteral("\\r");
                break;
            case u'\t':
                result += QStringLiteral("\\t");
                break;
            default:
                if (character.unicode() < 0x20)
                {
                    result += QStringLiteral("\\u%1").arg(int(character.unicode()), 4, 16, QLatin1Char('0'));
                }
                else
                {
                    result += character;
                }
                break;
        }
    }
    result += QLatin1Char('"');
    return result;
}

QString PDFAppMcpClientSetup::quoteToml(const QString& text)
{
    QString result;
    result.reserve(text.size() + 2);
    result += QLatin1Char('"');
    for (const QChar character : text)
    {
        switch (character.unicode())
        {
            case u'"':
                result += QStringLiteral("\\\"");
                break;
            case u'\\':
                result += QStringLiteral("\\\\");
                break;
            case u'\n':
                result += QStringLiteral("\\n");
                break;
            case u'\r':
                result += QStringLiteral("\\r");
                break;
            case u'\t':
                result += QStringLiteral("\\t");
                break;
            default:
                if (character.unicode() < 0x20 || character.unicode() == 0x7F)
                {
                    result += QStringLiteral("\\u%1").arg(int(character.unicode()), 4, 16, QLatin1Char('0'));
                }
                else
                {
                    result += character;
                }
                break;
        }
    }
    result += QLatin1Char('"');
    return result;
}

QString PDFAppMcpClientSetup::getDisplayToken(const QString& token)
{
    if (token.size() <= 12)
    {
        return token;
    }
    return token.left(6) + QChar(0x2026) + token.right(4);
}

PDFAppMcpSetup PDFAppMcpClientSetup::create(PDFAppMcpSetupClient client, const QString& address, const QString& token)
{
    // Values, which could be interpreted by a shell or a file, are never put
    // into a text (the token and the address come from the settings)
    const QString url = isSafeAddress(address) ? address : QStringLiteral("<address>");
    const QString key = token.isEmpty() ? QString() : (isSafeToken(token) ? token : QStringLiteral("<token>"));
    return createSetup(client, url, key);
}

PDFAppMcpSetup PDFAppMcpClientSetup::createForDisplay(PDFAppMcpSetupClient client, const QString& address, const QString& token)
{
    const QString url = isSafeAddress(address) ? address : QStringLiteral("<address>");
    const QString key = token.isEmpty() ? QString() : (isSafeToken(token) ? getDisplayToken(token) : QStringLiteral("<token>"));
    return createSetup(client, url, key);
}

PDFAppMcpSetup PDFAppMcpClientSetup::createSetup(PDFAppMcpSetupClient client, const QString& url, const QString& key)
{
    using H = PDFAppMcpClientSetupHelper;

    const QString server = getServerName();
    const bool hasToken = !key.isEmpty();

    PDFAppMcpSetup setup;
    setup.client = client;
    setup.containsToken = hasToken;

    switch (client)
    {
        case PDFAppMcpSetupClient::ClaudeCode:
        {
            // Figma 653:5730 (verbatim); without the token 955:150877
            QString command = QStringLiteral("claude mcp add --transport http %1 %2").arg(server, url);
            if (hasToken)
            {
                command += QStringLiteral(" --header \"Authorization: Bearer %1\"").arg(key);
            }
            setup.title = tr("Set up Claude");
            setup.subtitle = tr("Adds this server to Claude Code. It takes about a minute.");
            setup.steps.push_back(H::step(tr("Run this command in a terminal. It registers PDF4QT as an MCP server of Claude Code."), command, PDFAppMcpSetupCodeKind::Command));
            setup.steps.push_back(H::step(tr("Check that Claude Code lists the server as connected."), QStringLiteral("claude mcp list"), PDFAppMcpSetupCodeKind::Command));
            setup.steps.push_back(H::promptStep(QStringLiteral("Claude")));
            setup.copyButtonText = tr("Copy Command");
            setup.copyText = command;
            setup.documentationText = tr("Claude Code Documentation");
            setup.documentationLink = QStringLiteral("https://code.claude.com/docs/en/mcp");    // verified against the documentation at this address
            setup.note = hasToken ? tr("The command contains your access token. Run it again after you create a new token.")
                                  : tr("The command contains the address of the server. Run it again after you change the port.");
            break;
        }

        case PDFAppMcpSetupClient::ClaudeDesktop:
        {
            const QString configuration = H::createClaudeDesktop(url, key);
            setup.title = tr("Set up Claude");
            setup.subtitle = tr("Adds this server to Claude Desktop through the bridge mcp-remote, which needs Node.js. It takes a few minutes.");
            setup.steps.push_back(H::step(tr("In Claude Desktop, open Settings › Developer › Edit Config and add this entry to claude_desktop_config.json."),
                                          configuration, PDFAppMcpSetupCodeKind::Configuration));
            setup.steps.push_back(H::step(tr("Quit Claude Desktop completely and start it again. The tools of PDF4QT appear in a new chat."), QString(), PDFAppMcpSetupCodeKind::None));
            setup.steps.push_back(H::promptStep(QStringLiteral("Claude")));
            setup.copyButtonText = tr("Copy Configuration");
            setup.copyText = configuration;
            setup.documentationText = tr("Claude Desktop Documentation");
            setup.documentationLink = QStringLiteral("https://modelcontextprotocol.io/docs/develop/connect-local-servers");    // unverified address
            setup.note = hasToken ? tr("The configuration contains your access token. Copy it again after you create a new token.")
                                  : tr("The configuration contains the address of the server. Copy it again after you change the port.");
            break;
        }

        case PDFAppMcpSetupClient::Codex:
        {
            const QString configuration = H::createCodex(url, key);
            setup.title = tr("Set up Codex");
            setup.subtitle = tr("Adds this server to Codex. It takes about a minute.");
            setup.steps.push_back(H::step(tr("Add this entry to the Codex configuration file ~/.codex/config.toml. The tool timeout gives you time to confirm requests in PDF4QT."),
                                          configuration, PDFAppMcpSetupCodeKind::Configuration));
            setup.steps.push_back(H::step(tr("Check that Codex lists the server."), QStringLiteral("codex mcp list"), PDFAppMcpSetupCodeKind::Command));
            setup.steps.push_back(H::promptStep(QStringLiteral("Codex")));
            setup.copyButtonText = tr("Copy Configuration");
            setup.copyText = configuration;
            setup.documentationText = tr("Codex Documentation");
            setup.documentationLink = QStringLiteral("https://learn.chatgpt.com/docs/extend/mcp");    // verified against the documentation at this address
            setup.note = hasToken ? tr("The configuration contains your access token. Copy it again after you create a new token.")
                                  : tr("The configuration contains the address of the server. Copy it again after you change the port.");
            break;
        }

        case PDFAppMcpSetupClient::Gemini:
        {
            // Flags verified by wave 0 (https://github.com/google-gemini/gemini-cli/blob/main/docs/tools/mcp-server.md); --scope user,
            // because the default scope "project" is only the current folder
            QString command = QStringLiteral("gemini mcp add --scope user --transport http");
            if (hasToken)
            {
                command += QStringLiteral(" --header \"Authorization: Bearer %1\"").arg(key);
            }
            command += QStringLiteral(" %1 %2").arg(server, url);
            setup.title = tr("Set up Gemini");
            setup.subtitle = tr("Adds this server to the Gemini CLI. It takes about a minute.");
            setup.steps.push_back(H::step(tr("Run this command in a terminal. It registers PDF4QT as an MCP server of the Gemini CLI."), command, PDFAppMcpSetupCodeKind::Command));
            setup.steps.push_back(H::step(tr("Check that the Gemini CLI lists the server as connected."), QStringLiteral("gemini mcp list"), PDFAppMcpSetupCodeKind::Command));
            setup.steps.push_back(H::promptStep(QStringLiteral("Gemini")));
            setup.copyButtonText = tr("Copy Command");
            setup.copyText = command;
            setup.documentationText = tr("Gemini CLI Documentation");
            setup.documentationLink = QStringLiteral("https://github.com/google-gemini/gemini-cli/blob/main/docs/tools/mcp-server.md");    // unverified address
            setup.note = hasToken ? tr("The command contains your access token. Run it again after you create a new token.")
                                  : tr("The command contains the address of the server. Run it again after you change the port.");
            break;
        }

        case PDFAppMcpSetupClient::Copilot:
        {
            const QString configuration = H::createJsonHttpServer(QStringLiteral("servers"), true, url, key);
            setup.title = tr("Set up Copilot");
            setup.subtitle = tr("Adds this server to GitHub Copilot in Visual Studio Code. It takes about a minute.");
            setup.steps.push_back(H::step(tr("In Visual Studio Code, run the command MCP: Open User Configuration and add this entry to the servers of mcp.json."),
                                          configuration, PDFAppMcpSetupCodeKind::Configuration));
            setup.steps.push_back(H::step(tr("Run the command MCP: List Servers, choose pdf4qt and start it."), QString(), PDFAppMcpSetupCodeKind::None));
            setup.steps.push_back(H::promptStep(QStringLiteral("Copilot")));
            setup.copyButtonText = tr("Copy Configuration");
            setup.copyText = configuration;
            setup.documentationText = tr("Visual Studio Code Documentation");
            setup.documentationLink = QStringLiteral("https://code.visualstudio.com/docs/agent-customization/mcp-servers");    // unverified address (derived from the configuration reference of Visual Studio Code)
            setup.note = hasToken ? tr("The configuration contains your access token. Copy it again after you create a new token.")
                                  : tr("The configuration contains the address of the server. Copy it again after you change the port.");
            break;
        }

        case PDFAppMcpSetupClient::Cursor:
        {
            const QString configuration = H::createJsonHttpServer(QStringLiteral("mcpServers"), false, url, key);
            setup.title = tr("Set up Cursor");
            setup.subtitle = tr("Adds this server to Cursor. It takes about a minute.");
            setup.steps.push_back(H::step(tr("Add this entry to the Cursor configuration file ~/.cursor/mcp.json."), configuration, PDFAppMcpSetupCodeKind::Configuration));
            setup.steps.push_back(H::step(tr("Open Customize in the sidebar of Cursor and check that pdf4qt is turned on."), QString(), PDFAppMcpSetupCodeKind::None));
            setup.steps.push_back(H::promptStep(QStringLiteral("Cursor")));
            setup.copyButtonText = tr("Copy Configuration");
            setup.copyText = configuration;
            setup.documentationText = tr("Cursor Documentation");
            setup.documentationLink = QStringLiteral("https://cursor.com/docs/mcp");    // verified against the documentation at this address
            setup.note = hasToken ? tr("The configuration contains your access token. Copy it again after you create a new token.")
                                  : tr("The configuration contains the address of the server. Copy it again after you change the port.");
            break;
        }

        case PDFAppMcpSetupClient::Other:
        {
            // Figma 955:149400 (verbatim); without the token 955:150877 leaves the header out
            const QString configuration = H::createJsonHttpServer(QStringLiteral("mcpServers"), true, url, key);
            setup.title = tr("Set up another MCP client");
            setup.subtitle = tr("For any client that supports MCP over Streamable HTTP. Enter these values in its server settings.");
            setup.steps.push_back(H::step(tr("Add an MCP server of type Streamable HTTP (called “http” in some clients) with this address."), url, PDFAppMcpSetupCodeKind::Configuration));
            if (hasToken)
            {
                setup.steps.push_back(H::step(tr("Let the client send the access token in this request header."), QStringLiteral("Authorization: Bearer %1").arg(key),
                                              PDFAppMcpSetupCodeKind::Configuration));
            }
            setup.steps.push_back(H::step(tr("For a client configured by a file, add this entry to its MCP configuration. Key names differ between clients."),
                                          configuration, PDFAppMcpSetupCodeKind::Configuration));
            setup.copyButtonText = tr("Copy Configuration");
            setup.copyText = configuration;
            setup.documentationText = tr("Setup Guide");
            setup.documentationLink = QStringLiteral("/help/ai-assistant#other-mcp-clients");
            setup.note = hasToken ? tr("The header and the configuration contain your access token – copy them again after you create a new token. "
                                       "Without Require access token, leave the header out.")
                                  : tr("The configuration contains the address of the server. Copy it again after you change the port.");
            break;
        }
    }

    return setup;
}

}   // namespace pdf
