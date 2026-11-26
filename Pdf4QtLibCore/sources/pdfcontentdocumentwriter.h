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
        DocumentChanged,    ///< A fingerprint of a container does not match the document, or the structure model was not read from it: nothing was written
        Canceled
    };

    Status status = Status::Success;

    /// Errors by the index of the page (a page with an error is not written; the run offers
    /// "Skip Page N" = a new run with the page in the skipped set). Each error names its
    /// container (PDFContentWriteError::container) and its node, when it belongs to one. An
    /// error of a form or of a pattern container is listed under every page, which uses it
    /// (such a container is written only when none of its pages is skipped, so skipping one of
    /// them leaves the container out). The key -1 holds the errors, which belong to no page: of
    /// the structure, of a form no page uses, of a missing builder. Every error is found before
    /// anything is written: with an error of any kind the builder is as the caller gave it.
    std::map<PDFInteger, std::vector<PDFContentWriteError>> errors;

    /// Containers, whose fingerprint differs from the document (Status::DocumentChanged)
    std::vector<PDFContentContainerKey> changedContainers;

    /// Status::DocumentChanged: the structure of the document is not the one the modified
    /// structure model of the snapshot was read from (the structure is read again and compared
    /// with everything the model keeps from its reading)
    bool isStructureOutdated = false;

    /// The changed containers of forms and of patterns, which were not written, because a
    /// skipped page uses them (a skipped page stays as it is, also in what it shares with a
    /// written page): the key of the container -> the indices of the skipped pages using it,
    /// ascending. Not an error: the rest was written. Empty without skipped pages.
    std::map<PDFContentContainerKey, std::vector<PDFInteger>> skippedContainers;

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
/// computed over the given document and compared, and a modified structure model is compared
/// with the structure of the document read again, (1) the new objects of the snapshot
/// (PDFContentEditSnapshot::newObjects: pattern copies, later images, fonts, forms), which the
/// written containers reach, are added to the builder and their provisional references are
/// replaced by the real ones everywhere the writer writes (overrides, resources -
/// PDFContentResources::setObject -, other new objects); the ExtGState dictionaries of the editor
/// stay direct dictionaries of the resources, (2) the containers of forms and patterns, (3) the
/// pages, (4) layers (from stage E4), (5) the structure, (6) the facts. The cancellation is
/// checked after each container. Everything, which can refuse the write, is checked before the
/// step (1): a write either changes the builder as a whole or not at all.
///
/// - A container, which is unchanged (PDFContentContainer::isUnchanged), is not written at all;
///   its page keeps its objects (the first part of the gate of stage E0).
/// - A changed page gets one new content stream (Flate) replacing its whole /Contents and its own
///   /Resources dictionary (PDFContentResources::write - R-4); a form and a pattern get their
///   stream and their own /Resources rewritten.
/// - A form or a pattern without /Resources of its own (no entry, a null value or a reference to
///   an object, which does not exist: ISO 32000-2, 7.3.7 and 7.3.10) uses the resources of the
///   page it is drawn on (7.8.3) and stays so: it is written without the entry. Only when a name
///   was added, it gets a dictionary: a copy of the resources of the page it was built with
///   (PDFContentContainer::getInheritedResourcesPageIndex) with the added names. The copy is
///   right only when every page, which reaches the container, gives the names its operations use
///   the same objects as that page does (when its content draws a form, a pattern or a Type 3
///   font, which has no resources of its own either: every name of the page); otherwise the
///   container is refused with an error, which names the pages - no page changes its look
///   silently.
/// - A read-only container is never written.
/// - A skipped page stays as it is: its own container is not written, and neither is a changed
///   form or pattern, which the page uses - also when a written page uses it too
///   (PDFContentWriteResult::skippedContainers says so).
/// - The structure (a modified model): it is written whole or not at all. It is refused with an
///   error, when a changed container is left out because of a skipped page (the tags of the
///   content, which stays as it is, would be written changed: the error is listed under the
///   skipped page), and when the structure objects of the builder (the entry of the catalog, the
///   root, the elements, the nodes of the parent tree) are not those of \p document any more (a
///   second snapshot with a change of the structure written into one builder). A model, which
///   was not read from \p document, is Status::DocumentChanged.
/// - An error of any kind writes nothing (the document of the builder is unchanged then).
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
        std::set<PDFInteger> skippedPages;  ///< Indices of pages, which are not written (after an error of theirs) and stay as they are: a form or a pattern container used by a skipped page is not written either

        /// Write every container of the snapshot, also an unchanged one (the second part of the
        /// gate of stage E0: a forced rewrite without edits gives a stream equal to the joined
        /// original streams, byte for byte). Tests and the gate only. A snapshot of a session
        /// holds only the changed containers, so the gate fills its snapshot by hand with the
        /// unchanged containers.
        bool forceRewrite = false;
    };

    /// Writes the snapshot into the builder.
    ///
    /// A container is refused (Status::PageErrors), when its content in the builder is no longer
    /// the content of \p document: a builder of another document, or a second snapshot of the same
    /// container written into one builder. Each container goes into one builder at most once; a
    /// second change of the same page needs a new builder made from the built document. The same
    /// holds for the structure: one builder takes one snapshot with a modified structure model.
    ///
    /// A provisional reference of a new object is a reference with the generation
    /// PDFContentEditSession::NEW_OBJECT_GENERATION, which is a key of
    /// PDFContentEditSnapshot::newObjects: exactly these are replaced, and only in the entries the
    /// editor has added and in the new objects themselves. Any other reference is left as it is.
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
