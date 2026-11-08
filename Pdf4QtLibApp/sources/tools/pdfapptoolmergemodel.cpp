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

#include "pdfapptoolmergemodel.h"

#include "pdfapplocale.h"
#include "pdfappworkspace.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolpagegrid.h"

#include "pdfcatalog.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttool_pages.h"
#include "pdfimagepagefactory.h"
#include "pdfsecurityhandler.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"

#include <QIcon>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace pdf
{

/// Helpers of the plan and of the model of the pages of Merge
class PDFAppToolMergeModelHelper
{
public:
    PDFAppToolMergeModelHelper() = delete;

    /// Extent of the dot of a source of Details (408:32003) and of a group header (378:1244)
    static constexpr int DOT = 8;

    /// Tolerance of a named size in points (Letter is 612 x 792, A4 595.276 x 841.890)
    static constexpr double NAMED_SIZE_TOLERANCE = 1.0;

    /// Position of the input in the list of the ids, -1 for none
    static int indexOf(const QStringList& inputIds, const QString& inputId)
    {
        return int(inputIds.indexOf(inputId));
    }

    /// Positions of the inputs by their ids
    static std::map<QString, int> createPositions(const QStringList& inputIds)
    {
        std::map<QString, int> positions;
        for (int i = 0; i < inputIds.size(); ++i)
        {
            positions.emplace(inputIds[i], i);
        }
        return positions;
    }

    /// Position of the input, after every input for an unknown one
    static int getPosition(const std::map<QString, int>& positions, const QString& inputId)
    {
        const auto it = positions.find(inputId);
        return it != positions.cend() ? it->second : std::numeric_limits<int>::max();
    }

    /// Sorted unique indices, which are valid for a vector of \p count items
    static std::vector<size_t> normalized(std::vector<size_t> indices, size_t count)
    {
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        while (!indices.empty() && indices.back() >= count)
        {
            indices.pop_back();
        }
        return indices;
    }

    /// Orders the items by their keys; equal keys keep their order (the original index is the
    /// last part of the key, so the sort is stable without a comparator)
    static std::vector<PDFAppToolMergePlan::Item> orderedByKeys(const std::vector<PDFAppToolMergePlan::Item>& items, const std::vector<std::pair<PDFInteger, int>>& keys)
    {
        std::vector<std::tuple<PDFInteger, int, size_t>> order;
        order.reserve(items.size());
        for (size_t i = 0; i < items.size(); ++i)
        {
            order.emplace_back(keys[i].first, keys[i].second, i);
        }
        std::sort(order.begin(), order.end());

        std::vector<PDFAppToolMergePlan::Item> result;
        result.reserve(items.size());
        for (const auto& [first, second, index] : order)
        {
            result.push_back(items[index]);
        }
        return result;
    }

    /// Appends the pages of a chunk of the text of a plan (the syntax of the core, between two
    /// pending inputs) to the items; \returns false for an invalid chunk (\p error says why)
    static bool appendChunk(QStringList* chunk, const QStringList& inputIds, const std::vector<PDFInteger>& pageCounts, std::vector<PDFAppToolMergePlan::Item>* items, QString* error)
    {
        if (chunk->isEmpty())
        {
            return true;
        }

        const std::optional<std::vector<PDFDocumentToolMerge::PlanItem>> planItems = PDFDocumentToolMerge::parsePlan(chunk->join(QLatin1Char(',')), pageCounts, error);
        chunk->clear();
        if (!planItems)
        {
            return false;
        }
        for (const PDFDocumentToolMerge::PlanItem& planItem : *planItems)
        {
            PDFAppToolMergePlan::Item item;
            item.inputId = inputIds[planItem.input];
            item.pageIndex = planItem.pageIndex;
            item.turn = planItem.turn;
            items->push_back(item);
        }
        return true;
    }

    /// Millimeters of points
    static double getMillimeters(double points)
    {
        return points * 25.4 / 72.0;
    }

    /// Size of a page in points as it is shown (the crop box rotated by /Rotate, the user unit)
    static QSizeF getShownPageSize(const PDFPage* page)
    {
        const QSizeF size = page->getRotatedCropBox().size();
        return QSizeF(size.width() * page->getUserUnit(), size.height() * page->getUserUnit());
    }
};

// ------------------------------------------------------------- PDFAppToolMergePlan

PDFAppToolMergePlan::PDFAppToolMergePlan(std::vector<Item> items) :
    m_items(std::move(items))
{

}

PDFInteger PDFAppToolMergePlan::getPageCount() const
{
    PDFInteger count = 0;
    for (const Item& item : m_items)
    {
        count += item.isPending ? 0 : 1;
    }
    return count;
}

bool PDFAppToolMergePlan::hasPages() const
{
    for (const Item& item : m_items)
    {
        if (!item.isPending)
        {
            return true;
        }
    }
    return false;
}

bool PDFAppToolMergePlan::isClone(size_t itemIndex) const
{
    if (itemIndex >= m_items.size() || m_items[itemIndex].isPending)
    {
        return false;
    }

    const Item& item = m_items[itemIndex];
    for (size_t i = 0; i < itemIndex; ++i)
    {
        if (!m_items[i].isPending && m_items[i].pageIndex == item.pageIndex && m_items[i].inputId == item.inputId)
        {
            return true;
        }
    }
    return false;
}

PDFAppToolMergePlan PDFAppToolMergePlan::createDefault(const QStringList& inputIds)
{
    std::vector<Item> items;
    items.reserve(size_t(inputIds.size()));
    for (const QString& inputId : inputIds)
    {
        Item item;
        item.inputId = inputId;
        item.isPending = true;
        items.push_back(item);
    }
    return PDFAppToolMergePlan(std::move(items));
}

QString PDFAppToolMergePlan::toText(const QStringList& inputIds, bool withPending) const
{
    using Helper = PDFAppToolMergeModelHelper;

    // The clones are derived from the order: the first occurrence of a page is the page
    const std::map<QString, int> positions = Helper::createPositions(inputIds);
    std::set<std::pair<int, PDFInteger>> usedPages;
    std::vector<PDFDocumentToolMerge::PlanItem> run;
    QStringList parts;
    for (const Item& item : m_items)
    {
        const int input = Helper::getPosition(positions, item.inputId);
        if (input == std::numeric_limits<int>::max())
        {
            continue;
        }

        if (item.isPending)
        {
            if (withPending)
            {
                if (!run.empty())
                {
                    parts << PDFDocumentToolMerge::createPlan(run);
                    run.clear();
                }
                parts << PDFDocumentToolMerge::getInputLetters(input) + QLatin1Char(PENDING_MARK);
            }
            continue;
        }

        PDFDocumentToolMerge::PlanItem planItem;
        planItem.input = input;
        planItem.pageIndex = item.pageIndex;
        planItem.turn = item.turn;
        planItem.isClone = !usedPages.emplace(input, item.pageIndex).second;
        run.push_back(planItem);
    }

    if (!run.empty())
    {
        parts << PDFDocumentToolMerge::createPlan(run);
    }
    return parts.join(QLatin1Char(','));
}

std::optional<PDFAppToolMergePlan> PDFAppToolMergePlan::fromText(const QString& text, const QStringList& inputIds, const std::vector<PDFInteger>& pageCounts, QString* errorMessage)
{
    QString dummy;
    QString& error = errorMessage ? *errorMessage : dummy;
    error.clear();

    if (text.trimmed().isEmpty())
    {
        return createDefault(inputIds);
    }
    if (text.size() > PDFDocumentToolMerge::MAXIMUM_PLAN_LENGTH)
    {
        error = tr("The plan of the pages is too long.");
        return std::nullopt;
    }

    // The pages of an input, which are not known yet, are no pages of the core
    std::vector<PDFInteger> knownCounts(size_t(inputIds.size()), 0);
    for (size_t i = 0; i < knownCounts.size() && i < pageCounts.size(); ++i)
    {
        knownCounts[i] = qMax<PDFInteger>(pageCounts[i], 0);
    }

    // A chunk of pages between two pending inputs is a plan of the core
    using Helper = PDFAppToolMergeModelHelper;
    std::vector<Item> items;
    QStringList chunk;
    for (const QString& rawPart : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
        const QString part = rawPart.trimmed();
        if (part.endsWith(QLatin1Char(PENDING_MARK)))
        {
            if (!Helper::appendChunk(&chunk, inputIds, knownCounts, &items, &error))
            {
                return std::nullopt;
            }

            const int input = PDFDocumentToolMerge::getInputIndex(QStringView(part).chopped(1));
            if (input < 0 || input >= inputIds.size())
            {
                error = tr("The plan of the pages names a file, which is not in the list.");
                return std::nullopt;
            }

            Item item;
            item.inputId = inputIds[input];
            item.isPending = true;
            items.push_back(item);
        }
        else if (!part.isEmpty())
        {
            chunk << part;
        }
    }
    if (!Helper::appendChunk(&chunk, inputIds, knownCounts, &items, &error))
    {
        return std::nullopt;
    }
    return PDFAppToolMergePlan(std::move(items));
}

PDFAppToolMergePlan PDFAppToolMergePlan::resolved(const QStringList& inputIds, const std::vector<PDFInteger>& pageCounts) const
{
    using Helper = PDFAppToolMergeModelHelper;

    const std::map<QString, int> positions = Helper::createPositions(inputIds);
    std::vector<Item> items;
    items.reserve(m_items.size());
    for (const Item& item : m_items)
    {
        const int input = Helper::getPosition(positions, item.inputId);
        if (input == std::numeric_limits<int>::max())
        {
            continue;
        }

        const PDFInteger pageCount = size_t(input) < pageCounts.size() ? pageCounts[size_t(input)] : -1;
        if (item.isPending)
        {
            if (pageCount > 0)
            {
                // The pages are known: the pending input becomes all of its pages
                for (PDFInteger page = 0; page < pageCount; ++page)
                {
                    Item pageItem;
                    pageItem.inputId = item.inputId;
                    pageItem.pageIndex = page;
                    items.push_back(pageItem);
                }
            }
            else
            {
                items.push_back(item);
            }
        }
        else if (item.pageIndex >= 0 && item.pageIndex < pageCount)
        {
            items.push_back(item);
        }
    }
    return PDFAppToolMergePlan(std::move(items));
}

void PDFAppToolMergePlan::appendInput(const QString& inputId)
{
    Item item;
    item.inputId = inputId;
    item.isPending = true;
    m_items.push_back(item);
}

void PDFAppToolMergePlan::removeInput(const QString& inputId)
{
    std::vector<Item> items;
    items.reserve(m_items.size());
    for (const Item& item : m_items)
    {
        if (item.inputId != inputId)
        {
            items.push_back(item);
        }
    }
    m_items = std::move(items);
}

void PDFAppToolMergePlan::removeItems(const std::vector<size_t>& itemIndices)
{
    const std::vector<size_t> indices = PDFAppToolMergeModelHelper::normalized(itemIndices, m_items.size());
    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
    {
        m_items.erase(m_items.begin() + std::ptrdiff_t(*it));
    }
}

std::vector<size_t> PDFAppToolMergePlan::cloneItems(const std::vector<size_t>& itemIndices)
{
    const std::vector<size_t> indices = PDFAppToolMergeModelHelper::normalized(itemIndices, m_items.size());
    std::vector<size_t> clones;
    size_t shift = 0;
    for (const size_t index : indices)
    {
        const size_t position = index + shift;
        if (m_items[position].isPending)
        {
            continue;
        }

        const Item copy = m_items[position];
        m_items.insert(m_items.begin() + std::ptrdiff_t(position + 1), copy);
        clones.push_back(position + 1);
        ++shift;
    }
    return clones;
}

void PDFAppToolMergePlan::rotateItems(const std::vector<size_t>& itemIndices, bool isClockwise)
{
    for (const size_t index : PDFAppToolMergeModelHelper::normalized(itemIndices, m_items.size()))
    {
        Item& item = m_items[index];
        if (!item.isPending)
        {
            item.turn = isClockwise ? getPageRotationRotatedRight(item.turn) : getPageRotationRotatedLeft(item.turn);
        }
    }
}

std::vector<size_t> PDFAppToolMergePlan::moveItems(const std::vector<size_t>& itemIndices, size_t targetIndex)
{
    const std::vector<size_t> indices = PDFAppToolMergeModelHelper::normalized(itemIndices, m_items.size());
    if (indices.empty())
    {
        return indices;
    }

    // The moved items leave, the target is counted among the items, which stay
    std::vector<Item> moved;
    std::vector<Item> kept;
    moved.reserve(indices.size());
    kept.reserve(m_items.size() - indices.size());
    size_t insertion = 0;
    size_t next = 0;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (next < indices.size() && indices[next] == i)
        {
            moved.push_back(m_items[i]);
            ++next;
        }
        else
        {
            if (i < targetIndex)
            {
                ++insertion;
            }
            kept.push_back(m_items[i]);
        }
    }

    kept.insert(kept.begin() + std::ptrdiff_t(insertion), moved.cbegin(), moved.cend());
    m_items = std::move(kept);

    std::vector<size_t> result(moved.size());
    for (size_t i = 0; i < moved.size(); ++i)
    {
        result[i] = insertion + i;
    }
    return result;
}

