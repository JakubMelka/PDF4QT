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
class QToolButton;

namespace pdf
{

class PDFAppContext;
class PDFAppDocumentView;
class PDFAppParagraph;
class PDFAppToolPanelPopup;
class PDFTileRow;

/// The pages of the grid of a tool (Rotate 389:20177, Remove 388:20614): the pictures of the
/// thumbnails of the document (PDFAppThumbnailsModel, the same scheduler and cache keys) and
/// the state of each tile, which the tool gives. A tile shows its page as it will be saved:
/// the picture turned by the extra rotation of the tool and the size of the page turned with
/// it. A change of the tiles is one dataChanged of the range of the changed tiles.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageGridModel : public PDFAppThumbnailsModel
{
    Q_OBJECT

private:
    using BaseClass = PDFAppThumbnailsModel;

public:
    /// State of a tile of the tool
    struct Tile
    {
        PDFComponentPainter::PageTileState state = PDFComponentPainter::PageTileState::Normal;
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

    /// The document of the pages (the rows are reset, when the number of the pages changes)
    void setPageDocument(PDFDocumentPointer document, const QString& documentId);

    /// The tiles of all pages (missing ones are Normal). \returns the range of the rows,
    /// whose tile has changed (-1, -1 = none), it has got one dataChanged
    std::pair<int, int> setTiles(std::vector<Tile> tiles);

    /// Tile of the page (Normal for an invalid page)
    Tile getTile(int pageIndex) const;

    virtual QVariant data(const QModelIndex& index, int role) const override;

    /// The picture of a page turned by \p turn as it will be saved (the grids of the tools,
    /// which turn pages: Rotate, Merge); the turned picture is kept in QPixmapCache under
    /// \p key with the turn. A picture, which is no QPixmap, or no turn: the picture itself.
    static QVariant getTurnedPicture(const QVariant& picture, const QString& key, PageRotation turn);

private:
    PDFDocumentPointer m_pageDocument;
    std::vector<QSizeF> m_pageSizes;   ///< The visible sizes of the pages (crop box rotated by /Rotate)
    std::vector<Tile> m_tiles;
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

Q_SIGNALS:
    /// The tile of the page has been clicked (or Space on it)
    void pageClicked(int pageIndex, Qt::KeyboardModifiers modifiers);

    /// Delete on the current tile
    void deleteRequested(int pageIndex);

    /// The layout of the tiles has another height (emitted from the event loop after the
    /// layout of the view, never inside of it)
    void contentHeightChanged(int height);

protected:
    virtual void updateGeometries() override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;

private:
    void queueClick(int pageIndex, Qt::KeyboardModifiers modifiers, bool isDelete);
    void emitQueued();
    void emitContentHeight();

    int m_pressedPage = -1;
    int m_lastContentHeight = -1;
    bool m_isContentHeightQueued = false;
    int m_queuedPage = -1;
    Qt::KeyboardModifiers m_queuedModifiers;
    bool m_isQueuedDelete = false;
};

/// The tile of the grid of a tool: the page tile of the design and the marks of a tile of
/// the tool (K45), which the page tile has not: the dot of the file in front of the label
/// (Split At selected pages 383:5715, Interleave 385:13263), the muted page (Insert
/// 388:15487, the replaced pages 388:17167) and the cut before the tile (amber, Split
/// 383:5715). The marks lie inside the rectangle of the tile, so the repaint of one tile never
/// leaves a stale mark.
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
        QRectF page;
        QRectF dot;
        QRectF cut;
    };
    MarkRects getMarkRects(const QStyleOptionViewItem& option, const QModelIndex& index) const;

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
    /// default (owner 2026-11-07)
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

    /// The tiles of the tool: one dataChanged and, when a size may change, one sizeHintChanged
    void setTiles(std::vector<PDFAppToolPageGridModel::Tile> tiles);

    /// The size of the tiles: the slot of the delegate is the slot of Medium scaled, the
    /// tiles are laid out again and their pictures are rendered for the new size
    void setTileSize(TileSize size);
    TileSize getTileSize() const { return m_tileSize; }

    PDFAppToolPageGridView* getView() const { return m_view; }
    PDFAppToolPageGridModel* getModel() const { return m_model; }
    PDFPageTileDelegate* getDelegate() const { return m_delegate; }
    PDFAppThumbnailScheduler* getScheduler() const { return m_scheduler; }
    PDFLabel* getHeaderLabel() const { return m_header; }
    PDFAppParagraph* getHintLabel() const { return m_hint; }

Q_SIGNALS:
    void pageClicked(int pageIndex, Qt::KeyboardModifiers modifiers);
    void deleteRequested(int pageIndex);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updatePictureSize();
    QSize getScaledSlotSize(TileSize size) const;
    void onContentHeightChanged(int height);

    QPointer<PDFLabel> m_header;
    QPointer<PDFAppToolPageGridView> m_view;
    QPointer<PDFAppToolPageGridModel> m_model;
    QPointer<PDFPageTileDelegate> m_delegate;
    QPointer<PDFAppThumbnailScheduler> m_scheduler;
    QPointer<PDFAppParagraph> m_hint;
    QSize m_baseSlotSize;
    TileSize m_tileSize = TileSize::Medium;
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

    /// The size of the tiles stored by the user (Medium without a database)
    static TileSize readSize(const PDFAppContext* context);

    /// Stores the size of the tiles (the store announces it to its readers: the last call)
    static void storeSize(PDFAppContext* context, TileSize size);

    /// The chosen size shown by the button and the check of its row (no signal)
    void setSize(TileSize size);
    TileSize getSize() const { return m_size; }

    QToolButton* getButton() const;
    PDFAppToolPanelPopup* getPanel() const;
    PDFTileRow* getRow(TileSize size) const;

Q_SIGNALS:
    /// The user has chosen a size in the panel (the panel is closed)
    void sizeChosen(pdf::PDFAppToolPageGrid::TileSize size);

private:
    void update();
    void onRowClicked();

    QPointer<PDFAppToolPanelPopup> m_panel;
    std::vector<std::pair<TileSize, QPointer<PDFTileRow>>> m_rows;
    QString m_titleFormat;
    TileSize m_size = TileSize::Medium;
};

}   // namespace pdf

#endif // PDFAPPTOOLPAGEGRID_H
