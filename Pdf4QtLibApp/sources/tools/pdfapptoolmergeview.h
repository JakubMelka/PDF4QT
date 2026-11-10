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

#ifndef PDFAPPTOOLMERGEVIEW_H
#define PDFAPPTOOLMERGEVIEW_H

#include "pdfappglobal.h"
#include "pdfapptoolpagegrid.h"

#include "pdffluentwidgets.h"

#include <QAbstractItemView>
#include <QPointer>
#include <QWidget>

#include <vector>

class QPushButton;
class QStackedLayout;
class QTimer;
class QToolButton;

namespace pdf
{

class PDFAppCard;
class PDFAppParagraph;
class PDFAppToolGroupedPageModel;
class PDFAppToolMergePageModel;
class PDFPageTileDelegate;
class PDFSegmentedControl;

/// The drag of pages to another place in a view of the pages of Merge ("Drag pages in the
/// preview to move single pages", 378:1049; "rows can be dragged", 408:32003): a press on a
/// selected page without a modifier, a move over the distance of a drag, a release at the
/// place. The state only, the views compute the place and draw it. No QDrag: the pages never
/// leave the view (no nested event loop of a drag, no data of the pages outside).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMergeDragGesture
{
public:
    /// A press on a selected row (\p row), which may become a drag
    void press(int row, const QPoint& position);

    /// A move: \returns true, when the press has become a drag now
    bool move(const QPoint& position);

    /// The press or the drag ends (a release, a lost press)
    void reset();

    bool isPressed() const { return m_pressedRow >= 0; }
    bool isDragging() const { return m_isDragging; }
    int getPressedRow() const { return m_pressedRow; }

    /// Place of a drop: the moved rows go in front of this row (the row count = to the end)
    int getTargetRow() const { return m_targetRow; }
    void setTargetRow(int row) { m_targetRow = row; }

private:
    QPoint m_pressPosition;
    int m_pressedRow = -1;
    int m_targetRow = -1;
    bool m_isDragging = false;
};

/// The pages of the result of Merge in groups (stage of 378:1049): a card for each run of the
/// pages of one input (K14) - bg/surface, border/default, radius 10, the header (the dot of the
/// hue of the input 8, "A · Contract.pdf" Semi Bold 12, "Pages 1–6" Regular 11 text/tertiary,
/// a Tag "Image" / "Repaired" / "Left out") and the tiles of PDFPageTileDelegate (73 x 100 in the
/// size Small, 85 x 128 with the label, 8 apart, flowing in the lines of the card); a pending
/// input has its callout instead of the tiles (callout-neutral with the progress of the reading
/// 1390:34213, callout-red with the lock 378:2613, 1390:34515). The cards are 10 apart.
///
/// Large documents: the layout is arithmetic (the groups by a binary search, the tiles of a
/// group by their index), only the painted tiles ask for their pictures (the scheduler of the
/// thumbnails); 50 000 pages lay out in one pass over the groups.
///
/// The selection is the selection model of the view (shared with Details): a click selects the
/// page, Ctrl toggles it, Shift selects the range from the anchor; the arrows move the current
/// page in the flow (right-to-left: the flow starts at the right edge), Shift extends. A drag
/// of a selected page moves the selected pages (moveRequested), Delete and Alt + an arrow ask
/// for a removal and a move (removeRequested, moveByRequested). The signals come from the
/// event handlers of the view, never from inside its layout; a slot may change the model.
///
/// The groups of the other tools (R1 b of step 3): a header icon instead of the dot (Insert
/// 388:17789), the buttons of a group below its callout (Enter Password…, Remove File; real
/// push buttons, children of the viewport, made again when the groups change - an old one is
/// hidden and deleted later, it may be the sender of the click), tiles of their own size
/// (Group::tileScale, Group::hasTileCaptions: the blank pages of Insert 388:16811, the
/// captions of Interleave 385:13263) and a group without its card (Group::isPlain). A click of
/// a button is said by actionTriggered from the event loop (the slot may change the groups).
/// contentHeightChanged says the height of the cards from the event loop after the layout,
/// so a canvas can hug the cards (the hint 10 below them, 388:15746).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMergePageView : public QAbstractItemView
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemView;

public:
    /// Geometry of 378:1049 (the stage and its group cards)
    static constexpr int GROUP_GAP = 10;
    static constexpr int CARD_RADIUS = 10;
    static constexpr int CARD_PADDING_TOP = 9;      ///< Padding 8 + the border
    static constexpr int CARD_PADDING_SIDE = 11;    ///< Padding 10 + the border
    static constexpr int CARD_PADDING_BOTTOM = 11;  ///< Padding 10 + the border
    static constexpr int HEADER_HEIGHT = 15;
    static constexpr int HEADER_TAG_HEIGHT = 21;    ///< A header with a Tag (378:1595)
    static constexpr int HEADER_GAP = 8;
    static constexpr int DOT = 8;
    static constexpr int DOT_GAP = 6;
    static constexpr int TILE_GAP = 8;

