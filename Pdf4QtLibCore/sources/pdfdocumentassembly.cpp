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

#include "pdfdocumentassembly.h"

#include "pdfcatalog.h"
#include "pdfdocumentpageoperations.h"
#include "pdfimagepagefactory.h"
#include "pdfpagerangelist.h"
#include "pdfpagesizepresets.h"

#include <QDir>
#include <QHash>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <map>
#include <set>

namespace pdf
{

// The run of an assembly is a run of the merger: its pages fit the limit of the merger
static_assert(PDFDocumentAssembly::MAXIMUM_PAGE_COUNT <= PDFDocumentMerger::MAXIMUM_PAGE_COUNT);

/// Helpers of the assembly (plan of step 3b of phase 2, 4.1; package M1)
class PDFDocumentAssemblyHelper
{
public:
    PDFDocumentAssemblyHelper() = delete;

    using Assembly = PDFDocumentAssembly;
    using Data = PDFDocumentAssembly::Data;
    using Source = PDFDocumentAssembly::Source;
    using Page = PDFDocumentAssembly::Page;
    using Item = PDFDocumentAssembly::Item;
    using RemovedItem = PDFDocumentAssembly::RemovedItem;
    using Selection = PDFDocumentAssembly::Selection;
    using Edit = PDFDocumentAssembly::Edit;

    /// Size of a blank page in an empty assembly (A4 portrait)
    static QSizeF getDefaultBlankPageSize()
    {
        return PDFPageSizePresets::getSize(QString::fromLatin1(PDFPageSizePresets::A4)).value_or(QSizeF(595.276, 841.89));
    }

    /// Bytes of a blank page in the estimate of the size (KB5)
    static constexpr qint64 BLANK_PAGE_BYTES = 1024;

    /// A new id of an instance of an assembly (B18)
    static QString createInstanceId()
    {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    static const Data& getData(const Assembly& assembly)
    {
        return *assembly.m_data;
    }

    /// A new value of the same instance (the operations)
    static Assembly createValue(const Assembly& base, Data data)
    {
        data.instanceId = base.m_data->instanceId;
        normalizeTargetOriginals(&data);
        return Assembly(std::make_shared<const Data>(std::move(data)));
    }

    /// A new value of a new instance
    static Assembly createNewInstance(Data data)
    {
        data.instanceId = createInstanceId();
        normalizeTargetOriginals(&data);
        return Assembly(std::make_shared<const Data>(std::move(data)));
    }

    /// A refused operation: the same value, the same selection, the reason
    static Assembly refuse(const Assembly& assembly, const Selection& selection, Edit* edit, const QString& message)
    {
        if (edit)
        {
            edit->selection = selection;
            edit->errorMessage = message;
        }
        return assembly;
    }

    /// A done operation: the new value and its selection
    static Assembly accept(Assembly assembly, Selection selection, Edit* edit)
    {
        if (edit)
        {
            edit->selection = std::move(selection);
            edit->errorMessage.clear();
        }
        return assembly;
    }

    static QString createItemId(Data* data)
    {
        return QStringLiteral("p%1").arg(data->nextId++);
    }

    /// A new id of a source, which no source of the assembly has
    static QString createSourceId(Data* data)
    {
        QSet<QString> ids;
        for (const Source& source : data->sources)
        {
            ids.insert(source.id);
        }
        QString id;
        do
        {
            id = QStringLiteral("s%1").arg(data->nextId++);
        } while (ids.contains(id));
        return id;
    }

    /// The number of an id "p<n>" or "s<n>", 0 for another id
    static quint64 getIdNumber(const QString& id)
    {
        if (id.size() < 2 || (id.front() != QLatin1Char('p') && id.front() != QLatin1Char('s')))
        {
            return 0;
        }
        bool ok = false;
        const quint64 number = QStringView(id).mid(1).toULongLong(&ok);
        return ok ? number : 0;
    }

    /// The indices of the selected items, ascending (an unknown id is left out)
    static std::vector<size_t> getSelectedIndices(const std::vector<Item>& items, const Selection& selection)
    {
        QSet<QString> ids;
        for (const QString& id : selection)
        {
            ids.insert(id);
        }
        std::vector<size_t> indices;
        for (size_t i = 0; i < items.size() && !ids.isEmpty(); ++i)
        {
            if (ids.contains(items[i].id))
            {
                indices.push_back(i);
            }
        }
        return indices;
    }

    /// The selected indices, or all items when less than two are selected (Sort, Regroup; 387:13288)
    static std::vector<size_t> getSelectedOrAll(const std::vector<Item>& items, const Selection& selection)
    {
        std::vector<size_t> indices = getSelectedIndices(items, selection);
        if (indices.size() < 2)
        {
            indices.resize(items.size());
            for (size_t i = 0; i < items.size(); ++i)
            {
                indices[i] = i;
            }
        }
        return indices;
    }

    /// The ids of the items of the indices
    static Selection getIds(const std::vector<Item>& items, const std::vector<size_t>& indices)
    {
        Selection ids;
        ids.reserve(indices.size());
        for (const size_t index : indices)
        {
            ids.push_back(items[index].id);
        }
        return ids;
    }

    static Selection getIds(const std::vector<Item>& items)
    {
        Selection ids;
        ids.reserve(items.size());
        for (const Item& item : items)
        {
            ids.push_back(item.id);
        }
        return ids;
    }

    /// Where new items go: after the last selected item, without a selection at the end (B19)
    static size_t getInsertionIndex(const std::vector<Item>& items, const std::vector<size_t>& selectedIndices)
    {
        return selectedIndices.empty() ? items.size() : selectedIndices.back() + 1;
    }

    /// The items with \p newItems instead of the selected ones, at the place of the first selected (B50)
    static std::vector<Item> replaceSelected(const std::vector<Item>& items, const std::vector<size_t>& selectedIndices, std::vector<Item> newItems)
    {
        std::vector<bool> isSelected(items.size(), false);
        for (const size_t index : selectedIndices)
        {
            isSelected[index] = true;
        }

        std::vector<Item> result;
        result.reserve(items.size() + newItems.size());
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (!selectedIndices.empty() && i == selectedIndices.front())
            {
                std::move(newItems.begin(), newItems.end(), std::back_inserter(result));
            }
            if (!isSelected[i])
            {
                result.push_back(items[i]);
            }
        }
        return result;
    }

    static qint64 getPageCount(const std::vector<Item>& items)
    {
        qint64 count = 0;
        for (const Item& item : items)
        {
            count += qint64(item.pages.size());
        }
        return count;
    }

    /// The refusal of more than MAXIMUM_PAGE_COUNT pages (owner 2026-11-12)
    static QString getPageLimitText()
    {
        return Assembly::tr("An assembly has at most %Ln pages.", nullptr, int(Assembly::MAXIMUM_PAGE_COUNT));
    }

    /// The refusal of a source of more than MAXIMUM_PAGE_COUNT pages (it would not fit, even alone)
    static QString getSourcePageLimitText(const QString& name)
    {
        return Assembly::tr("%1 has more than %Ln pages, the most an assembly can have.", nullptr, int(Assembly::MAXIMUM_PAGE_COUNT)).arg(name.left(Assembly::MAXIMUM_NAME_LENGTH));
    }

    /// The limits of an assembly, which grows (plan 4.1, 6)
    static bool checkLimits(const Data& data, QString* errorMessage)
    {
        const size_t itemCount = data.items.size() + data.removedItems.size();
        if (itemCount > size_t(Assembly::MAXIMUM_ITEM_COUNT))
        {
            *errorMessage = Assembly::tr("An assembly has at most %Ln items.", nullptr, Assembly::MAXIMUM_ITEM_COUNT);
            return false;
        }
        qint64 pageCount = getPageCount(data.items);
        for (const RemovedItem& removedItem : data.removedItems)
        {
            pageCount += qint64(removedItem.item.pages.size());
        }
        if (pageCount > Assembly::MAXIMUM_PAGE_COUNT)
        {
            *errorMessage = getPageLimitText();
            return false;
        }
        if (data.sources.size() > size_t(Assembly::MAXIMUM_SOURCE_COUNT))
        {
            *errorMessage = Assembly::tr("An assembly has at most %Ln files.", nullptr, Assembly::MAXIMUM_SOURCE_COUNT);
            return false;
        }
        return true;
    }

    /// A page of a source of the assembly, which exists in its source (the bin keeps pages over a
    /// changed page count, they cannot go back)
    static bool isPageValid(const Data& data, const Page& page)
    {
        if (page.plan.input == Assembly::BLANK_INPUT)
        {
            return page.plan.pageSize.isValid() && PDFPageSizePresets::isValidSide(page.plan.pageSize.width()) && PDFPageSizePresets::isValidSide(page.plan.pageSize.height());
        }
        if (page.plan.input < 0 || size_t(page.plan.input) >= data.sources.size())
        {
            return false;
        }
        return page.plan.pageIndex >= 0 && page.plan.pageIndex < data.sources[size_t(page.plan.input)].pageCount;
    }

    /// RB4: one occurrence of each page of the target, which is not a clone, in the items
    static void normalizeTargetOriginals(Data* data)
    {
        if (data->sources.empty() || !data->sources.front().isTarget)
        {
            return;
        }

        std::map<PDFInteger, Page*> firstOccurrence;
        std::set<PDFInteger> originals;
        for (Item& item : data->items)
        {
            for (Page& page : item.pages)
            {
                if (page.plan.input != 0)
                {
                    continue;
                }
                firstOccurrence.emplace(page.plan.pageIndex, &page);
                if (!page.plan.isClone && !originals.insert(page.plan.pageIndex).second)
                {
                    page.plan.isClone = true;
                }
            }
        }
        for (const auto& [pageIndex, page] : firstOccurrence)
        {
            if (!originals.count(pageIndex))
            {
                page->plan.isClone = false;
            }
        }
    }

