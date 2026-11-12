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

#ifndef PDFAPPMANAGECANVAS_H
#define PDFAPPMANAGECANVAS_H

#include "pdfappglobal.h"
#include "pdfapptoolgroupedpagemodel.h"
#include "pdfapptoolpagegrid.h"

#include "pdfaccenthue.h"
#include "pdfdocument.h"
#include "pdfdocumentassembly.h"

#include <QAbstractTableModel>
#include <QItemSelection>
#include <QPointer>
#include <QSizeF>
#include <QString>
#include <QWidget>

#include <map>
#include <set>
#include <vector>

class QStackedLayout;
class QTimer;
class QToolButton;

namespace pdf
{

class PDFAppParagraph;
class PDFAppThumbnailScheduler;
class PDFAppToolMergeDetailsView;
class PDFAppToolMergePageView;
class PDFAppToolPageTileDelegate;
class PDFLabel;
class PDFLineEdit;
class PDFSegmentedControl;

/// What the canvas knows of a source of the assembly: a value given by the GUI, index-aligned with
/// PDFDocumentAssembly::getSources (the input of a page of the plan). It holds no QObject: the
/// document is an immutable shared pointer, the bytes of an image are implicitly shared.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageSourceView
{
public:
    QString letter;                 ///< "A", "B", ... by the position of the source
    PDFAccentHue hue = PDFAccentHue::Neutral;   ///< Hue of the letter and of the dot (PDFAppToolInputList::getHue)
    PDFDocumentPointer document;    ///< PDF: its document (pictures, sizes), null = not readable (yet)
    QByteArray imageData;           ///< Image: the bytes of its file (pictures)
    QSizeF imagePageSize;           ///< Image: the size of its page in points (PDFImagePageFactory::getPageSize)
    QString pictureKey;             ///< Prefix of the keys of its pictures, unique for its content (a new content = a new key)
};

/// The pages of an assembly for the grouped view of Merge (387:12569; plan of step 3b of phase 2,
/// chapter 2): a row per page of the shown items in their order. A group item is a card (the icon
/// "group", its name Semi Bold, "Group · 5 pages · Contract.pdf 1–5"); the consecutive single pages
/// lie on the stage without a card (Group::isPlain) with their captions ("rotated 90°", "Site plan",
/// "image", "blank page", "cropped"; in the tab of a document the name of another source and "new
/// in the document"). A tile has the dot of its source and its label: "A · 6" in the own tab (the
/// letter of the source · the position in the assembly, QB5), "3 · p. 7", "7 · B p. 2", "10 · C" in
/// the tab of a document (the position · the original page, RB11). A selected item draws all its
/// tiles selected. The search (387:15594) shows only the matching items.
///
/// Ownership: a child of the canvas; the values only (no pointer to the GUI). Thread: GUI only.
/// groupsChanged is emitted last by setContent (the reset is said before it).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManagePageModel : public PDFAppToolGroupedPageModel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolGroupedPageModel;

public:
    explicit PDFAppManagePageModel(QObject* parent);
    virtual ~PDFAppManagePageModel() override;

    /// The content: the assembly, its sources, the shown items (\p shownItems empty with
    /// \p isFiltered false = all), the host (the labels), the target pages new in the document
    void setContent(PDFDocumentAssembly assembly,
                    std::vector<PDFAppManageSourceView> sources,
                    const std::set<QString>& shownItems,
                    bool isFiltered,
                    bool isDocumentHost,
                    std::set<PDFObjectReference> newTargetPages);

    const PDFDocumentAssembly& getAssembly() const { return m_assembly; }
    const std::vector<PDFAppManageSourceView>& getSources() const { return m_sources; }

    /// The selected items: their tiles are drawn selected (one dataChanged of the changed rows)
    void setSelectedItems(const std::set<QString>& itemIds);

    /// Index of the item of the row in the assembly (-1 for an invalid row)
    int getItemIndex(int row) const;

    /// Id of the item of the row (empty for an invalid row)
    QString getItemId(int row) const;

    /// The rows of the item (empty, when it is not shown)
    std::vector<int> getRowsOfItem(const QString& itemId) const;

    /// The page of the row (nullptr for an invalid row)
    const PDFDocumentAssembly::Page* getPage(int row) const;

