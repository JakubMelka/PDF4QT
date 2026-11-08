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

#ifndef PDFAPPTOOLMERGEMODEL_H
#define PDFAPPTOOLMERGEMODEL_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanels_thumbnails.h"

#include "pdfaccenthue.h"
#include "pdfpage.h"

#include <QCoreApplication>
#include <QPixmap>
#include <QPointer>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <map>
#include <optional>
#include <vector>

namespace pdf
{

class PDFAppToolInput;
class PDFAppToolInputList;

/// The pages of the result of Merge (plan of step 2 of phase 2, 4.4, K13, K14): a value, the
/// GUI keeps it in the parameter "plan" of the tool (Undo / Redo of the parameter model).
///
/// An item is a page of an input (its id in the list, its page from 0, the turn of the user
/// relative to the rotation of the page) or a pending input: every page of an input, whose
/// pages are not known yet (it is being read, it waits for a password - 378:2613, 1390:34070);
/// the pending item keeps the place of the input and becomes its pages, when they are known
/// (resolved). A page used more than once is a clone of its first occurrence in the order of
/// the plan (the core copies the page object again, B4): the flag is derived, never stored.
///
/// Text of the parameter (toText / fromText): the syntax of PDFDocumentToolMerge::parsePlan
/// with the letters of the inputs by their position in the list (A, B, ..., AA), and a pending
/// input as "<letters>*" ("A1-6,B*,C1"). The run gets only the pages of the usable inputs, the
/// letters by their position among them (toText with the ids of the usable inputs).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMergePlan
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppToolMergePlan)

public:
    /// Mark of a pending input in the text ("B*")
    static constexpr char PENDING_MARK = '*';

    /// An item of the plan
    struct Item
    {
        QString inputId;                        ///< Id of the input (PDFAppToolInput::id)
        PDFInteger pageIndex = -1;              ///< Page of the input from 0 (-1 for a pending input)
        PageRotation turn = PageRotation::None; ///< Turn clockwise relative to the rotation of the page
        bool isPending = false;                 ///< Every page of the input, when its pages are known

        bool operator==(const Item&) const = default;
    };

    PDFAppToolMergePlan() = default;
    explicit PDFAppToolMergePlan(std::vector<Item> items);

    const std::vector<Item>& getItems() const { return m_items; }

    /// Number of the pages (pending items are no pages)
    PDFInteger getPageCount() const;

    /// The plan has a page (Create PDF needs one)
    bool hasPages() const;

    /// The item is a further occurrence of a page (a clone): the same page of the same input
    /// is before it in the plan
    bool isClone(size_t itemIndex) const;

    bool operator==(const PDFAppToolMergePlan&) const = default;

    // ------------------------------------------------------------------ text

    /// The plan of the inputs in their order: a pending item of each (all their pages)
    static PDFAppToolMergePlan createDefault(const QStringList& inputIds);

    /// Text of the plan for the inputs in the order \p inputIds; items of an input, which is
    /// not in \p inputIds, are left out. \p withPending false: no pending items (the run).
    QString toText(const QStringList& inputIds, bool withPending) const;

    /// Plan of a text for the inputs \p inputIds with the known page counts \p pageCounts
    /// (-1 = not known yet: only a pending item may name the input). std::nullopt and the
    /// translated reason for a text, which is no plan of these inputs. An empty text is the
    /// default plan.
    static std::optional<PDFAppToolMergePlan> fromText(const QString& text, const QStringList& inputIds, const std::vector<PDFInteger>& pageCounts, QString* errorMessage);

    /// The plan, whose pending items of the inputs with known pages (\p pageCounts > 0) are
    /// their pages, without the items of inputs, which are not in \p inputIds, and without the
    /// pages out of the range of their inputs; a pending item of an input with no page or with
    /// pages, which are not known yet, stays. An input without any item is not in the result
    /// (the user has removed all of its pages; it stays in the list).
    PDFAppToolMergePlan resolved(const QStringList& inputIds, const std::vector<PDFInteger>& pageCounts) const;

    // ------------------------------------------------------------ operations

    /// Adds the input at the end (a pending item)
    void appendInput(const QString& inputId);

    /// Removes every item of the input
    void removeInput(const QString& inputId);

    /// Removes the items (indices of getItems)
    void removeItems(const std::vector<size_t>& itemIndices);

    /// Inserts a copy of each item right after it. \returns the indices of the copies.
    std::vector<size_t> cloneItems(const std::vector<size_t>& itemIndices);

    /// Turns the pages of the items by a quarter turn (\p isClockwise) more
    void rotateItems(const std::vector<size_t>& itemIndices, bool isClockwise);

    /// Moves the items (their order kept) in front of the item \p targetIndex (an index of the
    /// plan before the move; getItems().size() = to the end). \returns the new indices.
    std::vector<size_t> moveItems(const std::vector<size_t>& itemIndices, size_t targetIndex);

    /// The items ordered by the order of their inputs \p inputIds; the items of one input keep
    /// their order (a file moved in the list takes its pages with it; Sort by name, by type)
    void orderByInputs(const QStringList& inputIds);

    /// Sort by the original page numbers of the files (K13): stable by the page of the input,
    /// equal numbers by the order of the inputs (A1, B1, C1, A2, ...); pending items go last
    /// in the order of their inputs
    void sortByPageNumber(const QStringList& inputIds);

    /// Reverse order: the runs of the pages of one input (the groups, K14) in the reverse
    /// order, the pages of a run keep their order (a grouped plan: the files reversed)
    void reverseRuns();

