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

#include "pdfcomponentsections.h"
#include "pdfdeferreddeletion.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"

#include <QAccessible>
#include <QBoxLayout>
#include <QEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QStyle>
#include <QtMath>

#include <limits>

namespace pdf
{

/// Layout of the path bar (computed in plain integers, no lambdas in the library)
class PDFPathBarHelper
{
public:
    PDFPathBarHelper() = delete;

    /// Largest number of the segments, which the bar lays out (a hostile depth
    /// of nested groups gives a long path; the bar shows the first and the last
    /// segments anyway, the ones between are hidden in the ellipsis)
    static constexpr int MAX_SEGMENTS = 256;

    /// Sum of the widths of the items and the gaps with separators between them
    static int getContentWidth(const std::vector<int>& itemWidths, int unit)
    {
        if (itemWidths.empty())
        {
            return 0;
        }

        qint64 width = qint64(unit) * qint64(itemWidths.size() - 1);
        for (int itemWidth : itemWidths)
        {
            width += itemWidth;
        }
        return int(qMin<qint64>(width, std::numeric_limits<int>::max() / 2));
    }

    /// Reduces the width of the item at most down to the minimum by the excess
    /// \returns The rest of the excess
    static int shrink(int& itemWidth, int minimumWidth, int excess)
    {
        const int reduction = qMin(excess, qMax(0, itemWidth - minimumWidth));
        itemWidth -= reduction;
        return excess - reduction;
    }
};

// ------------------------------------------------------------------ Card surface

PDFExpanderCardSurface::PDFExpanderCardSurface(QWidget* parent) :
    BaseClass(parent)
{
    PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void PDFExpanderCardSurface::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::State state;
    state.flags = isEnabled() ? QStyle::State_Enabled : QStyle::State_None;
    state.direction = layoutDirection();
    PDFComponentPainter::paintExpanderCard(&painter, QRectF(rect()), state);
}

void PDFExpanderCardSurface::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::EnabledChange)
    {
        update();
    }
}

// ----------------------------------------------------------------- Expander card

PDFExpanderCard::PDFExpanderCard(QWidget* parent) :
    PDFExpanderCard(QString(), parent)
{

}

PDFExpanderCard::PDFExpanderCard(const QString& title, QWidget* parent) :
    BaseClass(parent)
{
    // Ownership: every part is a child of the card (the header widgets, the
    // surface of the card and its layout, the summary); the content and the
    // expanded widget of the application become children of the surface. They
    // die with the card; a part deleted by the application is held by QPointer.
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(PDFComponentPainter::getExpanderSpacing());

    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(PDFComponentPainter::getExpanderSpacing());
    layout->addLayout(headerLayout);

    m_titleLabel = new PDFLabel(title, PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, this);
    m_titleLabel->setObjectName(QStringLiteral("expanderTitle"));
    headerLayout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);

    m_badge = new PDFTag(this);
    m_badge->setObjectName(QStringLiteral("expanderBadge"));
    m_badge->hide();
    headerLayout->addWidget(m_badge, 0, Qt::AlignVCenter);

    headerLayout->addStretch(1);

    m_expandLink = new PDFTextLink(this);
    m_expandLink->setObjectName(QStringLiteral("expanderLink"));
    headerLayout->addWidget(m_expandLink, 0, Qt::AlignVCenter);
    connect(m_expandLink, &PDFTextLink::clicked, this, &PDFExpanderCard::onExpandLinkClicked);

    m_card = new PDFExpanderCardSurface(this);
    m_card->setObjectName(QStringLiteral("expanderCard"));
    layout->addWidget(m_card);

    m_cardLayout = new QVBoxLayout(m_card);
    m_cardLayout->setContentsMargins(PDFComponentPainter::getExpanderCardMargins());
    m_cardLayout->setSpacing(PDFComponentPainter::getExpanderCardSpacing());

    m_summaryLabel = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextTertiary, m_card);
    m_summaryLabel->setObjectName(QStringLiteral("expanderSummary"));
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->hide();
    m_cardLayout->addWidget(m_summaryLabel);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    updateLinkText();
    updateParts();
}

