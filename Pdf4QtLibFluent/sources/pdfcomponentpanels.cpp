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

#include "pdfcomponentpanels.h"
#include "pdfcomponentbuttons.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QMenu>
#include <QToolTip>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionTab>
#include <QtMath>

#include <algorithm>

namespace pdf
{

/// Geometry of the toolbar field and of the document tabs
class PDFComponentPanelsHelper
{
public:
    PDFComponentPanelsHelper() = delete;

    static constexpr int FIELD_HEIGHT = 32;
    static constexpr int FIELD_MIN_WIDTH = 64;
    static constexpr int FIELD_TEXT_INSET = 11;     ///< Border 1 and padding 10
    static constexpr int FIELD_SUFFIX_GAP = 4;

    static constexpr int TAB_HEIGHT = 32;
    static constexpr int TAB_GAP = 8;               ///< Gap between two tabs (part of the tab rectangle)
    static constexpr int TAB_ITEM_GAP = 8;          ///< Gap between the label, the lock, the dot and the button
    static constexpr int TAB_LOCK = 12;
    static constexpr int TAB_DOT = 8;
    static constexpr int TAB_PROGRESS = 2;

    static QFont getValueFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold); }
    static QFont getSuffixFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::Body); }
};

PDFToolBarField::PDFToolBarField(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFToolBarField::PDFToolBarField(const QString& text, const QString& suffix, QWidget* parent) :
    BaseClass(text, parent),
    m_suffix(suffix)
{
    init();
}

void PDFToolBarField::init()
{
    setFont(PDFComponentPanelsHelper::getValueFont());
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setProperty(PDFWidgetStyleProperties::FIELD_HEIGHT, PDFComponentPanelsHelper::FIELD_HEIGHT);
    setAccessibleDescription(m_suffix);
    updateTextInsets();

    connect(this, &QLineEdit::textChanged, this, [this]()
    {
        updateTextInsets();
        updateGeometry();
    });
}

void PDFToolBarField::setSuffix(const QString& suffix)
{
    if (m_suffix != suffix)
    {
        if (accessibleDescription().isEmpty() || accessibleDescription() == m_suffix)
        {
            setAccessibleDescription(suffix);
        }

        m_suffix = suffix;
        updateTextInsets();
        updateGeometry();
        update();
    }
}

void PDFToolBarField::setSuffixVisible(bool visible)
{
    if (m_suffixVisible != visible)
    {
        m_suffixVisible = visible;
        updateTextInsets();
        updateGeometry();
        update();
    }
}

qreal PDFToolBarField::getValueWidth() const
{
    return PDFStylePainter::textWidth(text(), PDFComponentPanelsHelper::getValueFont());
}

qreal PDFToolBarField::getSuffixWidth() const
{
    if (!m_suffixVisible || m_suffix.isEmpty())
    {
        return 0.0;
    }

    return PDFStylePainter::textWidth(m_suffix, PDFComponentPanelsHelper::getSuffixFont());
}

QSize PDFToolBarField::sizeHint() const
{
    // Value, gap and suffix between the paddings; one pixel more for the cursor
    const qreal suffixWidth = getSuffixWidth();
    const qreal content = getValueWidth() + (suffixWidth > 0.0 ? PDFComponentPanelsHelper::FIELD_SUFFIX_GAP + suffixWidth : 0.0) + 1.0;
    const int width = qCeil(content) + 2 * PDFComponentPanelsHelper::FIELD_TEXT_INSET;
    return QSize(qMax(PDFComponentPanelsHelper::FIELD_MIN_WIDTH, width), PDFComponentPanelsHelper::FIELD_HEIGHT);
}

QSize PDFToolBarField::minimumSizeHint() const
{
    return QSize(PDFComponentPanelsHelper::FIELD_MIN_WIDTH, PDFComponentPanelsHelper::FIELD_HEIGHT);
}

void PDFToolBarField::updateTextInsets()
{
    // The value and the suffix are centered as a group, the suffix is drawn
    // by the field after the edited text
    const qreal suffixWidth = getSuffixWidth();
    const qreal suffixPart = suffixWidth > 0.0 ? PDFComponentPanelsHelper::FIELD_SUFFIX_GAP + suffixWidth : 0.0;
    const qreal content = getValueWidth() + 1.0 + suffixPart;
    const qreal available = width() - 2 * PDFComponentPanelsHelper::FIELD_TEXT_INSET;
    const int extra = qMax(0, qFloor((available - content) / 2.0));

    const int left = PDFComponentPanelsHelper::FIELD_TEXT_INSET + extra;
    const int right = PDFComponentPanelsHelper::FIELD_TEXT_INSET + extra + qCeil(suffixPart);
    const QMargins insets = layoutDirection() == Qt::RightToLeft ? QMargins(right, 0, left, 0) : QMargins(left, 0, right, 0);

    if (property(PDFWidgetStyleProperties::FIELD_TEXT_INSETS).value<QMargins>() != insets)
    {
        // QLineEdit takes the rectangle of its text from the style, when it is painted
        setProperty(PDFWidgetStyleProperties::FIELD_TEXT_INSETS, QVariant::fromValue(insets));
        update();
    }
}

void PDFToolBarField::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateTextInsets();
}

