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


#include "pdfappmcpprompts.h"
#include "pdfappmcpcontent.h"
#include "pdfappmcphost.h"
#include "pdfappmcppromptruntime.h"

#include "pdfmcperrorcodes.h"
#include "pdfmcpjson.h"

namespace pdf
{

std::vector<PDFMcpPromptDefinition> PDFAppMcpPrompts::createDefinitions(const std::vector<PDFMcpIcon>& icons)
{
    // Package D: the prompts = the workflows (PDFAppMcpContent has their texts,
    // getWorkflowUri / getWorkflowText / createPromptText for prompts/get)
    return PDFAppMcpContent::createPromptDefinitions(icons);
}

void PDFAppMcpPrompts::getPrompt(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call)
{
    // Package B: the arguments, the document (elicitation) and the messages
    PDFAppMcpPromptRuntime::getPrompt(host, call);
}

}   // namespace pdf
