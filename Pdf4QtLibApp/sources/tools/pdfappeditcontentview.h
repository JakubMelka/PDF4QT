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

#ifndef PDFAPPEDITCONTENTVIEW_H
#define PDFAPPEDITCONTENTVIEW_H

#include "pdfappglobal.h"
#include "pdfappeditcontentpreview.h"
#include "pdfdeferreddeletion.h"
#include "pdfglobal.h"
#include "pdfpage.h"

#include <QGraphicsView>
#include <QKeySequence>
#include <QPointer>
#include <QWidget>

#include <map>
#include <utility>
#include <vector>

class QTimer;

namespace pdf
{

class PDFAppEditContentContext;
class PDFAppEditContentScene;

// Edit Content, stage E1, package P11 (canvas). Plan: zadani_gui.md 4.15 (view, overlays over
// the view), 4.16 (events of the tools, drag out of the canvas, drops), 4.20 (keys of the
// canvas, way 1), 4.23 (accessibility).

/// The view of the canvas: one page (zadani_gui.md 4.15).
///
/// - /Rotate of the page is shown by the rotation of the view; the coordinate fields and the
///   rulers show the page space, which is not rotated. The background is the token BgTrack.
/// - The zoom and the scroll position are remembered per page until the tool closes: the view
///   keeps a map page -> zoom and center itself (the canvas is created once per entry of the
///   tool and is not rebuilt by a change of the language, so the view may keep this state).
/// - Mouse events go to the tool controller of the context with the point of the scene; the
///   items get none. A right click asks PDFAppEditContentContext::requestContextMenu(
///   PDFAppEditContentMenuIds::CONTEXT, ...).
/// - Key events (only while the view has the focus): PDFAppEditContentToolController::keyPress;
///   Tab and Shift+Tab walk through the objects (focusNextPrevChild). A ShortcutOverride of a key
///   the controller would use is accepted, so a shortcut of the application rebound by the user
///   to a bare key does not take it.
/// - The shortcuts of the application commands of the scope Canvas (Ctrl+A / C / X / V,
///   Ctrl+Shift+V, Delete; their actions live on the hidden canvas of the document and never
///   fire here): the view accepts their ShortcutOverride and turns the key press into
///   PDFAppEditContentContext::handleApplicationCommand of the command, whose current shortcut it
///   is (PDFAppContext::getCommandRegistry: a shortcut rebound by the user is followed), when the
///   context answers Enabled for it; else the key goes to the controller as above
///   (pe1_skeleton_notes.md 8.2, 11).
/// - A drop from outside goes to the first drop entry of the registry, which can take it.
/// - A drag of an object out of the canvas toward the left panel starts a QDrag (the allowed
///   exception of the nested event loops): the view holds itself by a QPointer and checks
///   PDFDeferredDeletion::isRetired after exec; its hook retire() cancels the drag.
/// - While the editing is not allowed (Preview, a run) the events are not forwarded; in Preview
///   the view shows the picture of PDFAppEditContentPreview instead of the items.
///
/// Threads and ownership: a part - a child of the canvas, created once at the entry of the tool,
/// deleted with the view of the document or by the shell. GUI thread. It owns its scene (a
/// child). The context is held by a QPointer and checked in every handler. The hook retire():
/// the drag is canceled, the mouse grab and the override cursor are released, the timers stop.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentView : public QGraphicsView, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QGraphicsView;

public:
    /// The limits of the zoom (1.0 = 100 %: one point of the page is 1/72 inch of the screen)
    static constexpr qreal MINIMUM_ZOOM = 0.08;
    static constexpr qreal MAXIMUM_ZOOM = 64.0;

    /// The MIME type of a drag of objects out of the canvas (onto a row of the tabs Layers and
    /// Structure) and of rows of the tab Objects: the node identifiers of the selection, no content
    static constexpr const char* NODES_MIME_TYPE = "application/x-pdf4qt-content-nodes";

    explicit PDFAppEditContentView(PDFAppEditContentContext* context, QWidget* parent);
    virtual ~PDFAppEditContentView() override;

    PDFAppEditContentScene* getContentScene() const;

    /// The zoom (1.0 = 100 %); setZoom keeps the center of the view, clamps to the limits and
    /// emits viewChanged last
    qreal getZoom() const;
    void setZoom(qreal zoom);

    /// The commands of the application routed to the tool (zadani_gui.md 4.20, way 2): the steps
    /// of the zoom of the application, Ctrl+0 = 100 %, the fits of the paper
    void zoomIn();
    void zoomOut();
    void zoomActualSize();
    void fitPage();
    void fitWidth();
    void fitHeight();

    /// The size of a pixel of the screen in the units of the scene (1 / zoom scaled by the
    /// resolution): the tools and the selection item draw constant sizes with it
    qreal getPixelSize() const;

    /// The center of the visible part of the page in the coordinates of the scene (Paste, a new
    /// text by the keyboard)
    QPointF getVisibleCenter() const;

    /// Scrolls so that the rectangle of the scene is visible (a selection made in the tree, the
    /// object of an error of writing)
    void ensureVisible(const QRectF& sceneRectangle);

    /// The text of the zoom item of the status bar ("151 %"; PDFAppLocale)
    QString getZoomText() const;