void PDFToolBarField::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange)
    {
        updateTextInsets();
    }
}

void PDFToolBarField::paintEvent(QPaintEvent* event)
{
    BaseClass::paintEvent(event);

    const qreal suffixWidth = getSuffixWidth();
    if (suffixWidth <= 0.0)
    {
        return;
    }

    // The suffix follows the value at the gap of 4 px (the insets are physical,
    // the value starts at the left inset, or ends at the right one in right-to-left)
    const QMargins insets = property(PDFWidgetStyleProperties::FIELD_TEXT_INSETS).value<QMargins>();
    const qreal valueWidth = getValueWidth() + 1.0;
    const qreal gap = PDFComponentPanelsHelper::FIELD_SUFFIX_GAP;

    QRectF suffixRect(insets.left() + valueWidth + gap, 0.0, suffixWidth, height());
    if (layoutDirection() == Qt::RightToLeft)
    {
        suffixRect.moveLeft(width() - insets.right() - valueWidth - gap - suffixWidth);
    }

    QPainter painter(this);
    const PDFThemeColors::Color color = isEnabled() ? PDFThemeColors::TextMuted : PDFThemeColors::TextDisabled;
    PDFStylePainter::text(&painter, suffixRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, m_suffix, PDFComponentPanelsHelper::getSuffixFont(), PDFStylePainter::color(color));
}

/// Geometry of the key value rows
class PDFKeyValueListHelper
{
public:
    PDFKeyValueListHelper() = delete;

    static constexpr int ROW_HEIGHT = 28;
    static constexpr int PADDING = 10;
    static constexpr int GAP = 12;
    static constexpr int KEY_WIDTH = 120;
    static constexpr int MIN_VALUE_WIDTH = 60;

    static QFont getKeyFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::Small); }
    static QFont getValueFont() { return PDFThemeFonts::getFont(PDFThemeFonts::Role::SmallMedium); }
};