    /// Position of the page of the row in the whole assembly (from 0; -1 for an invalid row)
    int getPosition(int row) const;

    /// Size of the page as it will be written in points (the turn and the crop applied), invalid
    /// when it is not known
    QSizeF getPageSize(int row) const;
    static QSizeF getPageSize(const PDFDocumentAssembly::Page& page, const std::vector<PDFAppManageSourceView>& sources);
    static QSizeF getPageSize(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Page& page, const std::vector<PDFAppManageSourceView>& sources);

    /// Texts of a tile: its label ("A · 6", "3 · p. 7") and its caption on the stage ("rotated 90°")
    QString getLabel(int row) const;
    QString getCaption(int row) const;

    virtual const std::vector<Group>& getGroups() const override { return m_groups; }
    virtual int getGroupOfRow(int row) const override;
    virtual bool isRowSelected(int row) const override;

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual QString getKey(int row) const override;
    virtual bool hasPictureSources() const override;
    virtual PDFAppThumbnailSource getPictureSource(int row) const override;

private:
    /// A row: the item and the page in it
    struct Row
    {
        int item = -1;
        int page = -1;
        int position = -1;      ///< Position of the page in the whole assembly
    };

    void createRows(const std::set<QString>& shownItems, bool isFiltered);
    std::vector<Group> createGroups() const;
    QString getGroupSubtitle(const PDFDocumentAssembly::Item& item) const;

    PDFDocumentAssembly m_assembly;
    std::vector<PDFAppManageSourceView> m_sources;
    std::set<PDFObjectReference> m_newTargetPages;
    std::vector<Row> m_rows;
    std::vector<int> m_rowGroups;
    std::vector<Group> m_groups;
    std::set<QString> m_selectedItems;
    bool m_isDocumentHost = false;
};

/// The items of the assembly as the table Details (387:15072): the columns #, Name, Type, Source,
/// Orig. page, Pages, Size, Orientation, Rotation, Tags; a row per shown item. A column heading
/// sorts only this view ("Column headings sort only this view – the page order is changed with
/// Sort"): sort keeps a permutation of the rows, the assembly is not changed. GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageItemModel : public QAbstractTableModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractTableModel;

public:
    enum Column
    {
        NumberColumn,
        NameColumn,
        TypeColumn,
        SourceColumn,
        OriginalPageColumn,
        PagesColumn,
        SizeColumn,
        OrientationColumn,
        RotationColumn,
        TagsColumn,
        ColumnCount
    };

    explicit PDFAppManageItemModel(QObject* parent);
    virtual ~PDFAppManageItemModel() override;

    /// The content (a reset); the order of a sorted column is kept
    void setContent(PDFDocumentAssembly assembly, std::vector<PDFAppManageSourceView> sources, const std::set<QString>& shownItems, bool isFiltered);

    /// Index of the item of the row in the assembly (-1 for an invalid row)
    int getItemIndex(int row) const;
    QString getItemId(int row) const;
    int getRowOfItem(const QString& itemId) const;

    /// The texts of an item (also the panels): its name, type, source, original pages, tags. The
    /// name (owner Q5 of 12. 11.) is the name the user gave, else the file name of its one source
    /// ("Annex_A.pdf", not "Annex_A.pdf, p. 1"), else the name of the core ("Blank page")
    static QString getItemName(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item);
    static QString getItemType(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item);
    static QString getItemSource(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item);
    static QString getOriginalPages(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item);
    static QString getItemTags(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item);

    /// The rotation of an item, "90°" (the column Rotation of 387:15072, Properties 387:16947,
    /// the row Rotation of Selection 387:13234): the turn clockwise in degrees, "None" without
    /// a turn (Merge keeps its "90° right" of Rotate Pages)
    static QString getRotationText(PageRotation turn);

    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    QVariant getSortValue(int item, int column) const;
    void applySort();

    PDFDocumentAssembly m_assembly;
    std::vector<PDFAppManageSourceView> m_sources;
    std::vector<int> m_rows;            ///< Index of the item of each row
    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