void PDFAppToolMergePlan::orderByInputs(const QStringList& inputIds)
{
    using Helper = PDFAppToolMergeModelHelper;

    const std::map<QString, int> positions = Helper::createPositions(inputIds);
    std::vector<std::pair<PDFInteger, int>> keys;
    keys.reserve(m_items.size());
    for (const Item& item : m_items)
    {
        keys.emplace_back(Helper::getPosition(positions, item.inputId), 0);
    }
    m_items = Helper::orderedByKeys(m_items, keys);
}

void PDFAppToolMergePlan::sortByPageNumber(const QStringList& inputIds)
{
    using Helper = PDFAppToolMergeModelHelper;

    // K13: the original page numbers, equal numbers by the order of the files (as PageMaster);
    // the pending inputs have no numbers yet, they go last
    const std::map<QString, int> positions = Helper::createPositions(inputIds);
    std::vector<std::pair<PDFInteger, int>> keys;
    keys.reserve(m_items.size());
    for (const Item& item : m_items)
    {
        keys.emplace_back(item.isPending ? std::numeric_limits<PDFInteger>::max() : item.pageIndex, Helper::getPosition(positions, item.inputId));
    }
    m_items = Helper::orderedByKeys(m_items, keys);
}

void PDFAppToolMergePlan::reverseRuns()
{
    // The runs of one input (the groups) in the reverse order, each run keeps its pages
    std::vector<std::pair<size_t, size_t>> runs;
    for (size_t i = 0; i < m_items.size();)
    {
        size_t end = i + 1;
        while (!m_items[i].isPending && end < m_items.size() && !m_items[end].isPending && m_items[end].inputId == m_items[i].inputId)
        {
            ++end;
        }
        runs.emplace_back(i, end);
        i = end;
    }

    std::vector<Item> items;
    items.reserve(m_items.size());
    for (auto it = runs.rbegin(); it != runs.rend(); ++it)
    {
        items.insert(items.end(), m_items.cbegin() + std::ptrdiff_t(it->first), m_items.cbegin() + std::ptrdiff_t(it->second));
    }
    m_items = std::move(items);
}

