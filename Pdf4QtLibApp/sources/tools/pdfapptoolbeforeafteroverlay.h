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

#ifndef PDFAPPTOOLBEFOREAFTEROVERLAY_H
#define PDFAPPTOOLBEFOREAFTEROVERLAY_H

#include "pdfappglobal.h"

#include "pdfcms.h"
#include "pdfdocument.h"
#include "pdfdocumentdrawinterface.h"

#include <QImage>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QTransform>
#include <QWidget>

#include <memory>
#include <vector>

namespace pdf
{

class PDFAppToolBeforeAfterJob;
class PDFAppToolBeforeAfterLink;
class PDFDrawWidgetProxy;
class PDFWidget;

/// Before and after over the canvas of a tool, which keeps its pages (Compress 291:994,
/// the split line 293:1346, the handle 293:1347, the chips 293:1350 and 293:1352): at the
/// left of the line the live canvas, the original; at the right of it the pages of the
/// result, rendered by a worker for what the canvas shows, again RENDER_DELAY ms after a
/// zoom or a scroll, scaled meanwhile. The original is always at the left: the line and
/// its keys are not mirrored in the right-to-left direction (plan of the first batch,
/// chapter 3). Without a result there is no line and no chip AFTER.
///
/// The widget itself is only the handle (mouse, arrows): the line, the chips and the pages
/// are drawn into the canvas by its draw interface, so the wheel and the mouse reach the
/// canvas everywhere else.
///
/// Ownership: a child of the canvas area (the shell of the tool deletes it, the canvas
/// area when it dies first). It holds the canvas and its proxy by QPointer. A render job
/// holds only its own shared data - the document of the result, the settings of the color
/// management taken in the thread of the user interface, the matrices - and it never
/// holds the overlay: its deletion cancels the job, nobody waits and a late result is
/// dropped (F12). At most the images of one result exist, bounded by MAXIMUM_PIXELS.
/// The overlay emits no signals.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolBeforeAfterOverlay : public QWidget, public IDocumentDrawInterface
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    static constexpr int HANDLE_SIZE = 28;
    static constexpr int HANDLE_ICON = 16;
    static constexpr int LINE_WIDTH = 2;
    static constexpr int CHIP_TOP = 36;         ///< Chips below the top of the canvas (126 of 90)
    static constexpr int CHIP_HEIGHT = 21;
    static constexpr int CHIP_PADDING = 10;
    static constexpr int CHIP_GAP = 4;          ///< Between the chips, which have no room in one line
    static constexpr int CHIP_FONT_SIZE = 11;
    static constexpr int RENDER_DELAY = 150;    ///< Rest after a zoom or a scroll before the pages are rendered again (ms)
    static constexpr int KEY_STEP = 16;         ///< A step of the arrows (Shift: 1 px)
    static constexpr qint64 MAXIMUM_PIXELS = 16 * 1024 * 1024;  ///< Largest area of the images of one result

    /// Room around the circle of the handle for its focus ring: the widget is larger
    /// than the shape of the design (getHandleRect)
    static constexpr int FOCUS_MARGIN = 3;

    /// \param pdfWidget Canvas of the document
    /// \param canvasArea Canvas area, the parent of the overlay
    explicit PDFAppToolBeforeAfterOverlay(PDFWidget* pdfWidget, QWidget* canvasArea);
    virtual ~PDFAppToolBeforeAfterOverlay() override;

    /// Text of the chip BEFORE, empty = no chip
    void setBeforeText(const QString& text);
    const QString& getBeforeText() const { return m_beforeText; }

    /// The result: \p afterText is the text of the chip AFTER, an empty one = no result, no
    /// line, no chip. A null document (a result without a document, 295:2182) has the line
    /// and the chip, the original stays at both sides.
    void setResult(const PDFDocumentPointer& document, const QString& afterText);
    const QString& getAfterText() const { return m_afterText; }
    bool hasResult() const { return !m_afterText.isEmpty(); }

    /// Hides everything of the overlay (the result dialog 296:2761 has a page without it)
    void setOverlayShown(bool isShown);
    bool isOverlayShown() const { return m_isOverlayShown; }

    /// Horizontal center of the line in the coordinates of the canvas area
    int getLinePosition() const;

    /// Moves the line (bounded by the canvas), the fraction of its width is kept when the canvas resizes
    void setLinePosition(int position);

    /// Rectangles in the coordinates of the canvas area
    QRect getHandleRect() const;
    QRect getLineRect() const;
    QRect getBeforeChipRect() const;
    QRect getAfterChipRect() const;

    /// Pages of the result, which have their images at the moment
    std::vector<PDFInteger> getRenderedPages() const;

    /// A render job runs or waits for its delay
    bool isRenderPending() const;

    /// Number of the render jobs finished for the current result
    int getFinishedRenderCount() const { return m_finishedRenderCount; }

    // IDocumentDrawInterface
    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;
    virtual void drawPostRendering(QPainter* painter, QRect rect) const override;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void focusInEvent(QFocusEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;

private:
    friend class PDFAppToolBeforeAfterLink;

    struct PageGeometry
    {
        PDFInteger pageIndex = -1;
        QTransform matrix;

        bool operator==(const PageGeometry&) const = default;
    };

    struct RenderedPage
    {
        PDFInteger pageIndex = -1;
        QImage image;
        QTransform imageToPage;
    };

    void updatePlacement();
    void updateCanvas();
    void cancelRender();
    void startRender();
    void onRenderFinished(const std::shared_ptr<pdf::PDFAppToolBeforeAfterJob>& job);

    /// Offset of the coordinates of the canvas area in the coordinates of the draw widget
    QPoint getCanvasOffset() const;

    /// Text of a chip in its rectangle
    void drawChip(QPainter* painter, const QRect& rect, const QString& text) const;

    /// Width of a chip of the text
    int getChipWidth(const QString& text) const;

    QPointer<PDFWidget> m_pdfWidget;
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<QWidget> m_canvasArea;
    QPointer<QWidget> m_drawWidget;
    QString m_beforeText;
    QString m_afterText;
    PDFDocumentPointer m_document;
    double m_lineFraction = 0.5;
    bool m_isOverlayShown = true;
    bool m_isDragging = false;

    std::shared_ptr<PDFAppToolBeforeAfterLink> m_link;
    std::shared_ptr<PDFAppToolBeforeAfterJob> m_job;
    std::vector<RenderedPage> m_renderedPages;
    std::vector<PageGeometry> m_requestedGeometry;      ///< What the last job renders or has rendered
    int m_finishedRenderCount = 0;

    // The state of a paint of the canvas: the pages it has shown (written while it paints)
    mutable std::vector<PageGeometry> m_paintedGeometry;
    mutable std::vector<PageGeometry> m_shownGeometry;
    mutable QTimer m_renderTimer;
};

}   // namespace pdf

#endif // PDFAPPTOOLBEFOREAFTEROVERLAY_H
