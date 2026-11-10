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

#ifndef PDFAPPTOOLCROPOVERLAY_H
#define PDFAPPTOOLCROPOVERLAY_H

#include "pdfappglobal.h"

#include <QPointer>
#include <QRectF>
#include <QWidget>

namespace pdf
{

class PDFAppToolPageStage;

/// The box of a crop over the page of a stage (plan of step 3 of phase 2, 4.3): Crop Pages
/// 389:22382 (the box 1.5 bg/accent, 8 handles 10 x 10 bg/surface with the border bg/accent,
/// the area outside of the box darkened by bg/scrim), the crop of an image of Images to PDF
/// 336:3159; Remove Crop shows the box dashed in border/strong without the handles and it
/// cannot be changed (405:32319). Keys: the arrows move the box, Shift + arrows change its size
/// (the right and the bottom side), a step of 1 unit, with Ctrl 10 units.
///
/// Contract (the skeleton of step 3a declared it; package B implemented it, package D uses it
/// for the crop of an image after B - step D2 of 9.4):
///  - The rectangles are in the coordinates of the page of the stage (units, the origin at its
///    top left corner, PDFAppToolPageStage::getPageToStageTransform); the box never leaves its
///    bounds, it is never smaller than getMinimumSize and never mirrored for right-to-left.
///  - Ownership: a child of its stage (PDFAppToolPageStage::addOverlay); the GUI of the tool
///    holds it by a QPointer; the overlay holds its stage by a QPointer.
///  - rectChanged during a drag (no undo step), rectChosen at the end of a drag or by a key (one
///    undo step of the tool); both emitted last, a slot may delete the overlay. setRect never
///    emits. Thread: GUI only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppCropOverlay : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 389:22382
    static constexpr int HANDLE_SIZE = 10;
    static constexpr qreal BOX_WIDTH = 1.5;

    /// A part of the box under the pointer
    enum class Part
    {
        None,
        Inside,
        TopLeft,
        Top,
        TopRight,
        Right,
        BottomRight,
        Bottom,
        BottomLeft,
        Left
    };

    /// \param stage The stage, whose page the box crops (the parent)
    explicit PDFAppCropOverlay(PDFAppToolPageStage* stage);
    virtual ~PDFAppCropOverlay() override;

    /// The area, which the box never leaves (the page; the image)
    void setBounds(const QRectF& bounds);
    const QRectF& getBounds() const { return m_bounds; }

    /// The box (no signal); it is bounded
    void setRect(const QRectF& rect);
    const QRectF& getRect() const { return m_rect; }

    /// The smallest width and height of the box (units of the page, default 3, Table C.1)
    void setMinimumSize(qreal minimumSize);
    qreal getMinimumSize() const { return m_minimumSize; }

    /// The box is shown dashed and gray and cannot be changed (Remove Crop, 405:32319)
    void setInactive(bool isInactive);
    bool isInactive() const { return m_isInactive; }

    /// The box and the handles in the coordinates of the overlay
    QRectF getBoxRect() const;
    QRectF getHandleRect(Part part) const;

    /// The part of the box at a position of the overlay
    Part getPartAt(const QPointF& position) const;

    /// The box bounded by the bounds and the minimum size
    QRectF bound(const QRectF& rect) const;

Q_SIGNALS:
    /// The box moves or changes its size during a drag
    void rectChanged(const QRectF& rect);

    /// The user has finished a change of the box (the end of a drag, a key)
    void rectChosen(const QRectF& rect);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void focusInEvent(QFocusEvent* event) override;
    virtual void focusOutEvent(QFocusEvent* event) override;

private:
    /// The box after a drag of \p part by \p delta (units of the page) from \p start
    QRectF getDraggedRect(Part part, const QRectF& start, const QPointF& delta) const;
    void updateCursor(Part part);

    QPointer<PDFAppToolPageStage> m_stage;
    QRectF m_bounds;
    QRectF m_rect;
    qreal m_minimumSize = 3.0;
    bool m_isInactive = false;

    Part m_dragPart = Part::None;
    QPointF m_dragStart;            ///< The position of the press (page coordinates)
    QRectF m_dragStartRect;
};

}   // namespace pdf

#endif // PDFAPPTOOLCROPOVERLAY_H
