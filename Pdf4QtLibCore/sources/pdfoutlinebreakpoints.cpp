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

#include "pdfoutlinebreakpoints.h"

#include "pdfaction.h"
#include "pdfcatalog.h"
#include "pdfdocument.h"
#include "pdfencoding.h"
#include "pdfobjectutils.h"

#include <algorithm>

namespace pdf
{

/// Helpers of the bookmarks
class PDFOutlineBreakPointsHelper
{
public:
    PDFOutlineBreakPointsHelper() = delete;

    /// The page of the target of an outline item (its /Dest, else the /D of its GoTo action),
    /// -1 for no page of the document
    static PDFInteger getTargetPage(const PDFDocument* document, const PDFDictionary* item)
    {
        const PDFObjectStorage* storage = &document->getStorage();
        PDFObject destinationObject = item->get("Dest");
        if (destinationObject.isNull())
        {
            const PDFDictionary* action = storage->getDictionaryFromObject(item->get("A"));
            const PDFObject type = action ? storage->getObject(action->get("S")) : PDFObject();
            if (!action || !type.isName() || type.getString() != "GoTo")
            {
                return -1;
            }
            destinationObject = action->get("D");
        }

        PDFDestination destination = PDFDestination::parse(storage, destinationObject);
        if (destination.getDestinationType() == DestinationType::Named)
        {
            const PDFDestination* namedDestination = document->getCatalog()->getNamedDestination(destination.getName());
            if (!namedDestination)
            {
                return -1;
            }
            destination = *namedDestination;
        }
        if (!destination.isValid() || !destination.getPageReference().isValid())
        {
            return -1;
        }

        const size_t pageIndex = document->getCatalog()->getPageIndexFromPageReference(destination.getPageReference());
        return pageIndex == PDFCatalog::INVALID_PAGE_INDEX ? -1 : PDFInteger(pageIndex);
    }

    /// Reads the siblings from \p first and their children (depth first)
    static void readItems(const PDFDocument* document, const PDFObject& first, int level, PDFObjectWalk* walk, std::vector<PDFOutlineBreakPoints::Bookmark>* bookmarks)
    {
        const PDFObjectStorage* storage = &document->getStorage();
        PDFObject next = first;
        while (next.isReference() && bookmarks->size() < size_t(PDFOutlineBreakPoints::MAXIMUM_BOOKMARK_COUNT))
        {
            // One set of the visited items for the whole outline: an item, which comes back
            // (a cycle of /Next or /First, an item shared by two parents), is read once
            if (!walk->enter(next, level))
            {
                return;
            }
            const PDFDictionary* item = storage->getDictionaryFromObject(next);
            if (!item)
            {
                return;
            }

            PDFOutlineBreakPoints::Bookmark bookmark;
            const PDFObject& title = storage->getObject(item->get("Title"));
            bookmark.title = title.isString() ? PDFEncoding::convertTextString(title.getString()) : QString();
            bookmark.level = level;
            bookmark.pageIndex = getTargetPage(document, item);
            bookmarks->push_back(std::move(bookmark));

            if (level + 1 < PDFOutlineBreakPoints::MAXIMUM_DEPTH)
            {
                readItems(document, item->get("First"), level + 1, walk, bookmarks);
            }
            next = item->get("Next");
        }
    }
};

std::vector<PDFOutlineBreakPoints::Bookmark> PDFOutlineBreakPoints::readBookmarks(const PDFDocument* document)
{
    std::vector<Bookmark> bookmarks;
    if (!document)
    {
        return bookmarks;
    }

    const PDFObjectStorage* storage = &document->getStorage();
    const PDFDictionary* trailer = storage->getDictionaryFromObject(storage->getTrailerDictionary());
    const PDFDictionary* catalog = trailer ? storage->getDictionaryFromObject(trailer->get("Root")) : nullptr;
    const PDFDictionary* outlines = catalog ? storage->getDictionaryFromObject(catalog->get("Outlines")) : nullptr;
    if (!outlines)
    {
        return bookmarks;
    }

    PDFObjectWalk walk(MAXIMUM_DEPTH);
    if (catalog->get("Outlines").isReference())
    {
        // The root is no item: an item, whose /Next or /First is the root, ends there
        walk.enter(catalog->get("Outlines"), 0);
    }
    PDFOutlineBreakPointsHelper::readItems(document, outlines->get("First"), 0, &walk, &bookmarks);
    return bookmarks;
}

std::vector<PDFOutlineBreakPoints::Group> PDFOutlineBreakPoints::createGroups(const std::vector<Bookmark>& bookmarks, const std::vector<PDFInteger>& pages, QStringList* skippedTitles)
{
    std::vector<Group> groups;
    if (pages.empty())
    {
        return groups;
    }

    // The start of each bookmark: the first page of the split from its page on
    struct Start
    {
        size_t position = 0;    ///< Index into pages
        size_t order = 0;       ///< Order of the bookmark in the outline
        QString title;
    };
    std::vector<Start> starts;
    for (size_t i = 0; i < bookmarks.size(); ++i)
    {
        const Bookmark& bookmark = bookmarks[i];
        const auto it = bookmark.pageIndex >= 0 ? std::lower_bound(pages.cbegin(), pages.cend(), bookmark.pageIndex) : pages.cend();
        if (it == pages.cend())
        {
            if (skippedTitles)
            {
                skippedTitles->append(bookmark.title);
            }
            continue;
        }
        starts.push_back({ size_t(std::distance(pages.cbegin(), it)), i, bookmark.title });
    }

    std::stable_sort(starts.begin(), starts.end(), [](const Start& left, const Start& right) { return left.position < right.position; });
    starts.erase(std::unique(starts.begin(), starts.end(), [](const Start& left, const Start& right) { return left.position == right.position; }), starts.end());

    for (size_t i = 0; i < starts.size(); ++i)
    {
        const size_t firstPosition = i == 0 ? 0 : starts[i].position;
        const size_t lastPosition = i + 1 < starts.size() ? starts[i + 1].position - 1 : pages.size() - 1;
        groups.push_back({ pages[firstPosition], pages[lastPosition], starts[i].title });
    }
    return groups;
}

}   // namespace pdf
