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

#ifndef PDFREFERENCEPOINTPICKER_H
#define PDFREFERENCEPOINTPICKER_H

#include "pdffluentglobal.h"

#include <QRect>
#include <QWidget>

namespace pdf
{

/// Reference point of a geometry (Figma 1024:21781 of the geometry dialog of an
/// annotation): a grid of 3 x 3 points on bg/subtle (62 x 62, padding 6, radius 8),
/// each point 14 x 14 with the radius 4 and the gap 4. The chosen point is filled
/// by bg/accent, the others are bg/surface with the border border/strong; the
/// point under the pointer has the border bg/accent. A click or the arrow keys
/// choose a point; the focus ring of 2 px is drawn inside of the widget after the
/// keyboard (PDFWidgetStyle::isFocusRingVisible). The grid is a geometric
/// picture: it is not mirrored for right-to-left (the corners of a page are the
/// same in every language). Disabled: the chosen point icon/disabled, the borders
/// text/disabled. Assistive technologies get the name of the point as the
/// accessible description of the widget.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFReferencePointPicker : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Geometry of 1024:21781
    static constexpr int EXTENT = 62;
    static constexpr int PADDING = 6;
    static constexpr int POINT = 14;
    static constexpr int GAP = 4;
    static constexpr int RADIUS = 8;
    static constexpr int POINT_RADIUS = 4;

    /// Index of a point: row * 3 + column, from the top left (0) to the bottom right (8)
    static constexpr int POINT_COUNT = 9;

    explicit PDFReferencePointPicker(QWidget* parent = nullptr);
    virtual ~PDFReferencePointPicker() override;

    /// Chosen point (0 - 8); an invalid index is ignored
    int getCurrentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);

    /// Chosen point as an alignment (Qt::AlignLeft | Qt::AlignTop, ...)
    Qt::Alignment getAlignment() const;
    void setAlignment(Qt::Alignment alignment);

    /// Point under the pointer (-1 = none)
    int getHoveredIndex() const { return m_hoveredIndex; }

    /// Rectangle of the point in the coordinates of the widget
    QRect getPointRect(int index) const;

    /// Index of the point at the position (-1 = none)
    int getPointAt(const QPoint& position) const;

    /// Alignment of the point and the point of the alignment (center for a
    /// missing horizontal / vertical part)
    static Qt::Alignment getAlignment(int index);
    static int getIndex(Qt::Alignment alignment);

    /// Name of the point ("Top left", "Center", ...)
    static QString getPointName(int index);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The user or the application has chosen another point
    void currentIndexChanged(int index);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    void setHoveredIndex(int index);
    void updateDescription();

    int m_currentIndex = 0;
    int m_hoveredIndex = -1;
};

}   // namespace pdf

#endif // PDFREFERENCEPOINTPICKER_H
