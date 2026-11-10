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

#ifndef PDFDOCUMENTPAGEOPERATIONS_H
#define PDFDOCUMENTPAGEOPERATIONS_H

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfpage.h"

#include <QCoreApplication>

#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace pdf
{

class PDFOperationControl;
class PDFDocumentPageRemoval;

/// Operations on the pages of a document (plan of the first batch of phase 2, chapters
/// 2.7 and 7): the rotation of the pages, used by Rotate Pages and by the commands
/// Rotate Left / Right of the thumbnails, and the removal of pages with everything,
/// which points at them. The input document is never changed; every function returns
/// a new document, which keeps the numbers of the objects of the input (a signed
/// document is saved by an incremental update, chapter 12 of the plan), its version
/// and the hash of its source data. A document, which cannot be changed, throws
/// PDFException.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentPageOperations
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFDocumentPageOperations)

public:
    PDFDocumentPageOperations() = delete;

    /// The rotation a page gets (the value of /Rotate written into the page)
    struct PageRotationChange
    {
        PDFInteger pageIndex = -1;                      ///< Index of the page (from 0)
        PageRotation rotation = PageRotation::None;     ///< The new rotation of the page
    };

    /// Changes of the pages \p pages turned by \p turn from their current rotation
    /// (Rotate Left = Rotate270, Rotate Right = Rotate90); pages, which the document
    /// does not have, and duplicates are left out
    static std::vector<PageRotationChange> createTurnChanges(const PDFDocument* document, const std::vector<PDFInteger>& pages, PageRotation turn);

    /// Writes the rotations of the pages (/Rotate of each page; the inherited values of
    /// the other pages stay). An own OCR layer of a rotated page, which was bound to its
    /// page, stays bound: its words are in the unrotated space of the page, only the
    /// rotation of the page has changed (PDFOCRTextLayerWriter::rebindFingerprintsOfPages).
    /// \param document The document
    /// \param changes The rotations, an invalid page throws PDFException
    static PDFDocument rotatePages(const PDFDocument* document, const std::vector<PageRotationChange>& changes);

    /// What a removal of pages has changed besides the pages
    struct RemovalStatistics
    {
        qint64 outlineItemsRemoved = 0;         ///< Items of the outline, which led to a removed page and had no other item below them
        qint64 outlineTargetsRemoved = 0;       ///< Items, which stay as headings of their items, whose own target was a removed page
        qint64 namedDestinationsRemoved = 0;    ///< Named destinations of a removed page (/Names /Dests and /Dests)
        qint64 namedPagesRemoved = 0;           ///< Entries of /Names /Pages and /Names /Templates, which were a removed page
        qint64 linksRemoved = 0;                ///< Links of the pages, which stay, to a removed page
        qint64 actionsRemoved = 0;              ///< Other actions (of buttons, of pages), which went to a removed page
        qint64 fieldsRemoved = 0;               ///< Form fields, which had their widgets only on removed pages
        qint64 signaturesRemoved = 0;           ///< Signature fields among them
        qint64 layersRemoved = 0;               ///< Optional content groups used only by removed pages
        qint64 objectsRemoved = 0;              ///< Objects, which nothing refers to any more (they are null now)
        bool structureTreeRemoved = false;      ///< The document had a structure tree (tags), it is removed
        bool threadsRemoved = false;            ///< The document had articles, they are removed
        bool documentActionsRemoved = false;    ///< The document had an action at its opening or document actions, they are removed
        bool pageLabelsRewritten = false;       ///< The page labels have been written again, so that every page keeps its label
        bool isCanceled = false;                ///< The removal has been canceled, the returned document is empty
    };

    /// Removes the pages and everything, which points at them, so that no object of the
    /// result points at a removed page (plan of the first batch, chapter 7): the outline
    /// (all levels; an item is removed, when its target was a removed page and no item
    /// below it stays; an item, which keeps items below it, loses only its target), the
    /// named destinations, /Names /Pages and /Names /Templates, the links and the GoTo
    /// actions of the pages, which stay, the widgets of the removed pages with their
    /// fields (a field without a widget is removed, also a signature field and its
    /// /Perms), the optional content groups used only by the removed pages, the page
    /// labels (each page, which stays, keeps its label). As the old PageMaster, the
    /// structure tree, the articles and the document actions are removed (owner answer
    /// Q8). The objects, which nothing refers to any more, are null; the pages of the
    /// result are one flat page tree. Removing every page throws PDFException.
    /// \param document The document
    /// \param pages Indices of the pages to remove (from 0); invalid ones and duplicates are left out
    /// \param control Cancellation (optional)
    /// \param statistics What has changed (optional)
    static PDFDocument removePages(const PDFDocument* document, const std::vector<PDFInteger>& pages, const PDFOperationControl* control, RemovalStatistics* statistics);

    /// What the removal reads of the whole document, made once for many parts of it (plan of
    /// step 3 of phase 2, R2: Split, Extract one file per page; PDFDocumentSplitter): the page
    /// tree flattened once (the inherited attributes written into the pages), the sets of the
    /// pages, of their annotations and of the layers, which the pages use. Without it every
    /// part of a split of 10 000 pages would walk the whole document again.
    ///
    /// Immutable after createPageIndex: the threads of the parts read it at once (a value of
    /// plain data and a document, whose storage is shared and never changed).
    class PDF4QTLIBCORESHARED_EXPORT PageIndex
    {
    public:
        /// The document of the index (its page tree flat, the object numbers of the document)
        const PDFDocument& getFlattenedDocument() const { return m_flattenedDocument; }

        /// Pages of the document
        size_t getPageCount() const { return m_pageCount; }

    private:
        friend class PDFDocumentPageOperations;
        friend class PDFDocumentPageRemoval;

        PDFDocument m_flattenedDocument;
        size_t m_pageCount = 0;
        std::set<PDFObjectReference> m_pages;           ///< Every page object of the page tree
        std::set<PDFObjectReference> m_annotations;     ///< Every annotation listed by a page
        std::set<PDFObjectReference> m_layersOfPages;   ///< Optional content groups of /OCGs, which a page reaches
    };

    /// The index of the document for keepPages; nullptr when canceled. Throws PDFException,
    /// when the document cannot be read.
    /// \param document The document (it must live as long as the index is used)
    /// \param control Cancellation (optional)
    static std::shared_ptr<const PageIndex> createPageIndex(const PDFDocument* document, const PDFOperationControl* control);

    /// A part of the document: the pages \p keptPages stay, every other page is removed by the
    /// rules of removePages (the same filters, one implementation). Unlike removePages the
    /// objects nothing refers to are not made null: the part is a new document, its caller
    /// finishes it by PDFNewDocumentFinisher, which removes them and numbers the rest anew (R2).
    /// The work is the size of the part and of the document-wide trees (outline, names, form),
    /// never a walk over all objects. Reentrant: the parts of one index run in parallel.
    /// \param document The document of the index
    /// \param index Index of \p document (createPageIndex)
    /// \param keptPages Pages of the part (indices from 0); invalid ones and duplicates are left out, at least one valid page
    /// \param control Cancellation (optional)
    /// \param statistics What has changed (optional)
    static PDFDocument keepPages(const PDFDocument* document, const PageIndex& index, const std::vector<PDFInteger>& keptPages, const PDFOperationControl* control, RemovalStatistics* statistics);

    /// Text of pages numbered from 1 in ranges, the one text of the page lists of the tools, their
    /// facts and their MCP texts (owner D2 / D3 2026-11-10, 385:9405 "1–3, 8, 10–12", 388:17167
    /// "13–14"): every run of consecutive pages, also of two pages, is a range with an en dash
    /// without spaces, the parts are separated by ", "; at most \p maximumParts parts, then "…"
    /// \param pageIndices Indices of the pages (from 0), sorted
    /// \param maximumParts Most parts before "…" (at least 1)
    /// \param formatNumber Formats a number of a page from 1 (the GUI: the locale of the
    ///        application); nullptr = ASCII digits
    static QString formatPageNumbers(const std::vector<PDFInteger>& pageIndices, int maximumParts = 6, const std::function<QString(PDFInteger)>& formatNumber = nullptr);
};

}   // namespace pdf

#endif // PDFDOCUMENTPAGEOPERATIONS_H
