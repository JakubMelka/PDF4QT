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

#include "pdfanchorgrid.h"
#include "pdfreferencepointpicker.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace pdf
{

PDFAnchorGrid::PDFAnchorGrid(QWidget* parent) :
    BaseClass(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAnchorGrid::update));
    updateDescription();
}

PDFAnchorGrid::~PDFAnchorGrid() = default;

void PDFAnchorGrid::setAnchor(Qt::Alignment anchor)
{
    const int index = PDFReferencePointPicker::getIndex(anchor);
    m_anchor = PDFReferencePointPicker::getAlignment(index);
    m_focusIndex = index;
    updateDescription();
    update();
}

void PDFAnchorGrid::setCellSize(int cellSize)
{
    m_cellSize = qMax(1, cellSize);
    updateGeometry();
    update();
}

QRect PDFAnchorGrid::getCellRect(int index) const
{
    if (index < 0 || index >= PDFReferencePointPicker::POINT_COUNT)
    {
        return QRect();
    }

    // The grid is centered in the widget; the cells go from the top left in every direction
    const int extent = 3 * m_cellSize + 2 * GAP;
    const QPoint origin((width() - extent) / 2, (height() - extent) / 2);
    return QRect(origin.x() + (index % 3) * (m_cellSize + GAP), origin.y() + (index / 3) * (m_cellSize + GAP), m_cellSize, m_cellSize);
}

int PDFAnchorGrid::getCellAt(const QPoint& position) const
{
    // A click into a gap belongs to the nearest cell
    for (int i = 0; i < PDFReferencePointPicker::POINT_COUNT; ++i)
    {
        if (getCellRect(i).adjusted(-GAP / 2, -GAP / 2, GAP / 2, GAP / 2).contains(position))
        {
            return i;
        }
    }
    return -1;
}

QSize PDFAnchorGrid::sizeHint() const
{
    const int extent = 3 * m_cellSize + 2 * GAP;
    return QSize(extent, extent);
}

QSize PDFAnchorGrid::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAnchorGrid::setHoveredIndex(int index)
{
    if (m_hoveredIndex != index)
    {
        m_hoveredIndex = index;
        update();
    }
}

void PDFAnchorGrid::updateDescription()
{
    setAccessibleDescription(PDFReferencePointPicker::getPointName(PDFReferencePointPicker::getIndex(m_anchor)));
}

void PDFAnchorGrid::choose(int index)
{
    if (index < 0 || index >= PDFReferencePointPicker::POINT_COUNT)
    {
        return;
    }

    m_focusIndex = index;
    m_anchor = PDFReferencePointPicker::getAlignment(index);
    updateDescription();
    update();

    // The last statement: a slot may close the popover and delete the grid
    Q_EMIT anchorChosen(m_anchor);
}

void PDFAnchorGrid::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool isEnabled = this->isEnabled();
    const int chosenIndex = PDFReferencePointPicker::getIndex(m_anchor);
    const PDFStylePainter::Radii radii(CELL_RADIUS);
    for (int i = 0; i < PDFReferencePointPicker::POINT_COUNT; ++i)
    {
        const QRectF cell(getCellRect(i));
        const bool isChosen = i == chosenIndex;
        const bool isHovered = isEnabled && i == m_hoveredIndex && !isChosen;

        PDFThemeColors::Color background = isHovered ? PDFThemeColors::BgHover : PDFThemeColors::BgSubtle;
        PDFThemeColors::Color dot = isEnabled ? PDFThemeColors::IconMuted : PDFThemeColors::IconDisabled;
        if (isChosen)
        {
            background = PDFThemeColors::BgAccentSubtle;
            dot = isEnabled ? PDFThemeColors::BgAccent : PDFThemeColors::IconDisabled;
        }

        PDFStylePainter::fill(&painter, cell, radii, background);
        if (isChosen)
        {
            PDFStylePainter::strokeInside(&painter, cell, radii, isEnabled ? PDFThemeColors::BgAccent : PDFThemeColors::IconDisabled, 1.0);
        }
        PDFStylePainter::fillEllipse(&painter, PDFStylePainter::centered(cell, QSizeF(DOT_SIZE, DOT_SIZE)), PDFStylePainter::color(dot));

        // The focus ring of 2 px around the cell of the keyboard (after the keyboard only)
        if (i == m_focusIndex && hasFocus() && PDFWidgetStyle::isFocusRingVisible(this))
        {
            PDFStylePainter::strokeInside(&painter, cell, radii, PDFThemeColors::BorderFocus, 2.0);
        }
    }
}

void PDFAnchorGrid::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        const int index = getCellAt(event->position().toPoint());
        if (index != -1)
        {
            event->accept();
            choose(index);
            return;
        }
    }
    BaseClass::mousePressEvent(event);
}

void PDFAnchorGrid::mouseMoveEvent(QMouseEvent* event)
{
    setHoveredIndex(getCellAt(event->position().toPoint()));
    BaseClass::mouseMoveEvent(event);
}

void PDFAnchorGrid::keyPressEvent(QKeyEvent* event)
{
    switch (event->key())
    {
        case Qt::Key_Space:
        case Qt::Key_Enter:
        case Qt::Key_Return:
            event->accept();
            choose(m_focusIndex);
            return;

        default:
            break;
    }

    // The arrows move the focus cell by the rule of the picker (the picture is not mirrored)
    const int index = PDFReferencePointPicker::getIndexAfterKey(m_focusIndex, event->key());
    if (index == -1)
    {
        BaseClass::keyPressEvent(event);
        return;
    }

    m_focusIndex = index;
    setAccessibleDescription(PDFReferencePointPicker::getPointName(index));
    update();
    event->accept();
}

bool PDFAnchorGrid::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::HoverLeave:
        case QEvent::Leave:
        case QEvent::Hide:
            setHoveredIndex(-1);
            break;

        case QEvent::FocusIn:
        case QEvent::FocusOut:
        case QEvent::EnabledChange:
            update();
            break;

        case QEvent::LanguageChange:
            updateDescription();
            break;

        default:
            break;
    }
    return BaseClass::event(event);
}

}   // namespace pdf

#include "moc_pdfanchorgrid.cpp"
