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


#ifndef PDFAPPMCPTOOL_APPLICATION_H
#define PDFAPPMCPTOOL_APPLICATION_H

#include "pdfappglobal.h"
#include "pdfappmcptoolhandler.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace pdf
{

/// Handler of the tool "application" (application.get_state, plan of step 7,
/// 3.1; package B): the state of PDF4QT, the permissions and the open
/// documents. The list of the documents needs Read documents: it is left out
/// with Deny and asks the read approval with Ask (once per assistant, while a
/// document is open). Not logged in the activity.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpApplicationTool : public PDFAppMcpToolHandler
{
public:
    virtual QString getToolName() const override { return QStringLiteral("application"); }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;

    /// The state of get_state (the object of the action); \p includeDocuments
    /// lists the open documents, else \p note says why they are missing
    static QByteArray createState(PDFAppMcpHost* host, const PDFMcpClientContext& client, bool includeDocuments, const QString& note);

    /// The state for the call (a tool result or, with \p isResource, the resource
    /// pdf4qt://application/state) with the read approval of the documents
    static void readState(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, bool isResource);

    /// Finishes the call with the state
    static void finishState(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, bool isResource, bool includeDocuments, const QString& note);
};

}   // namespace pdf

#endif // PDFAPPMCPTOOL_APPLICATION_H
