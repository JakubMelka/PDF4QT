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

#ifndef PDFAPPTOOLPAGESTAGE_H
#define PDFAPPTOOLPAGESTAGE_H

#include "pdfappglobal.h"

#include "pdfdocument.h"
#include "pdffluentwidgets.h"
#include "pdfpagegeometry.h"

#include <QImage>
#include <QPointer>
#include <QRectF>
#include <QScrollArea>
#include <QString>
#include <QTimer>
#include <QTransform>
#include <QWidget>

#include <memory>

namespace pdf
{

class PDFAppToolPageStageJob;
class PDFAppToolPageStageLink;
class PDFDrawWidgetProxy;

/// One page shown large in the canvas of a tool (plan of step 3 of phase 2, 4.3, K48): Crop
/// Pages 389:22382, Resize Pages, Set Page Boxes, the page of an image of Images to PDF and Add
/// Image as Page. Fit or 100 %, the picture rendered by a worker in the size of the stage, the
/// overlays (PDFAppCropOverlay, PDFAppPageBoxesOverlay) laid over the page.
///
/// Contract (the skeleton of step 3a declared it; package B implemented it):
///  - Ownership: the GUI of the tool creates it as its canvas page (the view takes it during
///    the mode) and holds it by a QPointer; the overlays are its children (addOverlay). The
///    worker gets a copy {PDFDocumentPointer, page, size, matrix, the settings of the rendering
///    taken in the GUI thread} - never the stage; its picture comes back queued through a link,
///    which the destructor of the stage cuts (a late picture is dropped, nobody waits).
///  - Coordinates: the overlays and the tools work in the coordinates of the page (units of the
///    shown box of the page - the crop box, or the box of setShownBox - as shown with its
///    /Rotate, the origin at its top left corner); getPageToStageTransform maps them to the
///    stage. A sheet (setSheet, the page after Resize) has its own coordinates of the same kind,
///    getSheetToStageTransform. The page is never mirrored for right-to-left.
///  - pageGeometryChanged is emitted after the rectangle of the page has changed (another page,
///    zoom, size of the stage) from the event loop, never inside of a resize; a slot may delete
///    the stage. Thread: GUI only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageStage : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Zoom of the page
    enum class Zoom
    {
        Fit,    ///< The whole page in the stage
        Actual  ///< 100 % (a scroll area around the stage)
    };

    /// Resolution of 100 % (K12 of step 2: a point of the page is 1/72 inch at 96 dpi)
    static constexpr double ACTUAL_DPI = 96.0;

    /// Rest after a change of the size before the page is rendered again (ms)
    static constexpr int RENDER_DELAY = 80;

    /// Largest picture of the page
    static constexpr qint64 MAXIMUM_PIXELS = 16 * 1024 * 1024;

    /// Space around the sheet in the mode Fit (room for the handles of the overlays)
    static constexpr int MARGIN = 8;

    /// Share of the stage, which the sheet with its texts takes in Fit (RS3FIGMA M11, the frames
    /// in the stage 774 x 754: Crop 389:22382 the page 622 high of 731 below its caption, Set Page
    /// Boxes 391:26688 595 of 708, Resize 391:23146 300 wide of 357 per page - 85 %)
    static constexpr double FIT_SHARE = 0.85;

    explicit PDFAppToolPageStage(QWidget* parent);
    virtual ~PDFAppToolPageStage() override;

    /// The page shown: the document (shared, immutable), its id (the keys of the pictures) and
    /// the index of the page from 0 (-1 = none)
    void setPage(PDFDocumentPointer document, const QString& documentId, PDFInteger pageIndex);
    const PDFDocumentPointer& getDocument() const { return m_document; }
    PDFInteger getPageIndex() const { return m_pageIndex; }

    /// The page of the document (nullptr = none)
    const PDFPage* getPage() const;

    /// An image instead of a page (the crop of an image of Images to PDF, 336:3159): the picture
    /// (a value, it may be smaller than the image) and the size of the image in pixels - the
    /// units of the coordinates of the page then; 100 % is one pixel of the image per logical
    /// pixel. Nothing is rendered by a worker. setPage ends it; an empty size ends it too.
    void setImage(const QImage& picture, const QSize& pixelSize);
    bool isImageShown() const { return !m_imagePixelSize.isEmpty(); }

    void setZoom(Zoom zoom);
    Zoom getZoom() const { return m_zoom; }

