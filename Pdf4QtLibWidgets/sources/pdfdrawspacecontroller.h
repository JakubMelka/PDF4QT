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

#ifndef PDFDRAWSPACECONTROLLER_H
#define PDFDRAWSPACECONTROLLER_H

#include "pdfsignaturehandler.h"
#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"
#include "pdfdocument.h"
#include "pdfrenderer.h"
#include "pdffont.h"
#include "pdfcolorspaces.h"
#include "pdfdocumentdrawinterface.h"
#include "pdfwidgetsnapshot.h"
#include "pdfimagescaling.h"

#include <QRectF>
#include <QObject>
#include <QMarginsF>
#include <QColor>

#include <optional>

class QPainter;
class QScreen;
class QScrollBar;
class QTimer;

namespace pdf
{
class PDFProgress;
class PDFWidget;
class PDFCMSManager;
class PDFTextLayoutGetter;
class PDFWidgetAnnotationManager;
class PDFFormManager;
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

    /// Returns the cache of the decoded ICC profiles of the document
    const PDFICCProfileCache* getIccProfileCache() const { return &m_iccProfileCache; }

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
    PDFICCProfileCache m_iccProfileCache;
};

/// Colors of the view of a document, which are not colors of the document: the
/// canvas around the pages and the selection inside of the form fields. An
/// application sets them by PDFDrawWidgetProxy::setViewColors (the new application
/// takes them from its theme). Without them the view keeps the colors of the old
/// applications: the canvas gray of PDFWidgetUtils::isDarkTheme and the selection
/// colors of the palette of the application.
struct PDF4QTLIBWIDGETSSHARED_EXPORT PDFViewColors
{
    /// Background around the pages
    QColor canvas;

    /// Background of the selected text of a form field
    QColor selection;

    /// Selected text of a form field
    QColor selectionText;

    /// Colors of the selection of annotations (PDFWidgetAnnotationManager). The
    /// defaults are the colors of the old applications, so a view, which sets only
    /// the colors above, keeps them.

    /// Frames of the selected and of the hovered annotation, the selection frame,
    /// the outline of the resize and rotation handles (its fill is 12 % of it)
    QColor annotationSelection = QColor(0, 120, 215);

    /// Fill of the resize and rotation handles, outline of the point handles
    QColor annotationHandle = QColor(Qt::white);

    /// Point handles (vertices of a polygon, ends of a line, ...) and the shape of a dragged point
    QColor annotationPoint = QColor(220, 0, 0);

    /// Fill of the point handle under the cursor / dragged
    QColor annotationPointActive = QColor(Qt::white);

    /// Rubber band selecting the annotations (its fill is 12 % of it)
    QColor rubberBand = QColor(0, 120, 215);

    /// Marker of the place, to which a dragged handle / point / annotation snaps
    QColor snapMarker = QColor(0, 160, 0);

    /// Badge of a selected or hovered locked annotation (flags Locked / ReadOnly).
    /// An invalid color (the default) draws no badge, as in the old applications.
    QColor lockBadge;

    /// Cross of the pick tools (the point of a created shape, the target of a link).
    /// An invalid color (the default) draws the cross of the old applications (white in
    /// the difference composition mode).
    QColor pickCross;

    /// Outline of the lock badge and the dashed frame of a selected locked annotation in the look
    /// of the design (PDFWidgetAnnotationManager::setDesignSelectionLook); an invalid color (the
    /// default) takes the color of the lock badge
    QColor lockBadgeBorder;

    /// Text, fill and outline of the box with the numbers of a drag (the length and the angle
    /// of a dragged point, the size of a resized selection) in the look of the design
    /// (PDFWidgetAnnotationManager::setDesignSelectionLook); invalid text or fill (the
    /// default) = the box of the old applications
    QColor infoText;
    QColor infoBackground;
    QColor infoBorder;

    /// Shadow of the box with the numbers of a drag in the look of the design (0 / 4 / 16 of
    /// drag-tooltip 1017:18217); an invalid color (the default) draws no shadow
    QColor infoShadow;

    /// Outline and shadow (0 / 4 / 16) of the circle of the magnifier (PDFMagnifierTool) in
    /// the look of the design (magnifier 1234:164735); an invalid outline (the default) draws
    /// the white line in the difference composition mode of the old applications, an invalid
    /// shadow draws no shadow
    QColor magnifierBorder;
    QColor magnifierShadow;

    bool operator==(const PDFViewColors&) const = default;

    /// Colors of the old applications (canvas gray of the dark / light theme,
    /// highlight colors of the palette of the application)
    // TODO(remove-old-apps): the default colors go - the new application always sets the colors of its theme (setViewColors)
    static PDFViewColors getDefaultColors();

    /// Colors for drawing of a form field: the colors of the proxy of the form
    /// manager of the parameters (PDFWidgetFormManager), or the default colors
    static PDFViewColors getColors(const PDFFormManager* formManager);
};

