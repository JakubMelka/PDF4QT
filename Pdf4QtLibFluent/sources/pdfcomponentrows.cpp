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

#include "pdfcomponentrows.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyle.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>

namespace pdf
{

/// Geometry of the navigation items
class PDFNavItemHelper
{
public:
    PDFNavItemHelper() = delete;

    static constexpr int HOME_WIDTH = 190;
    static constexpr int HOME_HEIGHT = 38;
    static constexpr int SETTINGS_WIDTH = 300;
    static constexpr int SETTINGS_HEIGHT = 52;
    static constexpr int PADDING = 12;
    static constexpr int ICON = 20;
    static constexpr int HOME_GAP = 10;
    static constexpr int SETTINGS_GAP = 12;
    static constexpr qreal RADIUS = 10.0;

    static constexpr int NAME_LINE = 16;        ///< Line of Semi Bold 13
    static constexpr int SUBTITLE_LINE = 13;    ///< Line of Regular 11
    static constexpr int TEXT_GAP = 1;

    /// Line between two groups of a navigation: the line of 1 px lies 10 px
    /// from the items around it, 6 px inside of the column of the items
    static constexpr int SEPARATOR_HEIGHT = 17;
    static constexpr int SEPARATOR_LINE = 8;
    static constexpr int SEPARATOR_INSET = 6;
};

/// Line between two groups of a navigation
class PDFNavSeparator : public QWidget
{
public:
    explicit PDFNavSeparator(QWidget* parent) :
        QWidget(parent)
    {
        setFixedHeight(PDFNavItemHelper::SEPARATOR_HEIGHT);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        const QRectF line(PDFNavItemHelper::SEPARATOR_INSET, PDFNavItemHelper::SEPARATOR_LINE, width() - 2 * PDFNavItemHelper::SEPARATOR_INSET, 1.0);
        PDFStylePainter::fillRect(&painter, line, PDFThemeColors::BorderSeparator);
    }
};

PDFNavItem::PDFNavItem(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFNavItem::PDFNavItem(const QString& text, const QString& iconName, Kind kind, QWidget* parent) :
    BaseClass(parent),
    m_iconName(iconName),
    m_kind(kind)
{
    setText(text);
    init();
}

void PDFNavItem::init()
{
    setCheckable(true);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PDFNavItem::setKind(Kind kind)
{
    if (m_kind != kind)
    {
        m_kind = kind;
        updateGeometry();
        update();
    }
}

void PDFNavItem::setIconName(const QString& iconName)
{
    if (m_iconName != iconName)
    {
        m_iconName = iconName;
        update();
    }
}

void PDFNavItem::setSubtitle(const QString& subtitle)
{
    if (m_subtitle != subtitle)
    {
        // The description of the item is replaced only, when it was the subtitle
        if (accessibleDescription().isEmpty() || accessibleDescription() == m_subtitle)
        {
            setAccessibleDescription(subtitle);
        }

        m_subtitle = subtitle;
        update();
    }
}

QSize PDFNavItem::sizeHint() const
{
    if (m_kind == Kind::Settings)
    {
        return QSize(PDFNavItemHelper::SETTINGS_WIDTH, PDFNavItemHelper::SETTINGS_HEIGHT);
    }

    return QSize(PDFNavItemHelper::HOME_WIDTH, PDFNavItemHelper::HOME_HEIGHT);
}

QSize PDFNavItem::minimumSizeHint() const
{
    // The texts are elided, the item keeps its height
    const int height = m_kind == Kind::Settings ? PDFNavItemHelper::SETTINGS_HEIGHT : PDFNavItemHelper::HOME_HEIGHT;
    return QSize(2 * PDFNavItemHelper::PADDING + PDFNavItemHelper::ICON, height);
}

void PDFNavItem::nextCheckState()
{
    // A click on the current item does nothing, it stays selected. An exclusive
    // group unchecks the previous item by this function, so it is kept there.
    if (!group() && isChecked())
    {
        return;
    }

    BaseClass::nextCheckState();
}

void PDFNavItem::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::EnabledChange)
    {
        update();
    }
}

void PDFNavItem::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF shape(rect());
    const PDFStylePainter::Radii radii(PDFNavItemHelper::RADIUS);
    const bool enabled = isEnabled();
    const bool selected = isChecked();

    // The selected item does not react to the pointer
    if (selected)
    {
        PDFStylePainter::fill(&painter, shape, radii, PDFThemeColors::BgAccentSubtle);
    }
    else if (enabled && isDown())
    {
        PDFStylePainter::fill(&painter, shape, radii, PDFThemeColors::BgPressed);
    }
    else if (enabled && underMouse())
    {
        PDFStylePainter::fill(&painter, shape, radii, PDFThemeColors::BgHover);
    }

    if (PDFWidgetStyle::isFocusRingVisible(this))
    {
        PDFStylePainter::strokeInside(&painter, shape, radii, PDFThemeColors::BorderFocus, 2.0);
    }

    PDFThemeColors::Color iconColor = PDFThemeColors::IconDefault;
    PDFThemeColors::Color textColor = m_kind == Kind::Settings ? PDFThemeColors::TextPrimary : PDFThemeColors::TextSecondary;
    PDFThemeColors::Color subtitleColor = PDFThemeColors::TextMuted;

    if (!enabled)
    {
        iconColor = PDFThemeColors::IconDisabled;
        textColor = PDFThemeColors::TextDisabled;
        subtitleColor = PDFThemeColors::TextDisabled;
    }
    else if (selected)
    {
        iconColor = PDFThemeColors::IconAccent;
        textColor = PDFThemeColors::TextAccent;
    }

