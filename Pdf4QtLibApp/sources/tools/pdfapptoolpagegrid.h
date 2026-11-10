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

#ifndef PDFAPPTOOLPAGEGRID_H
#define PDFAPPTOOLPAGEGRID_H

#include "pdfappglobal.h"
#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfapptoolgroupedpagemodel.h"

#include "pdfaccenthue.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdffluentwidgets.h"
#include "pdfpage.h"

#include <QPointer>
#include <QWidget>

#include <optional>
#include <utility>
#include <vector>

class QScrollBar;
class QSpacerItem;
class QToolButton;
class QVBoxLayout;

namespace pdf
{

class PDFAppContext;
class PDFAppDocument;
class PDFAppDocumentView;
class PDFAppParagraph;
class PDFAppToolMergePageView;
class PDFAppToolPanelPopup;
class PDFDrawWidgetProxy;
class PDFTileRow;

/// The pages of the grid of a tool (Rotate 389:20177, Remove 388:20614): the pictures of the
/// thumbnails of the document (PDFAppThumbnailsModel, the same scheduler and cache keys) and
/// the state of each tile, which the tool gives. A tile shows its page as it will be saved:
/// the picture turned by the extra rotation of the tool and the size of the page turned with
/// it. A change of the tiles is one dataChanged of the range of the changed tiles.
///
/// The rows are every page in its order (the default), or chosen pages in a chosen order, each
/// at most once: the result of Extract Pages (385:9405) and the files of Split in groups, which
/// the grouped view draws as cards (383:3315, PDFAppToolGroupedPageModel). The tiles are kept by
/// their pages, the pictures by the keys of their pages (a page has one picture in every row).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageGridModel : public PDFAppToolGroupedPageModel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppToolGroupedPageModel;

public:
    /// State of a tile of the tool
    struct Tile
    {
        PDFComponentPainter::PageTileState state = PDFComponentPainter::PageTileState::Normal;
        QString label;                                  ///< Label below the page instead of the label of the page ("1" of the new document 385:9405, "Handbook_p1.pdf" 385:11029), empty = the label of the page
        QString caption;                                ///< "90° right", "will be removed", "from page 8", empty = none
        QString badgeIconName;                          ///< Badge of the page, empty = none
        PageRotation extraRotation = PageRotation::None;///< Turn of the picture of the page (Rotate Pages)

        // Step 3 of phase 2 (plan 4.3, K45 with R13; the skeleton adds the fields, package A
        // draws them - the delegate and the roles of the model)
        std::optional<PDFAccentHue> dotColor;           ///< Dot of the file or the source of the page (Split At selected pages 383:5715, Interleave front / back 385:13263), std::nullopt = none
        bool isDimmed = false;                          ///< The page is drawn muted (not taken, Insert 388:15487; replaced, 388:17167)
        bool markerBefore = false;                      ///< The mark of a cut before the tile: a new file starts at this page (Split 383:5715)

        bool operator==(const Tile&) const = default;
    };

    explicit PDFAppToolPageGridModel(QObject* parent);
    virtual ~PDFAppToolPageGridModel() override;

    /// The document of the pages (the rows are reset, when the number of the pages changes;
    /// the rows are every page again)
    void setPageDocument(PDFDocumentPointer document, const QString& documentId);

    /// The tiles of all pages, indexed by the page (missing ones are Normal). \returns the range
    /// of the rows, whose tile has changed (-1, -1 = none), it has got one dataChanged
    std::pair<int, int> setTiles(std::vector<Tile> tiles);

    /// Tile of the page (Normal for an invalid page)
    Tile getTile(int pageIndex) const;

    /// The rows: every page in its order (std::nullopt), or the pages \p pages in this order
    /// (an invalid page or a page, which is there already, is left out); \p groups divide the
    /// rows into the cards of a grouped view in their order (their firstRow and rowCount are
    /// set here: each group takes its rowCount rows, empty = no group). Another set of rows is
    /// a reset; groupsChanged is emitted after a change, the last thing.
    void setRows(std::optional<std::vector<int>> pages, std::vector<Group> groups);

    /// The rows are every page in its order
    bool hasAllPages() const { return !m_rowPages.has_value(); }

    /// Page of the row (-1 for an invalid row) and the row of the page (-1 = no row)
    int getPageOfRow(int row) const;
    int getRowOfPage(int page) const;