PDFExpanderCard::~PDFExpanderCard()
{

}

QString PDFExpanderCard::getTitle() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

void PDFExpanderCard::setTitle(const QString& title)
{
    if (m_titleLabel)
    {
        m_titleLabel->setText(title);
    }
    updateLinkText();
}

QString PDFExpanderCard::getBadgeText() const
{
    return m_badge ? m_badge->text() : QString();
}

void PDFExpanderCard::setBadgeText(const QString& text)
{
    if (m_badge)
    {
        m_badge->setText(text);
        m_badge->setVisible(!text.isEmpty());
    }
}

QString PDFExpanderCard::getSummaryText() const
{
    return m_summaryLabel ? m_summaryLabel->text() : QString();
}

void PDFExpanderCard::setSummaryText(const QString& text)
{
    if (m_summaryLabel)
    {
        m_summaryLabel->setText(text);
    }
    updateParts();
}

QWidget* PDFExpanderCard::getContentWidget() const
{
    return m_contentWidget;
}

void PDFExpanderCard::setContentWidget(QWidget* widget)
{
    replaceCardWidget(m_contentWidget, widget, 0);
    updateParts();
}

QWidget* PDFExpanderCard::getExpandedWidget() const
{
    return m_expandedWidget;
}

void PDFExpanderCard::setExpandedWidget(QWidget* widget)
{
    replaceCardWidget(m_expandedWidget, widget, m_contentWidget ? 1 : 0);
    updateParts();
}

bool PDFExpanderCard::isExpandable() const
{
    return m_expandedWidget || (m_summaryLabel && !m_summaryLabel->text().isEmpty());
}

void PDFExpanderCard::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
    {
        return;
    }

    m_expanded = expanded;
    updateLinkText();
    updateParts();

    // Emit last: a slot may rebuild or retire the card
    Q_EMIT expandedChanged(expanded);
}

void PDFExpanderCard::toggleExpanded()
{
    setExpanded(!m_expanded);
}

PDFTextLink* PDFExpanderCard::getExpandLink() const
{
    return m_expandLink;
}

PDFTag* PDFExpanderCard::getBadge() const
{
    return m_badge;
}

PDFLabel* PDFExpanderCard::getTitleLabel() const
{
    return m_titleLabel;
}

PDFLabel* PDFExpanderCard::getSummaryLabel() const
{
    return m_summaryLabel;
}

QWidget* PDFExpanderCard::getCard() const
{
    return m_card;
}

void PDFExpanderCard::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateLinkText();
    }
}

void PDFExpanderCard::onExpandLinkClicked()
{
    // The last statement: the slot of expandedChanged may retire the card
    toggleExpanded();
}

void PDFExpanderCard::updateParts()
{
    const bool isSummaryShown = !m_expanded && m_summaryLabel && !m_summaryLabel->text().isEmpty();
    if (m_summaryLabel)
    {
        m_summaryLabel->setVisible(isSummaryShown);
    }

    if (m_expandedWidget)
    {
        m_expandedWidget->setVisible(m_expanded);
    }

    if (m_expandLink)
    {
        m_expandLink->setVisible(isExpandable());
    }

    // An empty card (a collapsed card with neither a content nor a summary) is not drawn
    if (m_card)
    {
        const bool hasContent = m_contentWidget || isSummaryShown || (m_expanded && m_expandedWidget);
        m_card->setVisible(hasContent);
    }
}

void PDFExpanderCard::updateLinkText()
{
    if (!m_expandLink)
    {
        return;
    }

    m_expandLink->setText(m_expanded ? tr("Collapse") : tr("Expand"));

    // The link says, which section it expands ("Expand, Transparency")
    m_expandLink->setAccessibleDescription(getTitle());
}

