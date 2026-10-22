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

#ifndef PDFAPPDOCUMENTVIEWCOMMANDS_H
#define PDFAPPDOCUMENTVIEWCOMMANDS_H

#include "pdfappdocumentcommands.h"

namespace pdf
{

/// Commands of package B (view): the selection and its tools, the zoom, the
/// rotation of the view, the page layouts, the first page on the right side, the
/// color modes and the annotations (settings of the application), the sidebar,
/// the full screen, the magnifier and the navigation Go to. The handler follows
/// the view: its changes are reported by stateChanged.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentViewCommandHandler : public PDFAppDocumentCommandHandler
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentCommandHandler;

public:
    explicit PDFAppDocumentViewCommandHandler(PDFAppDocumentTab* tab);
    virtual ~PDFAppDocumentViewCommandHandler() override;

    virtual void execute(const QString& commandId) override;
    virtual bool isEnabled(const QString& commandId) const override;
    virtual bool isChecked(const QString& commandId) const override;

private:
    /// Operation of the proxy of the command, -1 if the command has none
    static int getProxyOperation(const QString& commandId);

    /// Page layout (PDFAppViewLayout) of the command, -1 if the command is not a page layout
    static int getViewLayout(const QString& commandId);

    /// Predefined tool of the canvas (PDFToolManager::PredefinedTools) of the command, -1 otherwise
    static int getCanvasTool(const QString& commandId);
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTVIEWCOMMANDS_H