    /// Moves the pages, for which \p isMoved is true, from the items into the bin: an item of
    /// such pages only goes whole, the pages of a mixed item go as one item after it
    template<typename Predicate>
    static void moveMatchingPagesToBin(Data* data, Predicate isMoved)
    {
        std::vector<Item> items;
        items.reserve(data->items.size());
        for (size_t i = 0; i < data->items.size(); ++i)
        {
            Item& item = data->items[i];
            std::vector<Page> kept;
            std::vector<Page> moved;
            for (Page& page : item.pages)
            {
                (isMoved(page) ? moved : kept).push_back(std::move(page));
            }

            const QString previousId = items.empty() ? QString() : items.back().id;
            if (moved.empty())
            {
                item.pages = std::move(kept);
                items.push_back(std::move(item));
            }
            else if (kept.empty())
            {
                item.pages = std::move(moved);
                data->removedItems.push_back({ std::move(item), previousId, int(items.size()) });
            }
            else
            {
                item.pages = std::move(kept);
                RemovedItem removedItem;
                removedItem.item.id = createItemId(data);
                removedItem.item.pages = std::move(moved);
                removedItem.previousItemId = item.id;
                removedItem.index = int(items.size() + 1);
                items.push_back(std::move(item));
                data->removedItems.push_back(std::move(removedItem));
            }
        }
        data->items = std::move(items);
    }

    /// An item of one page
    static Item createPageItem(Data* data, Page page)
    {
        Item item;
        item.id = createItemId(data);
        item.pages.push_back(std::move(page));
        return item;
    }

    static Page createPage(int input, PDFInteger pageIndex)
    {
        Page page;
        page.plan.input = input;
        page.plan.pageIndex = pageIndex;
        return page;
    }

    /// A text compared with its numbers by their values ("file2" before "file10"), case
    /// insensitive; < 0, 0, > 0
    static int compareNatural(const QString& first, const QString& second)
    {
        qsizetype i = 0;
        qsizetype j = 0;
        while (i < first.size() && j < second.size())
        {
            const QChar a = first[i];
            const QChar b = second[j];
            if (a.isDigit() && b.isDigit())
            {
                const qsizetype startA = i;
                const qsizetype startB = j;
                while (i < first.size() && first[i].isDigit())
                {
                    ++i;
                }
                while (j < second.size() && second[j].isDigit())
                {
                    ++j;
                }
                // The numbers without their leading zeros: a longer number is larger
                QStringView numberA = QStringView(first).mid(startA, i - startA);
                QStringView numberB = QStringView(second).mid(startB, j - startB);
                while (numberA.size() > 1 && numberA.front() == QLatin1Char('0'))
                {
                    numberA = numberA.mid(1);
                }
                while (numberB.size() > 1 && numberB.front() == QLatin1Char('0'))
                {
                    numberB = numberB.mid(1);
                }
                if (numberA.size() != numberB.size())
                {
                    return numberA.size() < numberB.size() ? -1 : 1;
                }
                const int result = numberA.compare(numberB);
                if (result != 0)
                {
                    return result;
                }
                continue;
            }

            const int result = QString::compare(QString(a), QString(b), Qt::CaseInsensitive);
            if (result != 0)
            {
                return result;
            }
            ++i;
            ++j;
        }
        if (i < first.size())
        {
            return 1;
        }
        if (j < second.size())
        {
            return -1;
        }
        return 0;
    }

    /// The shown size of a page of a document: its effective crop box (14.11.2) in points with its
    /// /Rotate (Table 31)
    static QSizeF getShownPageSize(const PDFPage* page)
    {
        const double unit = page->getEffectiveUserUnit();
        QSizeF size = page->getEffectiveCropBox().size() * unit;
        if (page->getPageRotation() == PageRotation::Rotate90 || page->getPageRotation() == PageRotation::Rotate270)
        {
            size.transpose();
        }
        return size;
    }

    /// Title of the pages of one source (a part, a derived name): "Contract.pdf, p. 1–5"
    static QString getPagesTitle(const Source& source, std::vector<PDFInteger> pages, bool isWholeSource)
    {
        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
        if (isWholeSource && PDFInteger(pages.size()) == source.pageCount)
        {
            return source.name;
        }
        if (source.kind == PDFDocumentAssembly::SourceKind::Image && source.pageCount <= 1)
        {
            return source.name;
        }
        return Assembly::tr("%1, p. %2").arg(source.name, PDFDocumentPageOperations::formatPageNumbers(pages, 32));
    }

    /// Places the items of the bin back (B21): after their neighbor, when it is there (also a
    /// neighbor restored by the same operation), else at their index; the items of one neighbor
    /// in the order, in which they stood (a later removed one stood after an earlier one)
    static void placeRestoredItems(std::vector<Item>* items, std::vector<RemovedItem> restored)
    {
        std::list<Item> list(std::make_move_iterator(items->begin()), std::make_move_iterator(items->end()));
        QHash<QString, std::list<Item>::iterator> positions;
        for (auto it = list.begin(); it != list.end(); ++it)
        {
            positions.insert(it->id, it);
        }

        QHash<QString, std::vector<size_t>> children;
        for (size_t i = 0; i < restored.size(); ++i)
        {
            children[restored[i].previousItemId].push_back(i);
        }

        std::vector<bool> isPlaced(restored.size(), false);
        struct Expansion
        {
            QString anchorId;
            bool isFront = false;
            std::list<Item>::iterator anchor;
        };
        std::vector<Expansion> stack;

        auto expandAll = [&]()
        {
            while (!stack.empty())
            {
                const Expansion expansion = stack.back();
                stack.pop_back();
                auto childrenIt = children.find(expansion.anchorId);
                if (childrenIt == children.end())
                {
                    continue;
                }
                auto before = expansion.isFront ? list.begin() : std::next(expansion.anchor);
                const std::vector<size_t>& indices = childrenIt.value();
                for (auto it = indices.rbegin(); it != indices.rend(); ++it)
                {
                    if (isPlaced[*it])
                    {
                        continue;
                    }
                    isPlaced[*it] = true;
                    const QString id = restored[*it].item.id;
                    before = list.insert(before, std::move(restored[*it].item));
                    positions.insert(id, before);
                    stack.push_back({ id, false, before });
                }
            }
        };

        // The items, which were the first ones, and the items after a neighbor in the list
        stack.push_back({ QString(), true, list.end() });
        expandAll();
        const std::vector<Item*> anchors = [&list]()
        {
            std::vector<Item*> result;
            for (Item& item : list)
            {
                result.push_back(&item);
            }
            return result;
        }();
        for (Item* anchor : anchors)
        {
            stack.push_back({ anchor->id, false, positions.value(anchor->id) });
            expandAll();
        }

        // The neighbor is gone: the index of the removal
        for (size_t i = 0; i < restored.size(); ++i)
        {
            if (isPlaced[i])
            {
                continue;
            }
            isPlaced[i] = true;
            const size_t index = size_t(std::clamp(restored[i].index, 0, int(list.size())));
            auto before = list.begin();
            std::advance(before, qsizetype(index));
            const QString id = restored[i].item.id;
            auto it = list.insert(before, std::move(restored[i].item));
            positions.insert(id, it);
            stack.push_back({ id, false, it });
            expandAll();
        }

        items->assign(std::make_move_iterator(list.begin()), std::make_move_iterator(list.end()));
    }