PDFKeyValueList::PDFKeyValueList(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

int PDFKeyValueList::addRow(const QString& key, const QString& value)
{
    m_rows.push_back(Row{ key, value });
    updateDescription();
    updateGeometry();
    update();
    return getRowCount() - 1;
}

void PDFKeyValueList::setValue(int index, const QString& value)
{
    if (index >= 0 && index < getRowCount() && m_rows[index].value != value)
    {
        m_rows[index].value = value;
        updateDescription();
        update();
    }
}

void PDFKeyValueList::clear()
{
    m_rows.clear();
    updateDescription();
    updateGeometry();
    update();
}

QString PDFKeyValueList::getKey(int index) const
{
    return index >= 0 && index < getRowCount() ? m_rows[index].key : QString();
}

QString PDFKeyValueList::getValue(int index) const
{
    return index >= 0 && index < getRowCount() ? m_rows[index].value : QString();
}

void PDFKeyValueList::updateDescription()
{
    // The rows are only drawn, assistive technologies read them as the description
    QStringList lines;
    for (const Row& row : m_rows)
    {
        lines << QStringLiteral("%1: %2").arg(row.key, row.value);
    }
    setAccessibleDescription(lines.join(QLatin1Char('\n')));
}

int PDFKeyValueList::getRowHeight(int index) const
{
    // A long label wraps in its column of 120 px, the row grows with it
    const QFontMetrics metrics(PDFKeyValueListHelper::getKeyFont());
    const QRect bounds = metrics.boundingRect(QRect(0, 0, PDFKeyValueListHelper::KEY_WIDTH, 10000), Qt::TextWordWrap, m_rows[index].key);
    const int lineHeight = metrics.height();
    return qMax(PDFKeyValueListHelper::ROW_HEIGHT, PDFKeyValueListHelper::ROW_HEIGHT + bounds.height() - lineHeight);
}

QRect PDFKeyValueList::getRowRect(int index) const
{
    if (index < 0 || index >= getRowCount())
    {
        return QRect();
    }

    int top = contentsRect().top();
    for (int i = 0; i < index; ++i)
    {
        top += getRowHeight(i);
    }

    return QRect(contentsRect().left(), top, contentsRect().width(), getRowHeight(index));
}

int PDFKeyValueList::getRowAt(const QPoint& position) const
{
    for (int i = 0; i < getRowCount(); ++i)
    {
        if (getRowRect(i).contains(position))
        {
            return i;
        }
    }

    return -1;
}

int PDFKeyValueList::heightForWidth(int width) const
{
    Q_UNUSED(width);

    int height = 0;
    for (int i = 0; i < getRowCount(); ++i)
    {
        height += getRowHeight(i);
    }

    const QMargins margins = contentsMargins();
    return height + margins.top() + margins.bottom();
}

QSize PDFKeyValueList::sizeHint() const
{
    // Label column, gap and the widest value between the paddings
    qreal valueWidth = PDFKeyValueListHelper::MIN_VALUE_WIDTH;
    for (const Row& row : m_rows)
    {
        valueWidth = qMax(valueWidth, PDFStylePainter::textWidth(row.value, PDFKeyValueListHelper::getValueFont()));
    }

    const QMargins margins = contentsMargins();
    const int width = 2 * PDFKeyValueListHelper::PADDING + PDFKeyValueListHelper::KEY_WIDTH + PDFKeyValueListHelper::GAP + qCeil(valueWidth) + margins.left() + margins.right();
    return QSize(width, heightForWidth(width));
}

QSize PDFKeyValueList::minimumSizeHint() const
{
    const QMargins margins = contentsMargins();
    const int width = 2 * PDFKeyValueListHelper::PADDING + PDFKeyValueListHelper::KEY_WIDTH + PDFKeyValueListHelper::GAP + PDFKeyValueListHelper::MIN_VALUE_WIDTH + margins.left() + margins.right();
    return QSize(width, heightForWidth(width));
}

void PDFKeyValueList::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const QFont keyFont = PDFKeyValueListHelper::getKeyFont();
    const QFont valueFont = PDFKeyValueListHelper::getValueFont();
    const QFontMetricsF valueMetrics(valueFont);
    const PDFThemeColors::Color keyColor = isEnabled() ? PDFThemeColors::TextTertiary : PDFThemeColors::TextDisabled;
    const PDFThemeColors::Color valueColor = isEnabled() ? PDFThemeColors::TextPrimary : PDFThemeColors::TextDisabled;

    for (int i = 0; i < getRowCount(); ++i)
    {
        const QRect row = getRowRect(i);
        const QRect inner = row.adjusted(PDFKeyValueListHelper::PADDING, 0, -PDFKeyValueListHelper::PADDING, 0);
        const QRect keyRect(inner.left(), inner.top(), PDFKeyValueListHelper::KEY_WIDTH, inner.height());
        const QRect valueRect(keyRect.right() + 1 + PDFKeyValueListHelper::GAP, inner.top(), qMax(0, inner.width() - PDFKeyValueListHelper::KEY_WIDTH - PDFKeyValueListHelper::GAP), inner.height());

        painter.setFont(keyFont);
        painter.setPen(PDFStylePainter::color(keyColor));
        painter.drawText(QStyle::visualRect(layoutDirection(), inner, keyRect), Qt::AlignVCenter | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::TextWordWrap, m_rows[i].key);

        const QString value = valueMetrics.elidedText(m_rows[i].value, Qt::ElideRight, valueRect.width());
        PDFStylePainter::text(&painter, QRectF(QStyle::visualRect(layoutDirection(), inner, valueRect)), Qt::AlignVCenter | (isRightToLeft ? Qt::AlignLeft : Qt::AlignRight) | Qt::TextSingleLine,
                              value, valueFont, PDFStylePainter::color(valueColor));
    }
}

