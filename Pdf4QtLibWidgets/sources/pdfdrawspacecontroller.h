// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#ifndef PDFDRAWSPACECONTROLLER_H
#define PDFDRAWSPACECONTROLLER_H

#include "pdfsignaturehandler.h"
#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfrenderer.h"
#include "pdffont.h"
#include "pdfdocumentdrawinterface.h"
#include "pdfwidgetsnapshot.h"

#include <QRectF>
#include <QObject>
#include <QMarginsF>

class QPainter;
class QScrollBar;
class QTimer;

namespace pdf
{
class PDFProgress;
class PDFWidget;
class PDFCMSManager;
class PDFTextLayoutGetter;
class PDFWidgetAnnotationManager;
class PDFAsynchronousPageCompiler;
class PDFAsynchronousTextLayoutCompiler;

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
    /// in that case, draw space is cleared. Optional content activity can be nullptr,
    /// in that case, no content is suppressed.
    /// \param document Document
    void setDocument(const PDFModifiedDocument& document);

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
        constexpr inline explicit LayoutItem() : blockIndex(-1), pageIndex(-1), groupIndex(-1) { }
        constexpr inline explicit LayoutItem(PDFInteger blockIndex, PDFInteger pageIndex, PDFInteger groupIndex, const QRectF& pageRectMM) :
            blockIndex(blockIndex), pageIndex(pageIndex), groupIndex(groupIndex), pageRectMM(pageRectMM) { }

        bool operator ==(const LayoutItem&) const = default;

        bool isValid() const { return pageIndex != -1; }

        PDFInteger blockIndex;
        PDFInteger pageIndex;
        PDFInteger groupIndex; ///< Page group index
        QRectF pageRectMM;
    };

    using LayoutItems = std::vector<LayoutItem>;

    /// Returns the layout items for desired block. If block doesn't exist,
    /// then empty array is returned.
    /// \param blockIndex Index of the block
    LayoutItems getLayoutItems(size_t blockIndex) const;

    /// Returns layout for single page. If page index is invalid,
    /// or page layout cannot be found, then invalid layout item is returned.
    /// \param pageIndex Page index
    LayoutItem getLayoutItemForPage(PDFInteger pageIndex) const;

    /// Returns the document
    const PDFDocument* getDocument() const { return m_document; }

    /// Returns the font cache
    const PDFFontCache* getFontCache() const { return &m_fontCache; }

    /// Returns the font cache
    PDFFontCache* getFontCache() { return &m_fontCache; }

    /// Returns optional content activity
    const PDFOptionalContentActivity* getOptionalContentActivity() const { return m_optionalContentActivity; }

    /// Returns reference bounding box for correct calculation of zoom fit/fit vertical/fit horizontal.
    /// If zoom is set in a way to display this bounding box on a screen, then it is assured that
    /// any page on the screen will fit this bounding box, regardless of mode (single/two columns, etc.).
    QSizeF getReferenceBoundingBox() const;

    /// Returns page rotation
    PageRotation getPageRotation() const { return m_pageRotation; }

    /// Sets page rotation
    void setPageRotation(PageRotation pageRotation);

    /// Set custom layout. Custom layout provides a way how to define
    /// custom page layout, including blocks. Block indices must be properly defined,
    /// that means block index must start by zero and must be continuous. If this
    /// criteria are not fulfilled, behaviour is undefined.
    void setCustomLayout(LayoutItems customLayoutItems);

    /// Returns custom layout
    const LayoutItems& getCustomLayout() const { return m_customLayoutItems; }

signals:
    void drawSpaceChanged();
    void repaintNeeded();
    void pageImageChanged(bool all, const std::vector<PDFInteger>& pages);

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
    const PDFOptionalContentActivity* m_optionalContentActivity;

    PageLayout m_pageLayoutMode;
    LayoutItems m_layoutItems;
    BlockItems m_blockItems;
    PDFReal m_verticalSpacingMM;
    PDFReal m_horizontalSpacingMM;
    PageRotation m_pageRotation;
    LayoutItems m_customLayoutItems;

    /// Font cache
    PDFFontCache m_fontCache;
};