    /// The icon of a header (388:17789: 14 x 14, the title 20 from its leading edge)
    static constexpr int HEADER_ICON = 14;

    /// The buttons of a group below its callout (388:17789: 36 high, 8 below the callout, 8 apart)
    static constexpr int ACTION_HEIGHT = 36;
    static constexpr int ACTION_GAP = 8;

    /// The callouts of a pending input (1390:34213, 1390:34515)
    static constexpr int CALLOUT_RADIUS = 8;
    static constexpr int CALLOUT_PADDING_V = 11;    ///< Padding 10 + the border
    static constexpr int CALLOUT_PADDING_H = 13;    ///< Padding 12 + the border
    static constexpr int CALLOUT_GAP = 4;
    static constexpr int CALLOUT_ICON = 16;
    static constexpr int CALLOUT_TEXT_LINE = 13;
    static constexpr int PROGRESS_HEIGHT = 4;

    /// Line of the drop place (bg/accent)
    static constexpr int DROP_LINE = 2;

    /// Distance from the edge of the view, where a drag scrolls, and the step of the scroll
    static constexpr int AUTO_SCROLL_MARGIN = 24;
    static constexpr int AUTO_SCROLL_STEP = 16;

    explicit PDFAppToolMergePageView(QWidget* parent);
    virtual ~PDFAppToolMergePageView() override;

    /// The delegate of the tiles (the slot of its size gives the tiles)
    void setTileDelegate(PDFPageTileDelegate* delegate);

    /// The pages can be changed (a drag, Delete, Alt + an arrow); false while the tool runs
    void setEditable(bool isEditable);
    bool isEditable() const { return m_isEditable; }

    /// The rows selected in their order
    std::vector<int> getSelectedRows() const;

    /// Selects the rows (the first one is the current one and the anchor), scrolls to it
    void selectRows(const std::vector<int>& rows);

    /// Rectangle of the card of the group in the coordinates of the viewport (empty for an invalid group)
    QRect getGroupRect(int group) const;

    /// Scrolls the card of the group to the top of the view
    void scrollToGroup(int group);

    /// Height of the content (all cards)
    int getContentHeight() const { return m_contentHeight; }

    /// Size of a tile of the group (-1: of a group of the default tiles)
    QSize getTileSize(int group) const;

    /// The buttons of the groups can be clicked (false while the tool runs)
    void setActionsEnabled(bool isEnabled);

    /// The button of the action of the group of the input (nullptr = none)
    QPushButton* getActionButton(const QString& inputId, const QString& actionId) const;

    /// Row in front of which a drop at the position of the viewport places the pages
    int getDropRow(const QPoint& position) const;

    /// The place of a drop is shown (a drag runs)
    bool isDragging() const { return m_gesture.isDragging(); }
    int getDropTargetRow() const { return m_gesture.isDragging() ? m_gesture.getTargetRow() : -1; }

    virtual QRect visualRect(const QModelIndex& index) const override;
    virtual void scrollTo(const QModelIndex& index, ScrollHint hint = EnsureVisible) override;
    virtual QModelIndex indexAt(const QPoint& point) const override;
    virtual void setModel(QAbstractItemModel* model) override;
    virtual void reset() override;

Q_SIGNALS:
    /// The selected pages are dropped in front of \p targetRow
    void moveRequested(const std::vector<int>& rows, int targetRow);

    /// Alt + an arrow: the selected pages move by \p delta places
    void moveByRequested(int delta);

    /// Delete: the selected pages are removed
    void removeRequested();

    /// A button of the group of the input has been clicked (from the event loop)
    void actionTriggered(const QString& inputId, const QString& actionId);