    /// The groups of a Regroup (the pages of the selection in their order); empty and the reason
    static std::vector<Item> createRegroupedItems(Data* data, const std::vector<Page>& pages, PDFDocumentAssembly::RegroupMode mode,
                                                  const PDFDocumentAssembly::RegroupSettings& settings, QString* errorMessage)
    {
        using Mode = PDFDocumentAssembly::RegroupMode;

        std::vector<std::vector<Page>> groups;
        std::vector<QString> names;
        const size_t count = pages.size();
        switch (mode)
        {
            case Mode::OddAndEven:
            {
                // Two groups by the positions in the selection (KB2, QB5)
                std::vector<Page> odd;
                std::vector<Page> even;
                for (size_t i = 0; i < count; ++i)
                {
                    (i % 2 == 0 ? odd : even).push_back(pages[i]);
                }
                groups.push_back(std::move(odd));
                if (!even.empty())
                {
                    groups.push_back(std::move(even));
                }
                break;
            }

            case Mode::PagePairs:
            case Mode::EveryPages:
            {
                const int pageCount = mode == Mode::PagePairs ? 2 : settings.pageCount;
                if (pageCount < 1 || size_t(pageCount) > count)
                {
                    *errorMessage = Assembly::tr("A group has 1 to %Ln pages.", nullptr, int(count));
                    return {};
                }
                for (size_t i = 0; i < count; i += size_t(pageCount))
                {
                    groups.emplace_back(pages.begin() + qsizetype(i), pages.begin() + qsizetype(std::min(count, i + size_t(pageCount))));
                }
                break;
            }

            case Mode::AtPageNumbers:
            {
                std::vector<int> positions = settings.positions;
                for (const int position : positions)
                {
                    if (position < 1 || size_t(position) > count)
                    {
                        *errorMessage = Assembly::tr("Page %1 is not among the %Ln pages of the selection.", nullptr, int(count)).arg(position);
                        return {};
                    }
                }
                std::sort(positions.begin(), positions.end());
                positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
                if (positions.empty() || positions.front() != 1)
                {
                    positions.insert(positions.begin(), 1);
                }
                positions.push_back(int(count) + 1);
                for (size_t i = 0; i + 1 < positions.size(); ++i)
                {
                    groups.emplace_back(pages.begin() + (positions[i] - 1), pages.begin() + (positions[i + 1] - 1));
                }
                break;
            }

            case Mode::ByBookmarks:
            {
                // One PDF source (PDFOutlineBreakPoints reads its outline): a group starts at the
                // page of a bookmark, the pages before the first bookmark join the first group
                const int input = pages.empty() ? -1 : pages.front().plan.input;
                const bool isOneDocument = input >= 0 && data->sources[size_t(input)].kind == PDFDocumentAssembly::SourceKind::Pdf &&
                                           std::all_of(pages.cbegin(), pages.cend(), [input](const Page& page) { return page.plan.input == input; });
                if (!isOneDocument)
                {
                    *errorMessage = Assembly::tr("By bookmarks needs pages of one PDF file.");
                    return {};
                }

                std::vector<std::pair<PDFInteger, QString>> breaks;
                for (size_t i = 0; i < settings.breakPages.size(); ++i)
                {
                    if (settings.breakPages[i] >= 0)
                    {
                        breaks.emplace_back(settings.breakPages[i], i < size_t(settings.groupNames.size()) ? settings.groupNames[qsizetype(i)] : QString());
                    }
                }
                std::stable_sort(breaks.begin(), breaks.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
                breaks.erase(std::unique(breaks.begin(), breaks.end(), [](const auto& left, const auto& right) { return left.first == right.first; }), breaks.end());
                if (breaks.empty())
                {
                    *errorMessage = Assembly::tr("No bookmark starts a group.");
                    return {};
                }

                size_t currentSection = std::numeric_limits<size_t>::max();
                for (const Page& page : pages)
                {
                    auto it = std::upper_bound(breaks.cbegin(), breaks.cend(), page.plan.pageIndex, [](PDFInteger value, const auto& item) { return value < item.first; });
                    const size_t section = it == breaks.cbegin() ? 0 : size_t(std::distance(breaks.cbegin(), it) - 1);
                    if (section != currentSection)
                    {
                        currentSection = section;
                        groups.emplace_back();
                        names.push_back(breaks[section].second.left(Assembly::MAXIMUM_NAME_LENGTH));
                    }
                    groups.back().push_back(page);
                }
                break;
            }

            case Mode::AlternateTwoFiles:
            case Mode::AlternateSecondReversed:
            {
                // The pages of the first file and of the second one alternated (PDFDocumentPagePlan::interleave)
                const int firstInput = pages.empty() ? -1 : pages.front().plan.input;
                int secondInput = -1;
                std::vector<Page> front;
                std::vector<Page> back;
                bool isTwoFiles = firstInput >= 0;
                for (const Page& page : pages)
                {
                    if (page.plan.input == firstInput)
                    {
                        front.push_back(page);
                        continue;
                    }
                    if (page.plan.input < 0 || (secondInput >= 0 && page.plan.input != secondInput))
                    {
                        isTwoFiles = false;
                        break;
                    }
                    secondInput = page.plan.input;
                    back.push_back(page);
                }
                if (!isTwoFiles || secondInput < 0)
                {
                    *errorMessage = Assembly::tr("Alternate needs pages of two files.");
                    return {};
                }

                PDFDocumentPagePlan::InterleaveSettings interleaveSettings;
                interleaveSettings.isBackReversed = mode == Mode::AlternateSecondReversed;
                interleaveSettings.areExtrasKept = true;
                std::vector<Page> alternated;
                for (const PDFDocumentPagePlan::Item& side : PDFDocumentPagePlan::interleave(PDFInteger(front.size()), PDFInteger(back.size()), interleaveSettings))
                {
                    alternated.push_back((side.input == 0 ? front : back)[size_t(side.pageIndex)]);
                }
                groups.push_back(std::move(alternated));
                break;
            }

            case Mode::ReverseOrder:
                groups.emplace_back(pages.rbegin(), pages.rend());
                break;
        }

        std::vector<Item> items;
        for (size_t i = 0; i < groups.size(); ++i)
        {
            if (groups[i].empty())
            {
                continue;
            }
            Item item;
            item.id = createItemId(data);
            item.name = i < names.size() ? names[i] : QString();
            item.pages = std::move(groups[i]);
            item.isCreatedGroup = true;
            items.push_back(std::move(item));
        }
        if (items.empty())
        {
            *errorMessage = Assembly::tr("The selection has no pages.");
        }
        return items;
    }

    /// The key of a page of Sort (Source, Page number, Type): a blank page after the others
    static std::pair<int, PDFInteger> getSortKey(const Data& data, const Page& page, PDFDocumentAssembly::SortKey key)
    {
        const bool isBlank = page.plan.input < 0;
        switch (key)
        {
            case PDFDocumentAssembly::SortKey::Source:
                return { isBlank ? std::numeric_limits<int>::max() : page.plan.input, 0 };
            case PDFDocumentAssembly::SortKey::PageNumber:
                return { isBlank ? 1 : 0, page.plan.pageIndex };
            case PDFDocumentAssembly::SortKey::Type:
                return { isBlank ? 2 : (data.sources[size_t(page.plan.input)].kind == PDFDocumentAssembly::SourceKind::Image ? 1 : 0), 0 };
            default:
                return { 0, 0 };
        }
    }

    /// The pages of the target, by their page object, of a document
    static std::vector<PDFObjectReference> getPageReferences(const PDFDocument* document)
    {
        std::vector<PDFObjectReference> references;
        const PDFCatalog* catalog = document->getCatalog();
        references.reserve(catalog->getPageCount());
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            references.push_back(catalog->getPage(i)->getPageReference());
        }
        return references;
    }

    /// Text of a search of the canvas (387:15594): every word matches the item
    static bool isSearchMatch(const Assembly& assembly, const Item& item, const QStringList& words)
    {
        const QString name = Assembly::getItemName(assembly, item);
        for (const QString& word : words)
        {
            bool isMatch = false;
            if (word.compare(QLatin1String("grouped"), Qt::CaseInsensitive) == 0)
            {
                isMatch = item.isGroup();
            }
            else if (word.compare(QLatin1String("ungrouped"), Qt::CaseInsensitive) == 0)
            {
                isMatch = !item.isGroup();
            }
            else if (word.compare(QLatin1String("portrait"), Qt::CaseInsensitive) == 0 || word.compare(QLatin1String("landscape"), Qt::CaseInsensitive) == 0)
            {
                const bool isPortrait = word.compare(QLatin1String("portrait"), Qt::CaseInsensitive) == 0;
                isMatch = isOrientation(assembly, item, isPortrait);
            }
            else if (word.compare(QLatin1String("image"), Qt::CaseInsensitive) == 0)
            {
                isMatch = std::any_of(item.pages.cbegin(), item.pages.cend(), [&assembly](const Page& page)
                                      { return page.plan.input >= 0 && assembly.getSources()[size_t(page.plan.input)].kind == PDFDocumentAssembly::SourceKind::Image; });
            }
            else if (word.compare(QLatin1String("blank"), Qt::CaseInsensitive) == 0)
            {
                isMatch = std::any_of(item.pages.cbegin(), item.pages.cend(), [](const Page& page) { return page.plan.input < 0; });
            }
            if (!isMatch)
            {
                isMatch = name.contains(word, Qt::CaseInsensitive);
                for (size_t i = 0; i < item.pages.size() && !isMatch; ++i)
                {
                    const int input = item.pages[i].plan.input;
                    isMatch = input >= 0 && assembly.getSources()[size_t(input)].name.contains(word, Qt::CaseInsensitive);
                }
            }
            if (!isMatch)
            {
                return false;
            }
        }
        return true;
    }