    virtual const std::vector<Group>& getGroups() const override { return m_groups; }
    virtual int getGroupOfRow(int row) const override;
    virtual bool isRowSelected(int row) const override;
    virtual QString getKey(int row) const override;
    virtual PDFAppThumbnailSource getPictureSource(int row) const override;
    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

    /// The picture of a page turned by \p turn as it will be saved (the grids of the tools,
    /// which turn pages: Rotate, Merge); the turned picture is kept in QPixmapCache under
    /// \p key with the turn. A picture, which is no QPixmap, or no turn: the picture itself.
    static QVariant getTurnedPicture(const QVariant& picture, const QString& key, PageRotation turn);

private:
    PDFDocumentPointer m_pageDocument;
    std::vector<QSizeF> m_pageSizes;   ///< The visible sizes of the pages (crop box rotated by /Rotate)
    std::vector<Tile> m_tiles;
    std::optional<std::vector<int>> m_rowPages; ///< The page of each row, std::nullopt = every page
    std::vector<int> m_pageRows;                ///< The row of each page (-1 = none) for the chosen rows
    std::vector<Group> m_groups;
};

/// The list of the tiles: a press and a release of the left button on one tile is a click,
/// Space a click of the current tile, Shift + Space with Shift; Delete asks for the
/// current page (Remove Pages). The signals come from the event loop: a slot may delete
/// the view (Qt uses the view after its mouse events).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageGridView : public PDFListView
{
    Q_OBJECT

private:
    using BaseClass = PDFListView;

public:
    explicit PDFAppToolPageGridView(QWidget* parent);
    virtual ~PDFAppToolPageGridView() override;

    /// Height of the content (the tiles with the spacing of the view)
    int getContentHeight() const;

    /// Distance from the edge of a tile, in which a click belongs to the gap beside it
    static constexpr int GAP_ZONE = 10;

    /// A click between two tiles (Split At selected pages 383:5715 "Click between two pages to
    /// start a new file"): pageClicked says the row after the gap; a click in the middle of a
    /// tile is no click. Space is a click of the gap before the current tile.
    void setGapClicks(bool isGapClicks);
    bool isGapClicks() const { return m_isGapClicks; }

    /// The row after the gap at the point of the viewport (its leading edge, a cut mark of
    /// the tile and the trailing edge of the row before it), -1 = no gap (the first row has none)
    int getGapAt(const QPoint& point) const;

Q_SIGNALS:
    /// The tile of the page has been clicked (or Space on it); a row of the model
    void pageClicked(int pageIndex, Qt::KeyboardModifiers modifiers);

    /// Delete on the current tile
    void deleteRequested(int pageIndex);

    /// The layout of the tiles has another height (emitted from the event loop after the
    /// layout of the view, never inside of it)
    void contentHeightChanged(int height);

protected:
    virtual void updateGeometries() override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;

private:
    void queueClick(int pageIndex, Qt::KeyboardModifiers modifiers, bool isDelete);
    void emitQueued();
    void emitContentHeight();

    int m_pressedPage = -1;
    int m_pressedGap = -1;
    bool m_isGapClicks = false;
    int m_lastContentHeight = -1;
    bool m_isContentHeightQueued = false;
    int m_queuedPage = -1;
    Qt::KeyboardModifiers m_queuedModifiers;
    bool m_isQueuedDelete = false;
};

