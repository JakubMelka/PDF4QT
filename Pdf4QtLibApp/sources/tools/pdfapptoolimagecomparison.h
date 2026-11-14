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

#ifndef PDFAPPTOOLIMAGECOMPARISON_H
#define PDFAPPTOOLIMAGECOMPARISON_H

#include "pdfappglobal.h"

#include "pdffluentwidgets.h"

#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

namespace pdf
{

/// The image of one card of PDFAppToolImageComparison (image-area 298:3307: bg/muted, radius 6):
/// the image fits the area (Fit, 11 px from its edges as the drawn image 298:3308) or has its
/// pixels (100 %: an image pixel is a device pixel); a larger image is moved by dragging, by the
/// wheel (Shift = horizontally) and by the arrows. The part shown is a value: the point of the
/// image in the middle of the area, in fractions of the image (0.5, 0.5 = the middle). The image is
/// drawn over the paper (sample/white): a transparent image shows how it lies on a white page. A
/// null image shows the placeholder text (the preview is being computed). GUI thread; centerMoved
/// is emitted last, only by a move of the user.
///
/// Ownership: a child of its card (PDFAppToolImageComparison owns both).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageComparisonView : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Distance of the image from the edges of the area in Fit (298:3308 in 298:3307)
    static constexpr int FIT_MARGIN = 11;
    static constexpr int RADIUS = 6;
    static constexpr int ARROW_STEP = 40;   ///< Pixels of an arrow key and of a step of the wheel

    explicit PDFAppToolImageComparisonView(QWidget* parent);
    virtual ~PDFAppToolImageComparisonView() override;

    void setImage(const QImage& image);
    const QImage& getImage() const { return m_image; }

    void setPlaceholderText(const QString& text);
    const QString& getPlaceholderText() const { return m_placeholderText; }

    /// Fit (true) or 100 %
    void setFit(bool isFit);
    bool isFit() const { return m_isFit; }

    /// The point of the image in the middle of the area (fractions of the image)
    void setCenter(const QPointF& center);
    QPointF getCenter() const { return m_center; }

    /// Rectangle of the drawn image in the coordinates of the view (empty = no image)
    QRectF getImageRect() const;

    /// The image is larger than the area (100 %): it can be moved
    bool isMovable() const;

    /// Moves the image by \p delta logical pixels (a positive x shows more of the right part)
    void moveBy(const QPointF& delta);

Q_SIGNALS:
    /// The user has moved the image
    void centerMoved(QPointF center);

    /// The user has double-clicked the image (the comparison switches Fit / 100 %)
    void doubleClicked();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    /// Size of the drawn image in logical pixels
    QSizeF getDrawnSize() const;

    /// The center bounded so that the area is covered by the image (100 %); an image smaller
    /// than the area along an axis lies in its middle
    QPointF boundCenter(const QPointF& center) const;

    /// The scaled image of the drawn size (cached for Fit)
    QPixmap getScaledPixmap(const QSizeF& drawnSize) const;

    void updateCursor();

    QImage m_image;
    QString m_placeholderText;
    bool m_isFit = true;
    QPointF m_center = QPointF(0.5, 0.5);
    bool m_isDragging = false;
    QPointF m_dragPosition;
    mutable QPixmap m_scaledPixmap;     ///< Cache of the scaled image
    mutable QSize m_scaledPixmapSize;   ///< Device size of the cache
};

/// One card of the comparison (pane-BEFORE 298:3305: bg/surface, border/default 1, radius 10,
/// paddings 12 / 10 / 12 / 12, gap 10): the caption Semi Bold 11 text/tertiary with the spacing
/// of 6 %, the image area, the line of the details Regular 11 text/secondary, both centered.
///
/// Ownership: a child of PDFAppToolImageComparison.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageComparisonPane : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int RADIUS = 10;
    static constexpr int PADDING = 12;
    static constexpr int PADDING_TOP = 10;
    static constexpr int GAP = 10;

    explicit PDFAppToolImageComparisonPane(QWidget* parent);
    virtual ~PDFAppToolImageComparisonPane() override;

    PDFLabel* getCaptionLabel() const { return m_caption; }
    PDFLabel* getInfoLabel() const { return m_info; }
    PDFAppToolImageComparisonView* getView() const { return m_view; }

    void setTexts(const QString& caption, const QString& info);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    QPointer<PDFLabel> m_caption;
    QPointer<PDFAppToolImageComparisonView> m_view;
    QPointer<PDFLabel> m_info;
};

