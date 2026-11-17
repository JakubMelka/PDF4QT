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

#ifndef PDFDOCUMENTTOOL_OBJECTINSPECTOR_H
#define PDFDOCUMENTTOOL_OBJECTINSPECTOR_H

#include "pdfdocumenttool.h"

namespace pdf
{

/// The tool Object Inspector for MCP and PdfTool (review.object-inspector; plan of step 7 of phase 2, 4.3,
/// 7.1): an object of the document as JSON - its type, its entries or items, its references, the lengths and
/// a bounded content of a stream. The GUI does not run it: it shows the tree of PDFObjectInspector itself
/// (PDFAppToolGUIObjectInspector), the tool is the same core for the assistant.
///
/// Parameters: "object" ("12 0 R", "trailer", "catalog"; default "trailer"), "view" (the ids of
/// PDFObjectInspector::getViewId; the list of the roots of the view when "object" is empty), "depth" (1 to
/// MAXIMUM_DEPTH levels of the children), "decodeStream" (Bool), "maxBytes" (the shown bytes of a stream, at
/// most MAXIMUM_BYTES), "offset" / "limit" (the window of a list, at most MAXIMUM_LIMIT items).
///
/// Data: "object" (type, entries, items, references, "streamLength", "decodedLength", "content",
/// "isBinary", "truncated"), "roots" (a list of a view), "total", "truncated".
///
/// Permissions (owner answer Q7.2, R7-22 (6)): viewing the objects is reading the open document (no bit); the
/// content of a stream (decodeStream) is an extraction - bit 5 (CopyContent) of Table 22, the owner password
/// offered (getRequiredPermissions). The GUI asks the same bit for Copy of the content of a stream.
///
/// Package P1a implements it; the skeleton has the parameters, the permissions and a stub run (Failed).
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentToolObjectInspector : public PDFDocumentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentToolObjectInspector)

public:
    static constexpr const char* TOOL_ID = "review.object-inspector";

    /// Names of the parameters
    static constexpr const char* PARAMETER_OBJECT = "object";
    static constexpr const char* PARAMETER_VIEW = "view";
    static constexpr const char* PARAMETER_DEPTH = "depth";
    static constexpr const char* PARAMETER_DECODE_STREAM = "decodeStream";
    static constexpr const char* PARAMETER_MAX_BYTES = "maxBytes";
    static constexpr const char* PARAMETER_OFFSET = "offset";
    static constexpr const char* PARAMETER_LIMIT = "limit";

    /// Limits of the parameters (plan 7.1)
    static constexpr int MAXIMUM_DEPTH = 4;
    static constexpr int DEFAULT_MAX_BYTES = 64 * 1024;
    static constexpr int MAXIMUM_BYTES = 1024 * 1024;
    static constexpr int DEFAULT_LIMIT = 100;
    static constexpr int MAXIMUM_LIMIT = 1000;

    /// Keys of the data of the result
    static constexpr const char* DATA_OBJECT = "object";
    static constexpr const char* DATA_ROOTS = "roots";
    static constexpr const char* DATA_TOTAL = "total";
    static constexpr const char* DATA_TRUNCATED = "truncated";

    virtual QString getId() const override;
    virtual PDFDocumentToolParameterDefinitions getParameters() const override;
    virtual std::vector<PDFSecurityHandler::Permission> getRequiredPermissions(const QVariantMap& parameters) const override;
    virtual PDFDocumentToolResult execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const override;
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOL_OBJECTINSPECTOR_H
