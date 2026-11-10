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

#ifndef PDFANCHORGRID_H
#define PDFANCHORGRID_H

#include "pdffluentglobal.h"

#include <QRect>
#include <QWidget>

namespace pdf
{

/// The anchor of 3 x 3 cells (plan of step 3 of phase 2, 4.3, K14 with R11): one component for
/// Resize Pages (the panel of the toolbar, 391:23821: cells 40 x 40 with a dot 8 x 8, the chosen
/// cell accent) and the popovers of the combo Anchor of Add Image as Page and Images to PDF
/// (356:9556: cells 36 x 36, also with the dots).
///
/// A cell is bg/subtle with its dot icon/muted; the chosen cell is bg/accent-subtle with the
/// border and the dot bg/accent; the cell under the pointer is bg/hover. The arrows move the
/// focus cell (its focus ring of 2 px is drawn after the keyboard only), Space and Enter choose
/// it, a click chooses the cell under the pointer. Disabled: the dots and the border of the
/// chosen cell are icon/disabled.
///
/// Contract:
///  - Built on the 3 x 3 picker of the geometry dialog (PDFReferencePointPicker: the mapping of
///    a cell to Qt::Alignment and back, the rule of the keys, the names of the cells) - no
///    second copy of it.
///  - The value is Qt::Alignment of one horizontal and one vertical flag (AlignCenter = the
///    middle); the grid is a picture of the page: never mirrored for right-to-left.
///  - Colors only from the theme tokens (never a QPalette); Fluent, GUI thread only.
///  - anchorChosen is emitted after a choice of the user (a click, Space, Enter), last; a slot
///    may delete the grid (its popover closes). setAnchor never emits.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFAnchorGrid : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Sizes of a cell (391:23821 and 356:9556), the gap of the cells, the radius of a cell and
    /// the dot
    static constexpr int CELL_SIZE = 40;
    static constexpr int COMPACT_CELL_SIZE = 36;
    static constexpr int GAP = 4;
    static constexpr int CELL_RADIUS = 6;
    static constexpr int DOT_SIZE = 8;

    explicit PDFAnchorGrid(QWidget* parent = nullptr);
    virtual ~PDFAnchorGrid() override;

    /// The chosen anchor (no signal); the focus cell goes to it
    void setAnchor(Qt::Alignment anchor);
    Qt::Alignment getAnchor() const { return m_anchor; }

    /// Size of a cell (CELL_SIZE, COMPACT_CELL_SIZE)
    void setCellSize(int cellSize);
    int getCellSize() const { return m_cellSize; }

    /// The cell of the keyboard (0 - 8, row * 3 + column from the top left) and the cell under
    /// the pointer (-1 = none)
    int getFocusIndex() const { return m_focusIndex; }
    int getHoveredIndex() const { return m_hoveredIndex; }

    /// Rectangle of the cell in the coordinates of the widget, the cell at a position (-1 = none)
    QRect getCellRect(int index) const;
    int getCellAt(const QPoint& position) const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The user has chosen the anchor
    void anchorChosen(Qt::Alignment anchor);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    /// The user chooses the cell (the last call: emits)
    void choose(int index);
    void setHoveredIndex(int index);
    void updateDescription();

    Qt::Alignment m_anchor = Qt::AlignCenter;
    int m_cellSize = CELL_SIZE;
    int m_focusIndex = 4;
    int m_hoveredIndex = -1;
};

}   // namespace pdf

#endif // PDFANCHORGRID_H
