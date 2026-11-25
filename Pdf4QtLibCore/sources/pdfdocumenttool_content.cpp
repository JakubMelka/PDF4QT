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

// Edit Content, stage E0. This file belongs to package P05 (document writer, tool, JSON). The
// bodies below are the skeleton PE0SKEL: the tool has its id, no parameters yet and the values of
// its row of the catalog (the functions call the base class), and its run always fails with "not
// implemented" - a tool, which always fails, never a crash and never a success. The package
// replaces every body (the parameters and the rules of zadani_jadro.md 4.6).

#include "pdfdocumenttool_content.h"

#include <QtGlobal>

namespace pdf
{

QString PDFDocumentToolContent::getId() const
{
    return QString::fromLatin1(TOOL_ID);
}

PDFDocumentToolParameterDefinitions PDFDocumentToolContent::getParameters() const
{
    return PDFDocumentToolParameterDefinitions();
}

PDFDocumentToolResultKind PDFDocumentToolContent::getResultKind(const QVariantMap& parameters) const
{
    return PDFDocumentTool::getResultKind(parameters);
}

std::vector<PDFDocumentToolResultKind> PDFDocumentToolContent::getPossibleResultKinds() const
{
    return PDFDocumentTool::getPossibleResultKinds();
}

bool PDFDocumentToolContent::isPageChange(const QVariantMap& parameters) const
{
    return PDFDocumentTool::isPageChange(parameters);
}

std::vector<PDFSecurityHandler::Permission> PDFDocumentToolContent::getRequiredPermissions(const QVariantMap& parameters) const
{
    return PDFDocumentTool::getRequiredPermissions(parameters);
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolContent::checkGivenParameters(const QVariantMap& given) const
{
    return PDFDocumentTool::checkGivenParameters(given);
}

std::vector<PDFDocumentToolParameterError> PDFDocumentToolContent::validate(const PDFDocumentToolContext& context, const QVariantMap& parameters) const
{
    return PDFDocumentTool::validate(context, parameters);
}

PDFDocumentToolResult PDFDocumentToolContent::execute(const PDFDocumentToolContext& context, const QVariantMap& parameters, const PDFOperationControl* control, PDFProgressSink* progress) const
{
    Q_UNUSED(context);
    Q_UNUSED(parameters);
    Q_UNUSED(control);
    Q_UNUSED(progress);
    return PDFDocumentToolResult::createFailed(tr("Edit Content is not implemented yet."));
}

}   // namespace pdf
