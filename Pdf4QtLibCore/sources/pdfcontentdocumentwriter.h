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

#ifndef PDFCONTENTDOCUMENTWRITER_H
#define PDFCONTENTDOCUMENTWRITER_H

#include "pdfcontenteditsession.h"
#include "pdfcontentstreamwriter.h"
#include "pdfdocument.h"

#include <QCoreApplication>
#include <QString>

#include <map>
#include <set>
#include <vector>

namespace pdf
{
class PDFDocumentBuilder;
class PDFOperationControl;
class PDFProgressSink;

// Edit Content, stage E0, package P05 (document writer, tool, JSON). Plan: zadani_jadro.md 4.4
// (order of writing, errors per page), 4.6 (fingerprints); zadani.md KE.21, 9.4 point 4 (the gate).

/// The result of writing a snapshot into a document. Threads and ownership: a value owned by
/// the caller (the run of the tool); it points to nothing.
struct PDFContentWriteResult
{
    enum class Status
    {
        Success,            ///< Written (also when nothing had to be written)
        PageErrors,         ///< Some pages have errors: nothing was written at all, \p errors says which
        DocumentChanged,    ///< A fingerprint of a container does not match the document: nothing was written
        Canceled
    };

    Status status = Status::Success;

    /// Errors of the nodes by the index of the page (a page with an error is not written; the
    /// run offers "Skip Page N" = a new run with the page in the skipped set). Each error names
    /// its container (PDFContentWriteError::container). An error of a form or of a pattern
    /// container is listed under every written page, which uses it; skipping those pages skips
    /// the container.
    std::map<PDFInteger, std::vector<PDFContentWriteError>> errors;

    /// Containers, whose fingerprint differs from the document (Status::DocumentChanged)
    std::vector<PDFContentContainerKey> changedContainers;

    // Facts of the result (zadani_jadro.md 4.6)
    std::vector<PDFInteger> pagesChanged;   ///< Indices of the changed pages, ascending: computed by the document writer over the whole document - every page, whose content reaches a written container (PDFResourceWalk), also a page the session never loaded
    PDFInteger objectsAdded = 0;
    PDFInteger objectsEdited = 0;
    PDFInteger objectsRemoved = 0;
    bool isStructureChanged = false;
    PDFInteger fontsAdded = 0;

    bool isSuccess() const { return status == Status::Success; }
};

/// Writes a snapshot of a session into a document builder.
///
/// Order (zadani_jadro.md 4.4): (0) the fingerprint of every container of the snapshot is
/// computed over the given document and compared, (1) the new objects of the snapshot
/// (PDFContentEditSnapshot::newObjects: pattern copies, later images, fonts, forms), which the
/// written containers reach, are added to the builder and their provisional references are
/// replaced by the real ones everywhere the writer writes (overrides, resources -
/// PDFContentResources::setObject -, other new objects); the ExtGState dictionaries of the editor
/// stay direct dictionaries of the resources, (2) the containers of forms and patterns, (3) the
/// pages, (4) layers (from stage E4), (5) the structure, (6) the facts. The cancellation is
/// checked after each container.
///
/// - A container, which is unchanged (PDFContentContainer::isUnchanged), is not written at all;
///   its page keeps its objects (the first part of the gate of stage E0).
/// - A changed page gets one new content stream (Flate) replacing its whole /Contents and its own
///   /Resources dictionary (PDFContentResources::write - R-4); a form and a pattern get their
///   stream and their own /Resources rewritten.
/// - A read-only container is never written.
/// - An error of any page writes nothing (the document of the builder is unchanged then).
///
/// Threads and ownership: static and reentrant; runs in the thread of the run of the tool. It
/// reads the immutable snapshot and writes into the builder of the caller only.
class PDF4QTLIBCORESHARED_EXPORT PDFContentDocumentWriter
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFContentDocumentWriter)

public:
    PDFContentDocumentWriter() = delete;

    /// Options of a write (a plain value)
    struct Options
    {
        std::set<PDFInteger> skippedPages;  ///< Indices of pages, which are not written (after an error of theirs); a form or a pattern container used only by skipped pages is not written either

        /// Write every container of the snapshot, also an unchanged one (the second part of the
        /// gate of stage E0: a forced rewrite without edits gives a stream equal to the joined
        /// original streams, byte for byte). Tests and the gate only. A snapshot of a session
        /// holds only the changed containers, so the gate fills its snapshot by hand with the
        /// unchanged containers.
        bool forceRewrite = false;
    };

    /// Writes the snapshot into the builder.
    /// \param document The document the snapshot was made of and the builder was created from:
    ///        the fingerprints of step (0) are computed over it
    /// \param snapshot The snapshot
    /// \param builder Builder created from \p document
    /// \param options Options
    /// \param control Cancellation, may be nullptr
    /// \param progress Progress, may be nullptr
    static PDFContentWriteResult write(const PDFDocument* document,
                                       const PDFContentEditSnapshot& snapshot,
                                       PDFDocumentBuilder* builder,
                                       const Options& options,
                                       const PDFOperationControl* control,
                                       PDFProgressSink* progress);

    /// A document for the preview of one page: the document of \p document with the containers of
    /// the page of the snapshot written. nullptr with \p result set, when it cannot be written.
    /// \param document The document the snapshot was made of
    /// \param snapshot The snapshot
    /// \param pageIndex The page
    /// \param control Cancellation, may be nullptr
    /// \param result Errors, may be nullptr
    static PDFDocumentPointer writePagePreview(const PDFDocument* document,
                                               const PDFContentEditSnapshot& snapshot,
                                               PDFInteger pageIndex,
                                               const PDFOperationControl* control,
                                               PDFContentWriteResult* result);
};

}   // namespace pdf

#endif // PDFCONTENTDOCUMENTWRITER_H