    /// The height of the cards has changed (from the event loop after the layout)
    void contentHeightChanged(int height);

protected:
    virtual QModelIndex moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers) override;
    virtual int horizontalOffset() const override;
    virtual int verticalOffset() const override;
    virtual bool isIndexHidden(const QModelIndex& index) const override;
    virtual void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) override;
    virtual QRegion visualRegionForSelection(const QItemSelection& selection) const override;
    virtual void updateGeometries() override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool event(QEvent* event) override;
    virtual void leaveEvent(QEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void scrollContentsBy(int dx, int dy) override;

private:
    /// Layout of a group (in the coordinates of the content, left to right)
    struct GroupLayout
    {
        int top = 0;            ///< Top of the card
        int height = 0;         ///< Height of the card
        int tilesTop = 0;       ///< Top of the first line of the tiles
        int calloutHeight = 0;  ///< Height of the callout of a pending input
        int actionsTop = 0;     ///< Top of the buttons below the callout
        int inset = 0;          ///< Distance of the tiles from the edges (the padding of the card, 0 for a plain group)
        QSize tileSize;         ///< Size of the tiles of the group
        int columns = 1;        ///< Tiles in a line
    };

    /// A button of a group (388:17789)
    struct ActionButton
    {
        QString inputId;
        QString actionId;
        int group = -1;
        QPointer<QPushButton> button;
    };

    /// The y of the content is above the card (the order of std::upper_bound)
    static bool isAboveLayout(int contentY, const GroupLayout& layout);

    void layoutGroups();
    void onGroupsChanged();
    void onAutoScroll();
    void onActionButtonClicked();
    void emitQueuedAction();
    void emitContentHeight();
    void updateActionButtons();
    void positionActionButtons();
    QSize getTileSize() const;
    int getColumns(int group) const;
    int getInnerWidth() const;

    /// Rectangle of the tile of the row in the content (left to right; empty for an invalid row)
    QRect getTileRect(int row) const;

    /// The rectangle of the content mirrored for the right-to-left layout and moved by the scroll
    QRect toViewport(const QRect& contentRect) const;
    QPoint toContent(const QPoint& viewportPoint) const;

    /// Group, whose card is at the y of the content (-1 for none)
    int findGroupAt(int contentY) const;

    /// Row of the tile at the point of the content (-1 for none)
    int findRowAt(const QPoint& contentPoint) const;

    /// Height of the callout of the group for the inner width
    int getCalloutHeight(int group) const;

    void selectRange(int anchorRow, int row, bool isAdded);
    void setCurrentRow(int row, QItemSelectionModel::SelectionFlags flags);
    QItemSelection createRowSelection(int first, int last) const;
    void paintGroup(QPainter* painter, int group, const QRect& paintRect);
    void paintTiles(QPainter* painter, int group, const QRect& paintRect);
    void paintDropLine(QPainter* painter);
    int getNeighborRow(int row, int lineDelta) const;
    void updateDrag(const QPoint& viewportPosition);

    QPointer<PDFAppToolGroupedPageModel> m_mergeModel;  ///< The groups of the model (the interface of the grouped view, R1 b of step 3)
    QPointer<PDFPageTileDelegate> m_delegate;
    QPointer<QTimer> m_autoScrollTimer;
    std::vector<GroupLayout> m_layouts;
    std::vector<ActionButton> m_actionButtons;
    PDFAppToolMergeDragGesture m_gesture;
    QPoint m_lastDragPosition;
    QString m_queuedActionInput;
    QString m_queuedAction;
    int m_contentHeight = 0;
    int m_emittedContentHeight = -1;
    int m_anchorRow = -1;
    int m_hoverRow = -1;
    int m_autoScrollDelta = 0;
    bool m_isEditable = true;
    bool m_areActionsEnabled = true;
    bool m_isActionQueued = false;
    bool m_isContentHeightQueued = false;
};

/// The table Details (pages-merge-details 408:32003): the same pages as the thumbnails, a row
/// per page in the columns Page, Source (the dot of the hue and "A · Contract.pdf"), Type, Orig.
/// page, Size, Orientation, Rotation; the selection model of the thumbnails. Rows are dragged
/// (the selected rows of a press on a selected row), removed by Delete and moved by Alt + Up /
/// Down as the tiles (the same signals).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMergeDetailsView : public PDFTreeView
{
    Q_OBJECT

private:
    using BaseClass = PDFTreeView;

public:
    /// Height of a row of 408:32003
    static constexpr int ROW_HEIGHT = 27;

    explicit PDFAppToolMergeDetailsView(QWidget* parent);
    virtual ~PDFAppToolMergeDetailsView() override;

    void setEditable(bool isEditable);

    /// The widths of the columns of 408:32003
    void setDefaultColumnWidths();

    /// Row in front of which a drop at the position of the viewport places the rows
    int getDropRow(const QPoint& position) const;

    bool isDragging() const { return m_gesture.isDragging(); }

    /// The header and every row (408:32320 hugs its rows; a long list scrolls in the card)
    virtual QSize sizeHint() const override;

Q_SIGNALS:
    void moveRequested(const std::vector<int>& rows, int targetRow);
    void moveByRequested(int delta);
    void removeRequested();

protected:
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    std::vector<int> getSelectedRows() const;

    PDFAppToolMergeDragGesture m_gesture;
    bool m_isEditable = true;
};