/// The tile of the grid of a tool: the page tile of the design and the marks of a tile of
/// the tool (K45), which the page tile has not: the dot of the file in front of the label
/// (Split At selected pages 383:5715, Interleave 385:13263), the muted page (Insert
/// 388:15487, the replaced pages 388:17167) and the cut before the tile (Split 383:5715: the
/// insert-marker of 10 in the flow of the tiles with the amber line of 3 in its middle, 4 in
/// front of the page tile - a tile with a cut is wider by CUT_SLOT). The marks lie inside the
/// rectangle of the tile, so the repaint of one tile never leaves a stale mark.
///
/// The marks come from the roles of any model (the grid of one document, the grouped view of
/// Merge, Interleave, Split and Insert): PDFComponentRoles::HueRole (the int of PDFAccentHue,
/// the dot), DimmedRole, CutBeforeRole and SlotScaleRole (a tile larger than the slot of the
/// delegate: the blank pages of Insert 388:16811); the page tile reads the rest
/// (PDFPageTileDelegate). GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageTileDelegate : public PDFPageTileDelegate
{
    Q_OBJECT

private:
    using BaseClass = PDFPageTileDelegate;

public:
    /// Roles of the marks (the roles of the components end at PDFComponentRoles::LastRole)
    enum Role
    {
        DimmedRole = PDFComponentRoles::LastRole + 0x100,  ///< bool: the page is drawn muted
        CutBeforeRole,                                      ///< bool: a new file starts at this page
        SlotScaleRole,                                      ///< double: the slot of the tile is the slot of the delegate scaled (1 = none)
        PaperBorderRole                                     ///< bool: a page without a picture shows the border of its paper (the blank pages 388:17078)
    };

    /// Diameter of the dot, its gap to the label, the width of the cut, the gap of the label
    /// to the slot (PAGE_TILE_GAP of the page tile)
    static constexpr int DOT_SIZE = 6;
    static constexpr int DOT_GAP = 4;
    static constexpr int CUT_WIDTH = 3;
    static constexpr int CUT_MARKER = 10;
    static constexpr int CUT_SLOT = CUT_MARKER + 4;
    static constexpr int LABEL_GAP = 4;
    static constexpr int LABEL_HEIGHT = 13;

    /// Opacity of the veil of a muted page: the page is drawn at 35 % (388:15756, the lines of the
    /// dimmed pages 6-12 #9CA3AF become #DCDFE3 on the white card)
    static constexpr double DIMMED_OPACITY = 0.65;

    explicit PDFAppToolPageTileDelegate(QObject* parent);

    /// The color below the tiles: the veil of a muted page has it (bg/track for the grids on
    /// the canvas, the default; bg/surface for the tiles in the cards of a grouped view)
    void setBackgroundColor(PDFThemeColors::Color color) { m_backgroundColor = color; }
    PDFThemeColors::Color getBackgroundColor() const { return m_backgroundColor; }

    /// The content of the tile with the slot of the row (SlotScaleRole)
    PDFComponentPainter::PageTile getTileContent(const QModelIndex& index, bool withPixmap = true) const;

    /// The slot of the delegate scaled by \p scale (each side rounded)
    QSize getScaledSlotSize(double scale) const;

    /// The rectangles of the marks of the tile in \p rect (empty = no mark): the page, the
    /// dot and the cut; tests check the geometry by them
    struct MarkRects
    {
        QRectF tile;    ///< The page tile (the rectangle of the item without the mark of a cut)
        QRectF page;
        QRectF dot;
        QRectF cut;
    };
    MarkRects getMarkRects(const QStyleOptionViewItem& option, const QModelIndex& index) const;

    /// The rectangle of the page tile in the item \p rect: without the mark of a cut in front of it
    static QRectF getTileRect(const QRectF& rect, bool hasCut, bool isRightToLeft);

protected:
    virtual void paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const override;
    virtual QSize getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const override;

private:
    /// The rectangle \p leftToRight of the tile \p rect mirrored for a right-to-left layout
    static QRectF getMirrored(const QRectF& rect, const QRectF& leftToRight, bool isRightToLeft);

    /// The dot of the row (HueRole), std::nullopt = none
    static std::optional<PDFAccentHue> getDotHue(const QModelIndex& index);

    PDFThemeColors::Color m_backgroundColor = PDFThemeColors::BgTrack;
};

