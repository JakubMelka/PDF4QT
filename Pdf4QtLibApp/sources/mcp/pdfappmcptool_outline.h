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

#ifndef PDFAPPMCPTOOL_OUTLINE_H
#define PDFAPPMCPTOOL_OUTLINE_H

#include "pdfappglobal.h"
#include "pdfappmcptoolhandler.h"
#include "pdfappannotationoperations.h"

#include <QString>

#include <optional>

namespace pdf
{

/// Handler of the tool "outline" (plan of step 7, 3.8, owner Q1/O27; package
/// C): add, update, move, delete and replace (a flat list with levels) by the
/// outline operations of step 4 - one undo step with the flag Outline. An
/// outline, which cannot be edited (damaged, shared items, too large), is
/// refused. Items are 1-based dotted paths ("2.1.4"); an item given by its title
/// is chosen by the client when several items have the title (elicitation).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpOutlineTool : public PDFAppMcpToolHandler
{
public:
    virtual QString getToolName() const override { return QStringLiteral("outline"); }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;

    /// Path of an item id ("2.1.4" -> 1, 0, 3); std::nullopt for an invalid id
    static std::optional<PDFAppAnnotationOperations::OutlinePath> parseItemId(const QString& id);

    /// Item id of a path
    static QString formatItemId(const PDFAppAnnotationOperations::OutlinePath& path);
};

}   // namespace pdf

#endif // PDFAPPMCPTOOL_OUTLINE_H