bool PDFKeyValueList::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        // An elided value shows its whole text
        const QHelpEvent* helpEvent = static_cast<const QHelpEvent*>(event);
        const int index = getRowAt(helpEvent->pos());

        if (index >= 0)
        {
            const QRect inner = getRowRect(index).adjusted(PDFKeyValueListHelper::PADDING, 0, -PDFKeyValueListHelper::PADDING, 0);
            const qreal valueWidth = inner.width() - PDFKeyValueListHelper::KEY_WIDTH - PDFKeyValueListHelper::GAP;
            if (PDFStylePainter::textWidth(m_rows[index].value, PDFKeyValueListHelper::getValueFont()) > valueWidth)
            {
                QToolTip::showText(helpEvent->globalPos(), m_rows[index].value, this, getRowRect(index));
                return true;
            }
        }

        QToolTip::hideText();
        event->ignore();
        return true;
    }

    return BaseClass::event(event);
}

void PDFKeyValueList::contextMenuEvent(QContextMenuEvent* event)
{
    const int index = getRowAt(event->pos());
    if (index < 0 || m_rows[index].value.isEmpty())
    {
        BaseClass::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QAction* copyAction = menu.addAction(PDFThemeIcons::getIcon(QStringLiteral("copy")), tr("Copy Value"));
    if (menu.exec(event->globalPos()) == copyAction)
    {
        QGuiApplication::clipboard()->setText(m_rows[index].value);
    }
}

PDFDocumentTabBar::PDFDocumentTabBar(QWidget* parent) :
    BaseClass(parent)
{
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::DOCUMENT_TABS);
    setExpanding(false);
    setElideMode(Qt::ElideNone);
    setDrawBase(false);

    connect(this, &QTabBar::tabMoved, this, [this](int from, int to)
    {
        if (from >= 0 && to >= 0 && from < int(m_tabs.size()) && to < int(m_tabs.size()))
        {
            const TabState state = m_tabs[from];
            m_tabs.erase(m_tabs.begin() + from);
            m_tabs.insert(m_tabs.begin() + to, state);
        }
    });
}

const PDFDocumentTabBar::TabState* PDFDocumentTabBar::getTabState(int index) const
{
    return index >= 0 && index < int(m_tabs.size()) ? &m_tabs[index] : nullptr;
}

PDFDocumentTabBar::TabState* PDFDocumentTabBar::getTabState(int index)
{
    return index >= 0 && index < int(m_tabs.size()) ? &m_tabs[index] : nullptr;
}

bool PDFDocumentTabBar::isTabLocked(int index) const
{
    const TabState* state = getTabState(index);
    return state && state->locked;
}

