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

#ifndef PDFAPPMCPTOOLHANDLER_H
#define PDFAPPMCPTOOLHANDLER_H

#include "pdfappglobal.h"

#include "pdfmcpcall.h"

#include <QJsonObject>
#include <QPointer>
#include <QString>

#include <memory>

namespace pdf
{

class PDFAppMcpHost;
class PDFAppMcpToolSpec;
class PDFAppMcpActionSpec;

/// One validated tools/call for a handler (the host has checked the name, the
/// schema of the arguments and that the action is offered)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpToolInvocation
{
public:
    std::shared_ptr<PDFMcpCall> call;
    QPointer<PDFAppMcpHost> host;           ///< Null, when the host is deleted by the application while a handler runs its code
    QString toolName;                       ///< "annotations"
    QString actionName;                     ///< "add"
    QJsonObject arguments;                  ///< The whole arguments (common properties)
    QJsonObject actionArguments;            ///< The object of the action ({} when it is missing)
    QString documentId;                     ///< The common documentId (empty = omitted)
    const PDFAppMcpToolSpec* tool = nullptr;        ///< nullptr for a tool of the catalog
    const PDFAppMcpActionSpec* action = nullptr;    ///< nullptr for a tool of the catalog

    /// "annotations.add"
    QString getActionId() const { return toolName + QLatin1Char('.') + actionName; }
};

/// Handler of one tool (plan 2.3): one per tool (application … files), owned by
/// the host. A handler finishes the call itself - at once (a fast read in the
/// GUI thread), from a job, or after a confirmation - and records the activity
/// by the host.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpToolHandler
{
public:
    virtual ~PDFAppMcpToolHandler();

    /// Name of the tool ("text")
    virtual QString getToolName() const = 0;

    /// Handles the call (GUI thread)
    virtual void handle(const PDFAppMcpToolInvocation& invocation) = 0;
};

/// Handler of a tool, which is not implemented yet (wave 0): every call is
/// an execution error "not implemented in wave 0"
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpStubToolHandler : public PDFAppMcpToolHandler
{
public:
    explicit PDFAppMcpStubToolHandler(QString toolName);

    virtual QString getToolName() const override { return m_toolName; }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;

    /// The text of the stub
    static QString getNotImplementedText(const QString& actionId);

private:
    QString m_toolName;
};

}   // namespace pdf

#endif // PDFAPPMCPTOOLHANDLER_H