// ------------------------------------------------------- PDFAppToolMergePageModel

PDFAppToolMergePageModel::PDFAppToolMergePageModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolMergePageModel::~PDFAppToolMergePageModel() = default;

void PDFAppToolMergePageModel::setInputList(PDFAppToolInputList* list)
{
    beginResetModel();
    m_list = list;
    createRows();
    endResetModel();
    Q_EMIT groupsChanged();
}

PDFAppToolInputList* PDFAppToolMergePageModel::getInputList() const
{
    return m_list.data();
}

void PDFAppToolMergePageModel::setPlan(PDFAppToolMergePlan plan)
{
    beginResetModel();
    m_plan = std::move(plan);
    createRows();
    endResetModel();
    Q_EMIT groupsChanged();
}

void PDFAppToolMergePageModel::setLeftOutInputs(const QStringList& inputIds)
{
    if (m_leftOutInputs != inputIds)
    {
        m_leftOutInputs = inputIds;
        updateGroups();
    }
}

void PDFAppToolMergePageModel::updateGroups()
{
    std::vector<Group> groups = createGroups();
    if (groups != m_groups)
    {
        m_groups = std::move(groups);
        Q_EMIT groupsChanged();
    }
}

void PDFAppToolMergePageModel::createRows()
{
    const std::vector<PDFAppToolMergePlan::Item>& items = m_plan.getItems();
    m_rowItems.clear();
    m_itemRows.assign(items.size(), -1);
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!items[i].isPending)
        {
            m_itemRows[i] = int(m_rowItems.size());
            m_rowItems.push_back(int(i));
        }
    }
    m_isSelected.assign(m_rowItems.size(), 0);
    m_groups = createGroups();

    m_rowGroups.assign(m_rowItems.size(), -1);
    for (size_t group = 0; group < m_groups.size(); ++group)
    {
        for (int row = m_groups[group].firstRow; row < m_groups[group].firstRow + m_groups[group].rowCount; ++row)
        {
            m_rowGroups[size_t(row)] = int(group);
        }
    }
}

