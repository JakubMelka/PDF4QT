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

#include "pdfappcomparedifferencemodel.h"

#include "pdfapplocale.h"

#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"

#include <QFontMetricsF>
#include <QPainter>

#include <limits>
#include <map>
#include <optional>

namespace pdf
{

// ------------------------------------------------------------- PDFAppCompareDifferenceModel

PDFAppCompareDifferenceModel::PDFAppCompareDifferenceModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppCompareDifferenceModel::~PDFAppCompareDifferenceModel() = default;

void PDFAppCompareDifferenceModel::setResult(const PDFDiffResult& result)
{
    beginResetModel();
    m_result = result;
    m_groups.clear();

    // A group is a run of the differences of one pair of pages (the order of the result: by the page sequence)
    const std::vector<std::pair<PDFInteger, PDFInteger>> pairs = getPagePairs(m_result);
    for (size_t i = 0; i < pairs.size(); ++i)
    {
        const PDFInteger originalPage = pairs[i].first;
        const PDFInteger revisedPage = pairs[i].second;
        if (m_groups.empty() || m_groups.back().originalPage != originalPage || m_groups.back().revisedPage != revisedPage)
        {
            Group group;
            group.originalPage = originalPage;
            group.revisedPage = revisedPage;
            group.first = i;
            m_groups.push_back(group);
        }
        ++m_groups.back().count;
    }
    endResetModel();
}

std::vector<std::pair<PDFInteger, PDFInteger>> PDFAppCompareDifferenceModel::getPagePairs(const PDFDiffResult& result)
{
    // The pair of the page sequence of a page of one document (a difference of the content names its page only:
    // "Image removed" of the original page 3 belongs to the pair of the pages 3 / 3, 492:19314)
    std::map<PDFInteger, PDFInteger> revisedOfOriginal;
    std::map<PDFInteger, PDFInteger> originalOfRevised;
    for (const PDFDiffResult::PageSequenceItem& item : result.getPageSequence())
    {
        if (item.leftPage >= 0)
        {
            revisedOfOriginal.emplace(item.leftPage, item.rightPage);
        }
        if (item.rightPage >= 0)
        {
            originalOfRevised.emplace(item.rightPage, item.leftPage);
        }
    }

    const size_t count = result.getDifferencesCount();
    std::vector<std::pair<PDFInteger, PDFInteger>> pairs;
    pairs.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        PDFInteger originalPage = result.getLeftPage(i);
        PDFInteger revisedPage = result.getRightPage(i);
        if (!result.isPageMoveAddRemoveDifference(i))
        {
            if (originalPage >= 0 && revisedPage < 0)
            {
                const auto it = revisedOfOriginal.find(originalPage);
                revisedPage = it != revisedOfOriginal.cend() ? it->second : -1;
            }
            else if (revisedPage >= 0 && originalPage < 0)
            {
                const auto it = originalOfRevised.find(revisedPage);
                originalPage = it != originalOfRevised.cend() ? it->second : -1;
            }
        }
        pairs.emplace_back(originalPage, revisedPage);
    }
    return pairs;
}

PDFAppCompareDifferenceModel::Change PDFAppCompareDifferenceModel::getChange(const PDFDiffResult& result, size_t index)
{
    // The order of the colors of the old Diff: a replacement first, a move last
    if (result.isReplaceDifference(index))
    {
        return Change::Replaced;
    }
    if (result.isRemoveDifference(index))
    {
        return Change::Removed;
    }
    if (result.isAddDifference(index))
    {
        return Change::Added;
    }
    return Change::Moved;
}

QString PDFAppCompareDifferenceModel::getSign(Change change)
{
    // 492:19989: − + ≠ ↔
    switch (change)
    {
        case Change::Removed:
            return QString(QChar(0x2212));
        case Change::Added:
            return QStringLiteral("+");
        case Change::Replaced:
            return QString(QChar(0x2260));
        case Change::Moved:
            return QString(QChar(0x2194));
    }
    return QString();
}

QString PDFAppCompareDifferenceModel::getGroupTitle(PDFInteger originalPage, PDFInteger revisedPage)
{
    // 492:19989: "Page 1", "Original 5 → Revised 6", "Revised 5"
    if (originalPage >= 0 && originalPage == revisedPage)
    {
        return tr("Page %1").arg(PDFAppLocale::formatNumber(qint64(originalPage) + 1));
    }
    if (originalPage >= 0 && revisedPage >= 0)
    {
        return tr("Original %1 %2 Revised %3").arg(PDFAppLocale::formatNumber(qint64(originalPage) + 1), QString(QChar(0x2192)), PDFAppLocale::formatNumber(qint64(revisedPage) + 1));
    }
    if (originalPage >= 0)
    {
        return tr("Original %1").arg(PDFAppLocale::formatNumber(qint64(originalPage) + 1));
    }
    if (revisedPage >= 0)
    {
        return tr("Revised %1").arg(PDFAppLocale::formatNumber(qint64(revisedPage) + 1));
    }
    return tr("Document");
}

