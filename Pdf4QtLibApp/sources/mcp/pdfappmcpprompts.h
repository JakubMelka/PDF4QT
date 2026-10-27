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


#ifndef PDFAPPMCPPROMPTS_H
#define PDFAPPMCPPROMPTS_H

#include "pdfappglobal.h"

#include "pdfmcpcall.h"
#include "pdfmcpcatalog.h"

#include <memory>
#include <vector>

namespace pdf
{

class PDFAppMcpHost;

/// Prompts of the server (plan 4.3): the definitions and the workflow files
/// are OWNED BY PACKAGE D, prompts/get at run time BY PACKAGE B. WAVE 0 STUB:
/// no prompts.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpPrompts
{
public:
    PDFAppMcpPrompts() = delete;

    static std::vector<PDFMcpPromptDefinition> createDefinitions(const std::vector<PDFMcpIcon>& icons);

    /// prompts/get (an unknown prompt is -32602)
    static void getPrompt(PDFAppMcpHost* host, const std::shared_ptr<PDFMcpCall>& call);
};

}   // namespace pdf

#endif // PDFAPPMCPPROMPTS_H
