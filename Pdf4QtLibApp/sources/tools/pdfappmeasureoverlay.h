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

#ifndef PDFAPPMEASUREOVERLAY_H
#define PDFAPPMEASUREOVERLAY_H

#include "pdfappglobal.h"
#include "pdfapptooldragsource.h"

#include "pdfdocumentdrawinterface.h"
#include "pdfmeasure.h"
#include "pdfmeasurewriter.h"
#include "pdfsnapper.h"

#include <QColor>
#include <QCoreApplication>
#include <QCursor>
#include <QFont>
#include <QPointer>
#include <QWidget>

#include <optional>
#include <vector>

namespace pdf
{

class PDFAppToolCallout;
class PDFDocument;
class PDFDrawWidgetProxy;
class PDFWidget;

/// What a drawing on the page measures (the 8 kinds of the toolbar 471:6294 and the calibration of the menu
/// Scale, 471:7363)
enum class PDFAppMeasureDrawKind
{
    Horizontal,         ///< Two points, measured horizontally (by /Rotate, inside a viewport by its /BBox - R7-12 a)
    Vertical,           ///< Two points, measured vertically
    Linear,             ///< Distance between two points
    Perimeter,          ///< The corners, finished on the first point
    RectanglePerimeter, ///< Two opposite corners
    Area,               ///< The corners, finished on the first point
    RectangleArea,      ///< Two opposite corners
    Angle,              ///< Three points, the second one is the vertex
    Calibrate           ///< Two points of a known distance (Calibrate Scale…)
};

/// Texts and values of the measurements of the GUI (the numbers by PDFAppLocale, O17; the computations of the
/// core: PDFMeasureGeometry, PDFDocumentToolMeasure::getEffectiveScale)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasureText
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppMeasureText)

public:
    PDFAppMeasureText() = delete;

    /// The kind of the core of a drawing kind (Calibrate is a distance)
    static PDFMeasureKind getKind(PDFAppMeasureDrawKind drawKind);

    /// The id of the panel-only parameter "kind" of a drawing kind (PDFDocumentToolMeasure::DRAW_*), empty for Calibrate
    static QString getDrawKindId(PDFAppMeasureDrawKind drawKind);

    /// The drawing kind of an id, Linear for an unknown one
    static PDFAppMeasureDrawKind getDrawKind(const QString& id);

    /// Title of a drawing kind ("Linear"), its icon ("measure-line") and the status text of its next point
    static QString getDrawKindTitle(PDFAppMeasureDrawKind drawKind);
    static QString getDrawKindIconName(PDFAppMeasureDrawKind drawKind);

    /// Icon of a kind of the core (the rows of the existing annotations)
    static QString getKindIconName(PDFMeasureKind kind);

    /// A shown value with 2 places in the locale of the user interface ("11.20")
    static QString formatValue(PDFReal value);

    /// A number in the locale of the user interface: at most \p decimals places, the trailing zeros removed
    static QString formatTrimmed(PDFReal value, int decimals);

    /// A number of a scale in the locale of the user interface: at most 4 places, the trailing zeros removed
    static QString formatScaleNumber(PDFReal value);

    /// "1 mm = 50 mm" and "1:50" in the locale of the user interface; empty for an invalid scale
    static QString formatRatio(const PDFMeasureScale& scale);
    static QString formatProportion(const PDFMeasureScale& scale);

    /// The effective /UserUnit of a page of the document (1 for no page)
    static PDFReal getUserUnit(const PDFDocument* document, PDFInteger pageIndex);

    /// The value of a measurement in the unit of its quantity: \p scale, unless a viewport of the page gives one
    /// (12.9.2; \p effectiveScale and \p viewportName say which)
    static PDFReal getValue(const PDFDocument* document, PDFInteger pageIndex, PDFMeasureKind kind, const std::vector<QPointF>& points, const PDFMeasureScale& scale,
                            const PDFMeasureDisplayUnits& units, PDFMeasureScale* effectiveScale, QString* viewportName);

    /// The shown text of a measurement ("A = 31.95 sq. m"), as getValue
    static QString getText(const PDFDocument* document, PDFInteger pageIndex, PDFMeasureKind kind, const std::vector<QPointF>& points, const PDFMeasureScale& scale,
                           const PDFMeasureDisplayUnits& units, PDFMeasureScale* effectiveScale);

    /// The unit of the quantity of a kind
    static PDFMeasureUnit getUnit(PDFMeasureKind kind, const PDFMeasureDisplayUnits& units);
};

/// A measurement drawn by the overlay (a temporary one; an existing annotation is drawn by the view, the
/// overlay draws only its selection)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasureShape
{
public:
    PDFInteger pageIndex = -1;
    PDFMeasureKind kind = PDFMeasureKind::Distance;
    std::vector<QPointF> points;    ///< Normalized points in the space of the page
    QString text;                   ///< The label
    bool isSelected = false;        ///< The row of the list is selected (FM-28): the shape is outlined
    bool isAnnotation = false;      ///< An annotation of the document: only its selection is drawn
};

