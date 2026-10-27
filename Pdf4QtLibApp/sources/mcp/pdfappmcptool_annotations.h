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

#ifndef PDFAPPMCPTOOL_ANNOTATIONS_H
#define PDFAPPMCPTOOL_ANNOTATIONS_H

#include "pdfappglobal.h"
#include "pdfappmcptoolhandler.h"

#include <QString>

namespace pdf
{

/// Handler of the tool "annotations" (plan of step 7, 3.6; package C): add,
/// update, delete and reply - each call one undo step and at most one
/// confirmation of the user (Edit documents = Ask). The arguments are checked in
/// the GUI thread (a model gets "items[2]: highlight needs quads (from
/// text.search)"), a worker builds the changed document from a snapshot
/// (PDFAppAnnotationOperations), the author is the display name of the assistant.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpAnnotationsTool : public PDFAppMcpToolHandler
{
public:
    virtual QString getToolName() const override { return QStringLiteral("annotations"); }
    virtual void handle(const PDFAppMcpToolInvocation& invocation) override;

    /// "Claude Code added 3 highlights and 1 note" of the counts of the types of
    /// annotations.add (the undo step, the notification bar)
    static QString createAddDescription(const QString& clientName, const QStringList& types);

    /// "3 highlights, 1 note"
    static QString createAddSummary(const QStringList& types);

    /// "3 highlights" of a type of annotations.add
    static QString getTypeCountText(const QString& type, int count);
};

}   // namespace pdf

#endif // PDFAPPMCPTOOL_ANNOTATIONS_H
