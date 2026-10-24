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

#include "pdfreferencepointpicker.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace pdf
{

PDFReferencePointPicker::PDFReferencePointPicker(QWidget* parent) :
    BaseClass(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFReferencePointPicker::update));
    updateDescription();
}

PDFReferencePointPicker::~PDFReferencePointPicker() = default;

void PDFReferencePointPicker::setCurrentIndex(int index)
{
    if (index < 0 || index >= POINT_COUNT || index == m_currentIndex)
    {
        return;
    }

    m_currentIndex = index;
    updateDescription();
    update();
    Q_EMIT currentIndexChanged(index);
}

Qt::Alignment PDFReferencePointPicker::getAlignment() const
{
    return getAlignment(m_currentIndex);
}

void PDFReferencePointPicker::setAlignment(Qt::Alignment alignment)
{
    setCurrentIndex(getIndex(alignment));
}

Qt::Alignment PDFReferencePointPicker::getAlignment(int index)
{
    const int boundedIndex = qBound(0, index, POINT_COUNT - 1);
    static constexpr Qt::AlignmentFlag columns[] = { Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight };
    static constexpr Qt::AlignmentFlag rows[] = { Qt::AlignTop, Qt::AlignVCenter, Qt::AlignBottom };
    return Qt::Alignment(columns[boundedIndex % 3]) | rows[boundedIndex / 3];
}

int PDFReferencePointPicker::getIndex(Qt::Alignment alignment)
{
    int column = 1;
    if (alignment.testFlag(Qt::AlignLeft))
    {
        column = 0;
    }
    else if (alignment.testFlag(Qt::AlignRight))
    {
        column = 2;
    }

    int row = 1;
    if (alignment.testFlag(Qt::AlignTop))
    {
        row = 0;
    }
    else if (alignment.testFlag(Qt::AlignBottom))
    {
        row = 2;
    }
    return row * 3 + column;
}

QString PDFReferencePointPicker::getPointName(int index)
{
    static const char* const names[] = {
        QT_TRANSLATE_NOOP("pdf::PDFReferencePointPicker", "Top left"),
        QT_TRANSLATE_NOOP("pdf::PDFReferencePointPicker", "Top"),
        QT_TRANSLATE_NOOP("pdf::PDFReferencePointPicker", "Top right"),
        QT_TRANSLATE_NOOP("pdf::PDFReferencePointPicker", "Left"),
        QT_TRANSLATE_NOOP("pdf::PDFReferencePointPicker", "Center"),
        QT_TRANSLATE_NOOP("pdf::PDFReferencePointPicker", "Right"),
        QT_TRANSLATE_NOOP("pdf::PDFReferencePointPicker", "Bottom left"),
        QT_TRANSLATE_NOOP("pdf::PDFReferencePointPicker", "Bottom"),
        QT_TRANSLATE_NOOP("pdf::PDFReferencePointPicker", "Bottom right")
    };
    return QCoreApplication::translate("pdf::PDFReferencePointPicker", names[qBound(0, index, POINT_COUNT - 1)]);
}

QRect PDFReferencePointPicker::getPointRect(int index) const
{
    if (index < 0 || index >= POINT_COUNT)
    {
        return QRect();
    }

    // The grid is centered in the widget (a larger widget keeps the picture)
    const QPoint origin((width() - EXTENT) / 2 + PADDING, (height() - EXTENT) / 2 + PADDING);
    const int column = index % 3;
    const int row = index / 3;
    return QRect(origin.x() + column * (POINT + GAP), origin.y() + row * (POINT + GAP), POINT, POINT);
}

int PDFReferencePointPicker::getPointAt(const QPoint& position) const
{
    // The gaps belong to the nearest point, so a click is never lost
    for (int i = 0; i < POINT_COUNT; ++i)
    {
        if (getPointRect(i).adjusted(-GAP / 2, -GAP / 2, GAP / 2, GAP / 2).contains(position))
        {
            return i;
        }
    }
    return -1;
}

QSize PDFReferencePointPicker::sizeHint() const
{
    return QSize(EXTENT, EXTENT);
}

QSize PDFReferencePointPicker::minimumSizeHint() const
{
    return QSize(EXTENT, EXTENT);
}

void PDFReferencePointPicker::setHoveredIndex(int index)
{
    if (m_hoveredIndex != index)
    {
        m_hoveredIndex = index;
        update();
    }
}

void PDFReferencePointPicker::updateDescription()
{
    setAccessibleDescription(getPointName(m_currentIndex));
}

void PDFReferencePointPicker::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool isEnabled = this->isEnabled();
    const QRectF frame(QPointF((width() - EXTENT) / 2, (height() - EXTENT) / 2), QSizeF(EXTENT, EXTENT));
    PDFStylePainter::fill(&painter, frame, PDFStylePainter::Radii(RADIUS), PDFThemeColors::BgSubtle);

    for (int i = 0; i < POINT_COUNT; ++i)
    {
        const QRectF point(getPointRect(i));
        const PDFStylePainter::Radii radii(POINT_RADIUS);
        if (i == m_currentIndex)
        {
            PDFStylePainter::fill(&painter, point, radii, isEnabled ? PDFThemeColors::BgAccent : PDFThemeColors::IconDisabled);
            continue;
        }

        PDFStylePainter::fill(&painter, point, radii, PDFThemeColors::BgSurface);
        const bool isHovered = isEnabled && i == m_hoveredIndex;
        PDFThemeColors::Color border = isHovered ? PDFThemeColors::BgAccent : PDFThemeColors::BorderStrong;
        if (!isEnabled)
        {
            border = PDFThemeColors::TextDisabled;
        }
        PDFStylePainter::strokeInside(&painter, point, radii, border, 1.0);
    }

    // Focus ring of 2 px inside of the frame (the keyboard only)
    if (hasFocus() && PDFWidgetStyle::isFocusRingVisible(this))
    {
        PDFStylePainter::strokeInside(&painter, frame, PDFStylePainter::Radii(RADIUS), PDFThemeColors::BorderFocus, 2.0);
    }
}

void PDFReferencePointPicker::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        const int index = getPointAt(event->position().toPoint());
        if (index != -1)
        {
            setCurrentIndex(index);
            event->accept();
            return;
        }
    }
    BaseClass::mousePressEvent(event);
}

void PDFReferencePointPicker::mouseMoveEvent(QMouseEvent* event)
{
    setHoveredIndex(getPointAt(event->position().toPoint()));
    BaseClass::mouseMoveEvent(event);
}

void PDFReferencePointPicker::keyPressEvent(QKeyEvent* event)
{
    // The arrows move in the picture (it is not mirrored for right-to-left)
    const int column = m_currentIndex % 3;
    const int row = m_currentIndex / 3;
    int newColumn = column;
    int newRow = row;

    switch (event->key())
    {
        case Qt::Key_Left:
            newColumn = qMax(0, column - 1);
            break;
        case Qt::Key_Right:
            newColumn = qMin(2, column + 1);
            break;
        case Qt::Key_Up:
            newRow = qMax(0, row - 1);
            break;
        case Qt::Key_Down:
            newRow = qMin(2, row + 1);
            break;
        case Qt::Key_Home:
            newColumn = 0;
            newRow = 0;
            break;
        case Qt::Key_End:
            newColumn = 2;
            newRow = 2;
            break;
        default:
            BaseClass::keyPressEvent(event);
            return;
    }

    setCurrentIndex(newRow * 3 + newColumn);
    event->accept();
}

bool PDFReferencePointPicker::event(QEvent* event)
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