QString PDFAppCompareDifferenceModel::getRowText(const PDFDiffResult& result, size_t index)
{
    if (index >= result.getDifferencesCount())
    {
        return QString();
    }

    if (result.isTextTooLarge(index))
    {
        // B191: the texts of the pages are not in the difference
        return tr("Text differs (too large to compare word by word)");
    }

    using Type = PDFDiffResult::Type;
    switch (result.getType(index))
    {
        case Type::PageMoved:
            return tr("Page %1 moved to page %2").arg(PDFAppLocale::formatNumber(qint64(result.getLeftPage(index)) + 1), PDFAppLocale::formatNumber(qint64(result.getRightPage(index)) + 1));
        case Type::PageAdded:
            return tr("Page %1 added").arg(PDFAppLocale::formatNumber(qint64(result.getRightPage(index)) + 1));
        case Type::PageRemoved:
            return tr("Page %1 removed").arg(PDFAppLocale::formatNumber(qint64(result.getLeftPage(index)) + 1));
        case Type::RemovedTextCharContent:
            return tr("Text removed");
        case Type::AddedTextCharContent:
            return tr("Text added");
        case Type::RemovedVectorGraphicContent:
            return tr("Vector graphics removed");
        case Type::AddedVectorGraphicContent:
            return tr("Vector graphics added");
        case Type::RemovedImageContent:
            return tr("Image removed");
        case Type::AddedImageContent:
            return tr("Image added");
        case Type::RemovedShadingContent:
            return tr("Shading removed");
        case Type::AddedShadingContent:
            return tr("Shading added");
        case Type::TextReplaced:
            return tr("Text '%1' replaced by '%2'").arg(result.getRemovedText(index), result.getAddedText(index));
        case Type::TextAdded:
            return tr("Text '%1' added").arg(result.getAddedText(index));
        case Type::TextRemoved:
            return tr("Text '%1' removed").arg(result.getRemovedText(index));
        case Type::Invalid:
            break;
    }
    return result.getMessage(index);
}

QModelIndex PDFAppCompareDifferenceModel::findIndex(size_t differenceIndex) const
{
    // The groups are sorted by their first difference: the last group, which starts at or before the index
    size_t lower = 0;
    size_t upper = m_groups.size();
    while (lower < upper)
    {
        const size_t middle = lower + (upper - lower) / 2;
        if (m_groups[middle].first <= differenceIndex)
        {
            lower = middle + 1;
        }
        else
        {
            upper = middle;
        }
    }
    if (lower == 0)
    {
        return QModelIndex();
    }
    const size_t groupRow = lower - 1;
    const Group& group = m_groups[groupRow];
    if (differenceIndex >= group.first + group.count)
    {
        return QModelIndex();
    }
    return index(int(differenceIndex - group.first), 0, index(int(groupRow), 0));
}

std::optional<size_t> PDFAppCompareDifferenceModel::getDifferenceIndex(const QModelIndex& index) const
{
    if (!index.isValid() || index.internalId() == 0)
    {
        return std::nullopt;
    }
    const size_t groupIndex = size_t(index.internalId() - 1);
    if (groupIndex >= m_groups.size() || index.row() < 0 || size_t(index.row()) >= m_groups[groupIndex].count)
    {
        return std::nullopt;
    }
    return m_groups[groupIndex].first + size_t(index.row());
}

QModelIndex PDFAppCompareDifferenceModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0)
    {
        return QModelIndex();
    }

    // The internal id: 0 = a group, the group + 1 = a difference of the group
    if (!parent.isValid())
    {
        return size_t(row) < m_groups.size() ? createIndex(row, column, quintptr(0)) : QModelIndex();
    }
    if (parent.internalId() != 0 || parent.row() < 0 || size_t(parent.row()) >= m_groups.size())
    {
        return QModelIndex();
    }
    return size_t(row) < m_groups[size_t(parent.row())].count ? createIndex(row, column, quintptr(parent.row()) + 1) : QModelIndex();
}

QModelIndex PDFAppCompareDifferenceModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || child.internalId() == 0)
    {
        return QModelIndex();
    }
    return createIndex(int(child.internalId() - 1), 0, quintptr(0));
}

int PDFAppCompareDifferenceModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
    {
        return int(qMin(m_groups.size(), size_t(std::numeric_limits<int>::max())));
    }
    if (parent.internalId() != 0 || parent.row() < 0 || size_t(parent.row()) >= m_groups.size())
    {
        return 0;
    }
    return int(qMin(m_groups[size_t(parent.row())].count, size_t(std::numeric_limits<int>::max())));
}

int PDFAppCompareDifferenceModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant PDFAppCompareDifferenceModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    if (index.internalId() == 0)
    {
        if (index.row() < 0 || size_t(index.row()) >= m_groups.size())
        {
            return QVariant();
        }
        const Group& group = m_groups[size_t(index.row())];
        switch (role)
        {
            case Qt::DisplayRole:
            case Qt::AccessibleTextRole:
                return getGroupTitle(group.originalPage, group.revisedPage);
            case IsGroupRole:
                return true;
            case DifferenceIndexRole:
                return qint64(-1);
            default:
                break;
        }
        return QVariant();
    }

    const std::optional<size_t> differenceIndex = getDifferenceIndex(index);
    if (!differenceIndex)
    {
        return QVariant();
    }

    const Change change = getChange(m_result, *differenceIndex);
    switch (role)
    {
        case Qt::DisplayRole:
            return getRowText(m_result, *differenceIndex);
        case Qt::ToolTipRole:
            return m_result.getMessage(*differenceIndex);
        case Qt::AccessibleTextRole:
            return m_result.getMessage(*differenceIndex);
        case IsGroupRole:
            return false;
        case DifferenceIndexRole:
            return qint64(*differenceIndex);
        case ChangeRole:
            return int(change);
        default:
            break;
    }
    return QVariant();
}

Qt::ItemFlags PDFAppCompareDifferenceModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }

    // A group is a caption: it is not selected (F6 walks over the differences)
    return index.internalId() == 0 ? Qt::ItemIsEnabled : Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

// ------------------------------------------------------------- PDFAppCompareDifferenceDelegate

PDFAppCompareDifferenceDelegate::PDFAppCompareDifferenceDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppCompareDifferenceDelegate::~PDFAppCompareDifferenceDelegate() = default;

QColor PDFAppCompareDifferenceDelegate::getColor(PDFAppCompareDifferenceModel::Change change, const PDFDiffReportColors& colors)
{
    switch (change)
    {
        case PDFAppCompareDifferenceModel::Change::Removed:
            return colors.removed;
        case PDFAppCompareDifferenceModel::Change::Added:
            return colors.added;
        case PDFAppCompareDifferenceModel::Change::Replaced:
            return colors.replaced;
        case PDFAppCompareDifferenceModel::Change::Moved:
            return colors.moved;
    }
    return QColor();
}

void PDFAppCompareDifferenceDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing);
    const bool isRightToLeft = option.direction == Qt::RightToLeft;
    const QRectF rect = QRectF(option.rect);
    const QString text = index.data(Qt::DisplayRole).toString();

    if (index.data(PDFAppCompareDifferenceModel::IsGroupRole).toBool())
    {
        // group-Page 1: Semi Bold 11 text/secondary at 4, the bottom of the row
        const QRectF textRect = rect.adjusted(4, 6, -4, 0);
        PDFStylePainter::text(painter, textRect, (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop, text,
                              PDFThemeFonts::getFont(QFont::DemiBold, 11), PDFStylePainter::color(PDFThemeColors::TextSecondary));
        return;
    }

    // The row of 26 (the last px is the gap): selected bg/accent-subtle, hover bg/hover, radius 6
    const QRectF rowRect(rect.left(), rect.top(), rect.width(), rect.height() - 1);
    if (option.state.testFlag(QStyle::State_Selected))
    {
        PDFStylePainter::fill(painter, rowRect, PDFStylePainter::Radii(6), PDFThemeColors::BgAccentSubtle);
    }
    else if (option.state.testFlag(QStyle::State_MouseOver))
    {
        PDFStylePainter::fill(painter, rowRect, PDFStylePainter::Radii(6), PDFThemeColors::BgHover);
    }

    // The badge of the kind: a circle 16 in the color of the kind, the sign over it
    const auto change = PDFAppCompareDifferenceModel::Change(index.data(PDFAppCompareDifferenceModel::ChangeRole).toInt());
    const qreal badgeLeft = isRightToLeft ? rowRect.right() - BADGE_LEFT - BADGE : rowRect.left() + BADGE_LEFT;
    const QRectF badgeRect(badgeLeft, rowRect.center().y() - BADGE * 0.5, BADGE, BADGE);
    PDFStylePainter::fillEllipse(painter, badgeRect, getColor(change, m_colors));
    PDFStylePainter::text(painter, badgeRect, Qt::AlignCenter, PDFAppCompareDifferenceModel::getSign(change),
                          PDFThemeFonts::getFont(QFont::DemiBold, 10), PDFStylePainter::color(PDFThemeColors::TextPrimary));

    // The text Regular 12 at 32, elided at the trailing edge
    const QRectF textRect = isRightToLeft ? QRectF(rowRect.left() + 8, rowRect.top(), rowRect.width() - TEXT_LEFT - 8, rowRect.height())
                                          : QRectF(rowRect.left() + TEXT_LEFT, rowRect.top(), rowRect.width() - TEXT_LEFT - 8, rowRect.height());
    const QFont font = PDFThemeFonts::getFont(QFont::Normal, 12);
    const QString elided = QFontMetricsF(font).elidedText(text, Qt::ElideRight, textRect.width());
    PDFStylePainter::text(painter, textRect, (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, elided, font, PDFStylePainter::color(PDFThemeColors::TextPrimary));
}

QSize PDFAppCompareDifferenceDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const int height = index.data(PDFAppCompareDifferenceModel::IsGroupRole).toBool() ? GROUP_HEIGHT : ROW_HEIGHT;
    return QSize(option.rect.width(), height);
}

}   // namespace pdf

#include "moc_pdfappcomparedifferencemodel.cpp"