private:
    std::vector<Item> m_items;
};

/// The pages of the result of Merge for the views of the tool: the grouped thumbnails and the
/// table Details (408:32003) share this model and one selection model. A row is a page of the
/// plan (pending items have no row); the groups (K14) are the runs of consecutive pages of one
/// input and the pending inputs, which the canvas shows (Loading, a password; 378:2613,
/// 1390:34070). The pictures come from the inputs of the list (PDFAppThumbnailsModel, the
/// scheduler of the thumbnails, the keys of PDFAppToolInputList::getPictureKey), turned by the
/// turn of the page. The model has the columns of Details; the thumbnails use the column 0.
///
/// Ownership: a child of the canvas of the tool; the list is held by a QPointer (it outlives
/// the views, the tab owns it). Thread: GUI only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMergePageModel : public PDFAppThumbnailsModel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppThumbnailsModel;

public:
    /// Columns of Details (408:32003)
    enum Column
    {
        PageColumn,
        SourceColumn,
        TypeColumn,
        OriginalPageColumn,
        SizeColumn,
        OrientationColumn,
        RotationColumn,
        ColumnCount
    };

    /// The callout of a group of a pending input
    enum class GroupCallout
    {
        None,       ///< The pages of the group
        Reading,    ///< callout-neutral 1390:34213: the reading of the file with its progress
        Locked      ///< callout-red 378:2613, 1390:34515: a password is needed
    };

    /// A group of the canvas (group-A · Contract.pdf 378:1242)
    struct Group
    {
        QString inputId;
        int inputIndex = -1;        ///< Index of the input in the list (its letter and hue)
        int firstRow = 0;           ///< First row of the group
        int rowCount = 0;           ///< Rows of the group (0 for a pending input)
        PDFAccentHue hue = PDFAccentHue::Neutral;
        QString title;              ///< "A · Contract.pdf"
        QString subtitle;           ///< "Pages 1–6", "Pages unknown until unlocked"
        QString tag;                ///< "Image", "Repaired", "Left out", empty = none
        GroupCallout callout = GroupCallout::None;
        QString calloutTitle;       ///< "Password required", "Reading the file – 50 %"
        QString calloutText;
        double progress = -1.0;     ///< Progress of the reading (0 - 1), -1 = none

        bool operator==(const Group&) const = default;
    };

    explicit PDFAppToolMergePageModel(QObject* parent);
    virtual ~PDFAppToolMergePageModel() override;

    /// The inputs of the pages (nullptr = none)
    void setInputList(PDFAppToolInputList* list);
    PDFAppToolInputList* getInputList() const;

    /// The plan: the rows are made again (reset), the selection of the rows is cleared
    void setPlan(PDFAppToolMergePlan plan);
    const PDFAppToolMergePlan& getPlan() const { return m_plan; }

    /// Inputs, which were left out of a partial result (378:4376): their groups have the Tag
    /// Left out
    void setLeftOutInputs(const QStringList& inputIds);

    /// The texts of the groups follow the list (a reading finished, its progress, a password)
    void updateGroups();

    /// Item of the plan of the row (-1 for an invalid row)
    int getItemIndex(int row) const;

    /// Row of the item of the plan (-1 for a pending item or an invalid index)
    int getRowOfItem(size_t itemIndex) const;

    /// Item of the row (nullptr for an invalid row)
    const PDFAppToolMergePlan::Item* getItem(int row) const;

    /// Input of the row (nullptr for an invalid row or a missing input)
    const PDFAppToolInput* getInput(int row) const;

    /// The groups in their order
    const std::vector<Group>& getGroups() const { return m_groups; }

    /// Index of the group of the row (-1 for an invalid row)
    int getGroupOfRow(int row) const;

    /// Index of the group of the input, which is first in the canvas (-1 for none)
    int findGroupOfInput(const QString& inputId) const;

    /// Size of the page of the row as it will be saved in points (the turn applied), invalid
    /// when it is not known
    QSizeF getPageSize(int row) const;

    /// The rows drawn as selected (the selection of the views; PageTileStateRole Selected)
    void setSelectedRows(const std::vector<int>& rows);
    bool isRowSelected(int row) const;

    // ---------------------------------------------------------- texts

    /// "A4 · 210 × 297 mm", "216 × 279 mm" (a named size of Blank PDF in either orientation)
    static QString getSizeText(const QSizeF& sizeInPoints, bool withName);

    /// "Portrait", "Landscape", "Square"
    static QString getOrientationText(const QSizeF& size);

    /// "None", "90° right", "180°", "90° left"
    static QString getRotationText(PageRotation turn);

    /// "Printing and copying are not allowed" for a restricted document (Table 22), empty for
    /// no restriction
    static QString getRestrictionText(const PDFDocument* document);

    /// The same with the name of the file after what it does not allow: "Printing and copying
    /// of Statement.pdf are not allowed" (1390:34519); an empty name is the text above
    static QString getRestrictionText(const PDFDocument* document, const QString& fileName);

    /// Run of the result "Pages 7–15", "Page 16"
    static QString getRunText(int firstRow, int rowCount);

    // ---------------------------------------------------------- the model

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual QString getKey(int row) const override;
    virtual bool hasPictureSources() const override;
    virtual PDFAppThumbnailSource getPictureSource(int row) const override;

Q_SIGNALS:
    /// The groups have changed (their rows, texts or callouts): the canvas is laid out again
    void groupsChanged();

private:
    void createRows();
    std::vector<Group> createGroups() const;
    QPixmap getSourceDot(PDFAccentHue hue) const;
    QString getSourceText(int row) const;

    QPointer<PDFAppToolInputList> m_list;
    PDFAppToolMergePlan m_plan;
    std::vector<int> m_rowItems;            ///< Item of the plan of each row
    std::vector<int> m_itemRows;            ///< Row of each item of the plan (-1 for a pending one)
    std::vector<int> m_rowGroups;           ///< Group of each row
    std::vector<char> m_isSelected;         ///< Selection of each row
    std::vector<Group> m_groups;
    QStringList m_leftOutInputs;
    mutable std::map<QRgb, QPixmap> m_dots; ///< The dots of the sources of Details by their color
};

}   // namespace pdf

#endif // PDFAPPTOOLMERGEMODEL_H
