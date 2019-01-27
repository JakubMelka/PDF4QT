//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFDRAWSPACECONTROLLER_H
#define PDFDRAWSPACECONTROLLER_H

#include "pdfdocument.h"

#include <QRectF>
#include <QObject>
#include <QMarginsF>

class QPainter;
class QScrollBar;

namespace pdf
{
class PDFWidget;
class PDFDrawWidget;

/// This class controls draw space - page layout. Pages are divided into blocks
/// each block can contain one or multiple pages. Units are in milimeters.
/// Pages are layouted in zoom-independent mode.
class PDFDrawSpaceController : public QObject
{
    Q_OBJECT

public:
    explicit PDFDrawSpaceController(QObject* parent);

    /// Sets the document and recalculates the draw space. Document can be nullptr,
    /// in that case, draw space is cleared.
    /// \param document Document
    void setDocument(const PDFDocument* document);

    /// Sets the page layout. Page layout can be one of the PDF's page layouts.
    /// \param pageLayout Page layout
    void setPageLayout(PageLayout pageLayout);

    /// Returns the page layout
    PageLayout getPageLayout() const { return m_pageLayoutMode; }

    /// Returns the block count
    size_t getBlockCount() const { return m_blockItems.size(); }

    /// Return the bounding rectangle of the block. If block doesn't exist,
    /// then invalid rectangle is returned (no exception is thrown).
    /// \param blockIndex Index of the block
    QRectF getBlockBoundingRectangle(size_t blockIndex) const;

    /// Represents layouted page. This structure contains index of the block, index of the
    /// page and page rectangle, in which the page is contained.
    struct LayoutItem
    {
        constexpr inline explicit LayoutItem() : blockIndex(-1), pageIndex(-1), pageRotation(PageRotation::None) { }
        constexpr inline explicit LayoutItem(PDFInteger blockIndex, PDFInteger pageIndex, PageRotation rotation, const QRectF& pageRectMM) :
            blockIndex(blockIndex), pageIndex(pageIndex), pageRotation(rotation), pageRectMM(pageRectMM) { }

        PDFInteger blockIndex;
        PDFInteger pageIndex;
        PageRotation pageRotation;
        QRectF pageRectMM;
    };

    using LayoutItems = std::vector<LayoutItem>;

    /// Returns the layout items for desired block. If block doesn't exist,
    /// then empty array is returned.
    /// \param blockIndex Index of the block
    LayoutItems getLayoutItems(size_t blockIndex) const;

signals:
    void drawSpaceChanged();

private:
    /// Recalculates the draw space. Preserves setted page rotation.
    void recalculate();

    /// Clears the draw space. Emits signal if desired.
    void clear(bool emitSignal);

    /// Represents data for the single block. Contains block size in milimeters.
    struct LayoutBlock
    {
        constexpr inline explicit LayoutBlock() = default;
        constexpr inline explicit LayoutBlock(const QRectF& blockRectMM) : blockRectMM(blockRectMM) { }

        QRectF blockRectMM;
    };

    using BlockItems = std::vector<LayoutBlock>;

    const PDFDocument* m_document;

    PageLayout m_pageLayoutMode;
    LayoutItems m_layoutItems;
    BlockItems m_blockItems;
    PDFReal m_verticalSpacingMM;
    PDFReal m_horizontalSpacingMM;
};

/// This is a proxy class to draw space controller using widget. We have two spaces, pixel space
/// (on the controlled widget) and device space (device is draw space controller).
class PDFDrawWidgetProxy : public QObject
{
    Q_OBJECT

public:
    explicit PDFDrawWidgetProxy(QObject* parent);

    /// Sets the document and updates the draw space. Document can be nullptr,
    /// in that case, draw space is cleared.
    /// \param document Document
    void setDocument(const PDFDocument* document);

    void init(PDFWidget* widget);

    /// Updates the draw space area
    void update();

    /// Draws the actually visible pages on the painter using the rectangle.
    /// Rectangle is space in the widget, which is used for painting the PDF.
    /// \param painter Painter to paint the PDF pages
    /// \param rect Rectangle in which the content is painted
    void draw(QPainter* painter, QRect rect);

signals:
    void drawSpaceChanged();

private:
    struct LayoutItem
    {
        constexpr inline explicit LayoutItem() : pageIndex(-1), pageRotation(PageRotation::None) { }
        constexpr inline explicit LayoutItem(PDFInteger pageIndex, PageRotation rotation, const QRect& pageRect) :
            pageIndex(pageIndex), pageRotation(rotation), pageRect(pageRect) { }


        PDFInteger pageIndex;
        PageRotation pageRotation;
        QRect pageRect;
    };

    struct Layout
    {
        inline void clear()
        {
            items.clear();
            blockRect = QRect();
        }

        std::vector<LayoutItem> items;
        QRect blockRect;
    };

    static constexpr size_t INVALID_BLOCK_INDEX = std::numeric_limits<size_t>::max();

    // Minimal/maximal zoom is from 8% to 6400 %, according to the PDF 1.7 Reference,
    // Appendix C.

    static constexpr PDFReal MIN_ZOOM = 8.0 / 100.0;
    static constexpr PDFReal MAX_ZOOM = 6400.0 / 100.0;

    /// Converts rectangle from device space to the pixel space
    QRectF fromDeviceSpace(const QRectF& rect) const;

    /// Returns true, if we are in the block mode (multiple blocks with separate pages),
    /// or continuous mode (single block with continuous list of separated pages).
    bool isBlockMode() const;

    /// Flag, disables the update
    bool m_updateDisabled;

    /// Current block (in the draw space controller)
    size_t m_currentBlock;

    /// Number of pixels (fractional) per milimeter (unit is pixel/mm) of the screen,
    /// so, size of the area in milimeters can be computed as pixelCount * m_pixelPerMM [mm].
    PDFReal m_pixelPerMM;

    /// Zoom from widget space to device space. So, for example 2.00 corresponds to 200% zoom,
    /// and each 1 cm of widget area corresponds to 0.5 cm of the device space area.
    PDFReal m_zoom;

    /// Converts pixel to device space units (mm) using zoom
    PDFReal m_pixelToDeviceSpaceUnit;

    /// Converts device space units (mm) to real pixels using zoom
    PDFReal m_deviceSpaceUnitToPixel;

    /// Actual vertical offset of the draw space area in the widget (so block will be drawn
    /// with this vertical offset)
    PDFInteger m_verticalOffset;

    /// Actual horizontal offset of the draw space area in the widget (so block will be drawn
    /// with this horizontal offset)
    PDFInteger m_horizontalOffset;

    /// Draw space controller
    PDFDrawSpaceController* m_controller;

    /// Controlled draw widget (proxy is for this widget)
    PDFDrawWidget* m_widget;

    /// Vertical scrollbar
    QScrollBar* m_verticalScrollbar;

    /// Horizontal scrollbar
    QScrollBar* m_horizontalScrollbar;

    /// Current page layout
    Layout m_layout;
};

}   // namespace pdf

#endif // PDFDRAWSPACECONTROLLER_H