    const Qt::LayoutDirection direction = layoutDirection();
    auto place = [this, direction](const QRectF& logical)
    {
        return QRectF(QStyle::visualRect(direction, rect(), logical.toAlignedRect()));
    };

    const qreal height = shape.height();
    const qreal gap = m_kind == Kind::Settings ? PDFNavItemHelper::SETTINGS_GAP : PDFNavItemHelper::HOME_GAP;
    const QRectF iconRect(PDFNavItemHelper::PADDING, (height - PDFNavItemHelper::ICON) / 2.0, PDFNavItemHelper::ICON, PDFNavItemHelper::ICON);
    PDFStylePainter::icon(&painter, place(iconRect), m_iconName, iconColor);

    const qreal textLeft = PDFNavItemHelper::PADDING + PDFNavItemHelper::ICON + gap;
    const qreal textWidth = qMax(0.0, shape.width() - textLeft - PDFNavItemHelper::PADDING);
    const int alignment = Qt::AlignVCenter | (direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::TextSingleLine;

    if (m_kind == Kind::Settings)
    {
        // Name and subtitle form a block of 30 px in the middle of the item
        const qreal blockHeight = PDFNavItemHelper::NAME_LINE + PDFNavItemHelper::TEXT_GAP + PDFNavItemHelper::SUBTITLE_LINE;
        const qreal top = qRound((height - blockHeight) / 2.0);
        const QFont nameFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::BodySemiBold);
        const QFont subtitleFont = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionRegular);

        const QRectF nameRect(textLeft, top, textWidth, PDFNavItemHelper::NAME_LINE);
        const QRectF subtitleRect(textLeft, top + PDFNavItemHelper::NAME_LINE + PDFNavItemHelper::TEXT_GAP, textWidth, PDFNavItemHelper::SUBTITLE_LINE);
        const QFontMetricsF nameMetrics(nameFont);
        const QFontMetricsF subtitleMetrics(subtitleFont);
        PDFStylePainter::text(&painter, place(nameRect), alignment, nameMetrics.elidedText(text(), Qt::ElideRight, textWidth), nameFont, PDFStylePainter::color(textColor));
        PDFStylePainter::text(&painter, place(subtitleRect), alignment, subtitleMetrics.elidedText(m_subtitle, Qt::ElideRight, textWidth), subtitleFont, PDFStylePainter::color(subtitleColor));
        return;
    }

    const QFont font = PDFThemeFonts::getFont(selected ? PDFThemeFonts::Role::BodySemiBold : PDFThemeFonts::Role::Body);
    const QFontMetricsF metrics(font);
    const QRectF textRect(textLeft, 0.0, textWidth, height);
    PDFStylePainter::text(&painter, place(textRect), alignment, metrics.elidedText(text(), Qt::ElideRight, textWidth), font, PDFStylePainter::color(textColor));
}

PDFNavList::PDFNavList(QWidget* parent) :
    PDFNavList(PDFNavItem::Kind::Home, parent)
{

}

PDFNavList::PDFNavList(PDFNavItem::Kind kind, QWidget* parent) :
    BaseClass(parent),
    m_layout(new QVBoxLayout(this)),
    m_group(new QButtonGroup(this)),
    m_kind(kind)
{
    // Items of a group are 2 px apart
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);
    m_layout->addStretch(1);
    m_group->setExclusive(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    connect(m_group, &QButtonGroup::idToggled, this, [this](int id, bool isChecked)
    {
        if (isChecked)
        {
            Q_EMIT currentIndexChanged(id);
        }
    });
}

int PDFNavList::addItem(const QString& text, const QString& iconName, const QString& subtitle)
{
    PDFNavItem* item = new PDFNavItem(text, iconName, m_kind, this);
    item->setSubtitle(subtitle);
    item->installEventFilter(this);

    const int index = getCount();
    m_group->addButton(item, index);

    // The stretch stays the last item of the layout
    m_layout->insertWidget(m_layout->count() - 1, item);

    if (index == 0)
    {
        item->setChecked(true);
    }

    return index;
}

void PDFNavList::addGroup()
{
    m_layout->insertWidget(m_layout->count() - 1, new PDFNavSeparator(this));
}

int PDFNavList::getCount() const
{
    return static_cast<int>(m_group->buttons().size());
}

PDFNavItem* PDFNavList::getItem(int index) const
{
    return qobject_cast<PDFNavItem*>(m_group->button(index));
}

int PDFNavList::getCurrentIndex() const
{
    return m_group->checkedId();
}

void PDFNavList::setCurrentIndex(int index)
{
    if (PDFNavItem* item = getItem(index))
    {
        item->setChecked(true);
    }
}

bool PDFNavList::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        const int key = keyEvent->key();
        PDFNavItem* item = qobject_cast<PDFNavItem*>(watched);

        if (item && (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Home || key == Qt::Key_End))
        {
            // The arrows move the selection to the next enabled item, also into another group
            const int count = getCount();
            const int current = m_group->id(item);
            int step = key == Qt::Key_Up || key == Qt::Key_End ? -1 : 1;
            int index = current;

            if (key == Qt::Key_Home)
            {
                index = -1;
            }
            else if (key == Qt::Key_End)
            {
                index = count;
            }

            for (index += step; index >= 0 && index < count; index += step)
            {
                PDFNavItem* target = getItem(index);
                if (target && target->isEnabled() && target->isVisible())
                {
                    target->setFocus(Qt::TabFocusReason);
                    target->setChecked(true);
                    break;
                }
            }

            return true;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdf
