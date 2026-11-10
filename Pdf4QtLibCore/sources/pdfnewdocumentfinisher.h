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

#ifndef PDFNEWDOCUMENTFINISHER_H
#define PDFNEWDOCUMENTFINISHER_H

#include "pdfglobal.h"
#include "pdfdocument.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace pdf
{

class PDFOperationControl;

/// The last step of a new document made of pages of other documents (plan of step 3 of
/// phase 2, R2): one function for the merger (the end of a merge, PDFDocumentMergerRun::finish,
/// and of one input kept whole, runSingle) and for the splitter (each part of Split, Extract
/// one file per page, the outputs of Manage Pages) - never a second implementation of the keys
/// of a new document.
///
/// Contract (PDFDocumentMergerRun::finish and PDFDocumentSplitter call it, the merger has no
/// copy of its own):
///  - Input: a document, whose objects may keep the numbers of its source (the removed objects
///    are null, PDFDocumentPageOperations::removePages); it is consumed (a value).
///  - The keys of a new document: a new /ID (14.4), no /Encrypt (a PDFNoneSecurityHandler, owner
///    answer Q2 of step 2), the dates of the creation in /Info and - PDF 2.0 - in XMP (14.3.3),
///    no /Perms, no declaration of PDF/A or PDF/UA (R7 of step 2; a declaration in metadata,
///    which cannot be changed, is a failure). The values of the signatures are removed (owner
///    answer Q4 of step 2: a signature of another file is never valid, 12.8.1 - the field stays
///    empty for a new signature, its widgets lose their appearance, bit 2 AppendOnly of
///    /SigFlags goes, Table 225) with /Perms and /DSS of the catalog (12.8.4, 12.8.4.3); the
///    merger has removed them before (removeSignatures) - nothing is left then.
///  - The storage is renumbered and compacted (the objects nothing refers to go, the identical
///    ones are merged): the cross-reference table of a part of Split has at most the objects of
///    the part + 1 entries (R2: never the table of the whole source in each of 10 000 files).
///  - More than PDFDocumentMerger::MAXIMUM_OBJECT_COUNT objects is a failure.
///  - Threads: reentrant, the state is local; the parts of Split may be finished in parallel
///    (each thread its own document). A cancellation by \p control is checked between the
///    steps and gives isCanceled.
class PDF4QTLIBCORESHARED_EXPORT PDFNewDocumentFinisher
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFNewDocumentFinisher)

public:
    PDFNewDocumentFinisher() = delete;

    /// What the finishing does
    struct Settings
    {
        PDFVersion version = PDFVersion(1, 7);  ///< Version of the result
        QDateTime creationTime;         ///< Time of the creation of the new document
        bool isWholeSingleDocument = false; ///< The pages of one input document with its catalog (R3 of step 2: the merger of one PDF input, all its pages or a part; a part of Split): its /Info and its metadata stay (the build writes /ModDate and /Producer), no new dates unless isNewFile, its list of the layers is not completed
        bool isNewFile = false;         ///< With isWholeSingleDocument: the result is a new file made of the pages of the input (a part of Split, Extract, plan 5.6) - /Info gets the dates of the creation, PDF 2.0 a new XMP packet with them, an older version loses the /Info counterparts of the kept XMP packet (Table 349), which would contradict them
        bool removeConformance = true;  ///< The declaration of PDF/A and PDF/UA is removed (R7 of step 2)
    };

    /// The finished document
    struct Result
    {
        bool isSuccess = false;         ///< The document is valid
        bool isCanceled = false;        ///< Canceled, the document is empty
        QString errorMessage;           ///< Translated reason of a failure
        PDFDocument document;           ///< The new document
        QStringList conformanceRemoved; ///< Standards no longer declared ("PDF/A", "PDF/UA")
        qint64 signaturesRemoved = 0;   ///< Values of signature fields removed (the fields stay empty)
    };

    /// Finishes a new document (see the class)
    /// \param document The document of the pages, consumed
    /// \param settings What the finishing does
    /// \param control Cancellation (optional)
    static Result finish(PDFDocument document, const Settings& settings, const PDFOperationControl* control);
};

}   // namespace pdf

#endif // PDFNEWDOCUMENTFINISHER_H
