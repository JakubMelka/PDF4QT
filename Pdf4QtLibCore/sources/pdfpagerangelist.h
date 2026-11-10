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

#ifndef PDFPAGERANGELIST_H
#define PDFPAGERANGELIST_H

#include "pdfglobal.h"

#include <QCoreApplication>
#include <QString>

#include <optional>
#include <vector>

namespace pdf
{

/// An ordered list of pages written by the user (plan of step 3 of phase 2, 4.3, K23 with R13,
/// K42): the source pages of Insert Pages ("1-5, 7, 10-, odd, even", 388:15487), the pages of
/// Manage Pages, Insert PDF Pages of PageMaster, the cuts of Split. Unlike the page selection
/// of the tools over a set of pages (PDFDocumentToolPageSelection, tools.md 9.6) the order of
/// the text is the order of the pages.
///
/// Syntax: items separated by commas, spaces around them are ignored; ASCII only (the binder
/// of the panel writes the canonical text of the Fluent PDFPageRange, K42):
///  - "n" one page, from 1;
///  - "a-b" the pages a to b; a > b is a descending range (as the plan of Merge "A6-1");
///  - "a-" the pages a to the last one, "-b" the pages 1 to b;
///  - "odd", "even" the odd and the even pages of the whole document in ascending order.
/// A page may occur more than once: a further occurrence is another copy of the page (K23,
/// R13 - PageMaster used to leave the duplicates out silently and refused "5-1"). An empty
/// text is no page. A page outside of 1 to the page count, a number of more than 10 digits,
/// an unknown word and more than MAXIMUM_PAGE_COUNT pages in total are errors (hostile input,
/// SECURITY_TODO 12).
///
/// A value; every function is reentrant.
class PDF4QTLIBCORESHARED_EXPORT PDFPageRangeList
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFPageRangeList)

public:
    PDFPageRangeList() = default;

    /// Most pages of a list (PDFDocumentMerger::MAXIMUM_PAGE_COUNT)
    static constexpr PDFInteger MAXIMUM_PAGE_COUNT = 1000000;

    /// Longest text
    static constexpr int MAXIMUM_TEXT_LENGTH = 65536;

    /// Parses the text for a document of \p pageCount pages. std::nullopt and the translated
    /// reason ("Page 60 does not exist – the document has 48 pages") for an invalid text.
    static std::optional<PDFPageRangeList> parse(const QString& text, PDFInteger pageCount, QString* errorMessage);

    /// The pages (indices from 0) in the order of the text
    const std::vector<PDFInteger>& getPageIndices() const { return m_pageIndices; }

    /// Number of the pages of the list
    PDFInteger getPageCount() const { return PDFInteger(m_pageIndices.size()); }

    bool isEmpty() const { return m_pageIndices.empty(); }

    /// Canonical text of the list, which parse reads back to the same pages: consecutive
    /// pages become a range ("1-5"), a descending run a descending range ("9-7")
    QString toText() const;

    bool operator==(const PDFPageRangeList&) const = default;

private:
    std::vector<PDFInteger> m_pageIndices;
};

}   // namespace pdf

#endif // PDFPAGERANGELIST_H