/// The look of the labels on the canvas (the panel Labels 471:5490)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasureLabelStyle
{
public:
    QFont font;                                     ///< The font with its size in points and its style
    QColor textColor = Qt::black;                   ///< Text and lines, with the opacity
    QColor backgroundColor = QColor(0, 0, 0, 26);   ///< Background of the labels and areas, with the opacity
};

/// The drawing of Measure on the pages of the document tab (plan of step 7 of phase 2, 4.8; 471:5731, 471:6681,
/// 471:7363, 473:91785): IDocumentDrawInterface + IDrawWidgetInputInterface after the pattern of
/// PDFAppToolRedactOverlay (the drag source of the shell: Esc cancels the points of a drawing; the order of Esc of
/// PDFAppToolGUIMeasure - drawing → mode → selection → tool), the snapping of PDFSnapper (the corners and edges of
/// the drawing, the annotations, the first point of a shape), the big cross (C), the right button starts again.
/// Horizontal / vertical by /Rotate of the page, inside a viewport by the corners of its /BBox (R7-12 (a)). Each
/// measurement lies on the page of its first point; the measurements may lie on different pages (FM-9 cancelled).
/// Every point lies on the MediaBox of its page (a click beyond the edge of the page is placed on the edge; review
/// RS7CCORE L4 - the annotation of a measurement is never outside of its page, as MCP refuses it).
///
/// The frames (review RS7CFIGMA L1 - L4): the callout of the empty state over the canvas (471:5490, setCallout; it
/// goes with the first point), only the snapped point is marked while drawing (471:5731: a square of 10 at the
/// snapped point, the first corner of a shape as its close target - a ring of 14), the label of a horizontal line
/// beside it (471:6681: above, the label of the line being drawn below - 471:5731, 471:7363) and of a vertical one
/// at its right, the live label of a shape "Click the first point to close · A = …" above its first edge (471:6681).
///
/// Ownership: the widget is an empty child of the canvas area (no area of its own, transparent for the mouse),
/// deleted by the shell when the tool closes. It holds the PDFWidget and its proxy by QPointer and unregisters
/// itself in its destructor. The callout is a child of the canvas area too (transparent for the mouse), held by
/// QPointer and deleted by the destructor (or by the canvas area, when it dies first). A finished measurement is
/// emitted from the event loop (never inside of the dispatch of the input of the PDFWidget); every Q_EMIT is the
/// last call.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMeasureOverlay : public QWidget, public IDocumentDrawInterface, public IDrawWidgetInputInterface, public IPDFAppToolDragSource
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppMeasureOverlay(PDFWidget* pdfWidget, QWidget* canvasArea);
    virtual ~PDFAppMeasureOverlay() override;

    /// The drawing kind of the input, std::nullopt = no input (the clicks go to the view); a change starts again
    void setInput(std::optional<PDFAppMeasureDrawKind> input);
    std::optional<PDFAppMeasureDrawKind> getInput() const { return m_input; }

    /// The scale and the units of the label of the drawn measurement
    void setMeasureSettings(const PDFMeasureScale& scale, const PDFMeasureDisplayUnits& units);

    /// The measurements drawn over the pages (temporary ones) and the selections
    void setShapes(std::vector<PDFAppMeasureShape> shapes);
    const std::vector<PDFAppMeasureShape>& getShapes() const { return m_shapes; }

    /// Show of the toolbar (473:91785): hidden temporary measurements are not drawn (their selection is)
    void setShown(bool isShown);
    bool isShown() const { return m_isShown; }

    /// The big cross through the cursor (C, the toolbar)
    void setCrossShown(bool isCrossShown);
    bool isCrossShown() const { return m_isCrossShown; }

    void setLabelStyle(const PDFAppMeasureLabelStyle& style);

    /// The callout over the canvas (471:5490: blue, info, at 20, 20 of the canvas, as wide as the canvas without
    /// its margins); an empty title hides it. It is shown only before the first point of a drawing.
    void setCallout(const QString& title, const QString& text);
    PDFAppToolCallout* getCallout() const { return m_callout.data(); }

    /// The text of the label of the drawing being made on the canvas: the value, a shape, which can be closed, with
    /// "Click the first point to close · " (471:6681)
    QString getPreviewLabelText() const;

    /// The point of a page on its MediaBox (the nearest point of the box; unchanged for no page)
    QPointF clampToPage(PDFInteger pageIndex, const QPointF& pagePoint) const;

    /// The box of the label of a shape on the device (the matrix of its page; \p isPreview the shape being drawn),
    /// an empty box without a text: drawShape draws the label there (471:5975, 471:5731, 471:6681)
    QRectF getLabelRect(const PDFAppMeasureShape& shape, const QTransform& matrix, bool isPreview) const;

    /// The marks of the snapping, which drawPostRendering draws while drawing (device coordinates): the snapped point
    /// (471:5731, a square of 10) and the first corner of a shape, which closes it (471:6681, a ring of 14) - never
    /// the other snap points of the page (review RS7CFIGMA L2)
    std::vector<QRectF> getSnapMarks() const;

    /// Margin of the callout in the canvas (471:5490: 20, 20; the width of the canvas less 2 × 20)
    static constexpr int CALLOUT_MARGIN = 20;

    /// The points of the drawing being made and its page (-1 = none)
    const std::vector<QPointF>& getPoints() const { return m_points; }
    PDFInteger getPage() const { return m_page; }

    /// The label of the drawing being made ("4.80 m"), empty without one
    QString getPreviewText() const;

    /// Adds a point of the page (the mouse does it; also the tests): the point of a horizontal / vertical
    /// drawing is aligned, a completed drawing is emitted from the event loop
    void addPoint(PDFInteger pageIndex, const QPointF& pagePoint);

    /// Starts the drawing again (the right button): the picked points are forgotten
    void restart();

    /// The point aligned by a horizontal / vertical drawing to its first point (R7-12 a)
    QPointF adjustPoint(PDFInteger pageIndex, const QPointF& pagePoint) const;

    // IPDFAppToolDragSource: the points of an unfinished drawing
    virtual bool isToolDragActive() const override { return !m_points.empty(); }
    virtual void cancelToolDrag() override { restart(); }

    // IDocumentDrawInterface
    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const PDFPrecompiledPage* compiledPage,
                          PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const PDFColorConvertor& convertor,
                          QList<PDFRenderError>& errors) const override;
    virtual void drawPostRendering(QPainter* painter, QRect rect) const override;

    // IDrawWidgetInputInterface
    virtual void shortcutOverrideEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyPressEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void keyReleaseEvent(QWidget* widget, QKeyEvent* event) override;
    virtual void mousePressEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void mouseMoveEvent(QWidget* widget, QMouseEvent* event) override;
    virtual void wheelEvent(QWidget* widget, QWheelEvent* event) override;
    virtual QString getTooltip() const override;
    virtual const std::optional<QCursor>& getCursor() const override;
    virtual int getInputPriority() const override;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    /// A measurement is complete (normalized points of its kind, the page of its first point)
    void measurementPicked(pdf::PDFInteger pageIndex, pdf::PDFMeasureKind kind, std::vector<QPointF> points);

    /// The calibration line is complete: its length in points of the paper (× /UserUnit)
    void calibrationPicked(pdf::PDFInteger pageIndex, pdf::PDFReal length);

    /// The points of the drawing or the big cross have changed (the status text, the toolbar); the receiver
    /// connects it queued (it is emitted in the dispatch of the input)
    void drawingChanged();