    /// Every page of the item is portrait (the width is not larger than the height) or
    /// landscape (the width is not smaller) after its turn and crop (B31); a square is both
    static bool isOrientation(const Assembly& assembly, const Item& item, bool isPortrait)
    {
        for (const Page& page : item.pages)
        {
            const QSizeF size = Assembly::getPageSize(assembly, page);
            if (size.isEmpty())
            {
                return false;
            }
            if (isPortrait ? size.width() > size.height() : size.width() < size.height())
            {
                return false;
            }
        }
        return !item.pages.empty();
    }
};

PDFDocumentAssembly::PDFDocumentAssembly() :
    m_data(std::make_shared<const Data>(Data{ PDFDocumentAssemblyHelper::createInstanceId(), { }, { }, { }, 1 }))
{

}

PDFDocumentAssembly::PDFDocumentAssembly(std::shared_ptr<const Data> data) :
    m_data(std::move(data))
{

}

PDFDocumentAssembly PDFDocumentAssembly::create(std::vector<Source> sources, std::vector<Item> items, std::vector<RemovedItem> removedItems, QString* errorMessage)
{
    using Helper = PDFDocumentAssemblyHelper;

    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    Data data;
    data.sources = std::move(sources);
    data.items = std::move(items);
    data.removedItems = std::move(removedItems);

    auto fail = [&error](const QString& message)
    {
        error = message;
        return PDFDocumentAssembly();
    };

    if (!Helper::checkLimits(data, &error))
    {
        return PDFDocumentAssembly();
    }

    // The sources: unique ids, the target only as the input 0, sane counts
    QSet<QString> sourceIds;
    quint64 maximumNumber = 0;
    for (size_t i = 0; i < data.sources.size(); ++i)
    {
        const Source& source = data.sources[i];
        if (source.id.isEmpty() || source.id.size() > MAXIMUM_NAME_LENGTH || sourceIds.contains(source.id))
        {
            return fail(tr("The files of the assembly have no unique ids."));
        }
        sourceIds.insert(source.id);
        maximumNumber = qMax(maximumNumber, Helper::getIdNumber(source.id));
        if (source.isTarget && i != 0)
        {
            return fail(tr("Only the first file of an assembly can be the document of its tab."));
        }
        if (source.pageCount > MAXIMUM_PAGE_COUNT)
        {
            return fail(Helper::getSourcePageLimitText(source.name));
        }
        if (source.pageCount < 0 || source.name.size() > MAXIMUM_NAME_LENGTH)
        {
            return fail(tr("The file %1 of the assembly is not valid.").arg(source.name.left(MAXIMUM_NAME_LENGTH)));
        }
    }

    // The items: unique ids, at least one page, every page of a source within its pages
    QSet<QString> itemIds;
    auto checkItem = [&](const Item& item, bool isRemoved) -> bool
    {
        if (item.id.isEmpty() || item.id.size() > MAXIMUM_NAME_LENGTH || itemIds.contains(item.id) || item.pages.empty() || item.name.size() > MAXIMUM_NAME_LENGTH)
        {
            return false;
        }
        itemIds.insert(item.id);
        maximumNumber = qMax(maximumNumber, Helper::getIdNumber(item.id));
        for (const Page& page : item.pages)
        {
            if (isRemoved)
            {
                // The bin may keep a page over the page count of a changed source (it cannot go back)
                const bool isKnownSource = page.plan.input == BLANK_INPUT || (page.plan.input >= 0 && size_t(page.plan.input) < data.sources.size());
                if (!isKnownSource || page.plan.pageIndex < 0)
                {
                    return false;
                }
            }
            else if (!Helper::isPageValid(data, page))
            {
                return false;
            }
        }
        return true;
    };
    for (const Item& item : data.items)
    {
        if (!checkItem(item, false))
        {
            return fail(tr("An item of the assembly is not valid."));
        }
    }
    for (const RemovedItem& removedItem : data.removedItems)
    {
        if (!checkItem(removedItem.item, true))
        {
            return fail(tr("An item of the assembly is not valid."));
        }
    }

    data.nextId = maximumNumber + 1;
    return Helper::createNewInstance(std::move(data));
}

const std::vector<PDFDocumentAssembly::Source>& PDFDocumentAssembly::getSources() const
{
    return m_data->sources;
}

const std::vector<PDFDocumentAssembly::Item>& PDFDocumentAssembly::getItems() const
{
    return m_data->items;
}

const std::vector<PDFDocumentAssembly::RemovedItem>& PDFDocumentAssembly::getRemovedItems() const
{
    return m_data->removedItems;
}

const QString& PDFDocumentAssembly::getInstanceId() const
{
    return m_data->instanceId;
}

bool PDFDocumentAssembly::isEqual(const PDFDocumentAssembly& other) const
{
    return m_data == other.m_data ||
           (m_data->sources == other.m_data->sources && m_data->items == other.m_data->items && m_data->removedItems == other.m_data->removedItems);
}

PDFDocumentAssembly PDFDocumentAssembly::addSource(const PDFDocumentAssembly& assembly, Source source, QString* sourceId, QString* errorMessage)
{
    using Helper = PDFDocumentAssemblyHelper;

    QString dummyId;
    QString dummyError;
    QString& id = sourceId ? *sourceId : dummyId;
    QString& error = errorMessage ? *errorMessage : dummyError;
    id.clear();
    error.clear();

    Data data = Helper::getData(assembly);

    // B52: the same file is the same source (a parked one comes back)
    if (!source.path.isEmpty() && !source.isTarget)
    {
        const QString path = QDir::cleanPath(source.path);
        for (Source& existing : data.sources)
        {
            if (!existing.path.isEmpty() && existing.kind == source.kind && QDir::cleanPath(existing.path).compare(path, Qt::CaseInsensitive) == 0)
            {
                id = existing.id;
                if (!existing.isParked)
                {
                    return assembly;
                }
                existing.isParked = false;
                return Helper::createValue(assembly, std::move(data));
            }
        }
    }

    if (source.isTarget && !data.sources.empty())
    {
        error = tr("Only the first file of an assembly can be the document of its tab.");
        return assembly;
    }
    if (source.pageCount > MAXIMUM_PAGE_COUNT)
    {
        error = Helper::getSourcePageLimitText(source.name);
        return assembly;
    }
    if (source.pageCount < 0 || source.name.size() > MAXIMUM_NAME_LENGTH)
    {
        error = tr("The file %1 cannot be added.").arg(source.name.left(MAXIMUM_NAME_LENGTH));
        return assembly;
    }
    if (source.kind == SourceKind::Image && source.imageData.isEmpty())
    {
        error = tr("The image %1 has no data.").arg(source.name.left(MAXIMUM_NAME_LENGTH));
        return assembly;
    }

    const bool isIdFree = !source.id.isEmpty() && source.id.size() <= MAXIMUM_NAME_LENGTH &&
                          std::none_of(data.sources.cbegin(), data.sources.cend(), [&source](const Source& existing) { return existing.id == source.id; });
    if (!isIdFree)
    {
        source.id = Helper::createSourceId(&data);
    }
    else
    {
        data.nextId = qMax(data.nextId, Helper::getIdNumber(source.id) + 1);
    }
    source.isParked = false;
    id = source.id;
    data.sources.push_back(std::move(source));
    if (!Helper::checkLimits(data, &error))
    {
        id.clear();
        return assembly;
    }
    return Helper::createValue(assembly, std::move(data));
}

PDFDocumentAssembly PDFDocumentAssembly::group(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<size_t> indices = Helper::getSelectedIndices(assembly.getItems(), selection);
    if (indices.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("Select the pages to group."));
    }

    Data data = Helper::getData(assembly);
    Item group;
    group.id = Helper::createItemId(&data);
    group.isCreatedGroup = true;
    if (indices.size() == 1)
    {
        group.name = data.items[indices.front()].name;
    }
    for (const size_t index : indices)
    {
        const std::vector<Page>& pages = data.items[index].pages;
        group.pages.insert(group.pages.end(), pages.cbegin(), pages.cend());
    }
    const QString groupId = group.id;
    std::vector<Item> groups;
    groups.push_back(std::move(group));
    data.items = Helper::replaceSelected(data.items, indices, std::move(groups));
    return Helper::accept(Helper::createValue(assembly, std::move(data)), { groupId }, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::ungroup(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<size_t> indices = Helper::getSelectedIndices(assembly.getItems(), selection);
    if (std::none_of(indices.cbegin(), indices.cend(), [&assembly](size_t index) { return assembly.getItems()[index].isGroup(); }))
    {
        return Helper::refuse(assembly, selection, edit, tr("No group is selected."));
    }

    // The pages stay in place, each an item of its own without the name of the group
    Data data = Helper::getData(assembly);
    std::vector<bool> isSelected(data.items.size(), false);
    for (const size_t index : indices)
    {
        isSelected[index] = true;
    }

    std::vector<Item> items;
    Selection newSelection;
    for (size_t i = 0; i < data.items.size(); ++i)
    {
        Item& item = data.items[i];
        if (!isSelected[i] || !item.isGroup())
        {
            if (isSelected[i])
            {
                newSelection.push_back(item.id);
            }
            items.push_back(std::move(item));
            continue;
        }
        for (Page& page : item.pages)
        {
            Item pageItem = Helper::createPageItem(&data, std::move(page));
            newSelection.push_back(pageItem.id);
            items.push_back(std::move(pageItem));
        }
    }
    data.items = std::move(items);
    if (QString error; !Helper::checkLimits(data, &error))
    {
        return Helper::refuse(assembly, selection, edit, error);
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), std::move(newSelection), edit);
}

PDFDocumentAssembly PDFDocumentAssembly::rename(const PDFDocumentAssembly& assembly, const QString& itemId, const QString& name, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const QString newName = name.trimmed();
    if (newName.size() > MAXIMUM_NAME_LENGTH)
    {
        return Helper::refuse(assembly, { itemId }, edit, tr("A name has at most %Ln characters.", nullptr, MAXIMUM_NAME_LENGTH));
    }

    Data data = Helper::getData(assembly);
    auto it = std::find_if(data.items.begin(), data.items.end(), [&itemId](const Item& item) { return item.id == itemId; });
    if (it == data.items.end())
    {
        return Helper::refuse(assembly, { itemId }, edit, tr("The item is not in the assembly."));
    }
    if (it->name == newName)
    {
        return Helper::accept(assembly, { itemId }, edit);
    }
    it->name = newName;
    return Helper::accept(Helper::createValue(assembly, std::move(data)), { itemId }, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::regroup(const PDFDocumentAssembly& assembly, const Selection& selection, RegroupMode mode, const RegroupSettings& settings, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<size_t> indices = Helper::getSelectedOrAll(assembly.getItems(), selection);
    if (indices.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("The assembly has no pages."));
    }

    Data data = Helper::getData(assembly);
    std::vector<Page> pages;
    for (const size_t index : indices)
    {
        pages.insert(pages.end(), data.items[index].pages.cbegin(), data.items[index].pages.cend());
    }

    QString error;
    std::vector<Item> groups = Helper::createRegroupedItems(&data, pages, mode, settings, &error);
    if (groups.empty())
    {
        return Helper::refuse(assembly, selection, edit, error);
    }

    const Selection newSelection = Helper::getIds(groups);
    data.items = Helper::replaceSelected(data.items, indices, std::move(groups));
    return Helper::accept(Helper::createValue(assembly, std::move(data)), newSelection, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::sort(const PDFDocumentAssembly& assembly, const Selection& selection, SortKey key, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<size_t> indices = Helper::getSelectedOrAll(assembly.getItems(), selection);
    if (indices.size() < 2)
    {
        return Helper::accept(assembly, selection, edit);
    }

    const Data& source = Helper::getData(assembly);
    std::vector<size_t> order = indices;
    if (key == SortKey::Reverse)
    {
        std::reverse(order.begin(), order.end());
    }
    else if (key == SortKey::FileName)
    {
        // A blank page has no file: after the files
        std::stable_sort(order.begin(), order.end(), [&source](size_t left, size_t right)
        {
            const Page& leftPage = source.items[left].pages.front();
            const Page& rightPage = source.items[right].pages.front();
            const bool isLeftBlank = leftPage.plan.input < 0;
            const bool isRightBlank = rightPage.plan.input < 0;
            if (isLeftBlank || isRightBlank)
            {
                return !isLeftBlank && isRightBlank;
            }
            return PDFDocumentAssemblyHelper::compareNatural(source.sources[size_t(leftPage.plan.input)].name, source.sources[size_t(rightPage.plan.input)].name) < 0;
        });
    }
    else
    {
        std::stable_sort(order.begin(), order.end(), [&source, key](size_t left, size_t right)
        {
            return PDFDocumentAssemblyHelper::getSortKey(source, source.items[left].pages.front(), key) <
                   PDFDocumentAssemblyHelper::getSortKey(source, source.items[right].pages.front(), key);
        });
    }

    if (order == indices)
    {
        return Helper::accept(assembly, selection, edit);
    }

    // The permutation in the positions of the sorted items
    Data data = source;
    for (size_t i = 0; i < indices.size(); ++i)
    {
        data.items[indices[i]] = source.items[order[i]];
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), selection, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::clone(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<size_t> indices = Helper::getSelectedIndices(assembly.getItems(), selection);
    if (indices.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("Select the pages to clone."));
    }

    // RB4: the clones carry the flag, the original stays the original wherever it goes later
    Data data = Helper::getData(assembly);
    std::vector<Item> clones;
    for (const size_t index : indices)
    {
        Item clone = data.items[index];
        clone.id = Helper::createItemId(&data);
        for (Page& page : clone.pages)
        {
            page.plan.isClone = page.plan.input >= 0;
        }
        clones.push_back(std::move(clone));
    }

    const Selection newSelection = Helper::getIds(clones);
    const size_t position = Helper::getInsertionIndex(data.items, indices);
    data.items.insert(data.items.begin() + qsizetype(position), std::make_move_iterator(clones.begin()), std::make_move_iterator(clones.end()));
    if (QString error; !Helper::checkLimits(data, &error))
    {
        return Helper::refuse(assembly, selection, edit, error);
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), newSelection, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::move(const PDFDocumentAssembly& assembly, const Selection& selection, const QString& beforeItemId, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<Item>& items = assembly.getItems();
    const std::vector<size_t> indices = Helper::getSelectedIndices(items, selection);
    if (indices.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("Select the pages to move."));
    }

    std::vector<bool> isSelected(items.size(), false);
    for (const size_t index : indices)
    {
        isSelected[index] = true;
    }

    // The place: before the item, or before the first item after it, which does not move
    size_t before = items.size();
    if (!beforeItemId.isEmpty())
    {
        auto it = std::find_if(items.cbegin(), items.cend(), [&beforeItemId](const Item& item) { return item.id == beforeItemId; });
        if (it == items.cend())
        {
            return Helper::refuse(assembly, selection, edit, tr("The item is not in the assembly."));
        }
        before = size_t(std::distance(items.cbegin(), it));
        while (before < items.size() && isSelected[before])
        {
            ++before;
        }
    }

    Data data = Helper::getData(assembly);
    std::vector<Item> moved;
    std::vector<Item> result;
    result.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i == before)
        {
            result.push_back(Item());   // the place of the moved items
        }
        (isSelected[i] ? moved : result).push_back(std::move(data.items[i]));
    }
    if (before == items.size())
    {
        result.push_back(Item());
    }

    auto place = std::find_if(result.begin(), result.end(), [](const Item& item) { return item.id.isEmpty(); });
    const size_t placeIndex = size_t(std::distance(result.begin(), place));
    result.erase(place);
    result.insert(result.begin() + qsizetype(placeIndex), std::make_move_iterator(moved.begin()), std::make_move_iterator(moved.end()));

    if (result == items)
    {
        return Helper::accept(assembly, Helper::getIds(items, indices), edit);
    }
    data.items = std::move(result);
    return Helper::accept(Helper::createValue(assembly, std::move(data)), Helper::getIds(items, indices), edit);
}

PDFDocumentAssembly PDFDocumentAssembly::rotate(const PDFDocumentAssembly& assembly, const Selection& selection, PageRotation turn, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<size_t> indices = Helper::getSelectedIndices(assembly.getItems(), selection);
    if (indices.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("Select the pages to rotate."));
    }
    if (turn == PageRotation::None)
    {
        return Helper::accept(assembly, selection, edit);
    }

    Data data = Helper::getData(assembly);
    for (const size_t index : indices)
    {
        for (Page& page : data.items[index].pages)
        {
            page.plan.turn = getPageRotationCombined(page.plan.turn, turn);
        }
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), Helper::getIds(assembly.getItems(), indices), edit);
}