/// The canvas page of a tool, which works on pages (Rotate 389:20177, Remove 388:20614):
/// bg/track, the bar of the preview (preview-bar 389:20436: 36 high, Semi Bold 13), the
/// grid of the page tiles in a QListView of the icon mode (2000 pages: only the visible
/// tiles ask for their pictures, PDFAppThumbnailScheduler) and the hint below the tiles
/// (Regular 11 text/tertiary, 10 below them). The grid is as high as its tiles, while they
/// fit, so the hint follows the last row as in the frames; a longer grid scrolls.
///
/// Rows with groups (setRows) are the cards of the grouped view (PDFAppToolMergePageView with
/// the same model: the files of Split 383:3315) in the place of the grid; the hint may stand
/// above the tiles (383:5715) and the gap of the tiles may differ (4 in 383:5715).
///
/// Ownership: the grid owns both views, the model (a child of the list view), the delegates
/// and the scheduler of the pictures of the shown view (a new one, when the shown view
/// changes); the proxy and the document of the pictures are held by QPointer.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageGrid : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 389:20177: the padding of the canvas, the bar, its gap to the stage, the
    /// gap of the hint; the spacing of the view lies around the tiles (also in front of the first)
    static constexpr int PADDING = 20;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int BAR_GAP = 12;
    static constexpr int HINT_GAP = 10;
    static constexpr int TILE_SPACING = 6;

    /// Size of the tiles chosen in the toolbar of the tool (Thumbnails: Small / Medium /
    /// Large, pages-rotate-menus 1359:33500); Small is the slot of the frames, Medium the
    /// default (owner 2026-11-07), Small the default of Split, Extract and Interleave (owner
    /// 2026-11-09); a size chosen by the user is the size of every grid
    enum class TileSize
    {
        Small,
        Medium,
        Large
    };

    /// Scale of the slot of a size (Small 1, Medium 1.5, Large 2)
    static double getTileScale(TileSize size);

    /// Stored value of a size ("small", "medium", "large", the state setting
    /// PDFSettingKeys::toolsPageGridSize) and back (an unknown text is Medium)
    static QString getTileSizeId(TileSize size);
    static TileSize getTileSizeFromId(const QString& id);

    /// \param slotSize Slot of the page of the tiles in the size Small, the slot of the frames
    ///        (104 x 104 Rotate, 82 x 112 Remove); the grid starts in the size Medium
    /// \param parent Parent widget
    explicit PDFAppToolPageGrid(const QSize& slotSize, QWidget* parent);
    virtual ~PDFAppToolPageGrid() override;

    /// The document and its view (the pictures of the pages come from its compiled pages)
    void setDocument(PDFDocumentPointer document, const QString& documentId, PDFAppDocumentView* view);

    void setHeaderText(const QString& text);
    void setHintText(const QString& text);

    /// The text of the header (tests)
    QString getHeaderText() const;

    /// The tiles of the tool, indexed by the page: one dataChanged and, when a size may change,
    /// one sizeHintChanged
    void setTiles(std::vector<PDFAppToolPageGridModel::Tile> tiles);

    /// The rows of the pages and their groups (PDFAppToolPageGridModel::setRows): with groups
    /// the grouped view shows them as cards, without groups the grid does
    void setRows(std::optional<std::vector<int>> pages, std::vector<PDFAppToolGroupedPageModel::Group> groups);

    /// The cards of the grouped view are shown in the place of the grid
    bool isGrouped() const { return m_isGrouped; }

    /// The gap of the tiles of the grid (TILE_SPACING; 4 in Split At selected pages 383:5715)
    void setTileSpacing(int spacing);
    int getTileSpacing() const { return m_tileSpacing; }

    /// The hint above the tiles (383:5715, 10 above them) or below them (389:20890: 10, 385:9405:
    /// 12 below them) and its gap to the tiles
    void setHintPlacement(bool isAbove, int gap);

    /// A click between two tiles of the grid (PDFAppToolPageGridView::setGapClicks)
    void setGapClicks(bool isGapClicks);

    /// The size of the tiles: the slot of the delegate is the slot of Medium scaled, the
    /// tiles are laid out again and their pictures are rendered for the new size
    void setTileSize(TileSize size);
    TileSize getTileSize() const { return m_tileSize; }

    /// Scrolls the grid or the grouped view to the tile of the page (the card of its group)
    void scrollToPage(int pageIndex);

    PDFAppToolPageGridView* getView() const { return m_view; }
    PDFAppToolMergePageView* getGroupView() const;
    PDFAppToolPageGridModel* getModel() const { return m_model; }
    PDFPageTileDelegate* getDelegate() const { return m_delegate; }
    PDFPageTileDelegate* getGroupDelegate() const { return m_groupDelegate; }
    PDFAppThumbnailScheduler* getScheduler() const { return m_scheduler; }
    PDFLabel* getHeaderLabel() const { return m_header; }
    PDFAppParagraph* getHintLabel() const { return m_hint; }

