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

#include "pdfappfloatingbar.h"

#include "pdfcomponentpanels.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>

namespace pdf
{

PDFAppFloatingBarDivider::PDFAppFloatingBarDivider(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("floatingBarDivider"));
    setFixedSize(1, LENGTH);
    setFocusPolicy(Qt::NoFocus);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppFloatingBarDivider::update));
}

PDFAppFloatingBarDivider::~PDFAppFloatingBarDivider() = default;

QSize PDFAppFloatingBarDivider::sizeHint() const
{
    return QSize(1, LENGTH);
}

void PDFAppFloatingBarDivider::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(rect()), PDFThemeColors::BorderSeparator);
}

PDFAppFloatingBarShadow::PDFAppFloatingBarShadow(int radius, QWidget* parent) :
    BaseClass(parent),
    m_radius(radius)
{
    setObjectName(QStringLiteral("floatingBarShadow"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppFloatingBarShadow::update));
}

PDFAppFloatingBarShadow::~PDFAppFloatingBarShadow() = default;

void PDFAppFloatingBarShadow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // Shadow 0 / 4 / 16 of the card (the bar above draws the card)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF card(rect().marginsRemoved(PDFAppFloatingBar::getShadowMargins()));
    PDFStylePainter::shadow(&painter, card, PDFStylePainter::Radii(m_radius), QPointF(0.0, 4.0), 16.0,
                            PDFThemeManager::getInstance()->getColor(PDFThemeColors::EffectShadow));
}

QMargins PDFAppFloatingBar::getShadowMargins()
{
    // Shadow 0 / 4 / 16 (blur 16, offset 4 down) - as the info bars
    return QMargins(16, 12, 16, 20);
}

QMargins PDFAppFloatingBar::getContentPadding(Size size)
{
    // Border 1 + padding of the design (left 14, right 8, top / bottom 6; compact 4)
    return size == Size::Compact ? QMargins(5, 5, 5, 5) : QMargins(15, 7, 9, 7);
}

PDFAppFloatingBar::PDFAppFloatingBar(Size size, QWidget* parent) :
    BaseClass(parent),
    m_size(size == Size::Compact ? Size::Compact : Size::Normal)
{
    setObjectName(QStringLiteral("floatingBar"));
    setAttribute(Qt::WA_NoMousePropagation, true);

    m_layout = new QHBoxLayout(this);
    applyContentMargins();
    m_layout->setSpacing(m_size == Size::Compact ? COMPACT_GAP : NORMAL_GAP);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppFloatingBar::update));

    // The shadow is a sibling below the bar; the bar takes the pointer only over its card
    if (parent)
    {
        m_shadow = new PDFAppFloatingBarShadow(getRadius(), parent);
        m_shadow->hide();
        m_watchedParent = parent;
        parent->installEventFilter(this);
    }
    setMask(QRegion(getCardRect()));
}

PDFAppFloatingBar::~PDFAppFloatingBar()
{
    // The shadow goes with the bar (the parent may be deleting its children)
    delete m_shadow.data();
    if (m_watchedParent)
    {
        m_watchedParent->removeEventFilter(this);
    }
}

int PDFAppFloatingBar::getCardHeight() const
{
    if (m_cardHeight > 0)
    {
        return m_cardHeight;
    }
    return m_size == Size::Compact ? COMPACT_HEIGHT : NORMAL_HEIGHT;
}

void PDFAppFloatingBar::setCardHeight(int height)
{
    const int cardHeight = qBound(0, height, 200);
    if (m_cardHeight != cardHeight)
    {
        m_cardHeight = cardHeight;
        updateGeometry();
        updatePlacement();
    }
}

int PDFAppFloatingBar::getRadius() const
{
    return m_size == Size::Compact ? COMPACT_RADIUS : NORMAL_RADIUS;
}

QHBoxLayout* PDFAppFloatingBar::getContentLayout() const
{
    return m_layout.data();
}

void PDFAppFloatingBar::addWidget(QWidget* widget)
{
    if (widget && m_layout)
    {
        m_layout->addWidget(widget, 0, Qt::AlignVCenter);
        updatePlacement();
    }
}

PDFAppFloatingBarDivider* PDFAppFloatingBar::addDivider()
{
    PDFAppFloatingBarDivider* divider = new PDFAppFloatingBarDivider(this);
    addWidget(divider);
    return divider;
}

void PDFAppFloatingBar::addSpacing(int spacing)
{
    if (m_layout)
    {
        m_layout->addSpacing(qBound(0, spacing, 1000));
        updatePlacement();
    }
}

void PDFAppFloatingBar::setFoldableSwatchRow(PDFColorSwatchRow* row)
{
    if (m_foldableRow == row)
    {
        return;
    }

    if (m_foldableRow)
    {
        m_foldableRow->setVisibleSwatchLimit(-1);
    }
    m_foldableRow = row;
    updatePlacement();
}

PDFColorSwatchRow* PDFAppFloatingBar::getFoldableSwatchRow() const
{
    return m_foldableRow.data();
}

void PDFAppFloatingBar::placeAtTop()
{
    m_placement = Placement::Top;
    m_anchor = QRect();
    updatePlacement();
}

void PDFAppFloatingBar::placeAnchored(const QRect& anchor)
{
    m_placement = Placement::Anchored;
    m_anchor = anchor.normalized();
    updatePlacement();
}

QSize PDFAppFloatingBar::getCardSizeHint() const
{
    const QSize hint = m_layout ? m_layout->sizeHint() : QSize();
    const QMargins shadow = getShadowMargins();
    return QSize(qMax(0, hint.width() - shadow.left() - shadow.right()), getCardHeight());
}