PDFDocumentAssembly PDFDocumentAssembly::resetRotation(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<size_t> indices = Helper::getSelectedIndices(assembly.getItems(), selection);
    Data data = Helper::getData(assembly);
    bool isChanged = false;
    for (const size_t index : indices)
    {
        for (Page& page : data.items[index].pages)
        {
            isChanged = isChanged || page.plan.turn != PageRotation::None;
            page.plan.turn = PageRotation::None;
        }
    }
    if (!isChanged)
    {
        return Helper::accept(assembly, selection, edit);
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), Helper::getIds(assembly.getItems(), indices), edit);
}

PDFDocumentAssembly PDFDocumentAssembly::crop(const PDFDocumentAssembly& assembly, const Selection& selection, const QMarginsF& margins, bool isSameSource, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const bool isValid = std::isfinite(margins.left()) && std::isfinite(margins.top()) && std::isfinite(margins.right()) && std::isfinite(margins.bottom()) &&
                         margins.left() >= 0.0 && margins.top() >= 0.0 && margins.right() >= 0.0 && margins.bottom() >= 0.0;
    if (!isValid)
    {
        return Helper::refuse(assembly, selection, edit, tr("The margins must be zero or positive."));
    }

    // Q17: the crop of PDF pages (an image and a blank page have none); with isSameSource all
    // pages of their sources in the items
    const std::vector<size_t> indices = Helper::getSelectedIndices(assembly.getItems(), selection);
    Data data = Helper::getData(assembly);
    std::set<int> sources;
    std::vector<Page*> pages;
    for (const size_t index : indices)
    {
        for (Page& page : data.items[index].pages)
        {
            if (page.plan.input >= 0 && data.sources[size_t(page.plan.input)].kind == SourceKind::Pdf)
            {
                sources.insert(page.plan.input);
                pages.push_back(&page);
            }
        }
    }
    if (pages.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("Crop applies to pages of PDF files."));
    }
    if (isSameSource)
    {
        pages.clear();
        for (Item& item : data.items)
        {
            for (Page& page : item.pages)
            {
                if (sources.count(page.plan.input))
                {
                    pages.push_back(&page);
                }
            }
        }
    }

    for (Page* page : pages)
    {
        const Source& source = data.sources[size_t(page->plan.input)];
        if (page->plan.pageIndex < PDFInteger(source.pageSizes.size()))
        {
            const QSizeF size = source.pageSizes[size_t(page->plan.pageIndex)];
            if (margins.left() + margins.right() >= size.width() || margins.top() + margins.bottom() >= size.height())
            {
                return Helper::refuse(assembly, selection, edit, tr("The margins leave nothing of page %1 of %2.").arg(page->plan.pageIndex + 1).arg(source.name));
            }
        }
        page->plan.cropMargins = margins;
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), Helper::getIds(assembly.getItems(), indices), edit);
}