void PDFDocumentTabBar::setTabLocked(int index, bool locked)
{
    TabState* state = getTabState(index);
    if (state && state->locked != locked)
    {
        state->locked = locked;
        changeTabState(index);
    }
}

bool PDFDocumentTabBar::isTabUnsaved(int index) const
{
    const TabState* state = getTabState(index);
    return state && state->unsaved;
}

void PDFDocumentTabBar::setTabUnsaved(int index, bool unsaved)
{
    TabState* state = getTabState(index);
    if (state && state->unsaved != unsaved)
    {
        state->unsaved = unsaved;
        changeTabState(index);
    }
}

qreal PDFDocumentTabBar::getTabProgress(int index) const
{
    const TabState* state = getTabState(index);
    return state ? state->progress : -1.0;
}

void PDFDocumentTabBar::setTabProgress(int index, qreal progress)
{
    TabState* state = getTabState(index);
    const qreal value = progress < 0.0 ? -1.0 : qMin(progress, 1.0);
    if (state && state->progress != value)
    {
        state->progress = value;
        update(tabRect(index));
    }
}

void PDFDocumentTabBar::setTabClosable(int index, bool closable)
{
    if (index < 0 || index >= count() || isTabClosable(index) == closable)
    {
        return;
    }

    const ButtonPosition side = static_cast<ButtonPosition>(style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, this));

    if (closable)
    {
        PDFIconButton* button = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, this);
        button->setAccessibleName(tr("Close tab"));
        button->setToolTip(tr("Close tab"));
        button->setFocusPolicy(Qt::NoFocus);

        connect(button, &QAbstractButton::clicked, this, [this, button, side]()
        {
            for (int i = 0; i < count(); ++i)
            {
                if (tabButton(i, side) == button)
                {
                    Q_EMIT tabCloseRequested(i);
                    break;
                }
            }
        });

        setTabButton(index, side, button);
    }
    else if (QWidget* button = tabButton(index, side))
    {
        setTabButton(index, side, nullptr);
        button->deleteLater();
    }
}

bool PDFDocumentTabBar::isTabClosable(int index) const
{
    const ButtonPosition side = static_cast<ButtonPosition>(style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, this));
    return tabButton(index, side) != nullptr;
}

int PDFDocumentTabBar::getExtraWidth(int index) const
{
    const TabState* state = getTabState(index);
    if (!state)
    {
        return 0;
    }

    int width = 0;
    if (state->locked)
    {
        width += PDFComponentPanelsHelper::TAB_ITEM_GAP + PDFComponentPanelsHelper::TAB_LOCK;
    }
    if (state->unsaved)
    {
        width += PDFComponentPanelsHelper::TAB_ITEM_GAP + PDFComponentPanelsHelper::TAB_DOT;
    }
    return width;
}

QSize PDFDocumentTabBar::tabSizeHint(int index) const
{
    QSize size = BaseClass::tabSizeHint(index);
    size.rwidth() += getExtraWidth(index);
    return size;
}

QSize PDFDocumentTabBar::minimumTabSizeHint(int index) const
{
    // Labels of the documents are never elided
    return tabSizeHint(index);
}

void PDFDocumentTabBar::tabInserted(int index)
{
    if (index >= 0 && index <= int(m_tabs.size()))
    {
        m_tabs.insert(m_tabs.begin() + index, TabState());
    }
    BaseClass::tabInserted(index);
}

void PDFDocumentTabBar::tabRemoved(int index)
{
    if (index >= 0 && index < int(m_tabs.size()))
    {
        m_tabs.erase(m_tabs.begin() + index);
    }
    BaseClass::tabRemoved(index);
}

void PDFDocumentTabBar::changeTabState(int index)
{
    updateAccessibleName(index);

    // The tab changes its width
    QEvent styleChange(QEvent::StyleChange);
    QTabBar::event(&styleChange);
    updateGeometry();
    update();
}