/// The canvas of Merge (document-canvas 378:1082): bg/track, the padding 20, the bar of the
/// preview (preview-bar 378:1224: "Result · 16 pages" Semi Bold 13, Size: Small 1370:34102, the
/// segments Thumbnails / Details 91 x 32), 12 below it the pages - the groups or Details with
/// its note (408:32003). Without inputs the stage has the drop zone of 378:1827 instead (the
/// GUI gives it, PDFAppToolMergeEmptyState). The views share the model and one selection model.
///
/// Ownership: the canvas owns the views, the model, the scheduler and the delegate; the GUI of
/// the tool holds the canvas by a QPointer (the view of the tool owns it during the mode).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolMergeCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int PADDING = 20;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int BAR_GAP = 12;
    static constexpr int SEGMENT_WIDTH = 91;
    static constexpr int SEGMENT_HEIGHT = 32;
    static constexpr int NOTE_GAP = 10;

    /// Slot of a page in the size Small (378:1250: the box 73 x 100)
    static constexpr QSize SMALL_SLOT = QSize(73, 100);

    explicit PDFAppToolMergeCanvas(QWidget* parent);
    virtual ~PDFAppToolMergeCanvas() override;

    PDFAppToolMergePageModel* getModel() const { return m_model.data(); }
    PDFAppToolMergePageView* getPageView() const { return m_pageView.data(); }
    PDFAppToolMergeDetailsView* getDetailsView() const { return m_detailsView.data(); }
    PDFAppThumbnailScheduler* getScheduler() const { return m_scheduler.data(); }
    PDFPageTileDelegate* getDelegate() const { return m_delegate.data(); }
    QToolButton* getTileSizeButton() const { return m_tileSizeButton.data(); }
    PDFSegmentedControl* getViewSwitch() const { return m_viewSwitch.data(); }
    PDFLabel* getHeaderLabel() const { return m_header.data(); }
    PDFAppParagraph* getDetailsNote() const { return m_detailsNote.data(); }

    /// The empty state in the place of the pages (nullptr: the pages); the canvas takes it over
    void setEmptyState(QWidget* emptyState);
    QWidget* getEmptyState() const { return m_emptyState.data(); }

    /// Thumbnails (false) or Details (true)
    void setDetailsShown(bool isDetailsShown);
    bool isDetailsShown() const;

    void setHeaderText(const QString& text);

    /// The size of the tiles: the slot of Small scaled (PDFAppToolPageGrid::getTileScale)
    void setTileSize(PDFAppToolPageGrid::TileSize size);
    PDFAppToolPageGrid::TileSize getTileSize() const { return m_tileSize; }

    /// The pages can be changed (false while the tool runs: the bar is disabled too)
    void setEditable(bool isEditable);

    void retranslate();

Q_SIGNALS:
    /// Thumbnails / Details has been switched by the user
    void viewSwitched(bool isDetailsShown);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void onViewSwitchChanged(int index);
    void updatePictureSize();

    QPointer<PDFAppToolMergePageModel> m_model;
    QPointer<PDFPageTileDelegate> m_delegate;
    QPointer<PDFAppThumbnailScheduler> m_scheduler;
    QPointer<QWidget> m_bar;
    QPointer<PDFLabel> m_header;
    QPointer<QToolButton> m_tileSizeButton;
    QPointer<PDFSegmentedControl> m_viewSwitch;
    QPointer<QWidget> m_pages;
    QPointer<QStackedLayout> m_stack;
    QPointer<PDFAppToolMergePageView> m_pageView;
    QPointer<QWidget> m_detailsPage;
    QPointer<PDFAppToolMergeDetailsView> m_detailsView;
    QPointer<PDFAppParagraph> m_detailsNote;
    QPointer<QWidget> m_emptyState;
    PDFAppToolPageGrid::TileSize m_tileSize = PDFAppToolPageGrid::TileSize::Medium;
    bool m_isEditable = true;
};

}   // namespace pdf

#endif // PDFAPPTOOLMERGEVIEW_H
