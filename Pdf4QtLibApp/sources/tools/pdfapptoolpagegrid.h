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

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdffluentwidgets.h"
#include "pdfpage.h"

#include <QPointer>
#include <QWidget>

#include <utility>
#include <vector>

class QScrollBar;

namespace pdf
{

class PDFAppDocumentView;
class PDFAppParagraph;

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
        QString caption;                                ///< "90° right", "will be removed", empty = none
        QString badgeIconName;                          ///< Badge of the page, empty = none
        PageRotation extraRotation = PageRotation::None;///< Turn of the picture of the page (Rotate Pages)

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

}   // namespace pdf

#endif // PDFAPPTOOLPAGEGRID_H