    // ---- P11 (the canvas and its tests; no other package calls these) ----

    /// The step of zoomIn / zoomOut (the step of the application, PDFDrawWidgetProxy::ZOOM_STEP)
    static constexpr qreal ZOOM_STEP = 1.2;

    /// The margin of a fit of the paper (pixels of the screen)
    static constexpr int FIT_MARGIN = 16;

    /// The command of the application of the scope Canvas, whose current shortcut is the key of the
    /// event (pe1_skeleton_notes.md 8.2); empty = none
    QString getCanvasCommand(const QKeyEvent* event) const;

    /// The request of a picture of Preview for the visible part of the paper (an invalid request
    /// without a page)
    PDFAppEditContentPreviewRequest getPreviewRequest() const;

Q_SIGNALS:
    /// The zoom or the visible part changed
    void viewChanged();

protected:
    virtual void retire() override;

    virtual bool event(QEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void mouseDoubleClickEvent(QMouseEvent* event) override;
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void keyReleaseEvent(QKeyEvent* event) override;
    virtual void focusInEvent(QFocusEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;
    virtual void contextMenuEvent(QContextMenuEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void scrollContentsBy(int dx, int dy) override;
    virtual bool focusNextPrevChild(bool next) override;
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

private:
    /// The zoom and the center of a page, kept until the tool closes
    struct PageView
    {
        qreal zoom = 1.0;
        QPointF center;
    };

    void applyTransform();
    qreal getDeviceScale() const;
    PageRotation getRotation() const;
    void fit(bool isWidth, bool isHeight);
    void loadCommandKeys();
    bool isCanvasKey(const QKeyEvent* event) const;
    void updateCursor(const QPointF& scenePoint);
    void schedulePreview();
    void requestPreview();

    void onPageChanged();
    void onContainerChanged();
    void onPreviewActiveChanged(bool isActive);
    void onPreviewImageChanged();
    void onOptionChanged(const QString& name);
    void onToolStateChanged();

    QPointer<PDFAppEditContentContext> m_context;
    PDFAppEditContentScene* m_scene = nullptr;      ///< A child
    QTimer* m_previewTimer = nullptr;               ///< A child
    std::map<PDFInteger, PageView> m_pageViews;
    PDFInteger m_shownPage = -1;
    qreal m_zoom = 1.0;
    bool m_isFitPending = true;                     ///< The page is fitted when its paper is known
    bool m_isPanning = false;
    QPoint m_panPosition;
    std::vector<std::pair<QKeySequence, QString>> m_commandKeys;   ///< Canvas-scope shortcuts of the application -> the command
    bool m_isCommandKeysLoaded = false;
};

/// The canvas page of the tool (PDFDocumentToolGUI::createCanvasPage): the view, the widgets of
/// the canvas widget entries of the registry around it (rulers, the bar of the group), the label
/// of the mode at the top left and the place of the notice at the bottom edge (zadani_gui.md
/// 4.15 "overlays over the view"; the order from the top: ruler, bar, mode label).
///
/// The canvas registers itself at the context (PDFAppEditContentContext::setCanvas) in its
/// constructor. The notice widget is made by P14 and given by setNoticeWidget: the canvas only
/// places it (bottom edge, the width of the view minus the margins, wrapped text) and enlarges
/// the scroll range of the view by the height of the overlays.
///
/// Threads and ownership: a part (see PDFAppEditContentView); it owns the view and the widgets
/// of the entries (children). GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentCanvas : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAppEditContentCanvas(PDFAppEditContentContext* context, QWidget* parent);
    virtual ~PDFAppEditContentCanvas() override;

    /// The view and its scene: nullptr only in the skeleton (the stub canvas has none) - a
    /// caller checks them anyway, because the canvas itself may be gone
    PDFAppEditContentView* getView() const;
    PDFAppEditContentScene* getScene() const;

    /// The widget shown at the bottom edge over the view (the notice of P14); the canvas takes
    /// it as its child and retires the previous one; nullptr = none
    void setNoticeWidget(QWidget* widget);
    QWidget* getNoticeWidget() const;

    /// The label of the mode at the top left (the mode label of the active tool; hidden for an
    /// empty text)
    QString getModeLabelText() const;

    // ---- P11 (the canvas and its tests) ----

    /// The margin of the overlays from the edges of the view (pixels)
    static constexpr int OVERLAY_MARGIN = 12;

    /// The widget of a canvas widget entry (nullptr = none)
    QWidget* getEntryWidget(const QString& entryId) const;

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;

private:
    /// Places the overlays over the view: the bar of the group, the mode label, the floating panels, the notice
    void layoutOverlays();
    void onToolStateChanged();

    QPointer<PDFAppEditContentContext> m_context;
    QPointer<QWidget> m_noticeWidget;
    PDFAppEditContentView* m_view = nullptr;        ///< A child
    QWidget* m_modeLabel = nullptr;                 ///< A child
    std::vector<std::pair<QString, QPointer<QWidget>>> m_entryWidgets;     ///< The widgets of the entries by their identifiers
    std::vector<QPointer<QWidget>> m_topBars;
    std::vector<QPointer<QWidget>> m_floatingPanels;
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTVIEW_H