/// This is a proxy class to draw space controller using widget. We have two spaces, pixel space
/// (on the controlled widget) and device space (device is draw space controller).
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFDrawWidgetProxy : public QObject
{
    Q_OBJECT

public:
    explicit PDFDrawWidgetProxy(QObject* parent);
    virtual ~PDFDrawWidgetProxy() override;

    /// Sets the document and updates the draw space. Document can be nullptr,
    /// in that case, draw space is cleared. Optional content activity can be nullptr,
    /// in that case, no content is suppressed.
    /// \param document Document
    void setDocument(const PDFModifiedDocument& document, std::vector<PDFSignatureVerificationResult> signatureVerificationResult);

    void init(PDFWidget* widget);

    /// Updates the draw space area
    void update();

    /// Creates page point to device point matrix for the given rectangle. It creates transformation
    /// from page's media box to the target rectangle.
    /// \param page Page, for which we want to create matrix
    /// \param rectangle Page rectangle, to which is page media box transformed
    QTransform createPagePointToDevicePointMatrix(const PDFPage* page, const QRectF& rectangle) const;

    /// Draws the actually visible pages on the painter using the rectangle.
    /// Rectangle is space in the widget, which is used for painting the PDF.
    /// This function is using drawPages function to draw all pages. After that,
    /// custom drawing is performed.
    /// \sa drawPages
    /// \param painter Painter to paint the PDF pages
    /// \param rect Rectangle in which the content is painted
    void draw(QPainter* painter, QRect rect);

    /// Draws the actually visible pages on the painter using the rectangle.
    /// Rectangle is space in the widget, which is used for painting the PDF.
    /// \param painter Painter to paint the PDF pages
    /// \param rect Rectangle in which the content is painted
    /// \param features Rendering features
    void drawPages(QPainter* painter, QRect rect, PDFRenderer::Features features);

    /// Draws thumbnail image of the given size (so larger of the page size
    /// width or height equals to pixel size and the latter size is rescaled
    /// using the aspect ratio)
    /// \param pixelSize Pixel size
    QImage drawThumbnailImage(PDFInteger pageIndex, int pixelSize) const;

    enum Operation
    {
        ZoomIn,
        ZoomOut,
        ZoomFit,
        ZoomFitWidth,
        ZoomFitHeight,
        NavigateDocumentStart,
        NavigateDocumentEnd,
        NavigateNextPage,
        NavigatePreviousPage,
        NavigateNextStep,
        NavigatePreviousStep,
        RotateRight,
        RotateLeft
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
    /// \param widgetPosition Position of the mouse during zooming
    void zoom(PDFReal zoom, std::optional<QPointF> widgetPosition = std::nullopt);

    enum class ZoomHint
    {
        Fit,
        FitWidth,
        FitHeight
    };

    /// Calculates zoom using given hint (i.e. to fill whole space, fill vertical,
    /// or fill horizontal).
    /// \param hint Zoom hint type
    PDFReal getZoomHint(ZoomHint hint) const;

    /// Go to the specified page
    /// \param pageIndex Page to scroll to
    void goToPage(PDFInteger pageIndex);

    /// Go to the specified page and ensures point on the page is visible
    /// \param pageIndex Page to scroll to
    /// \param ensureVisibleRect Rectangle on page, which should be visible
    void goToPageAndEnsureVisible(PDFInteger pageIndex, QRectF ensureVisibleRect);

    /// Returns current zoom from widget space to device space. So, for example 2.00 corresponds to 200% zoom,
    /// and each 1 cm of widget area corresponds to 0.5 cm of the device space area.
    PDFReal getZoom() const { return m_zoom; }

    /// Sets the page layout. Page layout can be one of the PDF's page layouts.
    /// \param pageLayout Page layout
    void setPageLayout(PageLayout pageLayout);

    /// Sets custom page layout. If this function is used, page layout mode
    /// must be set to 'Custom'.
    /// \param layoutItems Layout items
    void setCustomPageLayout(PDFDrawSpaceController::LayoutItems layoutItems);

    /// Returns the page layout
    PageLayout getPageLayout() const { return m_controller->getPageLayout(); }

    /// Returns pages, which are intersecting rectangle (even partially)
    /// \param rect Rectangle to test
    std::vector<PDFInteger> getPagesIntersectingRect(QRect rect) const;

    /// Returns sorted vector of page indices, which should remain in the cache
    std::vector<PDFInteger> getActivePages() const;

    /// Returns page, under which is point. If no page is under the point,
    /// then -1 is returned. Point is in widget coordinates. If \p pagePoint
    /// is not nullptr, then point in page coordinate space is set here.
    /// \param point Point
    /// \param pagePoint Point in page coordinate system
    PDFInteger getPageUnderPoint(QPoint point, QPointF* pagePoint) const;

    /// Returns bounding box of pages, which are intersecting rectangle (even partially)
    /// \param rect Rectangle to test
    QRect getPagesIntersectingRectBoundingBox(QRect rect) const;

    /// Returns true, if we are in the block mode (multiple blocks with separate pages),
    /// or continuous mode (single block with continuous list of separated pages).
    bool isBlockMode() const;

    /// Updates renderer (in current implementation, renderer for page thumbnails)
    /// using given parameters.
    /// \param rendererEngine Renderer engine
    void updateRenderer(RendererEngine rendererEngine);

    /// Prefetches (prerenders) pages after page with pageIndex, i.e., prepares
    /// for non-flickering scroll operation.
    void prefetchPages(PDFInteger pageIndex);

    static constexpr PDFReal ZOOM_STEP = 1.2;

    const PDFDocument* getDocument() const { return m_controller->getDocument(); }
    PDFFontCache* getFontCache() const { return m_controller->getFontCache(); }
    const PDFOptionalContentActivity* getOptionalContentActivity() const { return m_controller->getOptionalContentActivity(); }
    PDFRenderer::Features getFeatures() const;
    const PDFMeshQualitySettings& getMeshQualitySettings() const { return m_meshQualitySettings; }
    PDFAsynchronousPageCompiler* getCompiler() const { return m_compiler; }
    const PDFCMSManager* getCMSManager() const;
    PDFProgress* getProgress() const { return m_progress; }
    void setProgress(PDFProgress* progress) { m_progress = progress; }
    PDFAsynchronousTextLayoutCompiler* getTextLayoutCompiler() const { return m_textLayoutCompiler; }
    PDFWidget* getWidget() const { return m_widget; }
    RendererEngine getRendererEngine() const { return m_rendererEngine; }
    PageRotation getPageRotation() const { return m_controller->getPageRotation(); }

    void setFeatures(PDFRenderer::Features features);
    void setPreferredMeshResolutionRatio(PDFReal ratio);
    void setMinimalMeshResolutionRatio(PDFReal ratio);
    void setColorTolerance(PDFReal colorTolerance);

    static constexpr PDFReal getMinZoom() { return MIN_ZOOM; }
    static constexpr PDFReal getMaxZoom() { return MAX_ZOOM; }

    void registerDrawInterface(IDocumentDrawInterface* drawInterface) { m_drawInterfaces.insert(drawInterface); }
    void unregisterDrawInterface(IDocumentDrawInterface* drawInterface) { m_drawInterfaces.erase(drawInterface); }

    /// Returns current paper color
    QColor getPaperColor();

    /// Transforms pixels to device space
    /// \param pixel Value in pixels
    PDFReal transformPixelToDeviceSpace(PDFReal pixel) const { return pixel * m_pixelToDeviceSpaceUnit; }

    /// Transforms value in device space to pixel value
    /// \param deviceSpaceValue Value in device space
    PDFReal transformDeviceSpaceToPixel(PDFReal deviceSpaceValue) const { return deviceSpaceValue * m_deviceSpaceUnitToPixel; }

    /// Returns snapshot of current view area
    PDFWidgetSnapshot getSnapshot() const;

    /// Sets page group transparency settings. All pages with a given group index
    /// will be displayed with this transparency settings.
    /// \param groupIndex Group index
    /// \param drawPaper Draw background paper
    /// \param transparency Page graphics transparency
    void setGroupTransparency(PDFInteger groupIndex, bool drawPaper = true, PDFReal transparency = 1.0);
    
    PDFWidgetAnnotationManager* getAnnotationManager() const;
    const std::vector<PDFSignatureVerificationResult>& getSignatureVerificationResult() const { return m_signatureVerificationResult; }

signals:
    void drawSpaceChanged();
    void pageLayoutChanged();
    void renderingError(pdf::PDFInteger pageIndex, const QList<pdf::PDFRenderError>& errors);
    void repaintNeeded();
    void pageImageChanged(bool all, const std::vector<pdf::PDFInteger>& pages);
    void textLayoutChanged();

private:
    struct LayoutItem
    {
        constexpr inline explicit LayoutItem() : pageIndex(-1), groupIndex(-1) { }
        constexpr inline explicit LayoutItem(PDFInteger pageIndex, PDFInteger groupIndex, const QRect& pageRect) :
            pageIndex(pageIndex), groupIndex(groupIndex), pageRect(pageRect) { }


        PDFInteger pageIndex;
        PDFInteger groupIndex; ///< Used to create group of pages (for transparency and overlay)
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

    struct GroupInfo
    {
        bool operator==(const GroupInfo&) const = default;

        bool drawPaper = true;
        PDFReal transparency = 1.0;
    };

    static constexpr size_t INVALID_BLOCK_INDEX = std::numeric_limits<size_t>::max();

    // Minimal/maximal zoom is from 8% to 6400 %, according to the PDF 1.7 Reference,
    // Appendix C.

    static constexpr PDFReal MIN_ZOOM = 8.0 / 100.0;
    static constexpr PDFReal MAX_ZOOM = 6400.0 / 100.0;

    static constexpr qint64 CACHE_CLEAR_TIMEOUT = 5000;
    static constexpr qint64 CACHE_PAGE_EXPIRATION_TIMEOUT = 30000;

    /// Converts rectangle from device space to the pixel space
    QRectF fromDeviceSpace(const QRectF& rect) const;

    void performPageCacheClear();

    void onTextLayoutChanged();
    void onOptionalContentGroupStateChanged();
    void onColorManagementSystemChanged();
    void onHorizontalScrollbarValueChanged(int value);
    void onVerticalScrollbarValueChanged(int value);

    void setHorizontalOffset(int value);
    void setVerticalOffset(int value);
    void setBlockIndex(int index);

    void updateHorizontalScrollbarFromOffset();
    void updateVerticalScrollbarFromOffset();

    GroupInfo getGroupInfo(int groupIndex) const;

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
    PDFWidget* m_widget;

    /// Vertical scrollbar
    QScrollBar* m_verticalScrollbar;

    /// Horizontal scrollbar
    QScrollBar* m_horizontalScrollbar;

    /// Current page layout
    Layout m_layout;

    /// Renderer features
    PDFRenderer::Features m_features;

    /// Mesh quality settings
    PDFMeshQualitySettings m_meshQualitySettings;

    /// Page compiler
    PDFAsynchronousPageCompiler* m_compiler;

    /// Text layout compiler
    PDFAsynchronousTextLayoutCompiler* m_textLayoutCompiler;

    /// Page image rasterizer for thumbnails
    PDFRasterizer* m_rasterizer;

    /// Progress
    PDFProgress* m_progress;

    /// Cache clear timer
    QTimer* m_cacheClearTimer;

    /// Additional drawing interfaces
    std::set<IDocumentDrawInterface*> m_drawInterfaces;

    /// Renderer engine
    RendererEngine m_rendererEngine;

    /// Page group info for rendering. Group of pages
    /// can be rendered with transparency or without paper
    /// as overlay.
    std::map<PDFInteger, GroupInfo> m_groupInfos;

    /// Signature verification results
    std::vector<PDFSignatureVerificationResult> m_signatureVerificationResult;
};

}   // namespace pdf

#endif // PDFDRAWSPACECONTROLLER_H
