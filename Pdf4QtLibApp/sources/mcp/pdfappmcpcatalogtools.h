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
    Overwrite,      ///< "overwrite": the changed document replaces its file (phase 2 result writer)
    OpenNewTab,     ///< "open_new_tab": a new document opens in a new tab (phase 2)
    SaveFile        ///< "save_file": a new document is written to "path" (phase 2)
};

/// The bridge of the tools of phase 2 (plan 3.10): one MCP tool per category
/// of PDFDocumentToolCatalog (pages, convert, edit, protect, optimize, review)
/// with the implemented tools of PDFDocumentToolRegistry as its actions
/// ("review.statistics" -> tool "review", action "statistics"). The object of
/// an action is PDFDocumentToolParameters::toJsonSchema (a Secret is
/// writeOnly); the common properties are documentId / documentIds, result and
/// path. The permission is the one of the result kind (Analysis R,
/// ChangeDocument E, files F) raised to what the result really contains.
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

    /// Values of "result" of a result kind (empty = no result property, Files)
    static QStringList getResultValues(PDFDocumentToolResultKind kind);

    /// Preselected value of "result"
    static QString getDefaultResult(PDFDocumentToolResultKind kind);

    /// Mode of a value of "result"
    static std::optional<PDFAppMcpToolResultMode> getResultMode(const QString& value);

    /// Permission of a result kind (before the result is known)
    static PDFAppMcpPermissionKind getBasePermission(PDFDocumentToolResultKind kind);

    /// Input schema of a category with its implemented tools; std::nullopt when
    /// no tool is implemented or no action is available
    static std::optional<PDFMcpSchema> createInputSchema(PDFDocumentToolCategory category, const PDFAppMcpPermissions& permissions);

    /// Output schema: structuredContent { action, <action>: PDFDocumentToolResult::toJson }
    static PDFMcpSchema createOutputSchema(PDFDocumentToolCategory category, const PDFAppMcpPermissions& permissions);

    /// Definitions of the categories with available tools
    static std::vector<PDFMcpToolDefinition> createDefinitions(const PDFAppMcpPermissions& permissions, const std::vector<PDFMcpIcon>& icons = std::vector<PDFMcpIcon>());

    /// The handler of every category tool (owned by the host)
    static std::unique_ptr<PDFAppMcpToolHandler> createHandler();
};

}   // namespace pdf

#endif // PDFAPPMCPCATALOGTOOLS_H