/// The canvas of Manage Pages (387:12569 document-canvas): bg/track, the padding 20, the bar of
/// the preview 36 high ("10 items · 17 pages" Semi Bold 13, the search field "Search pages" 210 x
/// 36, Size: Small, the segments Thumbnails / Details 2 x 91 x 32), 12 below it the pages - the
/// grouped view of Merge (PDFAppToolMergePageView over PDFAppManagePageModel, the tiles of
/// PDFAppToolPageTileDelegate, the pictures of the visible tiles by PDFAppThumbnailScheduler) or
/// Details (387:15072: the table in a card, its note below), and the hint of the search
/// (387:15594). Without items the GUI gives the empty state (409:32739) in the place of the bar
/// and the pages.
///
/// The canvas reads the assembly as a value and keeps the selection as the ids of the items
/// (both views show it); every change is asked from the GUI by a signal (the GUI makes it one undo
/// step and sets the new value). The signals come from the event handlers of the views or of the
/// field (never from inside setContent); a slot may rebuild or delete the canvas.
///
/// Ownership: the canvas owns its models, views, delegate and scheduler; the GUI holds the canvas
/// by a QPointer (the shell owns it during the mode); the empty state is taken over.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppManageCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 387:12569 (document-canvas, preview-bar) and 387:15072 (Details)
    static constexpr int PADDING = 20;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int BAR_GAP = 12;
    static constexpr int BAR_SPACING = 8;
    static constexpr int SEARCH_WIDTH = 210;
    static constexpr int SEGMENT_WIDTH = 91;
    static constexpr int SEGMENT_HEIGHT = 32;
    static constexpr int NOTE_GAP = 10;

    /// Delay of the search after the last key (plan 6: 200 ms)
    static constexpr int SEARCH_DELAY = 200;

    /// Slot of a page in the size Small (387:12815: the box 73 x 100 of a card; a loose page has
    /// the square box of its longer side, 100 x 100 in a tile 112 x 128, 387:12939)
    static constexpr QSize SMALL_SLOT = QSize(73, 100);

    /// Widths of the columns of Details but the last one (387:15072: the cells start at 10, 50,
    /// 174, 244, 358, 420, 468, 562, 636, 696 of the card of 772); the cells of the Library row
    /// have the paddings 10 at both ends, so the texts of the frame ("Orig. page", "Blank page",
    /// "420 × 297 mm", "Orientation", "Landscape") get their width, Name and Source give it.
    /// Together with Tags (55, "Image") they fill the viewport of 770 in the card - no column lies
    /// outside of it (the header stretches the last column only over its default size).
    static constexpr int DETAILS_COLUMN_WIDTHS[] = { 40, 106, 83, 107, 75, 53, 103, 83, 65 };
    static constexpr int DETAILS_LAST_COLUMN_WIDTH = 55;

    explicit PDFAppManageCanvas(QWidget* parent);
    virtual ~PDFAppManageCanvas() override;

    /// The assembly shown (a value, RB7), its sources, the host and the new target pages; the
    /// selection keeps the ids, which are still there
    void setContent(const PDFDocumentAssembly& assembly, std::vector<PDFAppManageSourceView> sources, bool isDocumentHost, std::set<PDFObjectReference> newTargetPages);
    const PDFDocumentAssembly& getAssembly() const { return m_assembly; }

    /// The selected items in the order of the assembly
    const PDFDocumentAssembly::Selection& getSelection() const { return m_selection; }

    /// Selects the items (both views, the first one comes into the view); no signal
    void setSelection(const PDFDocumentAssembly::Selection& selection);

    /// The text of the search and the shown items ("2 of 10 items", 387:15594); setSearchText
    /// filters at once (no signal)
    QString getSearchText() const;
    void setSearchText(const QString& text);
    bool isFiltered() const { return m_isFiltered; }
    int getShownItemCount() const;

    /// The empty state in the place of the bar and of the pages (nullptr: the pages); the canvas
    /// takes it over, an old one is deleted later (it may be the sender)
    void setEmptyState(QWidget* emptyState);
    QWidget* getEmptyState() const { return m_emptyState.data(); }

    /// Thumbnails (false) or Details (true)
    void setDetailsShown(bool isDetailsShown);
    bool isDetailsShown() const;

    /// The size of the tiles (the slot of Small scaled)
    void setTileSize(PDFAppToolPageGrid::TileSize size);

    /// The pages can be changed (false while the tool runs)
    void setEditable(bool isEditable);

    void setHeaderText(const QString& text);

    void retranslate();

    PDFAppManagePageModel* getPageModel() const { return m_pageModel.data(); }
    PDFAppManageItemModel* getItemModel() const { return m_itemModel.data(); }
    PDFAppToolMergePageView* getPageView() const { return m_pageView.data(); }
    PDFAppToolMergeDetailsView* getDetailsView() const { return m_detailsView.data(); }
    PDFAppToolPageTileDelegate* getDelegate() const { return m_delegate.data(); }
    PDFAppThumbnailScheduler* getScheduler() const { return m_scheduler.data(); }
    PDFLineEdit* getSearchField() const { return m_searchField.data(); }
    QToolButton* getTileSizeButton() const { return m_tileSizeButton.data(); }
    PDFSegmentedControl* getViewSwitch() const { return m_viewSwitch.data(); }
    PDFLabel* getHeaderLabel() const { return m_header.data(); }
    PDFAppParagraph* getDetailsNote() const { return m_detailsNote.data(); }
    PDFAppParagraph* getSearchHint() const { return m_searchHint.data(); }
    QWidget* getBar() const { return m_bar.data(); }

