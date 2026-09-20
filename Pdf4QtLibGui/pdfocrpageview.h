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

#ifndef PDFOCRPAGEVIEW_H
#define PDFOCRPAGEVIEW_H

#include "pdfocrmodel.h"

#include <QImage>
#include <QAbstractScrollArea>

#include <optional>

namespace pdfviewer
{

/// Zoomable view of a page image with the overlay of the OCR results and
/// the regions (UI-04, REGION-01, EDIT-04). The view works with an image and
/// a transformation from the canonical page space to the image space, so the
/// same geometry mapping is used as in the text layer writer (GEOM-03).
class PDFOCRPageView : public QAbstractScrollArea
{
    Q_OBJECT

private:
    using BaseClass = QAbstractScrollArea;

public:
    explicit PDFOCRPageView(QWidget* parent);

    enum class Mode
    {
        Select,
        DrawRecognizeRegion,
        DrawExcludeRegion,
        DrawLine,
        EditWordGeometry
    };

    /// Sets the image and the transformation from the page space to the image space
    void setImage(const QImage& image, const QTransform& pageToImage, const QString& caption);
    void clearImage(const QString& message);
    bool hasImage() const { return !m_image.isNull(); }

    /// Sets the page result (copied); nullptr clears the overlay
    void setPageResult(const pdf::PDFOCRPageResult* result);

    void setReviewThreshold(double threshold);
    void setOverlayVisible(bool visible);
    void setRegionsVisible(bool visible);
    void setSelectedWord(int wordId, bool ensureVisible);
    void setSelectedLine(int lineId);
    void setSelectedRegion(int regionId);
    int getSelectedRegion() const { return m_selectedRegionId; }

    void setMode(Mode mode);
    Mode getMode() const { return m_mode; }

    double getZoom() const { return m_zoom; }
    void setZoom(double zoom);
    void zoomIn();
    void zoomOut();
    void zoomFit();

signals:
    void wordClicked(int wordId);
    void regionClicked(int regionId);
    void rectangleDrawn(int mode, QRectF pageRectangle, pdf::PDFOCRQuad pageQuad);
    void regionGeometryChanged(int regionId, QRectF pageRectangle);
    void wordQuadChanged(int wordId, pdf::PDFOCRQuad quad);
    void zoomChanged(double zoom);
    void modeFinished();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool viewportEvent(QEvent* event) override;

private:
    enum class Handle
    {
        None,
        Move,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };

    void updateScrollBars();
    QTransform getImageToView() const;
    QTransform getPageToView() const;
    QPointF viewToImage(const QPointF& point) const;
    QRectF getEditedRectangleInView() const;
    Handle getHandleAt(const QPointF& viewPoint, const QRectF& viewRectangle) const;
    const pdf::PDFOCRWord* getWordAt(const QPointF& viewPoint) const;
    const pdf::PDFOCRRegion* getRegionAt(const QPointF& viewPoint) const;
    QRectF applyHandle(const QRectF& rectangle, Handle handle, const QPointF& delta) const;
    void drawWord(QPainter& painter, const pdf::PDFOCRWord& word, const QTransform& pageToView, bool selected) const;

    QImage m_image;
    QTransform m_pageToImage;
    QString m_caption;
    QString m_message;
    std::optional<pdf::PDFOCRPageResult> m_result;
    double m_threshold = 80.0;
    double m_zoom = 1.0;
    bool m_fitMode = true;
    bool m_overlayVisible = true;
    bool m_regionsVisible = true;
    int m_selectedWordId = 0;
    int m_selectedLineId = 0;
    int m_selectedRegionId = 0;
    Mode m_mode = Mode::Select;

    bool m_dragging = false;
    Handle m_activeHandle = Handle::None;
    QPointF m_dragStart;
    QPointF m_dragCurrent;
    QRectF m_dragOriginalRectangle;
};

}   // namespace pdfviewer

#endif // PDFOCRPAGEVIEW_H