QSize PDFAppFloatingBar::sizeHint() const
{
    const QMargins shadow = getShadowMargins();
    return getCardSizeHint() + QSize(shadow.left() + shadow.right(), shadow.top() + shadow.bottom());
}

QSize PDFAppFloatingBar::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAppFloatingBar::applyContentMargins()
{
    if (!m_layout)
    {
        return;
    }

    QMargins margins = getShadowMargins() + getContentPadding(m_size);
    if (isRightToLeft())
    {
        margins = QMargins(margins.right(), margins.top(), margins.left(), margins.bottom());
    }
    m_layout->setContentsMargins(margins);
}

void PDFAppFloatingBar::updateFolding()
{
    PDFColorSwatchRow* row = m_foldableRow.data();
    const QWidget* parent = parentWidget();
    if (!row || !parent || !m_layout || row->parentWidget() != this)
    {
        return;
    }

    // The most swatches, with which the card fits into the parent (at least one
    // swatch stays; the others are in the palette of More). The widths are computed,
    // the row is changed only when its limit changes (a change posts a layout
    // request, which places the bar again)
    m_layout->invalidate();
    const bool hasMore = row->getMoreButton() != nullptr;
    const int count = row->getSwatchCount();
    const int available = parent->width() - 2 * PARENT_MARGIN;
    const int otherWidth = getCardSizeHint().width() - PDFColorSwatchRow::getRowSize(row->getVisibleSwatchCount(), hasMore).width();
    int limit = count;
    while (limit > 1 && otherWidth + PDFColorSwatchRow::getRowSize(limit, hasMore).width() > available)
    {
        --limit;
    }

    const int newLimit = limit >= count ? -1 : limit;
    if (row->getVisibleSwatchLimit() != newLimit)
    {
        row->setVisibleSwatchLimit(newLimit);
        m_layout->invalidate();
    }
}

void PDFAppFloatingBar::updatePlacement()
{
    QWidget* parent = parentWidget();
    if (m_isUpdatingPlacement || !parent)
    {
        return;
    }

    m_isUpdatingPlacement = true;
    updateFolding();

    const QSize size = sizeHint();
    const QMargins shadow = getShadowMargins();
    const QSize cardSize = getCardSizeHint();
    const int parentWidth = parent->width();
    const int parentHeight = parent->height();

    QPoint cardTopLeft;
    if (m_placement == Placement::Anchored && m_anchor.isValid())
    {
        const int centerX = m_anchor.center().x();
        int top = m_anchor.top() - ANCHOR_GAP - cardSize.height();
        if (top < PARENT_MARGIN)
        {
            // No room above the rectangle: below it
            top = m_anchor.bottom() + 1 + ANCHOR_GAP;
        }
        cardTopLeft = QPoint(centerX - cardSize.width() / 2, top);
    }
    else
    {
        cardTopLeft = QPoint((parentWidth - cardSize.width()) / 2, TOP_GAP);
    }

    // Clamped to the parent (a bar wider than its parent starts at the margin)
    const int maximumX = qMax(PARENT_MARGIN, parentWidth - PARENT_MARGIN - cardSize.width());
    const int maximumY = qMax(PARENT_MARGIN, parentHeight - PARENT_MARGIN - cardSize.height());
    cardTopLeft.setX(qBound(PARENT_MARGIN, cardTopLeft.x(), maximumX));
    if (m_placement == Placement::Anchored)
    {
        cardTopLeft.setY(qBound(PARENT_MARGIN, cardTopLeft.y(), maximumY));
    }

    setGeometry(QRect(cardTopLeft - QPoint(shadow.left(), shadow.top()), size));
    m_isUpdatingPlacement = false;
    updateShadow();
}

QRect PDFAppFloatingBar::getCardRect() const
{
    return rect().marginsRemoved(getShadowMargins());
}

QRect PDFAppFloatingBar::getCardGeometry() const
{
    return getCardRect().translated(pos());
}

PDFAppFloatingBarShadow* PDFAppFloatingBar::getShadow() const
{
    return m_shadow.data();
}

void PDFAppFloatingBar::updateShadow()
{
    if (!m_shadow || m_shadow->parentWidget() != parentWidget())
    {
        return;
    }

    m_shadow->setGeometry(geometry());
    m_shadow->setVisible(!isHidden());
    m_shadow->stackUnder(this);
}

bool PDFAppFloatingBar::event(QEvent* event)
{
    const bool result = BaseClass::event(event);

    switch (event->type())
    {
        case QEvent::Resize:
            setMask(QRegion(getCardRect()));
            updateShadow();
            break;

        case QEvent::Move:
        case QEvent::Hide:
        case QEvent::ZOrderChange:
            updateShadow();
            break;

        case QEvent::LayoutDirectionChange:
            applyContentMargins();
            updatePlacement();
            break;

        case QEvent::Show:
        case QEvent::LayoutRequest:
            updatePlacement();
            break;

        default:
            break;
    }

    return result;
}

bool PDFAppFloatingBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_watchedParent && event->type() == QEvent::Resize && !isHidden())
    {
        updatePlacement();
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppFloatingBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // The card bg/surface, border border/default; the shadow 0 / 4 / 16 is drawn
    // by PDFAppFloatingBarShadow below the bar (the bar is masked to its card)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF card(getCardRect());
    const PDFStylePainter::Radii radii(getRadius());
    PDFStylePainter::fill(&painter, card, radii, PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(&painter, card, radii, PDFThemeColors::BorderDefault, 1.0);
}

void PDFAppFloatingBar::mousePressEvent(QMouseEvent* event)
{
    // The margins of the shadow are not a part of the bar for the pointer
    if (!getCardRect().contains(event->position().toPoint()))
    {
        event->ignore();
        return;
    }

    BaseClass::mousePressEvent(event);
}

}   // namespace pdf