PDFDocumentAssembly PDFDocumentAssembly::insertBlank(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    Data data = Helper::getData(assembly);
    const std::vector<size_t> indices = Helper::getSelectedIndices(data.items, selection);
    const size_t position = Helper::getInsertionIndex(data.items, indices);

    // The size and the orientation of the page before it as it is shown, A4 in an empty assembly
    QSizeF size;
    if (position > 0)
    {
        size = getPageSize(assembly, data.items[position - 1].pages.back());
    }
    size = size.isEmpty() ? Helper::getDefaultBlankPageSize() : PDFImagePageFactory::getBoundedSize(size);

    Page page = Helper::createPage(BLANK_INPUT, 0);
    page.plan.pageSize = size;
    Item item = Helper::createPageItem(&data, std::move(page));
    const QString itemId = item.id;
    data.items.insert(data.items.begin() + qsizetype(position), std::move(item));
    if (QString error; !Helper::checkLimits(data, &error))
    {
        return Helper::refuse(assembly, selection, edit, error);
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), { itemId }, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::insertPages(const PDFDocumentAssembly& assembly, const Selection& selection, const QString& sourceId, const std::vector<PDFInteger>& pages, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    Data data = Helper::getData(assembly);
    auto it = std::find_if(data.sources.begin(), data.sources.end(), [&sourceId](const Source& source) { return source.id == sourceId; });
    if (it == data.sources.end())
    {
        return Helper::refuse(assembly, selection, edit, tr("The file is not a source of the assembly."));
    }
    if (pages.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("No page is given."));
    }
    for (const PDFInteger page : pages)
    {
        if (page < 0 || page >= it->pageCount)
        {
            return Helper::refuse(assembly, selection, edit, tr("Page %1 does not exist – %2 has %Ln pages.", nullptr, int(it->pageCount)).arg(page + 1).arg(it->name));
        }
    }
    if (qint64(pages.size()) + Helper::getPageCount(data.items) > MAXIMUM_PAGE_COUNT)
    {
        return Helper::refuse(assembly, selection, edit, Helper::getPageLimitText());
    }

    it->isParked = false;
    const int input = int(std::distance(data.sources.begin(), it));
    std::vector<Item> items;
    items.reserve(pages.size());
    for (const PDFInteger page : pages)
    {
        items.push_back(Helper::createPageItem(&data, Helper::createPage(input, page)));
    }

    const Selection newSelection = Helper::getIds(items);
    const size_t position = Helper::getInsertionIndex(data.items, Helper::getSelectedIndices(data.items, selection));
    data.items.insert(data.items.begin() + qsizetype(position), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    if (QString error; !Helper::checkLimits(data, &error))
    {
        return Helper::refuse(assembly, selection, edit, error);
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), newSelection, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::replace(const PDFDocumentAssembly& assembly, const Selection& selection, const QString& sourceId, const std::vector<PDFInteger>& pages, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<size_t> indices = Helper::getSelectedIndices(assembly.getItems(), selection);
    if (indices.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("Select the pages to replace."));
    }

    // B20, B53: one operation - the new pages are inserted after the selection first; a refused
    // insertion changes nothing
    Edit insertion;
    const PDFDocumentAssembly inserted = insertPages(assembly, selection, sourceId, pages, &insertion);
    if (!insertion.errorMessage.isEmpty())
    {
        return Helper::refuse(assembly, selection, edit, insertion.errorMessage);
    }
    Edit removal;
    const PDFDocumentAssembly removed = remove(inserted, selection, &removal);
    if (!removal.errorMessage.isEmpty())
    {
        return Helper::refuse(assembly, selection, edit, removal.errorMessage);
    }
    return Helper::accept(removed, insertion.selection, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::remove(const PDFDocumentAssembly& assembly, const Selection& selection, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<size_t> indices = Helper::getSelectedIndices(assembly.getItems(), selection);
    if (indices.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("Select the pages to remove."));
    }

    // B21: each item remembers the item before it and its index among the items, which stay
    Data data = Helper::getData(assembly);
    for (size_t i = 0; i < indices.size(); ++i)
    {
        const size_t index = indices[i];
        RemovedItem removedItem;
        removedItem.item = data.items[index];
        removedItem.previousItemId = index > 0 ? data.items[index - 1].id : QString();
        removedItem.index = int(index - i);
        data.removedItems.push_back(std::move(removedItem));
    }
    data.items = Helper::replaceSelected(data.items, indices, { });
    return Helper::accept(Helper::createValue(assembly, std::move(data)), { }, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::restore(const PDFDocumentAssembly& assembly, const Selection& removedItemIds, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    QSet<QString> ids;
    for (const QString& id : removedItemIds)
    {
        ids.insert(id);
    }

    Data data = Helper::getData(assembly);
    std::vector<RemovedItem> restored;
    std::vector<RemovedItem> kept;
    PDFInteger invalidCount = 0;
    for (RemovedItem& removedItem : data.removedItems)
    {
        if (!ids.contains(removedItem.item.id))
        {
            kept.push_back(std::move(removedItem));
            continue;
        }

        // A page over the page count of a changed source cannot go back
        const bool isValid = std::all_of(removedItem.item.pages.cbegin(), removedItem.item.pages.cend(), [&data](const Page& page) { return Helper::isPageValid(data, page); });
        if (!isValid)
        {
            invalidCount += PDFInteger(removedItem.item.pages.size());
            kept.push_back(std::move(removedItem));
            continue;
        }
        restored.push_back(std::move(removedItem));
    }

    if (restored.empty())
    {
        return Helper::refuse(assembly, { }, edit, invalidCount > 0 ? tr("%Ln pages are no longer in their changed files.", nullptr, int(invalidCount)) : tr("Nothing to restore."));
    }

    Selection newSelection;
    for (const RemovedItem& removedItem : restored)
    {
        newSelection.push_back(removedItem.item.id);
        for (const Page& page : removedItem.item.pages)
        {
            if (page.plan.input >= 0)
            {
                data.sources[size_t(page.plan.input)].isParked = false;
            }
        }
    }
    data.removedItems = std::move(kept);
    Helper::placeRestoredItems(&data.items, std::move(restored));

    PDFDocumentAssembly result = Helper::createValue(assembly, std::move(data));
    if (edit)
    {
        edit->selection = std::move(newSelection);
        edit->errorMessage = invalidCount > 0 ? tr("%Ln pages are no longer in their changed files.", nullptr, int(invalidCount)) : QString();
    }
    return result;
}

PDFDocumentAssembly PDFDocumentAssembly::restoreAll(const PDFDocumentAssembly& assembly, Edit* edit)
{
    Selection ids;
    for (const RemovedItem& removedItem : assembly.getRemovedItems())
    {
        ids.push_back(removedItem.item.id);
    }
    return restore(assembly, ids, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::restoreCheckpoint(const PDFDocumentAssembly& assembly, const Checkpoint& checkpoint, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    // B51: the sources of a checkpoint are known (only parked); B22: the caller makes it an undo
    // step; the bin stays
    Data data = Helper::getData(assembly);

    // RB2: in the document host the pages of the target follow their page objects - the checkpoint
    // was made for an earlier version of the document (its page indices are of that version); a
    // page, whose object is gone, leaves the checkpoint
    const bool isTargetAssembly = !data.sources.empty() && data.sources.front().isTarget;
    std::map<PDFObjectReference, Page> targetPages;
    if (isTargetAssembly)
    {
        auto addTargetPages = [&targetPages](const Item& item)
        {
            for (const Page& page : item.pages)
            {
                if (page.plan.input == 0 && page.targetPage.isValid())
                {
                    targetPages.emplace(page.targetPage, page);
                }
            }
        };
        std::for_each(data.items.cbegin(), data.items.cend(), addTargetPages);
        for (const RemovedItem& removedItem : data.removedItems)
        {
            addTargetPages(removedItem.item);
        }
    }

    std::vector<Item> items;
    items.reserve(checkpoint.items.size());
    for (const Item& checkpointItem : checkpoint.items)
    {
        Item item = checkpointItem;
        if (isTargetAssembly)
        {
            std::vector<Page> pages;
            for (Page& page : item.pages)
            {
                if (page.plan.input != 0)
                {
                    pages.push_back(std::move(page));
                    continue;
                }
                auto current = targetPages.find(page.targetPage);
                if (current != targetPages.end())
                {
                    page.plan.pageIndex = current->second.plan.pageIndex;
                    page.targetVersion = current->second.targetVersion;
                    pages.push_back(std::move(page));
                }
            }
            item.pages = std::move(pages);
        }
        if (item.pages.empty())
        {
            continue;
        }
        for (const Page& page : item.pages)
        {
            if (!Helper::isPageValid(data, page))
            {
                return Helper::refuse(assembly, { }, edit, tr("The checkpoint %1 has a page, which is no longer in its file.").arg(checkpoint.name.left(MAXIMUM_NAME_LENGTH)));
            }
        }
        item.id = Helper::createItemId(&data);
        item.name = item.name.left(MAXIMUM_NAME_LENGTH);
        for (const Page& page : item.pages)
        {
            if (page.plan.input >= 0)
            {
                data.sources[size_t(page.plan.input)].isParked = false;
            }
        }
        items.push_back(std::move(item));
    }
    data.items = std::move(items);

    // RB2: every page of the target stays known - a page, which is neither in the restored items
    // nor in the bin (it was in the bin at the checkpoint and was restored since, or the document
    // got it later), goes to the bin; it is never removed without being listed there
    if (isTargetAssembly)
    {
        for (const Item& item : data.items)
        {
            for (const Page& page : item.pages)
            {
                targetPages.erase(page.targetPage);
            }
        }
        for (const RemovedItem& removedItem : data.removedItems)
        {
            for (const Page& page : removedItem.item.pages)
            {
                targetPages.erase(page.targetPage);
            }
        }

        // The page as it is in the document (no turn, crop or clone flag of an occurrence)
        std::vector<Page> missingPages;
        for (const auto& [reference, occurrence] : targetPages)
        {
            Page page = Helper::createPage(0, occurrence.plan.pageIndex);
            page.targetPage = reference;
            page.targetVersion = occurrence.targetVersion;
            missingPages.push_back(std::move(page));
        }
        std::sort(missingPages.begin(), missingPages.end(), [](const Page& left, const Page& right) { return left.plan.pageIndex < right.plan.pageIndex; });
        for (Page& page : missingPages)
        {
            RemovedItem removedItem;
            removedItem.item = Helper::createPageItem(&data, std::move(page));
            removedItem.previousItemId = data.items.empty() ? QString() : data.items.back().id;
            removedItem.index = int(data.items.size());
            data.removedItems.push_back(std::move(removedItem));
        }
    }
    if (QString error; !Helper::checkLimits(data, &error))
    {
        return Helper::refuse(assembly, { }, edit, error);
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), { }, edit);
}

PDFDocumentAssembly::Selection PDFDocumentAssembly::select(const PDFDocumentAssembly& assembly, const Selection& selection, SelectRule rule, const SelectSettings& settings)
{
    using Helper = PDFDocumentAssemblyHelper;

    const std::vector<Item>& items = assembly.getItems();
    std::vector<bool> isSelected(items.size(), false);
    for (const size_t index : Helper::getSelectedIndices(items, selection))
    {
        isSelected[index] = true;
    }

    std::vector<bool> result(items.size(), false);
    switch (rule)
    {
        case SelectRule::All:
            std::fill(result.begin(), result.end(), true);
            break;

        case SelectRule::None:
            break;

        case SelectRule::Invert:
            for (size_t i = 0; i < items.size(); ++i)
            {
                result[i] = !isSelected[i];
            }
            break;

        case SelectRule::PageRange:
        {
            // Positions in the assembly, or the original pages of one source (387:15926)
            int input = -1;
            PDFInteger pageCount = Helper::getPageCount(items);
            if (!settings.sourceId.isEmpty())
            {
                const auto& sources = assembly.getSources();
                auto it = std::find_if(sources.cbegin(), sources.cend(), [&settings](const Source& source) { return source.id == settings.sourceId; });
                if (it == sources.cend())
                {
                    return selection;
                }
                input = int(std::distance(sources.cbegin(), it));
                pageCount = it->pageCount;
            }
            QString error;
            const std::optional<PDFPageRangeList> range = PDFPageRangeList::parse(settings.pageRange, pageCount, &error);
            if (!range)
            {
                return selection;
            }
            const std::set<PDFInteger> pages(range->getPageIndices().cbegin(), range->getPageIndices().cend());
            PDFInteger position = 0;
            for (size_t i = 0; i < items.size(); ++i)
            {
                for (const Page& page : items[i].pages)
                {
                    const bool isInRange = input < 0 ? pages.count(position) > 0 : (page.plan.input == input && pages.count(page.plan.pageIndex) > 0);
                    result[i] = result[i] || isInRange;
                    ++position;
                }
                result[i] = result[i] || (settings.isAdded && isSelected[i]);
            }
            break;
        }

        case SelectRule::Odd:
        case SelectRule::Even:
        {
            // QB5: the positions in the assembly; every page of the item
            const PDFInteger remainder = rule == SelectRule::Odd ? 0 : 1;
            PDFInteger position = 0;
            for (size_t i = 0; i < items.size(); ++i)
            {
                bool isMatch = true;
                for (size_t j = 0; j < items[i].pages.size(); ++j, ++position)
                {
                    isMatch = isMatch && position % 2 == remainder;
                }
                result[i] = isMatch;
            }
            break;
        }

        case SelectRule::Portrait:
        case SelectRule::Landscape:
            for (size_t i = 0; i < items.size(); ++i)
            {
                result[i] = Helper::isOrientation(assembly, items[i], rule == SelectRule::Portrait);
            }
            break;

        case SelectRule::SearchResults:
        {
            const QStringList words = settings.searchText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (size_t i = 0; i < items.size() && !words.isEmpty(); ++i)
            {
                result[i] = Helper::isSearchMatch(assembly, items[i], words);
            }
            break;
        }
    }

    Selection ids;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (result[i])
        {
            ids.push_back(items[i].id);
        }
    }
    return ids;
}

PDFDocumentAssembly::Plan PDFDocumentAssembly::toPlan(const PDFDocumentAssembly& assembly)
{
    using Helper = PDFDocumentAssemblyHelper;

    // QB3 b: a group is a part with its name; outside of groups a run of the pages of one source;
    // a lone image or blank page belongs to the part before it (at the beginning to the part after
    // it); a renamed single page starts no part
    const std::vector<Source>& sources = assembly.getSources();
    Plan plan;
    plan.items.reserve(size_t(Helper::getPageCount(assembly.getItems())));
    plan.itemOfPage.reserve(plan.items.capacity());

    struct Run
    {
        size_t part = 0;
        int input = -1;
        std::vector<PDFInteger> pages;
    };
    std::vector<Run> runs;
    std::map<int, int> runsOfSource;
    int runInput = -1;      ///< The source of the run, which continues with its next page; -1 = none

    for (size_t itemIndex = 0; itemIndex < assembly.getItems().size(); ++itemIndex)
    {
        const Item& item = assembly.getItems()[itemIndex];
        if (item.isGroup())
        {
            PDFDocumentMerger::Part part;
            part.firstItem = plan.items.size();
            part.title = getItemName(assembly, item);
            plan.parts.push_back(std::move(part));
            runInput = -1;
        }

        for (const Page& page : item.pages)
        {
            if (!item.isGroup())
            {
                const bool isDocumentPage = page.plan.input >= 0 && sources[size_t(page.plan.input)].kind == SourceKind::Pdf;
                if (isDocumentPage)
                {
                    if (runInput != page.plan.input)
                    {
                        PDFDocumentMerger::Part part;
                        part.firstItem = plan.items.size();
                        plan.parts.push_back(std::move(part));
                        runs.push_back({ plan.parts.size() - 1, page.plan.input, { } });
                        ++runsOfSource[page.plan.input];
                        runInput = page.plan.input;
                    }
                    runs.back().pages.push_back(page.plan.pageIndex);
                }
                else
                {
                    runInput = -1;
                }
            }
            plan.items.push_back(page.plan);
            plan.itemOfPage.push_back(int(itemIndex));
        }
    }

    for (const Run& run : runs)
    {
        const Source& source = sources[size_t(run.input)];
        plan.parts[run.part].title = Helper::getPagesTitle(source, run.pages, runsOfSource[run.input] == 1);
    }

    // The images and blank pages at the beginning belong to the first part; without any part
    // (only images and blank pages) the result is one part
    if (!plan.items.empty())
    {
        if (plan.parts.empty())
        {
            PDFDocumentMerger::Part part;
            part.title = getItemName(assembly, assembly.getItems().front());
            plan.parts.push_back(std::move(part));
        }
        plan.parts.front().firstItem = 0;
    }
    return plan;
}

PDFDocumentAssembly::Summary PDFDocumentAssembly::getSummary(const PDFDocumentAssembly& assembly)
{
    using Helper = PDFDocumentAssemblyHelper;

    Summary summary;
    const std::vector<Source>& sources = assembly.getSources();
    for (const Source& source : sources)
    {
        if (!source.isParked)
        {
            ++summary.sourceCount;
            summary.sourcePageCount += source.pageCount;
        }
    }

    std::set<std::pair<int, PDFInteger>> usedPages;
    qint64 estimatedSize = 0;
    bool isSizeKnown = true;
    for (const Item& item : assembly.getItems())
    {
        for (const Page& page : item.pages)
        {
            ++summary.resultPageCount;
            if (page.plan.input < 0)
            {
                estimatedSize += Helper::BLANK_PAGE_BYTES;
                continue;
            }

            // KB5: a page of a PDF = the file size / its pages, a frame of an image = the bytes of the image / its frames
            const Source& source = sources[size_t(page.plan.input)];
            usedPages.emplace(page.plan.input, page.plan.pageIndex);
            const qint64 bytes = source.kind == SourceKind::Image && !source.imageData.isEmpty() ? qint64(source.imageData.size()) : source.size;
            if (bytes < 0 || source.pageCount <= 0)
            {
                isSizeKnown = false;
            }
            else
            {
                estimatedSize += bytes / source.pageCount;
            }
        }
    }
    summary.usedPageCount = qint64(usedPages.size());
    for (const RemovedItem& removedItem : assembly.getRemovedItems())
    {
        summary.removedPageCount += qint64(removedItem.item.pages.size());
    }
    summary.itemCount = int(assembly.getItems().size());
    summary.partCount = int(toPlan(assembly).parts.size());
    summary.estimatedSize = isSizeKnown ? estimatedSize : -1;
    return summary;
}

PDFDocumentAssembly PDFDocumentAssembly::createForDocument(const PDFDocument* document, quint64 documentVersion, const QString& name)
{
    using Helper = PDFDocumentAssemblyHelper;

    if (!document)
    {
        return PDFDocumentAssembly();
    }

    Data data;
    Source source = createDocumentSource(document, name, QString());
    source.isTarget = true;
    source.id = Helper::createSourceId(&data);
    data.sources.push_back(std::move(source));

    const std::vector<PDFObjectReference> references = Helper::getPageReferences(document);
    data.items.reserve(references.size());
    for (size_t i = 0; i < references.size(); ++i)
    {
        Page page = Helper::createPage(0, PDFInteger(i));
        page.targetPage = references[i];
        page.targetVersion = documentVersion;
        data.items.push_back(Helper::createPageItem(&data, std::move(page)));
    }
    return Helper::createNewInstance(std::move(data));
}

PDFDocumentAssembly PDFDocumentAssembly::mapToDocument(const PDFDocumentAssembly& assembly, const PDFDocument* document, quint64 documentVersion, PDFInteger* addedPageCount)
{
    using Helper = PDFDocumentAssemblyHelper;

    PDFInteger dummy = 0;
    PDFInteger& added = addedPageCount ? *addedPageCount : dummy;
    added = 0;

    const std::vector<Source>& sources = assembly.getSources();
    if (!document || sources.empty() || !sources.front().isTarget)
    {
        return assembly;
    }

    // RB2: the pages follow their page objects
    const std::vector<PDFObjectReference> references = Helper::getPageReferences(document);
    std::map<PDFObjectReference, PDFInteger> indices;
    for (size_t i = 0; i < references.size(); ++i)
    {
        indices.emplace(references[i], PDFInteger(i));
    }

    Data data = Helper::getData(assembly);
    bool isChanged = false;
    std::set<PDFObjectReference> knownPages;
    auto mapPages = [&](std::vector<Page>* pages)
    {
        std::vector<Page> mapped;
        mapped.reserve(pages->size());
        for (Page& page : *pages)
        {
            if (page.plan.input != 0)
            {
                mapped.push_back(std::move(page));
                continue;
            }
            auto it = indices.find(page.targetPage);
            if (it == indices.end())
            {
                isChanged = true;
                continue;
            }
            knownPages.insert(page.targetPage);
            isChanged = isChanged || page.plan.pageIndex != it->second || page.targetVersion != documentVersion;
            page.plan.pageIndex = it->second;
            page.targetVersion = documentVersion;
            mapped.push_back(std::move(page));
        }
        *pages = std::move(mapped);
    };

    std::vector<Item> items;
    for (Item& item : data.items)
    {
        mapPages(&item.pages);
        if (!item.pages.empty())
        {
            items.push_back(std::move(item));
        }
    }
    std::vector<RemovedItem> removedItems;
    for (RemovedItem& removedItem : data.removedItems)
    {
        mapPages(&removedItem.item.pages);
        if (!removedItem.item.pages.empty())
        {
            removedItems.push_back(std::move(removedItem));
        }
    }
    data.items = std::move(items);
    data.removedItems = std::move(removedItems);

    // The new pages of the document at the end
    for (size_t i = 0; i < references.size(); ++i)
    {
        if (!knownPages.count(references[i]))
        {
            Page page = Helper::createPage(0, PDFInteger(i));
            page.targetPage = references[i];
            page.targetVersion = documentVersion;
            data.items.push_back(Helper::createPageItem(&data, std::move(page)));
            ++added;
            isChanged = true;
        }
    }

    Source target = createDocumentSource(document, data.sources.front().name, data.sources.front().path);
    isChanged = isChanged || target.pageCount != data.sources.front().pageCount || target.pageSizes != data.sources.front().pageSizes;
    data.sources.front().pageCount = target.pageCount;
    data.sources.front().pageSizes = std::move(target.pageSizes);
    if (!isChanged)
    {
        return assembly;
    }
    return Helper::createValue(assembly, std::move(data));
}

PDFDocumentAssembly::Source PDFDocumentAssembly::createDocumentSource(const PDFDocument* document, const QString& name, const QString& path)
{
    Source source;
    source.kind = SourceKind::Pdf;
    source.name = name.left(MAXIMUM_NAME_LENGTH);
    source.path = path;
    if (document)
    {
        const PDFCatalog* catalog = document->getCatalog();
        source.pageCount = PDFInteger(catalog->getPageCount());
        source.pageSizes.reserve(catalog->getPageCount());
        for (size_t i = 0; i < catalog->getPageCount(); ++i)
        {
            source.pageSizes.push_back(PDFDocumentAssemblyHelper::getShownPageSize(catalog->getPage(i)));
        }
    }
    return source;
}

PDFDocumentAssembly::Source PDFDocumentAssembly::createImageSource(const QByteArray& data, const QByteArray& format, const QString& name, const QString& path, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    Source source;
    source.kind = SourceKind::Image;
    source.name = name.left(MAXIMUM_NAME_LENGTH);
    source.path = path;
    source.imageData = data;
    source.size = qint64(data.size());

    const PDFImagePageFactory::ImageInfo info = PDFImagePageFactory::readInfo(data, format);
    if (info.frameCount <= 0)
    {
        error = info.error;
        return source;
    }
    source.imageFormat = info.format;

    // The size of a page by the resolution of the image (Q6 of step 2); the frames of a TIFF take
    // the size of the first frame
    const std::optional<PDFImagePageFactory::Frame> frame = PDFImagePageFactory::readFrame(data, info.format, 0, 0.0, &error);
    if (!frame)
    {
        return source;
    }
    source.pageCount = info.frameCount;
    source.pageSizes.assign(size_t(info.frameCount), PDFImagePageFactory::getPageSize(*frame));
    return source;
}

PDFDocumentAssembly PDFDocumentAssembly::updateSource(const PDFDocumentAssembly& assembly, Source source, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    Data data = Helper::getData(assembly);
    auto it = std::find_if(data.sources.begin(), data.sources.end(), [&source](const Source& existing) { return existing.id == source.id; });
    if (it == data.sources.end())
    {
        return Helper::refuse(assembly, { }, edit, tr("The file is not a source of the assembly."));
    }
    if (it->kind == source.kind && source.pageCount > MAXIMUM_PAGE_COUNT)
    {
        return Helper::refuse(assembly, { }, edit, Helper::getSourcePageLimitText(source.name));
    }
    if (it->kind != source.kind || source.pageCount < 0)
    {
        return Helper::refuse(assembly, { }, edit, tr("The file %1 cannot replace the source.").arg(source.name.left(MAXIMUM_NAME_LENGTH)));
    }

    source.isTarget = it->isTarget;
    source.isParked = it->isParked;
    source.name = source.name.left(MAXIMUM_NAME_LENGTH);
    const int input = int(std::distance(data.sources.begin(), it));
    const PDFInteger pageCount = source.pageCount;
    *it = std::move(source);

    // 387:16980 Use Changed File: the pages over the new count go to the bin
    Helper::moveMatchingPagesToBin(&data, [input, pageCount](const Page& page) { return page.plan.input == input && page.plan.pageIndex >= pageCount; });
    return Helper::accept(Helper::createValue(assembly, std::move(data)), { }, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::removeSource(const PDFDocumentAssembly& assembly, const QString& sourceId, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    Data data = Helper::getData(assembly);
    auto it = std::find_if(data.sources.begin(), data.sources.end(), [&sourceId](const Source& existing) { return existing.id == sourceId; });
    if (it == data.sources.end())
    {
        return Helper::refuse(assembly, { }, edit, tr("The file is not a source of the assembly."));
    }
    if (it->isTarget)
    {
        return Helper::refuse(assembly, { }, edit, tr("The document of the tab cannot be removed."));
    }

    it->isParked = true;
    const int input = int(std::distance(data.sources.begin(), it));
    Helper::moveMatchingPagesToBin(&data, [input](const Page& page) { return page.plan.input == input; });
    return Helper::accept(Helper::createValue(assembly, std::move(data)), { }, edit);
}

PDFDocumentAssembly PDFDocumentAssembly::insertItems(const PDFDocumentAssembly& assembly, const Selection& selection, std::vector<Item> items, Edit* edit)
{
    using Helper = PDFDocumentAssemblyHelper;

    Data data = Helper::getData(assembly);
    if (items.empty())
    {
        return Helper::refuse(assembly, selection, edit, tr("Nothing to paste."));
    }
    for (Item& item : items)
    {
        const bool isValid = !item.pages.empty() && item.name.size() <= MAXIMUM_NAME_LENGTH &&
                             std::all_of(item.pages.cbegin(), item.pages.cend(), [&data](const Page& page) { return Helper::isPageValid(data, page); });
        if (!isValid)
        {
            return Helper::refuse(assembly, selection, edit, tr("The pages cannot be pasted into this assembly."));
        }
        item.id = Helper::createItemId(&data);
        for (const Page& page : item.pages)
        {
            if (page.plan.input >= 0)
            {
                data.sources[size_t(page.plan.input)].isParked = false;
            }
        }
    }

    const Selection newSelection = Helper::getIds(items);
    const size_t position = Helper::getInsertionIndex(data.items, Helper::getSelectedIndices(data.items, selection));
    data.items.insert(data.items.begin() + qsizetype(position), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    if (QString error; !Helper::checkLimits(data, &error))
    {
        return Helper::refuse(assembly, selection, edit, error);
    }
    return Helper::accept(Helper::createValue(assembly, std::move(data)), newSelection, edit);
}

PDFDocumentAssembly::Checkpoint PDFDocumentAssembly::createCheckpoint(const PDFDocumentAssembly& assembly, const QString& name)
{
    Checkpoint checkpoint;
    checkpoint.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    checkpoint.name = name.trimmed().left(MAXIMUM_NAME_LENGTH);
    checkpoint.time = QDateTime::currentDateTimeUtc();
    checkpoint.items = assembly.getItems();
    return checkpoint;
}

bool PDFDocumentAssembly::checkLimits(const PDFDocumentAssembly& assembly, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    return PDFDocumentAssemblyHelper::checkLimits(*assembly.m_data, &error);
}

bool PDFDocumentAssembly::checkDocumentPageCount(PDFInteger pageCount, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();
    if (pageCount > MAXIMUM_PAGE_COUNT)
    {
        error = tr("The document has more than %Ln pages, the most an assembly can have.", nullptr, int(MAXIMUM_PAGE_COUNT));
        return false;
    }
    return true;
}

QString PDFDocumentAssembly::getItemName(const PDFDocumentAssembly& assembly, const Item& item)
{
    if (!item.name.isEmpty())
    {
        return item.name;
    }
    if (item.pages.empty())
    {
        return QString();
    }

    const int input = item.pages.front().plan.input;
    const bool isOneSource = std::all_of(item.pages.cbegin(), item.pages.cend(), [input](const Page& page) { return page.plan.input == input; });
    if (!isOneSource)
    {
        return tr("%Ln pages", nullptr, int(item.pages.size()));
    }
    if (input < 0)
    {
        return item.pages.size() == 1 ? tr("Blank page") : tr("%Ln blank pages", nullptr, int(item.pages.size()));
    }

    std::vector<PDFInteger> pages;
    for (const Page& page : item.pages)
    {
        pages.push_back(page.plan.pageIndex);
    }
    return PDFDocumentAssemblyHelper::getPagesTitle(assembly.getSources()[size_t(input)], std::move(pages), false);
}

QSizeF PDFDocumentAssembly::getPageSize(const PDFDocumentAssembly& assembly, const Page& page)
{
    QSizeF size;
    if (page.plan.input < 0)
    {
        size = page.plan.pageSize;
    }
    else if (size_t(page.plan.input) < assembly.getSources().size())
    {
        const Source& source = assembly.getSources()[size_t(page.plan.input)];
        if (page.plan.pageIndex >= 0 && page.plan.pageIndex < PDFInteger(source.pageSizes.size()))
        {
            size = source.pageSizes[size_t(page.plan.pageIndex)];
        }
        if (source.kind == SourceKind::Pdf)
        {
            size = QSizeF(size.width() - page.plan.cropMargins.left() - page.plan.cropMargins.right(),
                          size.height() - page.plan.cropMargins.top() - page.plan.cropMargins.bottom());
        }
    }
    if (size.isEmpty())
    {
        return QSizeF();
    }
    if (page.plan.turn == PageRotation::Rotate90 || page.plan.turn == PageRotation::Rotate270)
    {
        size.transpose();
    }
    return size;
}

std::vector<PDFObjectReference> PDFDocumentAssembly::getTargetPages(const PDFDocumentAssembly& assembly)
{
    const std::vector<Source>& sources = assembly.getSources();
    if (sources.empty() || !sources.front().isTarget)
    {
        return { };
    }

    std::vector<PDFObjectReference> pages(size_t(sources.front().pageCount));
    auto addPages = [&pages](const Item& item)
    {
        for (const Page& page : item.pages)
        {
            if (page.plan.input == 0 && page.plan.pageIndex >= 0 && page.plan.pageIndex < PDFInteger(pages.size()) && page.targetPage.isValid())
            {
                pages[size_t(page.plan.pageIndex)] = page.targetPage;
            }
        }
    };
    for (const Item& item : assembly.getItems())
    {
        addPages(item);
    }
    for (const RemovedItem& removedItem : assembly.getRemovedItems())
    {
        addPages(removedItem.item);
    }
    return pages;
}

}   // namespace pdf