private:
    struct Pick
    {
        bool isValid = false;
        bool isCalibration = false;
        PDFInteger pageIndex = -1;
        PDFMeasureKind kind = PDFMeasureKind::Distance;
        std::vector<QPointF> points;
    };

    void updateCanvas();
    void buildSnapData();
    void emitPick();

    /// The callout at its place, shown when it has a title and no point is picked
    void updateCallout();

    /// The point of the mouse in the space of the page of the drawing (also outside of the page)
    std::optional<QPointF> mapToPage(PDFInteger pageIndex, const QPointF& position) const;

    /// The points of the drawing with the point under the cursor (the preview)
    std::vector<QPointF> getPreviewPoints() const;

    /// The drawing is complete with the points (a closed shape, the count of its kind)
    bool isComplete(const std::vector<QPointF>& points) const;

    void drawPageGraphics(QPainter* painter, PDFInteger pageIndex, const QTransform& matrix) const;
    void drawShape(QPainter* painter, const PDFAppMeasureShape& shape, const QTransform& matrix, bool isPreview) const;
    void drawLabel(QPainter* painter, const QString& text, const QPointF& position, qreal pixelsPerPoint, bool isAccent) const;

    /// The box of a label of \p text at \p position (its center) in the font of drawLabel
    QRectF getLabelBox(const QString& text, const QPointF& position, qreal pixelsPerPoint) const;

    /// The font of the labels at the zoom of the page (\p pixelsPerPoint)
    QFont getLabelFont(qreal pixelsPerPoint) const;

    /// The marks of the snapping while drawing (471:5731, 471:6681): the snapped point, the close target of a shape
    void drawSnapMarks(QPainter* painter) const;

    QPointer<PDFWidget> m_pdfWidget;
    QPointer<QWidget> m_canvasArea;
    QPointer<PDFAppToolCallout> m_callout;          ///< Child of the canvas area, deleted by the destructor
    QString m_calloutTitle;
    QPointer<PDFDrawWidgetProxy> m_proxy;
    QPointer<QWidget> m_drawWidget;
    std::optional<PDFAppMeasureDrawKind> m_input;
    std::optional<QCursor> m_cursor;
    std::vector<PDFAppMeasureShape> m_shapes;
    PDFAppMeasureLabelStyle m_style;
    PDFMeasureScale m_scale = PDFMeasureScale::createIdentity();
    PDFMeasureDisplayUnits m_units;
    bool m_isShown = true;
    bool m_isCrossShown = false;

    // The drawing
    PDFSnapper m_snapper;
    PDFInteger m_page = -1;
    std::vector<QPointF> m_points;
    QPointF m_mousePosition;
    bool m_hasMousePosition = false;
    Pick m_pick;
    bool m_isPickQueued = false;
};

}   // namespace pdf

#endif // PDFAPPMEASUREOVERLAY_H
