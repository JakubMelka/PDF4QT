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


#include "pdfappmcptoolhandler.h"
#include "pdfappmcphost.h"

namespace pdf
{

PDFAppMcpToolHandler::~PDFAppMcpToolHandler() = default;

PDFAppMcpStubToolHandler::PDFAppMcpStubToolHandler(QString toolName) :
    m_toolName(std::move(toolName))
{

}

void PDFAppMcpStubToolHandler::handle(const PDFAppMcpToolInvocation& invocation)
{
    invocation.host->finishWithError(invocation.call, invocation.getActionId(), nullptr, getNotImplementedText(invocation.getActionId()));
}

QString PDFAppMcpStubToolHandler::getNotImplementedText(const QString& actionId)
{
    return QStringLiteral("The action %1 is not implemented in wave 0 of PDF4QT.").arg(actionId);
}

}   // namespace pdf