    /// The box of the page, which is shown (the crop box; the media box for Set Page Boxes and
    /// for Remove Crop - what is outside of the crop box is drawn then)
    void setShownBox(PDFPageGeometry::Box box);
    PDFPageGeometry::Box getShownBox() const { return m_shownBox; }

    /// The shown box of the page in the default user space (empty without a page)
    QRectF getShownBoxRect() const;

    /// The size of the shown box as shown (units of the page; empty without a page)
    QSizeF getPageSize() const;

    /// The page drawn on a sheet of another size (Resize Pages, After): the sheet as shown and
    /// the rectangle of the page on it, both in the units of the page; an empty sheet = the page
    /// alone
    void setSheet(const QSizeF& sheetSize, const QRectF& contentRect);
    const QSizeF& getSheetSize() const { return m_sheetSize; }

    /// Fit fits this extent (units of the page) instead of the sheet: two stages, which show
    /// sizes to be compared, have one scale (Resize Pages, Now and After 391:23146); an empty
    /// extent = the sheet. The sheet stays centered.
    void setFitExtent(const QSizeF& extent);
    const QSizeF& getFitExtent() const { return m_fitExtent; }

    /// The view of the document, whose settings the picture follows (the color management, the
    /// features, the paper color; nullptr = the defaults)
    void setProxy(PDFDrawWidgetProxy* proxy);

    /// Rectangle of the page in the coordinates of the stage (empty without a page)
    QRectF getPageRect() const;

    /// Rectangle of the sheet in the coordinates of the stage (the page without a sheet)
    QRectF getSheetRect() const;

    /// Matrix from the coordinates of the page (units, the origin at its top left corner as
    /// shown) to the coordinates of the stage
    QTransform getPageToStageTransform() const;

    /// Matrix from the coordinates of the sheet to the coordinates of the stage
    QTransform getSheetToStageTransform() const;

    /// Pixels of the stage per unit of the page
    double getScale() const;

    /// Lays an overlay over the page: it becomes a child of the stage, covers it and follows
    /// the size of the stage
    void addOverlay(QWidget* overlay);

    /// The texts at the sheet (RS3FIGMA M11): \p above directly above the sheet, \p below
    /// directly below it in their order, \p gap between the sheet and each of them (389:22382
    /// the caption 10 below the page, 391:23146 the title and the caption 8 from it). The stage
    /// takes them as its children (they are no overlays) and places them at the sheet; Fit
    /// leaves room for them. A hidden text takes no room. nullptr = none.
    void setTexts(QWidget* above, const std::vector<QWidget*>& below, int gap);

    /// The vertical place of the sheet with its texts in Fit: Qt::AlignTop (the stages of Crop
    /// 389:22819 and Set Page Boxes 391:26688) or Qt::AlignVCenter (Resize 391:23594, default)
    void setVerticalAlignment(Qt::Alignment alignment);

    /// The horizontal place of the sheet in Fit: Qt::AlignHCenter (default), or Qt::AlignLeading /
    /// Qt::AlignTrailing - the sheet touches that side of the stage, its texts are centered on it
    /// (Resize 391:23594: Now and After at the chevron between them, gap 20 + 20 + 20)
    void setHorizontalAlignment(Qt::Alignment alignment);
    Qt::Alignment getHorizontalAlignment() const { return m_horizontalAlignment; }

    /// The texts have changed (their heights or visibility): the sheet and the texts are placed
    /// again
    void updateTextGeometry();