Q_SIGNALS:
    /// The selection of the user has changed (a click, a key, Details)
    void selectionChanged();

    /// The selected items are dropped before \p beforeItemId (empty = at the end)
    void moveRequested(const pdf::PDFDocumentAssembly::Selection& selection, const QString& beforeItemId);

    /// Alt + an arrow: the selected items move by \p delta places
    void moveByRequested(int delta);

    /// Delete: the selected items go to the bin
    void removeRequested();

    /// The context menu of the pages at the global position (387:14220)
    void contextMenuRequested(const QPoint& globalPosition);

    /// The search has changed (after its delay; Esc clears it)
    void searchChanged(const QString& text);

    /// Thumbnails / Details has been switched by the user
    void viewSwitched(bool isDetailsShown);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyContent();
    void updatePictureSize();
    void updateSearchHint();
    void selectInViews();
    void selectPageRows(const std::set<QString>& itemIds);
    static QItemSelection createRanges(const QAbstractItemModel* model, const std::set<QString>& itemIds, int columnCount);
    std::set<QString> getShownItems() const;

    void onPageSelectionChanged();
    void onDetailsSelectionChanged();
    void onPageMoveRequested(const std::vector<int>& rows, int targetRow);
    void onDetailsMoveRequested(const std::vector<int>& rows, int targetRow);
    void onPageContextMenu(const QPoint& position);
    void onDetailsContextMenu(const QPoint& position);
    void onViewSwitchChanged(int index);
    void onSearchEdited(const QString& text);
    void onSearchTimer();
    void onContentHeightChanged(int height);

    PDFDocumentAssembly m_assembly;
    std::vector<PDFAppManageSourceView> m_sources;
    std::set<PDFObjectReference> m_newTargetPages;
    PDFDocumentAssembly::Selection m_selection;
    QPointer<PDFAppManagePageModel> m_pageModel;
    QPointer<PDFAppManageItemModel> m_itemModel;
    QPointer<PDFAppToolPageTileDelegate> m_delegate;
    QPointer<PDFAppThumbnailScheduler> m_scheduler;
    QPointer<QWidget> m_bar;
    QPointer<PDFLabel> m_header;
    QPointer<PDFLineEdit> m_searchField;
    QPointer<QTimer> m_searchTimer;
    QPointer<QToolButton> m_tileSizeButton;
    QPointer<PDFSegmentedControl> m_viewSwitch;
    QPointer<QWidget> m_pages;
    QPointer<QStackedLayout> m_stack;
    QPointer<QWidget> m_thumbnailsPage;
    QPointer<PDFAppToolMergePageView> m_pageView;
    QPointer<PDFAppParagraph> m_searchHint;
    QPointer<QWidget> m_detailsPage;
    QPointer<PDFAppToolMergeDetailsView> m_detailsView;
    QPointer<PDFAppParagraph> m_detailsNote;
    QPointer<QWidget> m_emptyState;
    QString m_appliedSearch;
    PDFAppToolPageGrid::TileSize m_tileSize = PDFAppToolPageGrid::TileSize::Small;
    bool m_isFiltered = false;
    bool m_isEditable = true;
    bool m_isDocumentHost = false;
    bool m_isSyncing = false;
};

}   // namespace pdf

#endif // PDFAPPMANAGECANVAS_H
