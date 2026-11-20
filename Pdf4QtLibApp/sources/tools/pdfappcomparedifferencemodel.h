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

#ifndef PDFAPPCOMPAREDIFFERENCEMODEL_H
#define PDFAPPCOMPAREDIFFERENCEMODEL_H

#include "pdfappglobal.h"

#include "pdfdiff.h"
#include "pdfdiffreport.h"

#include <QAbstractItemModel>
#include <QStyledItemDelegate>

#include <optional>
#include <vector>

namespace pdf
{

/// The list of the differences of Compare (the left panel Differences, 492:19314, 492:104504, 492:105001; plan of
/// step 7 of phase 2, 4.9): the shown differences (the result filtered by the menu Filter) grouped by their pages -
/// a group is a run of differences of the same pair of pages ("Page 3" for the same page in both documents,
/// "Original 5 → Revised 6", "Revised 5" for an added page, "Original 7" for a removed one), each difference a child
/// with its sign − + ≠ ↔ (removed, added, replaced, moved) and its text (PDFDiffResult::getMessage - viewing the
/// documents on the screen, Q7.2). The model keeps a copy of the shown result; the selection of the view maps to
/// the index of the difference (findIndex, getDifferenceIndex) for F6 / Shift+F6 and the canvas. Virtual: the
/// groups are computed once per result, the rows are drawn by the view on demand (any number of differences; the
/// old dock had all of them as items). Numbers only by PDFAppLocale (K7.26).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareDifferenceModel : public QAbstractItemModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemModel;

public:
    /// The change of a difference (the sign of its row and the color of its kind)
    enum class Change
    {
        Removed,
        Added,
        Replaced,
        Moved
    };

    /// Roles of the rows
    enum Role
    {
        IsGroupRole = Qt::UserRole + 1,     ///< bool: a row of a group (the pages)
        DifferenceIndexRole,                ///< qint64: index of the difference in the shown result (-1 for a group)
        ChangeRole                          ///< int: Change of a difference
    };

    explicit PDFAppCompareDifferenceModel(QObject* parent = nullptr);
    virtual ~PDFAppCompareDifferenceModel() override;

    /// The shown differences (a reset of the model)
    void setResult(const PDFDiffResult& result);
    const PDFDiffResult& getResult() const { return m_result; }

    /// The change of the difference of the result
    static Change getChange(const PDFDiffResult& result, size_t index);

    /// The pair of pages (original, revised; from 0, -1 = none) of each difference: a difference of the content of a
    /// page has the pair of the page sequence of that page, a moved / added / removed page its own pages
    static std::vector<std::pair<PDFInteger, PDFInteger>> getPagePairs(const PDFDiffResult& result);

    /// The sign of a change (− + ≠ ↔)
    static QString getSign(Change change);

    /// The title of a group of the pages of the inputs (from 0, -1 = none)
    static QString getGroupTitle(PDFInteger originalPage, PDFInteger revisedPage);

    /// Text of the row of a difference (492:19314: "Text 'Q3 2025' replaced by 'Q4 2025'", "Image removed",
    /// "Page 5 moved to page 6"); the page is the title of its group. The tooltip keeps the whole message.
    static QString getRowText(const PDFDiffResult& result, size_t index);

    /// The row of the difference, an invalid index for none
    QModelIndex findIndex(size_t differenceIndex) const;

    /// The difference of the row, std::nullopt for a group or an invalid index
    std::optional<size_t> getDifferenceIndex(const QModelIndex& index) const;

    /// Number of the groups
    int getGroupCount() const { return int(m_groups.size()); }

    virtual QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    struct Group
    {
        PDFInteger originalPage = -1;
        PDFInteger revisedPage = -1;
        size_t first = 0;   ///< First difference of the group
        size_t count = 0;   ///< Differences of the group
    };

    PDFDiffResult m_result;
    std::vector<Group> m_groups;
};

/// The rows of the list of the differences (difference-list 492:19989): a group row of 21 (Semi Bold 11
/// text/secondary at 4), a difference row of 26 with 1 px between them - the kind badge 16 (the color of its kind,
/// the sign Semi Bold 10 in text/primary) at 8, the text Regular 12 text/primary at 32; the selected row bg/selected
/// (bg/accent-subtle), radius 6, the hover bg/hover. The colors of the kinds are those of the panel Colors.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCompareDifferenceDelegate : public QStyledItemDelegate
{
    Q_OBJECT

private:
    using BaseClass = QStyledItemDelegate;

public:
    static constexpr int GROUP_HEIGHT = 21;
    static constexpr int ROW_HEIGHT = 27;   ///< 26 + the gap 1
    static constexpr int BADGE = 16;
    static constexpr int BADGE_LEFT = 8;
    static constexpr int TEXT_LEFT = 32;

    explicit PDFAppCompareDifferenceDelegate(QObject* parent);
    virtual ~PDFAppCompareDifferenceDelegate() override;

    void setColors(const PDFDiffReportColors& colors) { m_colors = colors; }

    /// The color of the change by the colors
    static QColor getColor(PDFAppCompareDifferenceModel::Change change, const PDFDiffReportColors& colors);

    virtual void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    PDFDiffReportColors m_colors;
};

}   // namespace pdf

#endif // PDFAPPCOMPAREDIFFERENCEMODEL_H
