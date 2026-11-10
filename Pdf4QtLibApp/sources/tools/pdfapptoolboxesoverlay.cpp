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

#include "pdfapptoolboxesoverlay.h"
#include "pdfapptoolpagestage.h"

#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"

#include <QPainter>

#include <cmath>

namespace pdf
{

PDFAppPageBoxesOverlay::PDFAppPageBoxesOverlay(PDFAppToolPageStage* stage, Coordinates coordinates) :
    BaseClass(stage),
    m_stage(stage),
    m_coordinates(coordinates)
{
    setObjectName(QStringLiteral("toolBoxesOverlay"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    if (stage)
    {
        stage->addOverlay(this);
    }
}

PDFAppPageBoxesOverlay::~PDFAppPageBoxesOverlay() = default;

void PDFAppPageBoxesOverlay::setOutlines(std::vector<Outline> outlines)
{
    if (m_outlines != outlines)
    {
        m_outlines = std::move(outlines);
        update();
    }
}

QRectF PDFAppPageBoxesOverlay::mapToOverlay(const QRectF& rect) const
{
    if (!m_stage)
    {
        return QRectF();
    }
    const QTransform transform = m_coordinates == Coordinates::Page ? m_stage->getPageToStageTransform() : m_stage->getSheetToStageTransform();
    return transform.mapRect(rect);
}

void PDFAppPageBoxesOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!m_stage)
    {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Outline& outline : m_outlines)
    {
        if (outline.rect.isEmpty())
        {
            continue;
        }

        // The line lies inside of the rectangle, so a box at the edge of the page is seen whole
        const qreal width = outline.isThick ? THICK_WIDTH : BOX_WIDTH;
        const QRectF rect = mapToOverlay(outline.rect).adjusted(width * 0.5, width * 0.5, -width * 0.5, -width * 0.5);
        PDFPainterStateGuard guard(&painter);
        QPen pen(PDFStylePainter::color(outline.color), width, outline.isDashed ? Qt::DashLine : Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);
    }
}

// ------------------------------------------------------------- PDFAppPageBoxesLegend

PDFAppPageBoxesLegend::PDFAppPageBoxesLegend(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolBoxesLegend"));
    setFixedHeight(HEIGHT);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

PDFAppPageBoxesLegend::~PDFAppPageBoxesLegend() = default;

void PDFAppPageBoxesLegend::setItems(std::vector<Item> items)
{
    if (m_items != items)
    {
        m_items = std::move(items);
        QStringList names;
        for (const Item& item : m_items)
        {
            names << item.title;
        }
        setAccessibleDescription(names.join(QStringLiteral(", ")));
        updateGeometry();
        update();
    }
}

QFont PDFAppPageBoxesLegend::getFont(bool isBold) const
{
    return PDFThemeFonts::getFont(isBold ? QFont::DemiBold : QFont::Normal, 11);
}

int PDFAppPageBoxesLegend::getItemWidth(const Item& item) const
{
    return SWATCH_SIZE + SWATCH_GAP + int(std::ceil(PDFStylePainter::textWidth(item.title, getFont(item.isBold))));
}

QSize PDFAppPageBoxesLegend::sizeHint() const
{
    int width = 0;
    for (const Item& item : m_items)
    {
        width += (width > 0 ? ITEM_GAP : 0) + getItemWidth(item);
    }
    return QSize(width, HEIGHT);
}

void PDFAppPageBoxesLegend::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The legend is a picture of the boxes of the page: its order is not mirrored
    qreal x = qMax(0, (width() - sizeHint().width()) / 2);
    for (const Item& item : m_items)
    {
        const QRectF swatch(x, (height() - SWATCH_SIZE) * 0.5, SWATCH_SIZE, SWATCH_SIZE);
        PDFStylePainter::strokeInside(&painter, swatch, PDFStylePainter::Radii(2), item.color, 1.5);
        const int itemWidth = getItemWidth(item);
        const QRectF textRect(x + SWATCH_SIZE + SWATCH_GAP, 0, itemWidth - SWATCH_SIZE - SWATCH_GAP, height());
        PDFStylePainter::text(&painter, textRect, Qt::AlignLeft | Qt::AlignVCenter, item.title, getFont(item.isBold),
                              PDFStylePainter::color(item.isBold ? PDFThemeColors::TextPrimary : PDFThemeColors::TextSecondary));
        x += itemWidth + ITEM_GAP;
    }
}

}   // namespace pdf

#include "moc_pdfapptoolboxesoverlay.cpp"