std::vector<PDFAppToolMergePageModel::Group> PDFAppToolMergePageModel::createGroups() const
{
    std::vector<Group> groups;
    const std::vector<PDFAppToolMergePlan::Item>& items = m_plan.getItems();
    int nextRow = 0;
    for (size_t i = 0; i < items.size();)
    {
        const PDFAppToolMergePlan::Item& item = items[i];
        size_t end = i + 1;
        while (!item.isPending && end < items.size() && !items[end].isPending && items[end].inputId == item.inputId)
        {
            ++end;
        }

        Group group;
        group.inputId = item.inputId;
        group.firstRow = nextRow;
        group.rowCount = item.isPending ? 0 : int(end - i);
        nextRow += group.rowCount;
        i = end;

        const int inputIndex = m_list ? m_list->indexOf(item.inputId) : -1;
        const PDFAppToolInput* input = m_list ? m_list->getInput(inputIndex) : nullptr;
        if (!input)
        {
            continue;
        }

        group.inputIndex = inputIndex;
        group.hue = PDFAppToolInputList::getHue(inputIndex, input);
        group.title = tr("%1 · %2").arg(PDFAppToolInputList::getLetter(inputIndex), input->name);

        if (item.isPending)
        {
            // Only an input, which waits for something, has a group without its pages (1390:34070):
            // a damaged or unsupported file and a file without pages are only in the list
            switch (input->state)
            {
                case PDFAppToolInputState::Loading:
                {
                    const int percentage = m_list->getLoadingProgress(input->id);
                    group.subtitle = tr("Pages unknown until the file is read");
                    group.callout = GroupCallout::Reading;
                    group.calloutTitle = percentage >= 0 ? tr("Reading the file – %1 %").arg(percentage) : tr("Reading the file");
                    group.calloutText = tr("The pages of %1 appear here when it is read. Create PDF waits until every file is read.").arg(input->name);
                    group.progress = percentage >= 0 ? percentage / 100.0 : 0.0;
                    break;
                }

                case PDFAppToolInputState::PasswordRequired:
                    group.subtitle = tr("Pages unknown until unlocked");
                    group.callout = GroupCallout::Locked;
                    group.calloutTitle = tr("Password required");
                    group.calloutText = tr("%1 is encrypted. Enter its password to read the pages, or remove the file.").arg(input->name);
                    break;

                case PDFAppToolInputState::Restricted:
                {
                    const int pageCount = int(qMin<PDFInteger>(input->pageCount, std::numeric_limits<int>::max()));
                    const QString pages = pageCount == 1 ? tr("%Ln page", nullptr, pageCount) : tr("%Ln pages", nullptr, pageCount);
                    group.subtitle = pageCount > 0 ? tr("%1 · not in the result until unlocked").arg(pages) : tr("Not in the result until unlocked");
                    group.callout = GroupCallout::Locked;
                    group.calloutTitle = tr("Owner password required");
                    // 1390:34519: "Printing and copying of Statement.pdf are not allowed. The merged PDF …"
                    const QString restriction = getRestrictionText(input->document.data(), input->name);
                    group.calloutText = restriction.isEmpty() ? input->reason
                                                              : tr("%1. The merged PDF is not encrypted and would lose these restrictions – enter the owner password, or remove the file.").arg(restriction);
                    break;
                }

                case PDFAppToolInputState::Ready:
                case PDFAppToolInputState::Damaged:
                case PDFAppToolInputState::Unsupported:
                case PDFAppToolInputState::NoPages:
                    continue;
            }
        }
        else
        {
            group.subtitle = getRunText(group.firstRow, group.rowCount);
            if (m_leftOutInputs.contains(input->id))
            {
                group.tag = tr("Left out");
            }
            else if (input->isImage())
            {
                group.tag = tr("Image");
            }
            else if (input->isRepaired)
            {
                group.tag = tr("Repaired");
            }
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

int PDFAppToolMergePageModel::getItemIndex(int row) const
{
    return row >= 0 && size_t(row) < m_rowItems.size() ? m_rowItems[size_t(row)] : -1;
}

int PDFAppToolMergePageModel::getRowOfItem(size_t itemIndex) const
{
    return itemIndex < m_itemRows.size() ? m_itemRows[itemIndex] : -1;
}

const PDFAppToolMergePlan::Item* PDFAppToolMergePageModel::getItem(int row) const
{
    const int itemIndex = getItemIndex(row);
    return itemIndex >= 0 ? &m_plan.getItems()[size_t(itemIndex)] : nullptr;
}

const PDFAppToolInput* PDFAppToolMergePageModel::getInput(int row) const
{
    const PDFAppToolMergePlan::Item* item = getItem(row);
    return item && m_list ? m_list->findInput(item->inputId) : nullptr;
}

int PDFAppToolMergePageModel::getGroupOfRow(int row) const
{
    return row >= 0 && size_t(row) < m_rowGroups.size() ? m_rowGroups[size_t(row)] : -1;
}

int PDFAppToolMergePageModel::findGroupOfInput(const QString& inputId) const
{
    for (size_t i = 0; i < m_groups.size(); ++i)
    {
        if (m_groups[i].inputId == inputId)
        {
            return int(i);
        }
    }
    return -1;
}

QSizeF PDFAppToolMergePageModel::getPageSize(int row) const
{
    const PDFAppToolMergePlan::Item* item = getItem(row);
    const PDFAppToolInput* input = getInput(row);
    if (!item || !input || !input->isUsable())
    {
        return QSizeF();
    }

    QSizeF size;
    if (input->isImage())
    {
        // Q6: the size of the image by its resolution (the limits of the core, R15)
        size = PDFImagePageFactory::getPageSize(input->imageSize, input->imageDpiX, input->imageDpiY);
    }
    else if (input->document && item->pageIndex >= 0 && size_t(item->pageIndex) < input->document->getCatalog()->getPageCount())
    {
        size = PDFAppToolMergeModelHelper::getShownPageSize(input->document->getCatalog()->getPage(size_t(item->pageIndex)));
    }

    if (item->turn == PageRotation::Rotate90 || item->turn == PageRotation::Rotate270)
    {
        size.transpose();
    }
    return size;
}

void PDFAppToolMergePageModel::setSelectedRows(const std::vector<int>& rows)
{
    std::vector<char> isSelected(m_rowItems.size(), 0);
    for (const int row : rows)
    {
        if (row >= 0 && size_t(row) < isSelected.size())
        {
            isSelected[size_t(row)] = 1;
        }
    }

    // One change of the range of the changed rows (a tile is wider when it is selected)
    int first = -1;
    int last = -1;
    for (size_t i = 0; i < isSelected.size(); ++i)
    {
        if (isSelected[i] != m_isSelected[i])
        {
            first = first < 0 ? int(i) : first;
            last = int(i);
        }
    }
    m_isSelected = std::move(isSelected);
    if (first >= 0)
    {
        Q_EMIT dataChanged(index(first, 0), index(last, 0), { PDFComponentRoles::PageTileStateRole });
    }
}

bool PDFAppToolMergePageModel::isRowSelected(int row) const
{
    return row >= 0 && size_t(row) < m_isSelected.size() && m_isSelected[size_t(row)];
}

QString PDFAppToolMergePageModel::getSizeText(const QSizeF& sizeInPoints, bool withName)
{
    using Helper = PDFAppToolMergeModelHelper;

    if (!sizeInPoints.isValid() || !std::isfinite(sizeInPoints.width()) || !std::isfinite(sizeInPoints.height()))
    {
        return QString();
    }

    // Whole millimeters as the frames (210 × 297 mm; K25: Letter 216 × 279 mm)
    const QString dimensions = tr("%1 × %2 mm").arg(PDFAppLocale::formatNumber(qRound64(Helper::getMillimeters(sizeInPoints.width()))),
                                                    PDFAppLocale::formatNumber(qRound64(Helper::getMillimeters(sizeInPoints.height()))));
    if (!withName)
    {
        return dimensions;
    }

    struct NamedSize
    {
        const char* id;
        QString name;
    };
    const NamedSize namedSizes[] = {
        { PDFDocumentToolBlank::SIZE_A4, QStringLiteral("A4") },
        { PDFDocumentToolBlank::SIZE_A3, QStringLiteral("A3") },
        { PDFDocumentToolBlank::SIZE_A5, QStringLiteral("A5") },
        { PDFDocumentToolBlank::SIZE_LETTER, tr("Letter") },
        { PDFDocumentToolBlank::SIZE_LEGAL, tr("Legal") },
    };
    for (const NamedSize& namedSize : namedSizes)
    {
        const std::optional<QSizeF> size = PDFDocumentToolBlank::getNamedSize(QString::fromLatin1(namedSize.id));
        if (!size)
        {
            continue;
        }
        for (const QSizeF& oriented : { *size, size->transposed() })
        {
            if (qAbs(oriented.width() - sizeInPoints.width()) <= Helper::NAMED_SIZE_TOLERANCE && qAbs(oriented.height() - sizeInPoints.height()) <= Helper::NAMED_SIZE_TOLERANCE)
            {
                return tr("%1 · %2").arg(namedSize.name, dimensions);
            }
        }
    }
    return dimensions;
}

QString PDFAppToolMergePageModel::getOrientationText(const QSizeF& size)
{
    if (!size.isValid())
    {
        return QString();
    }
    if (qFuzzyCompare(size.width(), size.height()))
    {
        return tr("Square");
    }
    return size.width() > size.height() ? tr("Landscape") : tr("Portrait");
}

QString PDFAppToolMergePageModel::getRotationText(PageRotation turn)
{
    const QString text = PDFDocumentToolRotatePages::getTurnText(turn);
    return text.isEmpty() ? tr("None") : text;
}

QString PDFAppToolMergePageModel::getRestrictionText(const PDFDocument* document)
{
    return getRestrictionText(document, QString());
}

QString PDFAppToolMergePageModel::getRestrictionText(const PDFDocument* document, const QString& fileName)
{
    const PDFSecurityHandler* securityHandler = document ? document->getStorage().getSecurityHandler() : nullptr;
    if (!securityHandler)
    {
        return QString();
    }

    // ISO 32000-2, 7.6.4.2, Table 22: what the user password does not allow (by the table of
    // the revision of the handler, the rule of the core, SECURITY_TODO 31)
    using P = PDFSecurityHandler::Permission;
    QStringList denied;
    if (!PDFSecurityHandler::isAllowedByRevision(securityHandler, P::PrintLowResolution) || !PDFSecurityHandler::isAllowedByRevision(securityHandler, P::PrintHighResolution))
    {
        denied << tr("Printing");
    }
    if (!PDFSecurityHandler::isAllowedByRevision(securityHandler, P::CopyContent))
    {
        denied << (denied.isEmpty() ? tr("Copying") : tr("copying"));
    }
    if (!PDFSecurityHandler::isAllowedByRevision(securityHandler, P::Modify) || !PDFSecurityHandler::isAllowedByRevision(securityHandler, P::ModifyInteractiveItems) || !PDFSecurityHandler::isAllowedByRevision(securityHandler, P::ModifyFormFields))
    {
        denied << (denied.isEmpty() ? tr("Changes") : tr("changes"));
    }
    if (!PDFSecurityHandler::isAllowedByRevision(securityHandler, P::Assemble))
    {
        denied << (denied.isEmpty() ? tr("Assembling into another document") : tr("assembling into another document"));
    }

    // "Printing and copying are not allowed", with the file "Printing and copying of
    // Statement.pdf are not allowed" (1390:34519)
    const bool hasFile = !fileName.isEmpty();
    switch (denied.size())
    {
        case 0:
            return QString();
        case 1:
            return hasFile ? tr("%1 of %2 is not allowed").arg(denied.front(), fileName) : tr("%1 is not allowed").arg(denied.front());
        case 2:
            return hasFile ? tr("%1 and %2 of %3 are not allowed").arg(denied[0], denied[1], fileName) : tr("%1 and %2 are not allowed").arg(denied[0], denied[1]);
        default:
        {
            const QString last = denied.takeLast();
            const QString first = denied.join(tr(", "));
            return hasFile ? tr("%1 and %2 of %3 are not allowed").arg(first, last, fileName) : tr("%1 and %2 are not allowed").arg(first, last);
        }
    }
}

QString PDFAppToolMergePageModel::getRunText(int firstRow, int rowCount)
{
    if (rowCount <= 1)
    {
        return tr("Page %1").arg(PDFAppLocale::formatNumber(firstRow + 1));
    }
    return tr("Pages %1–%2").arg(PDFAppLocale::formatNumber(firstRow + 1), PDFAppLocale::formatNumber(qint64(firstRow) + rowCount));
}

QString PDFAppToolMergePageModel::getSourceText(int row) const
{
    const PDFAppToolInput* input = getInput(row);
    const int inputIndex = input && m_list ? m_list->indexOf(input->id) : -1;
    return input ? tr("%1 · %2").arg(PDFAppToolInputList::getLetter(inputIndex), input->name) : QString();
}

QPixmap PDFAppToolMergePageModel::getSourceDot(PDFAccentHue hue) const
{
    // The dot of the source (408:32003): the icon color of the hue of the input
    const QColor color = PDFThemeManager::getInstance()->getColor(PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon));
    auto it = m_dots.find(color.rgba());
    if (it == m_dots.end())
    {
        const qreal ratio = qMax<qreal>(1.0, getDevicePixelRatio());
        QPixmap pixmap(QSize(PDFAppToolMergeModelHelper::DOT, PDFAppToolMergeModelHelper::DOT) * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        PDFStylePainter::fillEllipse(&painter, QRectF(0, 0, PDFAppToolMergeModelHelper::DOT, PDFAppToolMergeModelHelper::DOT), color);
        painter.end();
        it = m_dots.emplace(color.rgba(), pixmap).first;
    }
    return it->second;
}

int PDFAppToolMergePageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rowItems.size());
}

int PDFAppToolMergePageModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PDFAppToolMergePageModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount() || index.column() >= ColumnCount)
    {
        return QVariant();
    }

    const int row = index.row();
    const PDFAppToolMergePlan::Item* item = getItem(row);
    const PDFAppToolInput* input = getInput(row);
    if (!item)
    {
        return QVariant();
    }

    const QSizeF pageSize = getPageSize(row);
    if (index.column() == PageColumn)
    {
        switch (role)
        {
            case Qt::DisplayRole:
                // The tile has the number of the page in the result (378:1049)
                return PDFAppLocale::formatNumber(qint64(row) + 1);

            case Qt::ToolTipRole:
            case Qt::AccessibleTextRole:
            {
                const QString page = input && input->isImage() ? input->name : tr("%1, p. %2").arg(input ? input->name : QString(), PDFAppLocale::formatNumber(item->pageIndex + 1));
                return tr("Page %1 of the result · %2").arg(PDFAppLocale::formatNumber(qint64(row) + 1), page);
            }

            case Qt::AccessibleDescriptionRole:
                return item->turn != PageRotation::None ? getRotationText(item->turn) : QString();

            case PDFComponentRoles::PageTileStateRole:
                return int(isRowSelected(row) ? PDFComponentPainter::PageTileState::Selected : PDFComponentPainter::PageTileState::Normal);

            case PDFComponentRoles::PageSizeRole:
                return pageSize.isValid() ? QVariant(pageSize) : QVariant();

            case PDFComponentRoles::PixmapRole:
                // Only the tiles draw the picture (Details has the number of the page)
                return PDFAppToolPageGridModel::getTurnedPicture(BaseClass::data(index, PDFComponentRoles::PixmapRole), getKey(row), item->turn);

            case Qt::TextAlignmentRole:
                return QVariant(Qt::AlignLeading | Qt::AlignVCenter);

            default:
                return QVariant();
        }
    }

    if (role == Qt::DecorationRole && index.column() == SourceColumn && input && m_list)
    {
        return QIcon(getSourceDot(PDFAppToolInputList::getHue(m_list->indexOf(input->id), input)));
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
    {
        return QVariant();
    }

    switch (index.column())
    {
        case SourceColumn:
            return getSourceText(row);
        case TypeColumn:
            return input && input->isImage() ? tr("Image") : tr("PDF page");
        case OriginalPageColumn:
            // An image of one frame has no page number of its own (408:32003 "–")
            if (input && input->isImage() && input->pageCount <= 1)
            {
                return QStringLiteral("–");
            }
            return PDFAppLocale::formatNumber(item->pageIndex + 1);
        case SizeColumn:
            return getSizeText(pageSize, false);
        case OrientationColumn:
            return getOrientationText(pageSize);
        case RotationColumn:
            return getRotationText(item->turn);
        default:
            break;
    }
    return QVariant();
}