/// Decoration of the pages of a view: a margin of the canvas around the pages
/// and a shadow below each page. An application turns it on by
/// PDFDrawWidgetProxy::setPageDecoration (the new application takes the values
/// from its design and the color of the shadow from its theme). The default
/// decoration is off - no margin and no shadow, as in the old applications.
// TODO(remove-old-apps): the decoration of the design becomes the default - the off state of the old applications goes
struct PDF4QTLIBWIDGETSSHARED_EXPORT PDFPageDecoration
{
    /// Margin of the canvas around the pages [pixels of the widget]. The fit
    /// zooms keep it free and the scrolled area includes it.
    int margin = 0;

    /// Color of the shadow, an invalid or a transparent color draws no shadow
    QColor shadowColor;

    /// Offset of the shadow from the page [pixels of the widget]
    QPointF shadowOffset;

    /// Blur of the shadow [pixels of the widget], twice the standard
    /// deviation of the Gaussian blur (as the blur of a drop shadow of Figma)
    qreal shadowBlur = 0.0;

    bool operator==(const PDFPageDecoration&) const = default;

    /// Returns true, if a shadow is drawn below the pages
    bool hasShadow() const { return shadowColor.isValid() && shadowColor.alpha() > 0; }
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

    /// Returns the fit, which the zoom follows (the zoom is computed again, when
    /// the widget or the layout changes), std::nullopt for a custom zoom
    std::optional<ZoomHint> getZoomFitHint() const;

    /// Calculates zoom using given hint for a specific page.
    /// If page is invalid, the current page (or a suitable fallback) is used.
    PDFReal getZoomHintForPage(ZoomHint hint, PDFInteger pageIndex) const;

    /// Go to the specified page
    /// \param pageIndex Page to scroll to
    void goToPage(PDFInteger pageIndex);

    /// Applies PDF destination to the current view.
    void goToDestination(const PDFDestination& destination);

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
    /// is not nullptr, then point in page coordinate space is set here. The point
    /// is mapped exactly - pass the exact position of a mouse event (QMouseEvent::position),
    /// the page point is never rounded to whole points of the page.
    /// \param point Point
    /// \param pagePoint Point in page coordinate system
    PDFInteger getPageUnderPoint(QPointF point, QPointF* pagePoint) const;

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
    const PDFICCProfileCache* getIccProfileCache() const { return m_controller->getIccProfileCache(); }
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

    /// Sets the colors of the view (canvas, selection of form fields); repaints
    void setViewColors(const PDFViewColors& colors);

    /// Removes the colors set by setViewColors (the default colors are used)
    void resetViewColors();

    /// Colors of the view: the colors of setViewColors, or the default colors
    PDFViewColors getViewColors() const;

    /// Returns true, if the colors are set by setViewColors (colors of the theme of the
    /// application - the markers drawn by them are not adjusted by the color mode of the pages)
    bool hasViewColors() const { return m_viewColors.has_value(); }

    /// Sets the decoration of the pages (margin around the pages, shadow below
    /// them). Values out of their limits are bounded (the margin to 0 - 512
    /// pixels, the blur to 0 - 32 pixels, the offset to -32 - 32 pixels, a non
    /// finite value is zero). The layout and the fit zoom are updated.
    void setPageDecoration(const PDFPageDecoration& decoration);

    /// Returns the decoration of the pages (default = no margin, no shadow)
    const PDFPageDecoration& getPageDecoration() const { return m_pageDecoration; }

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

    /// Draws the shadow of a rectangle (a drop shadow of Figma without a spread) - the
    /// overlays of the view in the look of the design (the box with the numbers of a drag)
    /// \param painter Painter
    /// \param rect Rectangle casting the shadow, the offset of the shadow applied [pixels of the widget]
    /// \param color Color of the shadow, an invalid or a transparent color draws nothing
    /// \param blur Blur of the shadow (twice the standard deviation, as in Figma)
    static void drawShadow(QPainter* painter, const QRectF& rect, const QColor& color, PDFReal blur);

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

    // Margin left around the fitted content [pixels]. It absorbs the rounding of the page
    // rectangles to the whole pixels, so the fit itself does not make a scrollbar appear.

    static constexpr PDFReal FIT_MARGIN_PIXELS = 1.0;

    // Limits of the page decoration [pixels]
    static constexpr int MAX_PAGE_MARGIN = 512;
    static constexpr PDFReal MAX_PAGE_SHADOW_BLUR = 32.0;
    static constexpr PDFReal MAX_PAGE_SHADOW_OFFSET = 32.0;

    static constexpr qint64 CACHE_CLEAR_TIMEOUT = 5000;
    static constexpr qint64 CACHE_PAGE_EXPIRATION_TIMEOUT = 30000;