void PDFExpanderCard::replaceCardWidget(QPointer<QWidget>& slot, QWidget* widget, int position)
{
    if (slot == widget)
    {
        return;
    }

    QWidget* previous = slot;
    slot = widget;

    if (previous)
    {
        if (m_cardLayout)
        {
            m_cardLayout->removeWidget(previous);
        }
        PDFDeferredDeletion::retire(previous);
    }

    if (widget && m_card && m_cardLayout)
    {
        widget->setParent(m_card);
        m_cardLayout->insertWidget(qBound(0, position, m_cardLayout->count()), widget);
    }
    else if (widget)
    {
        // The application has deleted the card: the widget is owned, but not shown
        widget->setParent(this);
        widget->hide();
    }
}

// ------------------------------------------------------------------ Path segment

PDFPathSegment::PDFPathSegment(QWidget* parent) :
    BaseClass(FocusRing::Outside, parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    // A click on a link does not take the focus (as the text links)
    setFocusPolicy(Qt::TabFocus);
    setFocusRingRadius(PDFComponentPainter::getPathSegmentRadius());
}

void PDFPathSegment::setSegmentSize(PDFComponentPainter::PathBarSize size)
{
    size = PDFComponentPainter::normalized(size);
    if (m_size != size)
    {
        m_size = size;
        updateGeometry();
        update();
    }
}

void PDFPathSegment::setCurrent(bool isCurrent)
{
    if (m_isCurrent == isCurrent)
    {
        return;
    }

    m_isCurrent = isCurrent;
    setFocusPolicy(isCurrent ? Qt::NoFocus : Qt::TabFocus);

    if (isCurrent)
    {
        unsetCursor();
        setDown(false);

        // The current segment takes no focus: the bar moves it to a link, when it has laid
        // out its new segments (PDFPathBar::setSegments); a segment out of a bar loses it
        if (hasFocus() && !qobject_cast<PDFPathBar*>(parentWidget()))
        {
            clearFocus();
        }
    }
    else
    {
        setCursor(Qt::PointingHandCursor);
    }

#if QT_CONFIG(accessibility)
    // The role changes between a text and a link
    QAccessibleEvent roleEvent(this, QAccessible::RoleChanged);
    QAccessible::updateAccessibility(&roleEvent);
#endif

    updateGeometry();
    update();
}

void PDFPathSegment::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintPathSegment(painter, QRectF(rect), PDFComponentPainter::PathSegment{ text(), m_size, m_isCurrent }, state);
}

QSize PDFPathSegment::getContentSizeHint() const
{
    return PDFComponentPainter::getPathSegmentSize(PDFComponentPainter::PathSegment{ text(), m_size, m_isCurrent });
}

QSize PDFPathSegment::getContentMinimumSizeHint() const
{
    // The bar elides the text of the first and of the last segment
    return QSize(0, PDFComponentPainter::getPathBarLineHeight(m_size));
}

void PDFPathSegment::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    const bool isNavigation = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Home || key == Qt::Key_End;
    if (isNavigation && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier)
    {
        PDFPathBar* bar = qobject_cast<PDFPathBar*>(parentWidget());
        if (bar && bar->moveFocus(this, key))
        {
            event->accept();
            return;
        }
    }

    BaseClass::keyPressEvent(event);
}

// ---------------------------------------------------------------------- Path bar

PDFPathBar::PDFPathBar(QWidget* parent) :
    PDFPathBar(QStringList(), parent)
{

}

PDFPathBar::PDFPathBar(const QStringList& segments, QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
    setSegments(segments);
}

PDFPathBar::~PDFPathBar()
{

}

