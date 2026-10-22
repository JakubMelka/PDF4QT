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

#ifndef PDFDOCUMENTTEXTINDEX_H
#define PDFDOCUMENTTEXTINDEX_H

#include "pdfglobal.h"
#include "pdftextlayout.h"

#include <QPolygonF>
#include <QRectF>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

namespace pdf
{

class PDFDocument;
class PDFOperationControl;
class PDFOptionalContentActivity;
class PDFProgress;

/// Text layout of the whole document (newapplication.md 2, step 3 plan 1.1): it is
/// built in a worker thread from a snapshot of the document (build), immutable
/// afterwards and shared by the search panel of the document tab, the speech and
/// MCP (std::shared_ptr<const PDFDocumentTextIndex>). The index is valid for the
/// version of the document it was built from; the owner keeps it, when a change of
/// the document does not touch the page contents.
///
/// The layouts are held in the compact PDFTextLayoutStorage (a fraction of the
/// memory of PDFTextLayout), the pages are processed in parallel and the build can
/// be canceled between the pages. The index has its own font and ICC profile
/// caches, so a build never touches the caches of the widgets.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentTextIndex
{
public:
    PDFDocumentTextIndex() = default;

    /// Builds the index of all pages. Blocking - call it from a worker thread. The
    /// pages are processed by PDFExecutionPolicy (Scope::Page).
    /// \param document Document (must stay alive during the build)
    /// \param optionalContentActivity Activity of the optional content (nullptr = the
    ///        default configuration of the document for the view); it is only read,
    ///        it must not be changed during the build
    /// \param operationControl Cancellation (nullptr = cannot be canceled)
    /// \param progress Progress (nullptr = none), one step per page
    /// \param processedPageCount Counter of the processed pages (nullptr = none), it
    ///        is incremented from the worker threads; a caller, which cannot keep a
    ///        PDFProgress alive during the build, reads it by a timer
    /// \returns the index, or nullptr, when the build was canceled or the document is nullptr
    static std::shared_ptr<const PDFDocumentTextIndex> build(const PDFDocument* document,
                                                             const PDFOptionalContentActivity* optionalContentActivity,
                                                             const PDFOperationControl* operationControl,
                                                             PDFProgress* progress,
                                                             std::atomic<PDFInteger>* processedPageCount = nullptr);

    /// Number of pages of the index
    PDFInteger getPageCount() const { return PDFInteger(m_storage.getCount()); }

    /// Text layout of the page (an empty layout for an invalid page index)
    PDFTextLayout getTextLayout(PDFInteger pageIndex) const;

    /// Plain text of the page in the reading order of its blocks (an empty string
    /// for an invalid page index or a page without text)
    QString getPageText(PDFInteger pageIndex) const;

    /// Storage of the text layouts of the pages
    const PDFTextLayoutStorage& getStorage() const { return m_storage; }

    /// Estimate of the memory used by the index in bytes
    qint64 getMemoryConsumptionEstimate() const { return m_storage.getMemoryConsumptionEstimate(); }

private:
    friend class PDFDocumentTextIndexBuilder;

    PDFTextLayoutStorage m_storage;
};

/// Options of the search in the text index
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentTextSearchOptions
{
public:
    bool caseSensitive = false;

    /// The query matches only whole words (it may be combined with the other options)
    bool wholeWords = false;

    /// The query is a regular expression (PCRE2 syntax of QRegularExpression)
    bool regularExpression = false;

    /// The query has wildcards: * is any sequence of characters inside of a word,
    /// ? is one character of a word (ignored, when regularExpression is set)
    bool wildcards = false;

    bool operator==(const PDFDocumentTextSearchOptions&) const = default;
};

/// One occurrence of the query in the document
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentTextSearchMatch
{
public:
    /// Page of the occurrence (from 0)
    PDFInteger pageIndex = -1;

    /// The matched text
    QString matched;

    /// Context of the occurrence (the lines of the text, where it lies)
    QString context;

    /// Position and length of the matched text in the context (start -1 = unknown)
    qsizetype contextMatchStart = -1;
    qsizetype contextMatchLength = 0;

    /// Characters of the occurrence in the text layout of the page of the index
    PDFTextSelectionItems textSelectionItems;

    /// Quadrilaterals of the occurrence in the page space (4 points per piece: top
    /// left, top right, bottom left, bottom right - the order of PDF quad points)
    QPolygonF quadrilaterals;

    /// Bounding rectangle of the quadrilaterals in the page space
    QRectF boundingRect;
};

using PDFDocumentTextSearchMatches = std::vector<PDFDocumentTextSearchMatch>;

/// Result of a search in the text index
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentTextSearchResult
{
public:
    /// Occurrences in the order of the pages and of the text
    PDFDocumentTextSearchMatches results;

    /// More results exist than the maximal number of results
    bool isTruncated = false;

    /// The search was canceled (the results are incomplete)
    bool isCanceled = false;

    /// Error of the query (an invalid regular expression), empty = none
    QString error;
};

/// Search of a text or a regular expression in PDFDocumentTextIndex. The query is
/// untrusted text (MCP, the search field) and it is guarded:
/// - its length is bounded (MAXIMUM_QUERY_LENGTH), a regular expression is
///   checked before it is used,
/// - an empty match (an expression, which matches the empty text) is skipped,
/// - one attempt of a match is bounded by the match limit of PCRE2 (catastrophic
///   backtracking ends with no match instead of running forever),
/// - the pages are searched in parallel in chunks, the cancellation is checked
///   before each page, so a search of thousands of pages stops at once,
/// - the number of the results is bounded (\p maximumResults); pages after the
///   chunk, where the limit was reached, are not searched.
class PDF4QTLIBCORESHARED_EXPORT PDFDocumentTextSearch
{
public:
    PDFDocumentTextSearch() = delete;

    /// Default maximal number of results (MCP)
    static constexpr size_t DEFAULT_MAXIMUM_RESULTS = 10000;

    /// Maximal length of a query (longer queries are refused)
    static constexpr int MAXIMUM_QUERY_LENGTH = 1024;

    /// Finds the query in all pages of the index
    /// \param index Text index
    /// \param query Text (or a regular expression with options.regularExpression)
    /// \param options Options of the search
    /// \param maximumResults Maximal number of results (0 = DEFAULT_MAXIMUM_RESULTS)
    /// \param operationControl Cancellation (nullptr = cannot be canceled)
    static PDFDocumentTextSearchResult find(const PDFDocumentTextIndex& index,
                                            const QString& query,
                                            const PDFDocumentTextSearchOptions& options,
                                            size_t maximumResults,
                                            const PDFOperationControl* operationControl);

    /// Returns the pattern of the regular expression of the query, or an empty
    /// text, when the query is searched as a plain text (no regular expression,
    /// no whole words, no wildcards)
    static QString createPattern(const QString& query, const PDFDocumentTextSearchOptions& options);
};

}   // namespace pdf

#endif // PDFDOCUMENTTEXTINDEX_H