Q_SIGNALS:
    /// The tile of the page has been clicked (the page of the row of the grid)
    void pageClicked(int pageIndex, Qt::KeyboardModifiers modifiers);
    void deleteRequested(int pageIndex);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updatePictureSize();
    void updateLayout();
    void updateScheduler();
    QSize getScaledSlotSize(TileSize size) const;
    void onContentHeightChanged(int height);
    void onGroupContentHeightChanged(int height);
    void onViewPageClicked(int row, Qt::KeyboardModifiers modifiers);
    void onViewDeleteRequested(int row);

    QPointer<QVBoxLayout> m_layout;
    QPointer<PDFLabel> m_header;
    QPointer<PDFAppToolPageGridView> m_view;
    QPointer<QWidget> m_groupPage;
    QPointer<PDFAppToolMergePageView> m_groupView;
    QPointer<PDFAppToolPageGridModel> m_model;
    QPointer<PDFPageTileDelegate> m_delegate;
    QPointer<PDFPageTileDelegate> m_groupDelegate;
    QPointer<PDFAppThumbnailScheduler> m_scheduler;
    QPointer<PDFAppParagraph> m_hint;
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<PDFAppDocument> m_appDocument;
    QSize m_baseSlotSize;
    TileSize m_tileSize = TileSize::Medium;
    int m_tileSpacing = TILE_SPACING;
    int m_hintGap = HINT_GAP;
    bool m_isHintAbove = false;
    bool m_isGrouped = false;
};

/// The menu of the size of the tiles of a page grid, one implementation for every tool with a
/// page grid: Thumbnails: Medium in the toolbar of Rotate and Remove (tb-thumbnail-size
/// 1359:33481, panel-thumbnail-size 1359:33719), Size: Small in the bar of the preview of
/// Merge (1370:34102, K11). The panel has the rows S / M / L (the tile, the title, the
/// description, the check of the chosen size); the button says the chosen size by its format.
/// The chosen size is the state setting PDFSettingKeys::toolsPageGridSize shared by all page
/// grids (readSize, storeSize).
///
/// Ownership: the panel belongs to its button (the toolbar of the tool), the menu is a child
/// of the GUI of the tool and holds the panel and the rows by QPointer. A row closes the panel
/// first, then the menu emits sizeChosen (the last statement: a slot may delete the menu).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolTileSizeMenu : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    using TileSize = PDFAppToolPageGrid::TileSize;

    /// Width of panel-thumbnail-size 1359:33719
    static constexpr int PANEL_WIDTH = 260;

    /// \param panel The panel of the button (the rows are added to it)
    /// \param titleFormat Text of the button with %1 the title of the size ("Thumbnails: %1")
    /// \param parent Parent object (the GUI of the tool)
    explicit PDFAppToolTileSizeMenu(PDFAppToolPanelPopup* panel, const QString& titleFormat, QObject* parent);
    virtual ~PDFAppToolTileSizeMenu() override;

    /// The size of the tiles stored by the user, \p defaultSize when the user has chosen none
    /// (Medium of most grids, Small of Split, Extract and Interleave - owner 2026-11-09)
    static TileSize readSize(const PDFAppContext* context, TileSize defaultSize = TileSize::Medium);

    /// Stores the size of the tiles chosen by the user (the store announces it to its readers:
    /// the last call); every size is stored, also the default of a tool
    static void storeSize(PDFAppContext* context, TileSize size);

    /// The chosen size shown by the button and the check of its row (no signal)
    void setSize(TileSize size);
    TileSize getSize() const { return m_size; }

    /// The size of the tool without a choice of the user: its row says "The default size"
    void setDefaultSize(TileSize size);
    TileSize getDefaultSize() const { return m_defaultSize; }

    QToolButton* getButton() const;
    PDFAppToolPanelPopup* getPanel() const;
    PDFTileRow* getRow(TileSize size) const;

Q_SIGNALS:
    /// The user has chosen a size in the panel (the panel is closed)
    void sizeChosen(pdf::PDFAppToolPageGrid::TileSize size);

private:
    void update();
    void onRowClicked();

    /// The description of the row of the size (the default size says it)
    QString getDescription(TileSize size) const;

    QPointer<PDFAppToolPanelPopup> m_panel;
    std::vector<std::pair<TileSize, QPointer<PDFTileRow>>> m_rows;
    QString m_titleFormat;
    TileSize m_size = TileSize::Medium;
    TileSize m_defaultSize = TileSize::Medium;
};

}   // namespace pdf

#endif // PDFAPPTOOLPAGEGRID_H
