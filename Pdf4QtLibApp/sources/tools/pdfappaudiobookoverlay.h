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

#ifndef PDFAPPAUDIOBOOKOVERLAY_H
#define PDFAPPAUDIOBOOKOVERLAY_H

#include "pdfappglobal.h"
#include "pdfapptooldragsource.h"
#include "pdfglobal.h"
#include "pdfpage.h"

#include <QPointer>
#include <QRectF>
#include <QTransform>
#include <QWidget>

#include <vector>

namespace pdf
{

/// The overlay of Create Audio Book on the picture of a page (Figma 341:9229, 360:13257; plan of
/// step 4 of phase 2, 4.7; package P4): the rectangles of the items of the page in the colors of
/// their states (Read green, Selected blue, Modified orange (amber), Skipped red; the border and a
/// light fill of accent/<hue>/icon), the selection by a click (Ctrl adds) and by a rectangle
/// (360:13257: the dashed rectangle, items completely inside are selected, Esc cancels).
///
/// It lies over the whole page picture (PDFAppToolPagePicture, its parent): the page is drawn by
/// the picture into its paper rectangle, the overlay maps the coordinates of the page by the media
/// box and the rotation of the page (plain values, it holds no document) into that rectangle.
///
/// Ownership: the picture is its parent (the canvas of the GUI owns both); the GUI keeps it in a
/// QPointer. Its signals are emitted last, from the events of the mouse and the keys.
///
/// Esc (owner 2026-11-17, one step each): a rectangle being drawn is canceled first (the shell,
/// IPDFAppToolDragSource - the mode stays, a new rectangle can be drawn), then the mode ends
/// (cancelRectangleMode by PDFAppToolGUIAudioBook::handleEscape), then the tool is left.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAudioBookOverlay : public QWidget, public IPDFAppToolDragSource
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// State of an item on the page (the legend of 341:9229)
    enum class State
    {
        Read,
        Selected,
        Modified,
        Skipped
    };

    /// An item of the shown page: its rectangle in the coordinates of the page and its index in the
    /// list of the GUI
    struct Item
    {
        QRectF rect;
        size_t index = 0;
        State state = State::Read;
    };

    /// \param picture The picture of the page (the parent): its rectangle of the paper is the page
    explicit PDFAppAudioBookOverlay(QWidget* picture);
    virtual ~PDFAppAudioBookOverlay() override;

    /// The page: its media box and rotation (the matrix of PDFRenderer::createMediaBoxToDevicePointMatrix)
    void setPageGeometry(const QRectF& mediaBox, PageRotation rotation);

    /// The items of the page
    void setItems(std::vector<Item> items);
    const std::vector<Item>& getItems() const { return m_items; }

    /// Select by rectangle (360:13257): a press and a drag draws the rectangle, the release selects;
    /// Esc cancels. Off by a selection or by Esc.
    void setRectangleMode(bool isRectangleMode);
    bool isRectangleMode() const { return m_isRectangleMode; }

    /// Esc of the mode of the rectangle (PDFDocumentToolGUI::handleEscape of the tool): the mode ends
    /// without a rectangle (a rectangle being drawn has been canceled by the shell before, see
    /// cancelToolDrag), rectangleCanceled is emitted (the last call). \returns false, when the mode was off
    bool cancelRectangleMode();

    // IPDFAppToolDragSource: a rectangle being drawn in the mode of the rectangle; Esc drops it, the
    // mode stays and the release of the button selects nothing
    virtual bool isToolDragActive() const override { return m_isRectangleMode && m_isDragging; }
    virtual void cancelToolDrag() override;

    /// The paper rectangle of the picture in the overlay (empty = no page shown)
    QRect getPaperRect() const;

    /// The matrix of the coordinates of the page to the overlay
    QTransform getPageToWidgetTransform() const;

    /// The index of the item at the point of the overlay: the smallest rectangle, which contains
    /// it (an item inside of another one can be hit); -1 = none
    qint64 getItemAt(const QPointF& point) const;

    /// The colors of a state (accent/<hue>/icon of 341:9229)
    static QColor getStateColor(State state);

Q_SIGNALS:
    /// An item has been clicked (\p index -1 = a place without an item); \p isAdding with Ctrl
    void itemClicked(qint64 index, bool isAdding);

    /// A rectangle has been drawn in the mode of the rectangle (coordinates of the page)
    void rectangleSelected(QRectF pageRectangle);

    /// The mode of the rectangle has ended without a rectangle (Esc)
    void rectangleCanceled();

protected:
    virtual bool event(QEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QRectF m_mediaBox;
    PageRotation m_rotation = PageRotation::None;
    std::vector<Item> m_items;
    bool m_isRectangleMode = false;
    bool m_isDragging = false;
    QPointF m_dragStart;
    QPointF m_dragEnd;
};

}   // namespace pdf

#endif // PDFAPPAUDIOBOOKOVERLAY_H