void PDFPathBar::setSegments(const QStringList& segments)
{
    // A hostile path: widgets for the first segment and the last ones only, the
    // segments between them are hidden in the ellipsis (the list itself is shared)
    m_segments = segments;
    const int count = int(qMin<qsizetype>(m_segments.size(), PDFPathBarHelper::MAX_SEGMENTS));
    m_hiddenCount = int(m_segments.size()) - count;

    // Ownership: the segments are children of the bar. The widgets of the
    // remaining indices are reused (the focus stays on the same index), the
    // extra ones are retired - a slot of segmentActivated (emitted from the
    // click of a segment) may set new segments, so the clicked segment must
    // never be deleted synchronously.
    while (int(m_segmentWidgets.size()) > count)
    {
        QPointer<PDFPathSegment> segment = m_segmentWidgets.back();
        m_segmentWidgets.pop_back();
        if (segment)
        {
            PDFDeferredDeletion::retire(segment.data());
        }
    }

    for (int i = 0; i < count; ++i)
    {
        if (i >= int(m_segmentWidgets.size()))
        {
            m_segmentWidgets.emplace_back();
        }

        if (!m_segmentWidgets[i])
        {
            PDFPathSegment* segment = new PDFPathSegment(this);
            segment->setObjectName(QStringLiteral("pathSegment"));
            connect(segment, &PDFPathSegment::clicked, this, &PDFPathBar::onSegmentClicked);
            m_segmentWidgets[i] = segment;
        }

        PDFPathSegment* segment = m_segmentWidgets[i];
        segment->setText(m_segments[toSegmentIndex(i)]);
        segment->setSegmentSize(m_size);
        segment->setCurrent(i == count - 1);
    }

    updateGeometry();
    updateLayout();
    moveFocusFromCurrentSegment();
}

void PDFPathBar::moveFocusFromCurrentSegment()
{
    PDFPathSegment* current = m_segmentWidgets.empty() ? nullptr : m_segmentWidgets.back().data();
    if (!current || !current->isCurrent() || !current->hasFocus())
    {
        return;
    }

    // The nearest shown link before the current segment (the parent of the path)
    for (qsizetype i = m_layout.shown.size() - 1; i >= 0; --i)
    {
        PDFPathSegment* link = m_segmentWidgets[size_t(m_layout.shown[i])];
        if (link && !link->isCurrent() && link->isVisibleTo(this) && link->isEnabled())
        {
            link->setFocus(Qt::BacktabFocusReason);
            return;
        }
    }

    // No link: the next widget of the focus chain takes it, as by Tab (QWidget::hide moves the
    // focus so too); the focus is cleared only, when no widget of the window takes it
    if (!focusNextPrevChild(true) && current->hasFocus())
    {
        current->clearFocus();
    }
}

void PDFPathBar::setBarSize(Size size)
{
    size = PDFComponentPainter::normalized(size);
    if (m_size == size)
    {
        return;
    }

    m_size = size;
    for (const QPointer<PDFPathSegment>& segment : m_segmentWidgets)
    {
        if (segment)
        {
            segment->setSegmentSize(size);
        }
    }

    updateGeometry();
    updateLayout();
}

QList<int> PDFPathBar::getShownSegments() const
{
    QList<int> shown;
    shown.reserve(m_layout.shown.size());
    for (int widgetIndex : m_layout.shown)
    {
        shown.append(toSegmentIndex(widgetIndex));
    }
    return shown;
}

bool PDFPathBar::isShortened() const
{
    return !m_layout.ellipsisRect.isEmpty();
}

PDFPathSegment* PDFPathBar::getSegmentWidget(int index) const
{
    const int widgetIndex = toWidgetIndex(index);
    if (widgetIndex < 0 || widgetIndex >= int(m_segmentWidgets.size()))
    {
        return nullptr;
    }
    return m_segmentWidgets[size_t(widgetIndex)];
}

int PDFPathBar::toSegmentIndex(int widgetIndex) const
{
    // The widget 0 is the first segment, the others the last ones
    return widgetIndex <= 0 ? widgetIndex : widgetIndex + m_hiddenCount;
}

int PDFPathBar::toWidgetIndex(int segmentIndex) const
{
    if (segmentIndex <= 0)
    {
        return segmentIndex == 0 ? 0 : -1;
    }
    return segmentIndex > m_hiddenCount ? segmentIndex - m_hiddenCount : -1;
}

QSize PDFPathBar::sizeHint() const
{
    const Layout layout = computeLayout(std::numeric_limits<int>::max() / 4);
    return QSize(layout.width, PDFComponentPainter::getPathBarLineHeight(m_size));
}

QSize PDFPathBar::minimumSizeHint() const
{
    const Layout layout = computeLayout(0);
    return QSize(layout.width, PDFComponentPainter::getPathBarLineHeight(m_size));
}

