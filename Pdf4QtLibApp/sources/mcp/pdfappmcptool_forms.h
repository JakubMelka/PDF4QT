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

#ifndef PDFAPPMCPTOOL_FORMS_H
#define PDFAPPMCPTOOL_FORMS_H

#include "pdfappglobal.h"
#include "pdfappmcptoolhandler.h"

#include <QStringList>

namespace pdf
{

/// Handler of the tool "forms" (plan of step 7, 3.7; package C): set_values and
/// reset of the fields of an AcroForm by a headless PDFFormManager of Core on a
/// snapshot in a worker - one undo step (flag FormField), JavaScript is not run
/// (O16), read only fields, a read only form (locked, no permission to fill the
/// form, XFA) and push buttons are refused. A name matches the fully qualified
/// name, else a unique suffix ("name" of "applicant.name"); several matches are
/// chosen by the client (elicitation) or listed in the error.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpFormsTool : public PDFAppMcpToolHandler
{
public:
    virtual QString getToolName() const override { return QStringLiteral("forms"); }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;

    /// Fully qualified names of \p names, which match \p name: the exact name, else
    /// the names ending with ".<name>"
    static QStringList matchFieldNames(const QStringList& names, const QString& name);
};

}   // namespace pdf

#endif // PDFAPPMCPTOOL_FORMS_H