/// Two cards side by side, BEFORE / AFTER of Optimize Images and Grayscale (297:3135,
/// 308:6911) or ORIGINAL / BITONAL of Black & White (304:5102), with their captions, Fit / 100 %,
/// Sync zoom (Black & White) and the line below the cards ("Estimated size: 1.2 MB → 310 KB · 74 %
/// smaller"); the cards are never mirrored for right-to-left (as Compress). The images are values
/// (QImage) - no pointer into a document. GUI thread; zoomChanged is emitted last.
///
/// The widget is the whole canvas of the tool (document-canvas 297:3201: bg/track, padding 20, gap
/// 12): the bar of the preview (preview-bar 298:3274: the title Semi Bold 13, the segments Fit /
/// 100 % 120 x 32, the chip Sync zoom 304:5219 and the button Show on page 298:3299), the cards
/// (panes 298:3304, gap 12) and the line of the estimate (298:3329: Semi Bold 13 text/primary and
/// the detail Regular 12 text/secondary, gap 8).
///
/// Zoom: Fit shows each image whole; 100 % shows the pixels of each image, a larger image is moved
/// in its card. With Sync zoom both cards show the same part of their images (the same point in
/// fractions of the image, so images of different pixel sizes - a downsampled AFTER - stay at the
/// same place); without it each card is moved alone. A double click switches Fit / 100 %.
///
/// Ownership: the canvas page of the tool GUI is its parent, the GUI keeps it in a QPointer.
/// Package P2 delivers it as its first milestone (R4-3); P3 uses it.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolImageComparison : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Zoom of the cards
    enum class Zoom
    {
        Fit,        ///< The images fit their cards
        ActualSize  ///< 100 %: a pixel of the image is a pixel of the screen
    };

    static constexpr int PADDING = 20;
    static constexpr int GAP = 12;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int BAR_GAP = 8;
    static constexpr int SEGMENT_WIDTH = 60;
    static constexpr int SEGMENT_HEIGHT = 32;
    static constexpr int SYNC_ZOOM_WIDTH = 114;
    static constexpr int ESTIMATE_GAP = 8;

    explicit PDFAppToolImageComparison(QWidget* parent);
    virtual ~PDFAppToolImageComparison() override;

    /// The two images (a null image = the card says it is being computed)
    void setImages(const QImage& before, const QImage& after);
    const QImage& getBeforeImage() const { return m_before; }
    const QImage& getAfterImage() const { return m_after; }

    /// The captions of the cards ("BEFORE", "2480 × 3508 px · DeviceRGB · DCTDecode · 1.2 MB")
    void setCaptions(const QString& beforeTitle, const QString& beforeDetail, const QString& afterTitle, const QString& afterDetail);

    /// The line below the cards
    void setFooterText(const QString& text);

    /// The line below the cards in its two parts: the estimate (Semi Bold) and its detail
    /// ("Estimated size: 1.2 MB → 310 KB", "74 % smaller · downsampled from 300 to 200 dpi")
    void setFooterTexts(const QString& text, const QString& detail);

    /// The title of the bar of the preview ("Image 1 · page 308", "Page 3")
    void setTitle(const QString& title);

    /// Texts of a card without an image; empty = "Computing the preview…"
    void setPlaceholderTexts(const QString& before, const QString& after);

    Zoom getZoom() const { return m_zoom; }
    void setZoom(Zoom zoom);

    /// Sync zoom (304:5102): both cards show the same part; the control is shown only by Black & White
    bool isSyncZoom() const { return m_isSyncZoom; }
    void setSyncZoom(bool isSyncZoom);
    void setSyncZoomVisible(bool isVisible);

    /// The button Show on page (tb-show-on-page 298:3299); hidden = no button
    void setShowOnPageVisible(bool isVisible);

    /// Parts (tests)
    PDFLabel* getTitleLabel() const { return m_title; }
    PDFSegmentedControl* getZoomControl() const { return m_zoomControl; }
    QPushButton* getSyncZoomChip() const { return m_syncZoomChip; }
    QToolButton* getShowOnPageButton() const { return m_showOnPageButton; }
    PDFAppToolImageComparisonPane* getBeforePane() const { return m_beforePane; }
    PDFAppToolImageComparisonPane* getAfterPane() const { return m_afterPane; }
    PDFLabel* getFooterLabel() const { return m_footer; }
    PDFLabel* getFooterDetailLabel() const { return m_footerDetail; }

Q_SIGNALS:
    /// The user has changed the zoom or Sync zoom (from the event loop, once for the changes of
    /// one event; a slot may delete the comparison)
    void zoomChanged();

    /// The user has pressed Show on page
    void showOnPageRequested();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();
    void updateViews();
    void onZoomIndexChanged(int index);
    void onSyncZoomToggled(bool isChecked);
    void onShowOnPageClicked();
    void onBeforeCenterMoved(QPointF center);
    void onAfterCenterMoved(QPointF center);
    void onViewDoubleClicked();
    void postZoomChanged();
    void emitZoomChanged();

    QImage m_before;
    QImage m_after;
    Zoom m_zoom = Zoom::Fit;
    bool m_isSyncZoom = true;
    bool m_isUpdatingControls = false;
    bool m_isZoomChangePending = false;
    QString m_beforePlaceholder;
    QString m_afterPlaceholder;

    QPointer<PDFLabel> m_title;
    QPointer<PDFSegmentedControl> m_zoomControl;
    QPointer<QPushButton> m_syncZoomChip;
    QPointer<QToolButton> m_showOnPageButton;
    QPointer<PDFAppToolImageComparisonPane> m_beforePane;
    QPointer<PDFAppToolImageComparisonPane> m_afterPane;
    QPointer<PDFLabel> m_footer;
    QPointer<PDFLabel> m_footerDetail;
};

}   // namespace pdf

#endif // PDFAPPTOOLIMAGECOMPARISON_H
