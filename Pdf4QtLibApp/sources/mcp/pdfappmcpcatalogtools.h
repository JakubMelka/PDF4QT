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

#ifndef PDFAPPMCPCATALOGTOOLS_H
#define PDFAPPMCPCATALOGTOOLS_H

#include "pdfappglobal.h"
#include "pdfappmcppermissions.h"
#include "pdfappmcptoolhandler.h"
#include "pdfappmcptoolset.h"

#include "pdfdocumenttoolcatalog.h"
#include "pdfmcpcatalog.h"
#include "pdfmcpschema.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace pdf
{

/// What happens with the result of a tool of the catalog (the property "result")
enum class PDFAppMcpToolResultMode
{
    Keep,           ///< "keep": the changed document is committed to its tab (one undo step)
    SaveCopy,       ///< "save_copy": the changed document is written to "path"; the tab is unchanged
    Overwrite,      ///< "overwrite": the file of the document gets the result first, then the tab (one undo step, saved); always confirmed
    OpenNewTab,     ///< "open_new_tab": the new document of the tool opens unsaved in a new tab (Edit, no file is written)
    SaveFile,       ///< "save_file": the one output file or the new document of the tool is written to "path"
    ReturnText      ///< "return_text": the text of the one output file is the result of the call (Read only)
};

/// The bridge of the tools of phase 2 (plan 3.10): one MCP tool per category
/// of PDFDocumentToolCatalog (pages, convert, edit, protect, optimize, review)
/// with the implemented tools of PDFDocumentToolRegistry as its actions
/// ("review.statistics" -> tool "review", action "statistics"). The object of
/// an action is PDFDocumentToolParameters::toJsonSchema (a Secret is
/// writeOnly); the common properties are documentId / documentIds, result and
/// path. The permission is the one of the value of result (\p getResultPermissions);
/// an analysis is raised to what its result really contains.
///
/// A new tool needs nothing here: its row of the catalog (the result kind, the
/// preselected result, the signature impact, the locked access, the required
/// permissions) and the definitions of its parameters decide. An output file of the
/// result (PDFDocumentToolOutputFile): one file per run; its type says the extensions of
/// save_file (F9) and its encoding ("text/plain; charset=windows-1250", UTF-8 without
/// a charset) for return_text: decoded by PDFDocumentTextExporter::createDecoder. Verified on
/// Windows (code page 1250, Qt 6.11.2 without ICU) by UnitTestsDocumentToolExtractText
/// systemEncodingDecoder and UnitTestsAppToolExtractText mcpReturnTextEncodings: Qt knows
/// "windows-1250" by its name, createDecoder takes the code page of the system as the
/// encoding System also for a Qt, which does not.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpCatalogTools
{
public:
    PDFAppMcpCatalogTools() = delete;

    /// The name is a tool name of a category (pages, convert, edit, protect, optimize, review)
    static bool isCatalogToolName(QStringView name);

    /// Tool name of a category ("review")
    static QString getToolName(PDFDocumentToolCategory category);

    /// Id of the tool of the catalog of an MCP tool and action ("review.statistics")
    static QString getToolId(const QString& toolName, const QString& actionName);

    /// Implemented tools of the category in the order of the catalog
    static std::vector<const PDFDocumentToolCatalogEntry*> getImplementedTools(PDFDocumentToolCategory category);

    /// Values of "result" of a result kind
    static QStringList getResultValues(PDFDocumentToolResultKind kind);

    /// Default value of "result": the preselected choice of the result dialog
    /// (Save as a file of an export is return_text - only Read, chapter 6 of the plan)
    static QString getDefaultResult(const PDFDocumentToolCatalogEntry& entry);

    /// Mode of a value of "result"
    static std::optional<PDFAppMcpToolResultMode> getResultMode(const QString& value);

    /// What a value of "result" needs (chapter 6 of the plan of phase 2 and chapter 7 of the
    /// plan of its step 2): Read of the documents the tool reads (none for a tool without an
    /// input: Blank PDF), then keep R + E, save_copy R + F, overwrite R + E + F, return_text R,
    /// open_new_tab Edit (a new tab is a new document, not a file), save_file Files
    /// \param mode The value of "result"
    /// \param inputKind The input of the tool (PDFDocumentToolInputKind::None reads no document)
    static PDFAppMcpPermissionKinds getResultPermissions(PDFAppMcpToolResultMode mode, PDFDocumentToolInputKind inputKind);

    /// The permission, whose Deny takes the tool out of the offer (before the value of
    /// "result" is known): Read of a tool with a document, then a tool changing the
    /// document or making a new one needs Edit or Files (one of its results is still
    /// possible). A category is offered, when at least one of its tools is.
    static PDFAppMcpPermissionKind getBasePermission(const PDFDocumentToolCatalogEntry& entry, const PDFAppMcpPermissions& permissions);

    /// Input schema of a category with its implemented tools; std::nullopt when
    /// no tool is implemented or no action is available
    static std::optional<PDFMcpSchema> createInputSchema(PDFDocumentToolCategory category, const PDFAppMcpPermissions& permissions);

    /// Output schema: structuredContent { action, <action>: PDFDocumentToolResult::toJson }
    static PDFMcpSchema createOutputSchema(PDFDocumentToolCategory category, const PDFAppMcpPermissions& permissions);

    /// The contract of a category with its implemented tools for the reference of the
    /// documentation (every permission Allow, the shape of PDFAppMcpToolSet::getTools);
    /// std::nullopt when no tool is implemented
    static std::optional<PDFAppMcpToolSpec> createToolSpec(PDFDocumentToolCategory category);

    /// Definitions of the categories with available tools
    static std::vector<PDFMcpToolDefinition> createDefinitions(const PDFAppMcpPermissions& permissions, const std::vector<PDFMcpIcon>& icons = std::vector<PDFMcpIcon>());

    /// The handler of every category tool (owned by the host)
    static std::unique_ptr<PDFAppMcpToolHandler> createHandler();
};

}   // namespace pdf

#endif // PDFAPPMCPCATALOGTOOLS_H