    enum class ZoomMode
    {
        Custom,
        Fit,
        FitWidth,
        FitHeight
    };

    /// Converts rectangle from device space to the pixel space
    QRectF fromDeviceSpace(const QRectF& rect) const;

    /// Draws the shadows of the pages intersecting the rectangle (page decoration)
    /// \param painter Painter
    /// \param rect Rectangle in which the content is painted
    void drawPageShadows(QPainter* painter, QRect rect);

    /// Returns the distance, to which the shadow reaches from the shadow rectangle [pixels]
    /// \param blur Blur of the shadow
    static PDFReal getPageShadowExtent(PDFReal blur);

    /// Creates the picture of the shadow of a square, whose four quarters are
    /// the corners of a shadow of a page and whose middle row and column are its
    /// edges. The square ends getPageShadowExtent(blur) from the edges of the picture.
    /// \param blur Blur of the shadow
    /// \param color Color of the shadow
    /// \param devicePixelRatio Device pixel ratio of the picture
    static QImage createPageShadowTile(PDFReal blur, const QColor& color, PDFReal devicePixelRatio);

    /// Draws the picture of createPageShadowTile over the rectangle of a shadow (nine-slice:
    /// corners, edges; the middle stays empty, it lies below the shadowed rectangle)
    /// \param painter Painter
    /// \param tile Picture of createPageShadowTile
    /// \param shadowRect Rectangle of the whole shadow (the shadowed rectangle enlarged by the extent)
    static void drawShadowTile(QPainter* painter, const QImage& tile, const QRectF& shadowRect);

    /// Returns the screen on which the widget is currently displayed. Primary screen
    /// is returned as a fallback, when the widget is not assigned to any screen.
    QScreen* getCurrentScreen() const;

    /// Calculates the count of the pixels per one millimeter on the screen, on which
    /// the widget is currently displayed. This is the single source of truth for the
    /// conversion between the device space (millimeters) and the pixels - zoom hints
    /// must use it too, otherwise the layout and the zoom would disagree on screens
    /// with a different physical size or a different dpi than the primary one.
    PDFReal getPixelPerMM() const;

    /// Returns the size of the area available for the pages, when the given scrollbars
    /// are visible. Zero size is returned, when the widget is not set.
    /// \param verticalScrollbar Is the vertical scrollbar visible?
    /// \param horizontalScrollbar Is the horizontal scrollbar visible?
    QSizeF getViewportSize(bool verticalScrollbar, bool horizontalScrollbar) const;

    /// Calculates the zoom, at which the reference size fits into the viewport. Space
    /// of the scrollbars, which will become visible at that zoom, is reserved.
    /// \param hint Zoom hint type
    /// \param referenceSizeMM Size to be fitted into the viewport [mm]
    PDFReal getZoomToFitSize(ZoomHint hint, QSizeF referenceSizeMM) const;

    void zoomImpl(PDFReal zoom, std::optional<QPointF> widgetPosition = std::nullopt);
    PDFInteger getPreferredPageForZoom(PDFInteger preferredPageIndex = -1) const;
    PDFReal getZoomForMode(ZoomMode mode, PDFInteger preferredPageIndex = -1) const;
    void applyZoomMode(ZoomMode mode, PDFInteger preferredPageIndex = -1);
    std::optional<PDFInteger> getDestinationPageIndex(const PDFDestination& destination) const;
    void alignViewToPagePoint(PDFInteger pageIndex, std::optional<PDFReal> left, std::optional<PDFReal> top);
    void fitToDestinationRectangle(PDFInteger pageIndex, const QRectF& rectangle);

    /// Returns a snapshot of the registered draw interfaces. Drawing must always
    /// iterate over the snapshot - a draw interface can (de)activate a tool while
    /// it is being drawn, which modifies the set of the registered draw interfaces
    /// and would invalidate the iterators.
    std::vector<IDocumentDrawInterface*> getDrawInterfaces() const;

    void performPageCacheClear();

    /// Informs the page compiler, which pages are currently interesting for the
    /// user, so it can cancel compilation of pages, which are not displayed anymore.
    void updateCompilerActivePages();

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
    ZoomMode m_zoomMode;

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

    /// Cache of the images downscaled for the current zoom. Precompiled pages are
    /// independent on the zoom, so the downscaled images cannot be stored in them;
    /// this cache is not counted into the memory limit of their cache either.
    PDFScaledImageCache m_scaledImageCache;

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

    /// Colors of the view set by the application (none = the default colors)
    std::optional<PDFViewColors> m_viewColors;

    /// Decoration of the pages (margin, shadow), default is none
    PDFPageDecoration m_pageDecoration;

    /// Picture of the shadow of the page decoration (see createPageShadowTile)
    QImage m_pageShadowTile;
};

}   // namespace pdf

#endif // PDFDRAWSPACECONTROLLER_H
