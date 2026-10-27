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

#ifndef PDFAPPMCPCLIENTSETUP_H
#define PDFAPPMCPCLIENTSETUP_H

#include "pdfappglobal.h"

#include <QCoreApplication>
#include <QString>

#include <vector>

namespace pdf
{

/// A client of the card "Connect an assistant" of the AI page (plan 5.2). The
/// tile Claude has the two variants Claude Code and Claude Desktop (filter
/// chips of 653:5735), every other tile has one setup.
enum class PDFAppMcpSetupClient
{
    ClaudeCode,
    ClaudeDesktop,
    Codex,
    Gemini,
    Copilot,
    Cursor,
    Other
};

/// Kind of the content of a code box of a setup step
enum class PDFAppMcpSetupCodeKind
{
    None,           ///< The step has no code box
    Command,        ///< A command of a terminal (monospaced font)
    Configuration,  ///< A file content or a value (monospaced font, can have more lines)
    Prompt          ///< An example question for the assistant (Figma 653:5778: the font of the text, not monospaced)
};

/// One numbered step of a setup card
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpSetupStep
{
public:
    QString text;                                               ///< Instruction (translated)
    QString code;                                               ///< Content of the code box (empty for None)
    PDFAppMcpSetupCodeKind codeKind = PDFAppMcpSetupCodeKind::None;
};

/// The texts of the setup card of a client (Figma 653:5730 / 955:149400; the
/// cards of Claude Desktop, Codex, Gemini, Copilot and Cursor are missing in
/// Figma and are built from the same pattern)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpSetup
{
public:
    PDFAppMcpSetupClient client = PDFAppMcpSetupClient::ClaudeCode;
    QString title;                          ///< "Set up Claude"
    QString subtitle;                       ///< "Adds this server to Claude Code. It takes about a minute."
    std::vector<PDFAppMcpSetupStep> steps;  ///< Numbered from 1 in this order
    QString copyButtonText;                 ///< "Copy Command" / "Copy Configuration"
    QString copyText;                       ///< What the copy button copies (a command or a configuration)
    QString documentationText;              ///< "Claude Code Documentation" / "Setup Guide"
    QString documentationLink;              ///< An https address, or a route of the application ("/help/...")
    QString note;                           ///< The note after the buttons
    bool containsToken = false;             ///< The copied text contains the access token
};

/// Setup texts of the clients of the AI page (plan 5.2, package D writes the
/// texts, package E shows them). Pure functions: the address and the token are
/// parameters, nothing is read from the settings. The commands and the
/// configurations are checked by UnitTestsAppMcpContent::setupTexts (the
/// address, the token, no header without the token, JSON parsed by Qt, TOML
/// well-formed, quotes escaped).
///
/// A token or an address with characters, which a shell or a configuration
/// file could interpret (anything else than letters, digits and "-._~+/=" in a
/// token; an address, which is not "http://<host>:<port>/mcp"), is never put
/// into a text: the placeholder "<token>" / "<address>" stands there instead.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpClientSetup
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppMcpClientSetup)

public:
    PDFAppMcpClientSetup() = delete;

    /// Name of the server in the configurations of the clients
    static QString getServerName() { return QStringLiteral("pdf4qt"); }

    /// The tiles of "Connect an assistant" in their order (the default variant of
    /// each tile): Claude Code, Codex, Gemini, Copilot, Cursor, Other client
    static std::vector<PDFAppMcpSetupClient> getTileClients();

    /// The variants of the tile of the client (Claude Code and Claude Desktop for
    /// Claude, else the client alone)
    static std::vector<PDFAppMcpSetupClient> getVariants(PDFAppMcpSetupClient client);

    /// Name of the tile: "Claude", "Codex", "Gemini", "Copilot", "Cursor", "Other client"
    static QString getTileName(PDFAppMcpSetupClient client);

    /// Vendor of the tile: "Anthropic", "OpenAI", "Google", "GitHub", "Anysphere", "Any MCP client"
    static QString getVendor(PDFAppMcpSetupClient client);

    /// Name of the variant (the filter chip): "Claude Code", "Claude Desktop", …
    static QString getVariantName(PDFAppMcpSetupClient client);

    /// clientInfo.name of the client for PDFAppMcpClientNames::describe (the
    /// monogram and the hue of the tile); empty for Other client
    static QString getClientInfoName(PDFAppMcpSetupClient client);

    /// The setup of the client. \p address is the shown address of the server
    /// ("http://127.0.0.1:8765/mcp"); \p token is the access token, empty when
    /// Require access token is off (no header part then).
    static PDFAppMcpSetup create(PDFAppMcpSetupClient client, const QString& address, const QString& token);

    /// The setup as create, but the token shortened for the display by
    /// getDisplayToken (the code boxes of Figma show "q7LmT4…x2Vd"); the copy
    /// buttons copy the texts of create
    static PDFAppMcpSetup createForDisplay(PDFAppMcpSetupClient client, const QString& address, const QString& token);

    /// A token shortened for the display ("q7LmT4…x2Vd" of Figma: 6 characters,
    /// an ellipsis, 4 characters); a short token is returned unchanged
    static QString getDisplayToken(const QString& token);

    /// The token can be put into a command or a configuration as it is
    static bool isSafeToken(const QString& token);

    /// The address is "http://<host>:<port>/mcp" of an IPv4 / IPv6 address or a
    /// host name (no characters, which a shell or a file would interpret)
    static bool isSafeAddress(const QString& address);

    /// Text of a JSON string (with the quotes)
    static QString quoteJson(const QString& text);

    /// Text of a TOML basic string (with the quotes)
    static QString quoteToml(const QString& text);

private:
    /// The setup of a checked address \p url and a checked token \p key (empty = none)
    static PDFAppMcpSetup createSetup(PDFAppMcpSetupClient client, const QString& url, const QString& key);
};

}   // namespace pdf

#endif // PDFAPPMCPCLIENTSETUP_H