    /// A picture of the current page and size is shown / a rendering waits or runs (tests)
    bool hasPicture() const;
    bool isRenderPending() const;
    int getFinishedRenderCount() const { return m_finishedRenderCount; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The page has another rectangle in the stage
    void pageGeometryChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    friend class PDFAppToolPageStageLink;

    /// The size of the sheet as shown (the page without a sheet)
    QSizeF getSheetSizeOrPage() const;

    /// Size of the sheet in pixels at 100 %
    QSize getActualSize() const;

    /// Height of the texts above / below the sheet with their gaps (0 without a text)
    int getTextHeight(bool isAbove) const;

    /// Height of a text of the width; -1 for none or a hidden one
    static int getWidgetHeight(const QWidget* widget, int width);
    bool isText(const QWidget* widget) const;
    void layoutTexts();

    void onGeometryChanged();
    void queueGeometryChanged();
    void emitGeometryChanged();
    void scheduleRender();
    void cancelRender();
    void startRender();
    void onRenderFinished(const std::shared_ptr<pdf::PDFAppToolPageStageJob>& job);

    PDFDocumentPointer m_document;
    QString m_documentId;
    PDFInteger m_pageIndex = -1;
    Zoom m_zoom = Zoom::Fit;
    PDFPageGeometry::Box m_shownBox = PDFPageGeometry::Box::CropBox;
    QSizeF m_sheetSize;
    QSizeF m_fitExtent;
    QRectF m_contentRect;
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<QWidget> m_textAbove;                  ///< Children of the stage, placed at the sheet
    std::vector<QPointer<QWidget>> m_textsBelow;
    int m_textGap = 0;
    Qt::Alignment m_verticalAlignment = Qt::AlignVCenter;
    Qt::Alignment m_horizontalAlignment = Qt::AlignHCenter;

    std::shared_ptr<PDFAppToolPageStageLink> m_link;
    std::shared_ptr<PDFAppToolPageStageJob> m_job;
    QImage m_sourcePicture;         ///< The picture of the shown image (setImage)
    QSize m_imagePixelSize;         ///< The size of the shown image in pixels, empty = a page
    QImage m_image;                 ///< The picture of the page
    QRectF m_imagePageRect;         ///< The rectangle of the page, for which the picture was rendered
    PDFInteger m_imagePageIndex = -1;
    PDFDocumentPointer m_imageDocument;
    QTimer m_renderTimer;
    int m_finishedRenderCount = 0;
    QRectF m_lastPageRect;
    bool m_isGeometryChangedQueued = false;
};

/// The canvas of the page tools (document-canvas 389:22382: bg/track, padding 20, gap 12): the
/// bar of the preview (preview-bar: the title Semi Bold 13, the segments Fit / 100 % 60 wide
/// each) and one or two pages (Resize: Now and After with an arrow, 391:23146), each with its
/// title directly above and its caption directly below the page (Regular 12 text/secondary; an
/// error text/danger; the stage places them, M11), and a row between the page and its caption
/// (the legend of Set Page Boxes 391:26688). One page lies at the top of its stage, two are
/// centered (389:22819, 391:23594). The stages are in a scroll
/// area: 100 % scrolls. The texts are set by the tool; the segments follow the language.
///
/// Ownership: the GUI of the tool creates it as its canvas page; the stages, the labels and the
/// row below are its children (held by QPointer). Thread: GUI only. No signals.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolPageCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 389:22382
    static constexpr int PADDING = 20;
    static constexpr int GAP = 12;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int SEGMENT_WIDTH = 60;
    static constexpr int SEGMENT_HEIGHT = 32;
    static constexpr int COLUMN_GAP = 60;      ///< 391:23594: gap 20, the chevron 20, gap 20
    static constexpr int SINGLE_TEXT_GAP = 10;  ///< 389:22819, 391:26688: the texts 10 from the page
    static constexpr int DOUBLE_TEXT_GAP = 8;   ///< 391:23595: the title and the caption 8 from the page

    /// \param stageCount 1 (Crop, Boxes) or 2 (Resize: Now and After)
    explicit PDFAppToolPageCanvas(int stageCount, QWidget* parent);
    virtual ~PDFAppToolPageCanvas() override;

    void setTitle(const QString& title);

    /// Title above and caption below a stage; \p isError colors the caption text/danger
    void setStageTitle(int index, const QString& title);
    void setCaption(int index, const QString& caption, bool isError = false);

    /// A widget below the captions (the legend of the boxes); the canvas takes it
    void setBottomWidget(QWidget* widget);

    /// The zoom of all stages; the segments follow
    void setZoom(PDFAppToolPageStage::Zoom zoom);
    PDFAppToolPageStage::Zoom getZoom() const;

    int getStageCount() const { return int(m_stages.size()); }
    PDFAppToolPageStage* getStage(int index) const;
    PDFLabel* getTitleLabel() const { return m_title; }
    PDFLabel* getStageTitleLabel(int index) const;
    PDFLabel* getCaptionLabel(int index) const;
    PDFSegmentedControl* getZoomControl() const { return m_zoomControl; }
    QScrollArea* getScrollArea() const { return m_scrollArea; }

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void onZoomIndexChanged(int index);
    void updateTexts();

    QPointer<PDFLabel> m_title;
    QPointer<PDFSegmentedControl> m_zoomControl;
    QPointer<QScrollArea> m_scrollArea;
    std::vector<QPointer<PDFAppToolPageStage>> m_stages;
    std::vector<QPointer<PDFLabel>> m_stageTitles;
    std::vector<QPointer<PDFLabel>> m_captions;
};

}   // namespace pdf

#endif // PDFAPPTOOLPAGESTAGE_H
