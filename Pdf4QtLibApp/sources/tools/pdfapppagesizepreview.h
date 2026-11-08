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

#ifndef PDFAPPPAGESIZEPREVIEW_H
#define PDFAPPPAGESIZEPREVIEW_H

#include "pdfappglobal.h"

#include <QPointer>
#include <QSizeF>
#include <QString>
#include <QWidget>

class QScrollArea;

namespace pdf
{

class PDFLabel;
class PDFSegmentedControl;

/// The stage of a page size (stage of convert-blank 337:1976, 356:9975, 1390:47108): the
/// paper of a size in points, the width above it ("210 mm", Medium 11 text/secondary), the
/// height at its trailing side (12 px apart) and the caption below it (Regular 11), all
/// 12 px apart and centered as one column. Fit scales the paper into the stage, 100 % has
/// the actual size of the page at 96 dpi (K12); the stage is then as large as its content
/// and the scroll area of PDFAppPageSizePreview scrolls it. The paper is drawn by the stage
/// (reading/paper/page with the shadow 0 / 8 / 24 of effect/shadow), the texts are labels.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageSizeStage : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Space above the width and below the caption, at each side of the row of the paper
    /// (337:2135: the width at y 52 of 754, the caption 52 above the bottom; 1390:47108: the
    /// paper of 16:9 640 wide in 774)
    static constexpr int MARGIN_VERTICAL = 52;
    static constexpr int MARGIN_HORIZONTAL = 35;

    /// Gap of the texts and the paper (stage 337:2135, page-row 337:2137)
    static constexpr int GAP = 12;

    /// Line of the texts (337:2136: 13 high)
    static constexpr int TEXT_LINE = 13;

    /// Resolution of the actual size (K12: 100 % = a point of the page is 1/72 inch at 96 dpi)
    static constexpr double ACTUAL_DPI = 96.0;

    /// Shadow of the paper (337:2138)
    static constexpr int SHADOW_OFFSET = 8;
    static constexpr int SHADOW_BLUR = 24;

    explicit PDFAppPageSizeStage(QWidget* parent);
    virtual ~PDFAppPageSizeStage() override;

    /// Size of the page in points (an empty or invalid size draws no paper)
    void setPageSize(const QSizeF& size);
    const QSizeF& getPageSize() const { return m_pageSize; }

    /// Texts of the width, of the height and of the caption
    void setTexts(const QString& widthText, const QString& heightText, const QString& caption);

    /// Fit (true) or the actual size (false)
    void setFit(bool isFit);
    bool isFit() const { return m_isFit; }

    /// Scale of the drawn paper to the actual size (Fit 0.53 for A4 in 337:1976); 0 without a paper
    double getZoom() const { return m_zoom; }

    /// The paper in the coordinates of the stage (empty without a page)
    QRect getPaperRect() const;

    /// Size of the paper of the page at the actual size in logical pixels (at least 1 x 1)
    QSize getActualPaperSize() const;

    PDFLabel* getWidthLabel() const { return m_widthLabel; }
    PDFLabel* getHeightLabel() const { return m_heightLabel; }
    PDFLabel* getCaptionLabel() const { return m_captionLabel; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The zoom has changed (sent from the event loop after the layout, never inside of a
    /// resize: a slot may delete the stage)
    void zoomChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    /// Size of the paper in this size of the stage (Fit) or the actual size
    QSize computePaperSize(const QSize& stageSize) const;

    /// Size of the stage around a paper of the size (the texts and the margins)
    QSize getContentSize(const QSize& paperSize) const;

    void updatePaper();
    void queueZoomChanged();
    void emitZoomChanged();

    QPointer<QWidget> m_paper;  ///< The place of the paper in the layout, drawn by the stage
    QPointer<PDFLabel> m_widthLabel;
    QPointer<PDFLabel> m_heightLabel;
    QPointer<PDFLabel> m_captionLabel;
    QSizeF m_pageSize;
    double m_zoom = 0.0;
    double m_emittedZoom = -1.0;
    bool m_isFit = true;
    bool m_isZoomQueued = false;
    bool m_isUpdating = false;
};

/// Canvas of the size of the new pages (document-canvas 337:2009: bg/track, padding 20, gap
/// 12): the bar of the preview (preview-bar 337:2118: the title Semi Bold 13 "Page 1 of 3",
/// the segments Fit / 100 % 60 wide each) and the stage (PDFAppPageSizeStage) in a scroll area.
/// Blank PDF shows the size of its new pages; Resize Pages and Images to PDF are to use it for
/// their page sizes. The texts of the page are set by the tool; the segments follow the
/// language themselves.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppPageSizePreview : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 337:2009
    static constexpr int PADDING = 20;
    static constexpr int GAP = 12;
    static constexpr int BAR_HEIGHT = 36;
    static constexpr int SEGMENT_WIDTH = 60;
    static constexpr int SEGMENT_HEIGHT = 32;

    explicit PDFAppPageSizePreview(QWidget* parent);
    virtual ~PDFAppPageSizePreview() override;

    /// Title of the bar ("Page 1 of 3")
    void setTitle(const QString& title);

    /// The page: its size in points, the texts of its dimensions and the caption
    void setPage(const QSizeF& size, const QString& widthText, const QString& heightText, const QString& caption);

    /// Fit (true) or 100 % (false); the segments follow
    void setFit(bool isFit);
    bool isFit() const;

    /// Zoom of the paper (getZoom of the stage)
    double getZoom() const;

    PDFLabel* getTitleLabel() const { return m_title; }
    PDFSegmentedControl* getZoomControl() const { return m_zoom; }
    QScrollArea* getScrollArea() const { return m_scrollArea; }
    PDFAppPageSizeStage* getStage() const { return m_stage; }

Q_SIGNALS:
    /// The zoom of the paper has changed (from the event loop, see PDFAppPageSizeStage)
    void zoomChanged();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void onZoomIndexChanged(int index);
    void onStageZoomChanged();
    void updateTexts();
    void updateStageMode();

    QPointer<PDFLabel> m_title;
    QPointer<PDFSegmentedControl> m_zoom;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<PDFAppPageSizeStage> m_stage;
};

}   // namespace pdf

#endif // PDFAPPPAGESIZEPREVIEW_H
