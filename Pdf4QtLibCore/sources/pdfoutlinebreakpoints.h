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

#ifndef PDFOUTLINEBREAKPOINTS_H
#define PDFOUTLINEBREAKPOINTS_H

#include "pdfglobal.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <vector>

namespace pdf
{

class PDFDocument;

/// The pages of the bookmarks of a document as the places, where a new file or group starts
/// (plan of step 3 of phase 2, 4.3, 5.6): Split At bookmarks (383:6771), Manage Pages By
/// bookmarks, PageMaster.
///
/// The outline (ISO 32000-2, 12.3.3) is read raw by one PDFObjectWalk for the whole walk (an
/// item is read once, also when more items lead to it - a hostile outline, whose /First or
/// /Next comes back, ends there) with a depth limit. The target of an item is its /Dest or the
/// /D of its GoTo action /A (12.6.4.2); a named destination (12.3.2.3) is resolved by the
/// catalog; a target, which is no page of the document, is no page (pageIndex -1).
/// A value; reentrant.
class PDF4QTLIBCORESHARED_EXPORT PDFOutlineBreakPoints
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFOutlineBreakPoints)

public:
    PDFOutlineBreakPoints() = delete;

    /// Deepest level of the outline, which is read (deeper items are not listed)
    static constexpr int MAXIMUM_DEPTH = 256;

    /// Most bookmarks, which are listed
    static constexpr int MAXIMUM_BOOKMARK_COUNT = 100000;

    /// One bookmark (an item of the outline)
    struct Bookmark
    {
        QString title;              ///< Title of the item
        int level = 0;              ///< 0 = top level
        PDFInteger pageIndex = -1;  ///< Page of its target (from 0), -1 = no page of the document

        bool operator==(const Bookmark&) const = default;
    };

    /// A run of pages, which starts at a bookmark
    struct Group
    {
        PDFInteger firstPage = 0;   ///< First page (from 0)
        PDFInteger lastPage = 0;    ///< Last page (from 0)
        QString title;              ///< Title of the bookmark, which starts it

        bool operator==(const Group&) const = default;
    };

    /// The bookmarks of the document in the order of the outline (depth first: an item, then its
    /// children), at most MAXIMUM_BOOKMARK_COUNT
    static std::vector<Bookmark> readBookmarks(const PDFDocument* document);

    /// The groups of the chosen bookmarks within the pages \p pages (indices from 0, sorted):
    /// a bookmark starts its group at the first page of \p pages from its page on; the starts
    /// sorted, two bookmarks of one start are one group (the title of the first in the order
    /// of the outline), the pages before the first start join the first group (383:6771). A
    /// bookmark without a page, or after the last page of \p pages, is left out and its title
    /// is added to \p skippedTitles. A group ends before the next one starts. Empty, when no
    /// bookmark starts a group.
    /// \param bookmarks The chosen bookmarks
    /// \param pages The pages, which are split
    /// \param skippedTitles Titles of the bookmarks left out (optional)
    static std::vector<Group> createGroups(const std::vector<Bookmark>& bookmarks, const std::vector<PDFInteger>& pages, QStringList* skippedTitles);
};

}   // namespace pdf

#endif // PDFOUTLINEBREAKPOINTS_H
