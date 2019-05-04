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

#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfrenderer.h"
#include "pdffont.h"

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
    virtual ~PDFDrawSpaceController() override;

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

    /// Returns the document
    const PDFDocument* getDocument() const { return m_document; }

    /// Returns the font cache
    const PDFFontCache* getFontCache() const { return &m_fontCache; }

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

    static constexpr size_t FONT_CACHE_LIMIT = 32;
    static constexpr size_t REALIZED_FONT_CACHE_LIMIT = 128;

    const PDFDocument* m_document;

    PageLayout m_pageLayoutMode;
    LayoutItems m_layoutItems;
    BlockItems m_blockItems;
    PDFReal m_verticalSpacingMM;
    PDFReal m_horizontalSpacingMM;

    /// Font cache
    PDFFontCache m_fontCache;
};

/// This is a proxy class to draw space controller using widget. We have two spaces, pixel space
/// (on the controlled widget) and device space (device is draw space controller).
class PDFFORQTLIBSHARED_EXPORT PDFDrawWidgetProxy : public QObject
{
    Q_OBJECT

public:
    explicit PDFDrawWidgetProxy(QObject* parent);
    virtual ~PDFDrawWidgetProxy() override;

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

    enum Operation
    {
        NavigateDocumentStart,
        NavigateDocumentEnd,
        NavigateNextPage,
        NavigatePreviousPage,
        NavigateNextStep,
        NavigatePreviousStep
    };

    /// Performs the desired operation (for example navigation).
    /// \param operation Operation to be performed
    void performOperation(Operation operation);

    /// Scrolls by pixels, if it is possible. If it is not possible to scroll,
    /// then nothing happens. Returns pixel offset, by which view camera was moved.
    /// \param offset Offset in pixels
    QPoint scrollByPixels(QPoint offset);

    /// Sets the zoom. Tries to preserve current offsets (so the current visible
    /// area will be visible after the zoom).
    /// \param zoom New zoom
    void zoom(PDFReal zoom);

    /// Returns current zoom from widget space to device space. So, for example 2.00 corresponds to 200% zoom,
    /// and each 1 cm of widget area corresponds to 0.5 cm of the device space area.
    PDFReal getZoom() const { return m_zoom; }

    /// Sets the page layout. Page layout can be one of the PDF's page layouts.
    /// \param pageLayout Page layout
    void setPageLayout(PageLayout pageLayout);

    /// Returns the page layout
    PageLayout getPageLayout() const { return m_controller->getPageLayout(); }

    /// Returns pages, which are intersecting rectangle (even partially)
    /// \param rect Rectangle to test
    std::vector<PDFInteger> getPagesIntersectingRect(QRect rect) const;

    /// Returns bounding box of pages, which are intersecting rectangle (even partially)
    /// \param rect Rectangle to test
    QRect getPagesIntersectingRectBoundingBox(QRect rect) const;

    /// Returns true, if we are in the block mode (multiple blocks with separate pages),
    /// or continuous mode (single block with continuous list of separated pages).
    bool isBlockMode() const;

    static constexpr PDFReal ZOOM_STEP = 1.2;

signals:
    void drawSpaceChanged();
    void pageLayoutChanged();
    void renderingError(PDFInteger pageIndex, const QList<PDFRenderError>& errors);

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

    void onHorizontalScrollbarValueChanged(int value);
    void onVerticalScrollbarValueChanged(int value);

    void setHorizontalOffset(int value);
    void setVerticalOffset(int value);
    void setBlockIndex(int index);

    void updateHorizontalScrollbarFromOffset();
    void updateVerticalScrollbarFromOffset();

    template<typename T>
    struct Range
    {
        constexpr inline Range() : min(T()), max(T()) { }
        constexpr inline Range(T value) : min(value), max(value) { }
        constexpr inline Range(T min, T max) : min(min), max(max) { }

        T min;
        T max;

        constexpr inline T bound(T value) { return qBound(min, value, max); }
    };

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

    /// Range of vertical offset
    Range<PDFInteger> m_verticalOffsetRange;

    /// Actual horizontal offset of the draw space area in the widget (so block will be drawn
    /// with this horizontal offset)
    PDFInteger m_horizontalOffset;

    /// Range for horizontal offset
    Range<PDFInteger> m_horizontalOffsetRange;

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
