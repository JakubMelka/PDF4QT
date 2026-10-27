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


#ifndef PDFAPPMCPPROMPTRUNTIME_H
#define PDFAPPMCPPROMPTRUNTIME_H

#include "pdfappglobal.h"

#include "pdfmcpcall.h"

#include <QMap>
#include <QString>

#include <memory>

namespace pdf
{

class PDFAppDocument;
class PDFAppMcpHost;

/// prompts/get at run time (plan of step 7, 4.3; package B): the arguments of
/// the request (strings, the required ones present), the document of the
/// argument "document" (an id or a title; omitted = the active document tab or
/// the only open document; ambiguous = elicitation of plan 2.8, the fallback
/// text without the capability), then one user message with the instructions
/// of the workflow (PDFAppMcpContent::createPromptText of package D) and a user
/// message with the workflow file as an embedded resource. The prompt never
/// reads the document itself: naming a document needs Read documents (the
/// approval with Ask), with Deny the document is left to the model.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpPromptRuntime
{
public:
    PDFAppMcpPromptRuntime() = delete;

    /// Longest argument of a prompt (longer ones are refused)
    static constexpr int MAXIMUM_ARGUMENT_LENGTH = 16384;

    static void getPrompt(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call);

    /// The prompt with the resolved document (after the read approval)
    static void finishPrompt(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call, bool isDocumentAllowed);

    /// The result of prompts/get
    static PDFMcpCallResult createResult(const QString& description, const QString& text, const QString& workflowUri, const QString& workflowText);
};

}   // namespace pdf

#endif // PDFAPPMCPPROMPTRUNTIME_H