void PDFDocumentTabBar::updateAccessibleName(int index)
{
#if QT_CONFIG(accessibility)
    const TabState* state = getTabState(index);
    if (!state)
    {
        return;
    }

    // The lock and the dot are only drawn, so they are told to assistive technologies
    QString name = tabText(index);
    name.remove(QLatin1Char('&'));
    if (state->locked)
    {
        name = tr("%1, locked").arg(name);
    }
    if (state->unsaved)
    {
        name = tr("%1, unsaved changes").arg(name);
    }
    setAccessibleTabName(index, name);
#else
    Q_UNUSED(index);
#endif
}

void PDFDocumentTabBar::paintEvent(QPaintEvent* event)
{
    BaseClass::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;

    for (int i = 0; i < count(); ++i)
    {
        const TabState* state = getTabState(i);
        const QRect tab = tabRect(i);

        if (!state || !isTabVisible(i) || !tab.intersects(event->rect()) || (!state->locked && !state->unsaved && state->progress < 0.0))
        {
            continue;
        }

        QStyleOptionTab option;
        initStyleOption(&option, i);
        const bool isSelected = option.state.testFlag(QStyle::State_Selected);

        // Shape of the tab without the gap to the next tab
        const QRect logicalShape(tab.left(), tab.bottom() + 1 - PDFComponentPanelsHelper::TAB_HEIGHT, qMax(0, tab.width() - PDFComponentPanelsHelper::TAB_GAP), PDFComponentPanelsHelper::TAB_HEIGHT);
        const QRect shape = QStyle::visualRect(layoutDirection(), tab, logicalShape);

        // The lock and the dot follow the label
        const QRect textRect = style()->subElementRect(QStyle::SE_TabBarTabText, &option, this);
        QString label = tabText(i);
        label.remove(QLatin1Char('&'));
        const QFont font = PDFThemeFonts::getFont(isSelected ? PDFThemeFonts::Role::BodySemiBold : PDFThemeFonts::Role::BodyMedium);
        qreal position = PDFStylePainter::textWidth(label, font) + PDFComponentPanelsHelper::TAB_ITEM_GAP;

        auto place = [&](qreal extent)
        {
            const qreal top = shape.top() + (shape.height() - extent) / 2.0;
            const qreal left = isRightToLeft ? textRect.right() + 1 - position - extent : textRect.left() + position;
            position += extent + PDFComponentPanelsHelper::TAB_ITEM_GAP;
            return QRectF(left, top, extent, extent);
        };

        if (state->locked)
        {
            // Amber lock in every state of the tab
            PDFThemeIcons::paintFilled(&painter, place(PDFComponentPanelsHelper::TAB_LOCK), QStringLiteral("lock"),
                                       PDFStylePainter::color(PDFThemeColors::IconDefault), PDFStylePainter::color(PDFThemeColors::AccentAmberIcon));
        }

        if (state->unsaved)
        {
            // The dot has the color of the label
            PDFStylePainter::fillEllipse(&painter, place(PDFComponentPanelsHelper::TAB_DOT), PDFStylePainter::color(isSelected ? PDFThemeColors::TextPrimary : PDFThemeColors::TextTertiary));
        }

        if (state->progress >= 0.0)
        {
            const QRectF track(shape.left(), shape.bottom() + 1 - PDFComponentPanelsHelper::TAB_PROGRESS, shape.width(), PDFComponentPanelsHelper::TAB_PROGRESS);
            QRectF value(track.left(), track.top(), track.width() * state->progress, track.height());
            if (isRightToLeft)
            {
                value.moveRight(track.right());
            }

            PDFStylePainter::fillRect(&painter, track, PDFThemeColors::BgTrack);
            PDFStylePainter::fillRect(&painter, value, PDFThemeColors::BgAccent);
        }
    }
}

}   // namespace pdf