bool PDFPathBar::moveFocus(const PDFPathSegment* from, int key)
{
    // The links shown now, in their logical order
    QList<PDFPathSegment*> links;
    for (int widgetIndex : m_layout.shown)
    {
        PDFPathSegment* segment = m_segmentWidgets[size_t(widgetIndex)];
        if (segment && !segment->isCurrent() && segment->isVisibleTo(this) && segment->isEnabled())
        {
            links.append(segment);
        }
    }

    const int index = int(links.indexOf(const_cast<PDFPathSegment*>(from)));
    const int count = int(links.size());
    if (index < 0)
    {
        return false;
    }

    // Left and Right go by the picture (mirrored in the right-to-left layout)
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    int target = index;
    switch (key)
    {
        case Qt::Key_Left:
            target = isRightToLeft ? index + 1 : index - 1;
            break;

        case Qt::Key_Right:
            target = isRightToLeft ? index - 1 : index + 1;
            break;

        case Qt::Key_Home:
            target = 0;
            break;

        case Qt::Key_End:
            target = count - 1;
            break;

        default:
            return false;
    }

    target = qBound(0, target, count - 1);
    if (target != index)
    {
        links.at(target)->setFocus(target < index ? Qt::BacktabFocusReason : Qt::TabFocusReason);
    }
    return true;
}

void PDFPathBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_layout.separatorRects.empty() && m_layout.ellipsisRect.isEmpty())
    {
        return;
    }

    QPainter painter(this);
    PDFComponentPainter::State state;
    state.flags = isEnabled() ? QStyle::State_Enabled : QStyle::State_None;
    state.direction = layoutDirection();

    const int top = (height() - PDFComponentPainter::getPathBarLineHeight(m_size)) / 2;
    for (const QRect& separatorRect : m_layout.separatorRects)
    {
        const QRect visual = QStyle::visualRect(layoutDirection(), rect(), separatorRect.translated(0, top));
        PDFComponentPainter::paintPathSeparator(&painter, QRectF(visual), state);
    }

    if (!m_layout.ellipsisRect.isEmpty())
    {
        const QRect visual = QStyle::visualRect(layoutDirection(), rect(), m_layout.ellipsisRect.translated(0, top));
        PDFComponentPainter::paintPathEllipsis(&painter, QRectF(visual), m_size, state);
    }
}

void PDFPathBar::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateLayout();
}

void PDFPathBar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LayoutDirectionChange:
        case QEvent::FontChange:
        case QEvent::StyleChange:
            updateGeometry();
            updateLayout();
            break;

        case QEvent::EnabledChange:
            update();
            break;

        default:
            break;
    }
}

void PDFPathBar::onSegmentClicked()
{
    const int widgetIndex = getWidgetIndex(sender());
    const int index = widgetIndex >= 0 ? toSegmentIndex(widgetIndex) : -1;

    // The current segment is not a link (it does not click, a program may)
    if (index < 0 || index >= getSegmentCount() - 1)
    {
        return;
    }

    // Emit last: a slot may set new segments or retire the bar
    Q_EMIT segmentActivated(index);
}

