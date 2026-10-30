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

#include "pdfappmcpclientnames.h"
#include "pdfrepositoryhelper.h"

#include "pdfmcpassistants.h"

namespace pdf
{

/// Helpers of the client names
class PDFAppMcpClientNamesHelper
{
public:
    PDFAppMcpClientNamesHelper() = delete;

    static PDFAppMcpKnownClient create(QStringList names, const char* displayName, const char* monogram, PDFAccentHue hue, bool isCommandLine, const char* source)
    {
        PDFAppMcpKnownClient client;
        client.clientNames = std::move(names);
        client.displayName = QString::fromLatin1(displayName);
        client.monogram = QString::fromLatin1(monogram);
        client.hue = hue;
        client.isCommandLine = isCommandLine;
        client.source = QString::fromLatin1(source);
        return client;
    }

    static std::vector<PDFAppMcpKnownClient> createTable()
    {
        // clientInfo.name of the clients (plan 2.7). The source is the public
        // repository of the client, where its MCP client sets the name; a name,
        // which was not checked in the source of the client, is marked unverified -
        // the display only depends on it, never a decision.
        return {
            create({ QStringLiteral("claude-code") }, "Claude Code", "Cl", PDFAccentHue::Amber, true,
                   "unverified (no local copy of the Claude Code client source; public name of its MCP client)"),
            create({ QStringLiteral("claude-ai"), QStringLiteral("Claude Desktop") }, "Claude Desktop", "Cl", PDFAccentHue::Amber, false,
                   "unverified (Claude Desktop through mcp-remote forwards the name of Claude Desktop)"),
            create({ QStringLiteral("codex-mcp-client"), QStringLiteral("codex") }, "Codex CLI", "Cx", PDFAccentHue::Green, true,
                   "unverified (codex-rs mcp-client of https://github.com/openai/codex)"),
            create({ QStringLiteral("gemini-cli-mcp-client") }, "Gemini CLI", "Ge", PDFAccentHue::Blue, true,
                   "https://github.com/google-gemini/gemini-cli (MCP client of packages/core: new Client({ name: 'gemini-cli-mcp-client' }))"),
            create({ QStringLiteral("Visual Studio Code"), QStringLiteral("Visual Studio Code - Insiders") }, "GitHub Copilot", "Co", PDFAccentHue::Violet, false,
                   "https://github.com/microsoft/vscode (MCP request handler: clientInfo.name = productService.nameLong)"),
            create({ QStringLiteral("cursor-vscode"), QStringLiteral("cursor") }, "Cursor", "Cu", PDFAccentHue::Neutral, false,
                   "unverified (no local copy of the Cursor client source)"),
            create({ QStringLiteral("mcp-inspector"), QStringLiteral("inspector-client") }, "MCP Inspector", "", PDFAccentHue::Teal, true,
                   "unverified (https://modelcontextprotocol.io/docs/tools/inspector names the binary mcp-inspector, not its clientInfo)")
        };
    }
};

const std::vector<PDFAppMcpKnownClient>& PDFAppMcpClientNames::getKnownClients()
{
    static const std::vector<PDFAppMcpKnownClient> clients = PDFAppMcpClientNamesHelper::createTable();
    return clients;
}

QString PDFAppMcpClientNames::sanitizeName(const QString& name)
{
    const QString sanitized = PDFRepositoryHelper::removeInvisibleCharacters(name, true, MAXIMUM_NAME_LENGTH).simplified();
    return sanitized.isEmpty() ? PDFMcpAssistantRegistry::getDefaultClientName() : sanitized;
}

QString PDFAppMcpClientNames::createMonogram(const QString& name)
{
    QString monogram;
    for (const QChar c : name)
    {
        if (c.isLetterOrNumber() && !c.isSurrogate())
        {
            monogram += monogram.isEmpty() ? c.toUpper() : c.toLower();
            if (monogram.size() == 2)
            {
                break;
            }
        }
    }
    return monogram;
}

PDFAppMcpClientInfo PDFAppMcpClientNames::describe(const QString& clientName, const QString& clientTitle)
{
    PDFAppMcpClientInfo info;
    for (const PDFAppMcpKnownClient& client : getKnownClients())
    {
        for (const QString& name : client.clientNames)
        {
            if (name.compare(clientName, Qt::CaseInsensitive) == 0)
            {
                info.displayName = client.displayName;
                info.monogram = client.monogram;
                info.hue = client.hue;
                info.isCommandLine = client.isCommandLine;
                info.isKnown = true;
                return info;
            }
        }
    }

    // A self-reported name: the title, else the name, with the teal mcp icon
    const QString title = PDFRepositoryHelper::removeInvisibleCharacters(clientTitle, true, MAXIMUM_NAME_LENGTH).simplified();
    info.displayName = title.isEmpty() ? sanitizeName(clientName) : title;
    info.hue = PDFAccentHue::Teal;
    return info;
}

QString PDFAppMcpClientNames::getDisplayName(const QString& clientName, const QString& clientTitle)
{
    return describe(clientName, clientTitle).displayName;
}

}   // namespace pdf