QVariant PDFAppToolMergePageModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QVariant();
    }

    // The columns of 408:32003
    switch (section)
    {
        case PageColumn:
            return tr("Page");
        case SourceColumn:
            return tr("Source");
        case TypeColumn:
            return tr("Type");
        case OriginalPageColumn:
            return tr("Orig. page");
        case SizeColumn:
            return tr("Size");
        case OrientationColumn:
            return tr("Orientation");
        case RotationColumn:
            return tr("Rotation");
        default:
            break;
    }
    return QVariant();
}

Qt::ItemFlags PDFAppToolMergePageModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
}

QString PDFAppToolMergePageModel::getKey(int row) const
{
    const PDFAppToolMergePlan::Item* item = getItem(row);
    if (!item || !m_list)
    {
        return QString();
    }

    // The picture of the page of the input (its content generation) of the size of the model;
    // the generation of the model drops every picture at a change of the size or the drawing
    return m_list->getPictureKey(item->inputId, int(qMin<PDFInteger>(item->pageIndex, std::numeric_limits<int>::max())), getPictureSize(), getDevicePixelRatio())
           + QStringLiteral("/") + QString::number(getGeneration());
}

bool PDFAppToolMergePageModel::hasPictureSources() const
{
    return !m_list.isNull();
}

PDFAppThumbnailSource PDFAppToolMergePageModel::getPictureSource(int row) const
{
    const PDFAppToolMergePlan::Item* item = getItem(row);
    if (!item || !m_list || item->pageIndex > std::numeric_limits<int>::max())
    {
        return PDFAppThumbnailSource();
    }
    return m_list->getPictureSource(item->inputId, int(item->pageIndex));
}

}   // namespace pdf

#include "moc_pdfapptoolmergemodel.cpp"
