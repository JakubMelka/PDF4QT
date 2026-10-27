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

#ifndef PDFAPPMCPCONTENT_H
#define PDFAPPMCPCONTENT_H

#include "pdfappglobal.h"

#include "pdfmcpcatalog.h"
#include "pdfmcpschema.h"

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

class PDFAppMcpToolSpec;
class PDFAppMcpActionSpec;

/// A tool call written in a document of the server: a fenced block ```json of
/// the parameters of tools/call, {"name": <tool>, "arguments": {...}}
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpDocumentedCall
{
public:
    QString toolName;
    QJsonObject arguments;
    int line = 0;               ///< Line of the block (from 1)
    QString error;              ///< Not empty, when the block is not a valid call
};

/// Content of the MCP server for assistants (plan of step 7, chapter 4, package
/// D): the static resources (docs, the reference of every tool, skills,
/// workflows and the user manual), the static templates, the prompts and their
/// workflows, and the generated reference tables of the tools.
///
/// The files are English Markdown in the resources of the application
/// (resources/mcp.qrc, :/mcp/...); the reference tables of docs/tools/<tool>.md
/// are generated from the contract (PDFAppMcpToolSet) by createToolSummary /
/// createActionReference and UnitTestsAppMcpContent checks, that the files
/// contain them exactly. The static resources are read once per process (the
/// resources never change) and shared by every catalog.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpContent
{
public:
    PDFAppMcpContent() = delete;

    /// Folder of the files in the resources
    static QString getResourceDirectory() { return QStringLiteral(":/mcp"); }

    /// URI scheme of the server
    static QString getScheme() { return QStringLiteral("pdf4qt://"); }

    /// Names of the general documents (pdf4qt://docs/<name>.md) in their order
    static QStringList getDocumentNames();

    /// Names of the skills (pdf4qt://skills/<name>/SKILL.md)
    static QStringList getSkillNames();

    /// Names of the workflows (pdf4qt://workflows/<name>.md) = the names of the prompts
    static QStringList getWorkflowNames();

    /// The static resources with their contents (docs, the tool references,
    /// skills, workflows, the English topics of the user manual), read once;
    /// call it first in the thread of the user interface (it loads the help catalog)
    static const std::vector<PDFMcpResourceDefinition>& getStaticResources();

    /// A static resource of the URI (nullptr = none)
    static const PDFMcpResourceDefinition* findStaticResource(QStringView uri);

    /// Templates of the static files: pdf4qt://docs/tools/{tool}.md,
    /// pdf4qt://help/{topic}.md, pdf4qt://skills/{skill}/SKILL.md,
    /// pdf4qt://workflows/{name}.md
    static std::vector<PDFMcpResourceTemplateDefinition> createStaticTemplates();

    // Prompts (plan 4.3) ------------------------------------------------------

    /// Definitions of prompts/list: the workflows with their arguments
    static std::vector<PDFMcpPromptDefinition> createPromptDefinitions(const std::vector<PDFMcpIcon>& icons);

    /// URI of the workflow of a prompt (empty for an unknown prompt)
    static QString getWorkflowUri(const QString& promptName);

    /// Markdown of the workflow of a prompt (empty for an unknown prompt)
    static QString getWorkflowText(const QString& promptName);

    /// Text of the user message of prompts/get: the instruction to follow the
    /// workflow (embedded as a resource next to it) with the given arguments.
    /// \p arguments are the arguments of the request (untrusted texts: bounded,
    /// control and bidirectional characters removed, unknown names ignored);
    /// "documentId" may be added by the caller, when it has resolved "document".
    static QString createPromptText(const QString& promptName, const QMap<QString, QString>& arguments);

    /// Fixed values of a prompt argument for completion/complete (criteria,
    /// colors, depth); empty for a free argument
    static QStringList getPromptArgumentValues(const QString& promptName, const QString& argumentName);

    /// Longest text of a prompt argument in the prompt text
    static constexpr int MAXIMUM_ARGUMENT_LENGTH = 16384;

    // Reference of the tools (docs/tools/<tool>.md) ---------------------------

    /// Generated summary of a tool: its actions with their permissions and the
    /// table of the common parameters
    static QString createToolSummary(const PDFAppMcpToolSpec& tool);

    /// Generated reference of an action: heading, description, permission, the
    /// tables of the parameters and of the result
    static QString createActionReference(const PDFAppMcpToolSpec& tool, const PDFAppMcpActionSpec& action);

    /// Table of the properties of an input object (Parameter | Type | Required | Description)
    static QString createParameterTable(const PDFMcpSchema& schema);

    /// Table of the properties of an output object (Field | Type | Description)
    static QString createResultTable(const PDFMcpSchema& schema);

    /// Text of the type of a schema ("integer 1–500, default 100")
    static QString getTypeText(const PDFMcpSchema& schema, bool isPlural = false);

    /// Name of a permission for the documents ("Read documents")
    static QString getPermissionText(int permissionKind);

    // Checks of the documents ---------------------------------------------------

    /// The tool calls of the fenced ```json blocks of a document, which have
    /// "name" and "arguments" (other JSON blocks are not calls)
    static std::vector<PDFAppMcpDocumentedCall> extractCalls(const QString& markdown);

    /// The pdf4qt:// URIs of a document
    static QStringList extractUris(const QString& markdown);
};

}   // namespace pdf

#endif // PDFAPPMCPCONTENT_H
