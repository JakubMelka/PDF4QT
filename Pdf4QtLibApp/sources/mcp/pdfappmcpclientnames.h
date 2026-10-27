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

#ifndef PDFAPPMCPCLIENTNAMES_H
#define PDFAPPMCPCLIENTNAMES_H

#include "pdfappglobal.h"

#include "pdfaccenthue.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

/// How an assistant is shown (plan 2.7): the display name, the monogram tile
/// and its hue as in Figma (Claude Code "Cl" amber, Codex CLI "Cx" green,
/// Gemini CLI "Ge" blue, GitHub Copilot "Co" violet, Cursor "Cu" neutral), the
/// CLI badge, or the teal mcp icon of an unknown client
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpClientInfo
{
public:
    QString displayName;            ///< "Claude Code" (author of the annotations, texts of the dialogs)
    QString monogram;               ///< "Cl"; empty = the mcp icon
    PDFAccentHue hue = PDFAccentHue::Teal;
    bool isCommandLine = false;     ///< The CLI badge of the avatar
    bool isKnown = false;           ///< A client of the table (not a self-reported name)
};

/// A known client: the clientInfo.name values it sends and where they were verified
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpKnownClient
{
public:
    QStringList clientNames;        ///< clientInfo.name values (compared without case)
    QString displayName;
    QString monogram;
    PDFAccentHue hue = PDFAccentHue::Teal;
    bool isCommandLine = false;
    QString source;                 ///< Where the name was verified (the copies of SCR/mcp/c), or "unverified"
};

/// Display names of the assistants (clientInfo is self-reported: display only,
/// never a security decision). A known name gives its row of the table, else
/// clientInfo.title, else the sanitized name (at most 64 characters, no
/// bidirectional or control characters) with the teal mcp icon.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpClientNames
{
public:
    PDFAppMcpClientNames() = delete;

    /// Longest display name
    static constexpr int MAXIMUM_NAME_LENGTH = 64;

    static const std::vector<PDFAppMcpKnownClient>& getKnownClients();

    /// Presentation of a client of clientInfo.name / clientInfo.title
    static PDFAppMcpClientInfo describe(const QString& clientName, const QString& clientTitle = QString());

    /// Display name only
    static QString getDisplayName(const QString& clientName, const QString& clientTitle = QString());

    /// Name for the user interface from an untrusted text (sanitized, bounded;
    /// "MCP client" for an empty one)
    static QString sanitizeName(const QString& name);

    /// Monogram of an unknown name (the first two letters), empty for none
    static QString createMonogram(const QString& name);
};

}   // namespace pdf

#endif // PDFAPPMCPCLIENTNAMES_H
