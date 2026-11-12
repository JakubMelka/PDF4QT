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

#ifndef PDFAPPMCPMANAGEACTION_H
#define PDFAPPMCPMANAGEACTION_H

#include "pdfappglobal.h"
#include "pdfmcprepository.h"

#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace pdf
{

class PDFAppDocument;
class PDFAppMcpHost;
class PDFAppMcpToolInvocation;

/// The action pages.manage of MCP (plan of step 3b of phase 2, chapter 7, Q9 b, QB4 with RB9,
/// RB8; package M4). The bridge of the catalog tools (PDFAppMcpCatalogTools) calls prepare for
/// Manage Pages in place of its resolution of documentIds; everything after it (the result, the
/// permissions, the job, the confirmations, the progress, the cancellation) is the common flow.
/// The skeleton declares it; package M4 defines it.
///
/// Contract of prepare (GUI thread, called by the handler of the catalog tools):
///  - The assembly mode: "assemblyPath" of the action - the file is read with the permission Open
///    and save files (Ask); each PDF source of the assembly must be an open document (the same
///    path), else the error lists the paths ("open them with files.open first"); a source on
///    another computer or a device is refused (RB8); the images are in the file; a file with
///    problems (pages, which would go to the bin) and an assembly without a PDF source are
///    refused. \p parameters get the assembly (PDFDocumentToolManage::PARAMETER_ASSEMBLY; the
///    assemblyPath stays) and the outline, the images and the file name of the file, which the
///    call has not given; \p inputDocuments the open documents of the PDF sources in the order of
///    the sources (images skipped). The permissions of every source are checked by the output of
///    the parameters (a new document needs every permission). The result must be given:
///    open_new_tab, save_file (one-pdf) or save_files (per-group, per-page).
///  - The document mode: documentId with "pages" (QB4 / RB9): an ordered PDFPageRangeList, which
///    names every page of the document at least once (a repetition is a clone; a missing page is
///    an error listing the missing pages - removal is only pages.remove), checked here by
///    PDFDocumentToolManage::createAssemblyOfPages before any question; the tool makes the
///    assembly of "pages" itself over the version it runs on. The output is "document" (keep,
///    save_copy, overwrite; a certified document save_copy only); a locked document is refused
///    for every result. \p inputDocuments stays empty: the common flow resolves documentId and
///    checks the busy state and the permissions.
///  - A refusal returns false with an English \p errorText for the model and its \p status;
///    nothing is started. The host may be gone after a confirmation: prepare runs no foreign
///    code and asks nothing (a read of the file waits for no dialog).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMcpManageAction
{
public:
    PDFAppMcpManageAction() = delete;

    /// The tool is Manage Pages (pages.manage)
    static bool isManageTool(const QString& toolId);

    /// Makes the parameters and the inputs of a call of pages.manage (see the class)
    /// \param host The host of the call
    /// \param invocation The call
    /// \param parameters The parameters of the action, completed by it
    /// \param inputDocuments The open documents of the sources (the assembly mode)
    /// \param errorText English reason of a refusal
    /// \param status Status of the activity of a refusal
    static bool prepare(PDFAppMcpHost* host, const PDFAppMcpToolInvocation& invocation, QVariantMap* parameters,
                        std::vector<QPointer<PDFAppDocument>>* inputDocuments, QString* errorText, PDFMcpActivityStatus* status);
};

}   // namespace pdf

#endif // PDFAPPMCPMANAGEACTION_H