PDFPathBar::Layout PDFPathBar::computeLayout(int width) const
{
    Layout layout;

    // The segments with a widget (indices of m_segmentWidgets); the segments of a
    // hostile path without a widget are in the ellipsis after the first segment
    const int count = int(m_segmentWidgets.size());
    if (count <= 0)
    {
        return layout;
    }
    const bool isTruncated = m_hiddenCount > 0;

    const int gap = PDFComponentPainter::getPathBarSpacing(m_size);
    const int separator = PDFComponentPainter::getPathSeparatorExtent();
    const int unit = separator + 2 * gap;
    const int lineHeight = PDFComponentPainter::getPathBarLineHeight(m_size);
    const int ellipsisWidth = qCeil(PDFComponentPainter::getPathEllipsisWidth(m_size));

    std::vector<int> widths;
    widths.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        widths.push_back(PDFComponentPainter::getPathSegmentSize(PDFComponentPainter::PathSegment{ m_segments[toSegmentIndex(i)], m_size, i == count - 1 }).width());
    }

    // The first index of the run of the trailing segments shown after the
    // ellipsis; 1 = nothing hidden by the width (all widgets shown)
    int trailingStart = 1;
    const int fullWidth = PDFPathBarHelper::getContentWidth(widths, unit) + (isTruncated ? ellipsisWidth + unit : 0);
    if (count > 2 && fullWidth > width)
    {
        // With a truncated path the ellipsis is there anyway, the widget 1 may go into it too
        const int lowestStart = isTruncated ? 1 : 2;
        trailingStart = count - 1;
        int trailingWidth = widths[count - 1];
        for (int candidate = count - 1; candidate >= lowestStart; --candidate)
        {
            if (candidate < count - 1)
            {
                trailingWidth += widths[candidate];
            }

            // Items: the first segment, the ellipsis and the segments candidate ... count - 1
            const qint64 itemCount = 2 + (count - candidate);
            const qint64 candidateWidth = qint64(widths[0]) + ellipsisWidth + trailingWidth + qint64(unit) * (itemCount - 1);
            if (candidateWidth > width)
            {
                break;
            }
            trailingStart = candidate;
        }
    }

    // Items in their order (-1 = the ellipsis)
    std::vector<int> items;
    std::vector<int> itemWidths;
    items.push_back(0);
    itemWidths.push_back(widths[0]);
    if (trailingStart > 1 || isTruncated)
    {
        items.push_back(-1);
        itemWidths.push_back(ellipsisWidth);
    }
    for (int i = trailingStart; i < count; ++i)
    {
        items.push_back(i);
        itemWidths.push_back(widths[i]);
    }

    // Not even "first > … > last" fits: the first segment is elided down to an
    // ellipsis, then the last one
    int excess = PDFPathBarHelper::getContentWidth(itemWidths, unit) - width;
    if (excess > 0)
    {
        const int minimumWidth = (count == 1) ? 0 : ellipsisWidth;
        excess = PDFPathBarHelper::shrink(itemWidths.front(), minimumWidth, excess);
        if (excess > 0 && items.size() > 1)
        {
            excess = PDFPathBarHelper::shrink(itemWidths.back(), ellipsisWidth, excess);
        }
    }

    int x = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const QRect itemRect(x, 0, itemWidths[i], lineHeight);
        if (items[i] < 0)
        {
            layout.ellipsisRect = itemRect;
        }
        else
        {
            layout.shown.append(items[i]);
            layout.segmentRects.push_back(itemRect);
        }

        x += itemWidths[i];
        if (i + 1 < items.size())
        {
            layout.separatorRects.push_back(QRect(x + gap, (lineHeight - separator) / 2, separator, separator));
            x += unit;
        }
    }
    layout.width = x;
    return layout;
}

void PDFPathBar::updateLayout()
{
    m_layout = computeLayout(width());

    const int top = (height() - PDFComponentPainter::getPathBarLineHeight(m_size)) / 2;
    std::vector<bool> isShown(m_segmentWidgets.size(), false);
    for (qsizetype i = 0; i < m_layout.shown.size(); ++i)
    {
        const size_t index = size_t(m_layout.shown[i]);
        PDFPathSegment* segment = m_segmentWidgets[index];
        if (segment)
        {
            isShown[index] = true;
            segment->setGeometry(QStyle::visualRect(layoutDirection(), rect(), m_layout.segmentRects[size_t(i)].translated(0, top)));
        }
    }

    for (size_t i = 0; i < m_segmentWidgets.size(); ++i)
    {
        if (m_segmentWidgets[i])
        {
            m_segmentWidgets[i]->setVisible(isShown[i]);
        }
    }

    update();
}

int PDFPathBar::getWidgetIndex(const QObject* segment) const
{
    for (size_t i = 0; i < m_segmentWidgets.size(); ++i)
    {
        if (segment && m_segmentWidgets[i] == segment)
        {
            return int(i);
        }
    }
    return -1;
}

}   // namespace pdf

#include "moc_pdfcomponentsections.cpp"
