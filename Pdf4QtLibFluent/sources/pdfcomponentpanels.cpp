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
#include "pdfcomponentcards.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleimpl.h"
#include "pdfwidgetstyleproperties.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QScreen>
#include <QPointer>
#include <QSignalBlocker>
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
#include <limits>

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

    /// Sets the accessible name and description of a widget, which draws its
    /// texts, unless the application has set its own (the last default texts
    /// are kept in \p name and \p description)
    static void replaceAccessibleTexts(QWidget* widget, QString& name, const QString& newName, QString& description, const QString& newDescription)
    {
        if (widget->accessibleName().isEmpty() || widget->accessibleName() == name)
        {
            widget->setAccessibleName(newName);
        }
        if (widget->accessibleDescription().isEmpty() || widget->accessibleDescription() == description)
        {
            widget->setAccessibleDescription(newDescription);
        }
        name = newName;
        description = newDescription;
    }

    /// Returns the square of an item of a document tab (the lock, the dot)
    /// following the label at the \p position (from the leading edge of the
    /// text), and moves the position after the item and its gap
    static QRectF placeTabItem(qreal extent, const QRect& shape, const QRect& textRect, bool isRightToLeft, qreal& position)
    {
        const qreal top = shape.top() + (shape.height() - extent) / 2.0;
        const qreal left = isRightToLeft ? textRect.right() + 1 - position - extent : textRect.left() + position;
        position += extent + TAB_ITEM_GAP;
        return QRectF(left, top, extent, extent);
    }

    /// Places a nested widget (nothing for a deleted one) at the logical
    /// rectangle inside of the bounds (mirrored in the right-to-left layout)
    static void placeWidget(QWidget* widget, Qt::LayoutDirection direction, const QRect& bounds, const QRect& logical)
    {
        if (widget)
        {
            widget->setGeometry(QStyle::visualRect(direction, bounds, logical));
        }
    }
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

    connect(this, &QLineEdit::textChanged, this, &PDFToolBarField::onTextChanged);
}

void PDFToolBarField::onTextChanged()
{
    // The field is as wide as its value
    updateTextInsets();
    updateGeometry();
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

    // The painter has the direction of the widget, the alignment is absolute (components.md 6.6)
    QPainter painter(this);
    const PDFThemeColors::Color color = isEnabled() ? PDFThemeColors::TextMuted : PDFThemeColors::TextDisabled;
    const Qt::Alignment alignment = (layoutDirection() == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute;
    PDFStylePainter::text(&painter, suffixRect, Qt::AlignVCenter | alignment | Qt::TextSingleLine, m_suffix, PDFComponentPanelsHelper::getSuffixFont(), PDFStylePainter::color(color));
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

    // The painter has the direction of the widget (the base direction of the
    // texts), the alignments are absolute: the key at the leading edge of its
    // column, the value at the trailing edge (components.md 6.6)
    QPainter painter(this);
    painter.setLayoutDirection(layoutDirection());
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const Qt::Alignment leading = (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignAbsolute;
    const Qt::Alignment trailing = (isRightToLeft ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignAbsolute;
    const QFont keyFont = PDFKeyValueListHelper::getKeyFont();
    const QFont valueFont = PDFKeyValueListHelper::getValueFont();
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
        painter.drawText(QStyle::visualRect(layoutDirection(), inner, keyRect), Qt::AlignVCenter | leading | Qt::TextWordWrap, m_rows[i].key);

        // The value is data: its base direction is the one of the whole value
        // (components.md 6.6), elided as the other components (no space before the ellipsis)
        const QString value = PDFComponentPainter::elidedText(m_rows[i].value, valueFont, valueRect.width());
        PDFComponentPainter::drawDataText(&painter, QRectF(QStyle::visualRect(layoutDirection(), inner, valueRect)), Qt::AlignVCenter | trailing | Qt::TextSingleLine,
                                          value, valueFont, PDFStylePainter::color(valueColor), PDFComponentPainter::getDataTextDirection(m_rows[i].value));
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

    // The application can delete the list while the menu runs its event loop:
    // the menu (a child of the list) is deleted with it, so it is not an object
    // on the stack, and the value is taken before
    const QString value = m_rows[index].value;
    QPointer<QMenu> menu = new QMenu(this);
    QAction* copyAction = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("copy")), tr("Copy Value"));
    QAction* chosenAction = menu->exec(event->globalPos());
    if (menu && chosenAction == copyAction)
    {
        QGuiApplication::clipboard()->setText(value);
    }
    delete menu.data();
}

/// Close button of a document tab. QTabBar keeps its buttons as plain pointers
/// and hides the old button, when it is replaced (QTabBar::setTabButton), so a
/// button deleted by the application takes itself out of its tab from its own
/// destructor, while it is still whole (a slot of the signal destroyed would let
/// QTabBar hide a widget, whose derived parts are gone).
class PDFDocumentTabCloseButton : public PDFIconButton
{
public:
    explicit PDFDocumentTabCloseButton(PDFDocumentTabBar* tabBar) :
        PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, tabBar),
        m_tabBar(tabBar)
    {
    }

    virtual ~PDFDocumentTabCloseButton() override
    {
        if (m_tabBar)
        {
            m_tabBar->forgetCloseButton(this);
        }
    }

    /// Forgets the tab bar (its destructor, the buttons are deleted after it by ~QWidget)
    void detach()
    {
        m_tabBar = nullptr;
    }

private:
    /// The tab bar, which has created the button. Not a QPointer: a QPointer is
    /// cleared only by ~QObject, after ~QWidget of the tab bar has deleted its
    /// children; ~PDFDocumentTabBar clears it by detach while the tab bar is whole.
    PDFDocumentTabBar* m_tabBar;
};

PDFDocumentTabBar::PDFDocumentTabBar(QWidget* parent) :
    BaseClass(parent)
{
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::DOCUMENT_TABS);
    setExpanding(false);
    setElideMode(Qt::ElideNone);
    setDrawBase(false);

    connect(this, &QTabBar::tabMoved, this, &PDFDocumentTabBar::onTabMoved);
}

void PDFDocumentTabBar::onTabMoved(int from, int to)
{
    // The state of the tab moves with it
    if (from >= 0 && to >= 0 && from < int(m_tabs.size()) && to < int(m_tabs.size()))
    {
        const TabState state = m_tabs[from];
        m_tabs.erase(m_tabs.begin() + from);
        m_tabs.insert(m_tabs.begin() + to, state);
    }
}

void PDFDocumentTabBar::onCloseButtonClicked()
{
    // The close button of the tab is the sender (the tab moves, the button stays with it)
    const QWidget* button = qobject_cast<const QWidget*>(sender());
    if (!button)
    {
        return;
    }

    for (int i = 0; i < count(); ++i)
    {
        if (tabButton(i, LeftSide) == button || tabButton(i, RightSide) == button)
        {
            Q_EMIT tabCloseRequested(i);
            break;
        }
    }
}

void PDFDocumentTabBar::forgetCloseButton(PDFDocumentTabCloseButton* button)
{
    // Called by the destructor of the button, while the button is whole:
    // QTabBar::setTabButton hides the button and forgets it (QTabBar keeps the
    // button as a plain pointer)
    m_closeButtons.removeAll(button);
    for (int i = 0; i < count(); ++i)
    {
        for (ButtonPosition position : { LeftSide, RightSide })
        {
            if (tabButton(i, position) == button)
            {
                setTabButton(i, position, nullptr);
            }
        }
    }
}

const PDFDocumentTabBar::TabState* PDFDocumentTabBar::getTabState(int index) const
{
    return index >= 0 && index < int(m_tabs.size()) ? &m_tabs[index] : nullptr;
}

PDFDocumentTabBar::TabState* PDFDocumentTabBar::getTabState(int index)
{
    return index >= 0 && index < int(m_tabs.size()) ? &m_tabs[index] : nullptr;
}

PDFDocumentTabBar::~PDFDocumentTabBar()
{
    // The close buttons are deleted after this destructor (by ~QWidget, or
    // later by the application, which has taken one): they must not call the
    // tab bar, which is no longer whole
    for (PDFDocumentTabCloseButton* button : std::as_const(m_closeButtons))
    {
        button->detach();
    }
    m_closeButtons.clear();
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
        PDFDocumentTabCloseButton* button = new PDFDocumentTabCloseButton(this);
        m_closeButtons.append(button);
        button->setAccessibleName(tr("Close tab"));
        button->setToolTip(tr("Close tab"));
        button->setFocusPolicy(Qt::NoFocus);

        connect(button, &QAbstractButton::clicked, this, &PDFDocumentTabBar::onCloseButtonClicked);
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

bool PDFDocumentTabBar::isTabPasswordProtected(int index) const
{
    const TabState* state = getTabState(index);
    return state && state->passwordProtected;
}

void PDFDocumentTabBar::setTabPasswordProtected(int index, bool passwordProtected)
{
    TabState* state = getTabState(index);
    if (state && state->passwordProtected != passwordProtected)
    {
        state->passwordProtected = passwordProtected;
        changeTabState(index);
    }
}

void PDFDocumentTabBar::initStyleOption(QStyleOptionTab* option, int tabIndex) const
{
    if (!option)
    {
        return;
    }

    BaseClass::initStyleOption(option, tabIndex);

    // The icon password replaces the icon of the tab (the icon of the tab is kept)
    const TabState* state = getTabState(tabIndex);
    if (state && state->passwordProtected)
    {
        option->icon = PDFThemeIcons::getIcon(QStringLiteral("password"));
    }
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
    if (state->passwordProtected && tabIcon(index).isNull())
    {
        // QTabBar measures the tab by its own icon: the place of the icon
        // password of a tab without an icon is added (the difference, which the
        // style gives to the icon)
        QStyleOptionTab option;
        initStyleOption(&option, index);
        const int withIcon = style()->sizeFromContents(QStyle::CT_TabBarTab, &option, QSize(), this).width();
        option.icon = QIcon();
        const int withoutIcon = style()->sizeFromContents(QStyle::CT_TabBarTab, &option, QSize(), this).width();
        width += qMax(0, withIcon - withoutIcon);
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
    relayoutTabStates();
}

void PDFDocumentTabBar::tabRemoved(int index)
{
    if (index >= 0 && index < int(m_tabs.size()))
    {
        m_tabs.erase(m_tabs.begin() + index);
    }
    BaseClass::tabRemoved(index);
    relayoutTabStates();
}

void PDFDocumentTabBar::relayoutTabStates()
{
    // QTabBar lays the tabs out before it calls tabInserted / tabRemoved, so
    // the extra widths of the states were taken from the old positions
    for (const TabState& state : m_tabs)
    {
        if (state.locked || state.unsaved || state.passwordProtected)
        {
            QEvent styleChange(QEvent::StyleChange);
            QTabBar::event(&styleChange);
            break;
        }
    }
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

void PDFDocumentTabBar::tabLayoutChange()
{
    BaseClass::tabLayoutChange();

    // QTabBar lays the tabs out after setTabText: the names of the tabs with a
    // state follow a renamed document (Save As of a protected document)
    updateAccessibleNames();
}

void PDFDocumentTabBar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateAccessibleNames();
    }
}

void PDFDocumentTabBar::updateAccessibleNames()
{
    // Inside of insertTab / removeTab the states are not yet at the positions of
    // the tabs (the names move with the tabs by themselves)
    if (int(m_tabs.size()) != count())
    {
        return;
    }

    for (int i = 0; i < count(); ++i)
    {
        const TabState* state = getTabState(i);
        if (state && (state->passwordProtected || state->locked || state->unsaved))
        {
            updateAccessibleName(i);
        }
    }
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
    if (state->passwordProtected)
    {
        name = tr("%1, password protected").arg(name);
    }
    if (state->locked)
    {
        name = tr("%1, locked").arg(name);
    }
    if (state->unsaved)
    {
        name = tr("%1, unsaved changes").arg(name);
    }
    if (!state->passwordProtected && !state->locked && !state->unsaved)
    {
        // QTabBar names the tab by its current text
        name.clear();
    }
    if (accessibleTabName(index) != name)
    {
        setAccessibleTabName(index, name);
    }
#else
    Q_UNUSED(index);
#endif
}

void PDFDocumentTabBar::getTabItemRects(int index, QRectF* lockRect, QRectF* dotRect, QRect* shape, bool* isSelected) const
{
    *lockRect = QRectF();
    *dotRect = QRectF();
    *shape = QRect();
    *isSelected = false;

    const TabState* state = getTabState(index);
    if (!state || index >= count())
    {
        return;
    }

    QStyleOptionTab option;
    initStyleOption(&option, index);
    *isSelected = option.state.testFlag(QStyle::State_Selected);

    // Shape of the tab without the gap to the next tab
    const QRect tab = tabRect(index);
    const QRect logicalShape(tab.left(), tab.bottom() + 1 - PDFComponentPanelsHelper::TAB_HEIGHT, qMax(0, tab.width() - PDFComponentPanelsHelper::TAB_GAP), PDFComponentPanelsHelper::TAB_HEIGHT);
    *shape = QStyle::visualRect(layoutDirection(), tab, logicalShape);

    // The lock and the dot follow the label
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const QRect textRect = style()->subElementRect(QStyle::SE_TabBarTabText, &option, this);
    QString label = tabText(index);
    label.remove(QLatin1Char('&'));
    const QFont font = PDFThemeFonts::getFont(*isSelected ? PDFThemeFonts::Role::BodySemiBold : PDFThemeFonts::Role::BodyMedium);
    qreal position = PDFStylePainter::textWidth(label, font) + PDFComponentPanelsHelper::TAB_ITEM_GAP;

    if (state->locked)
    {
        *lockRect = PDFComponentPanelsHelper::placeTabItem(PDFComponentPanelsHelper::TAB_LOCK, *shape, textRect, isRightToLeft, position);
    }
    if (state->unsaved)
    {
        *dotRect = PDFComponentPanelsHelper::placeTabItem(PDFComponentPanelsHelper::TAB_DOT, *shape, textRect, isRightToLeft, position);
    }
}

QRect PDFDocumentTabBar::getTabLockRect(int index) const
{
    if (index < 0 || index >= count() || !isTabVisible(index))
    {
        return QRect();
    }

    QRectF lockRect;
    QRectF dotRect;
    QRect shape;
    bool isSelected = false;
    getTabItemRects(index, &lockRect, &dotRect, &shape, &isSelected);
    return lockRect.isEmpty() ? QRect() : QRect(lockRect.topLeft().toPoint(), lockRect.size().toSize());
}

int PDFDocumentTabBar::getLockAt(const QPoint& position) const
{
    // The target of the pointer is the lock with a margin of 4 px (the icon has 12 px)
    for (int i = 0; i < count(); ++i)
    {
        const QRect lockRect = getTabLockRect(i);
        if (!lockRect.isEmpty() && lockRect.adjusted(-4, -4, 4, 4).contains(position))
        {
            return i;
        }
    }
    return -1;
}

void PDFDocumentTabBar::mousePressEvent(QMouseEvent* event)
{
    m_pressedLock = event->button() == Qt::LeftButton ? getLockAt(event->position().toPoint()) : -1;
    BaseClass::mousePressEvent(event);
}

void PDFDocumentTabBar::mouseReleaseEvent(QMouseEvent* event)
{
    const int pressedLock = m_pressedLock;
    m_pressedLock = -1;

    // QTabBar may emit its signals (a moved tab) - the tab bar may be deleted by them
    QPointer<PDFDocumentTabBar> guard(this);
    const QPoint position = event->position().toPoint();
    const bool isLeftButton = event->button() == Qt::LeftButton;
    BaseClass::mouseReleaseEvent(event);

    // The release on the lock of the pressed tab (the lock is looked up again at
    // the position of the release, the tab may have moved)
    if (guard && pressedLock >= 0 && isLeftButton && getLockAt(position) == pressedLock)
    {
        Q_EMIT lockClicked(pressedLock);
    }
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

        QRectF lockRect;
        QRectF dotRect;
        QRect shape;
        bool isSelected = false;
        getTabItemRects(i, &lockRect, &dotRect, &shape, &isSelected);

        if (state->locked)
        {
            // Amber lock in every state of the tab
            PDFThemeIcons::paintFilled(&painter, lockRect, QStringLiteral("lock"),
                                       PDFStylePainter::color(PDFThemeColors::IconDefault), PDFStylePainter::color(PDFThemeColors::AccentAmberIcon));
        }

        if (state->unsaved)
        {
            // The dot has the color of the label
            PDFStylePainter::fillEllipse(&painter, dotRect, PDFStylePainter::color(isSelected ? PDFThemeColors::TextPrimary : PDFThemeColors::TextTertiary));
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

// ---------------------------------------------------------------- Page range

/// Helper of the parser of the ranges of pages
class PDFPageRangeHelper
{
public:
    PDFPageRangeHelper() = delete;

    /// Longest text of a part shown in an error message
    static constexpr int MAX_QUOTED_LENGTH = 32;

    /// Parses a page number: an optional plus sign and decimal digits of any
    /// script, no other characters (the text is trimmed). A number, which does
    /// not fit qint64, is not a number.
    static bool parseNumber(QStringView text, qint64& value)
    {
        text = text.trimmed();
        if (text.startsWith(QLatin1Char('+')))
        {
            text = text.mid(1);
        }

        if (text.isEmpty())
        {
            return false;
        }

        qint64 result = 0;
        for (const QChar character : text)
        {
            const int digit = character.isDigit() ? character.digitValue() : -1;
            if (digit < 0 || digit > 9)
            {
                return false;
            }

            if (result > (std::numeric_limits<qint64>::max() - digit) / 10)
            {
                return false;
            }

            result = result * 10 + digit;
        }

        value = result;
        return true;
    }

    /// Returns the part quoted in an error message: a long part is shortened
    /// (never inside of a surrogate pair), the part is isolated by FIRST STRONG
    /// ISOLATE and POP DIRECTIONAL ISOLATE, so a text of another direction (or
    /// a control character of the direction) does not reorder the message
    static QString quoted(QStringView text)
    {
        text = text.trimmed();
        QString part;
        if (text.size() > MAX_QUOTED_LENGTH)
        {
            qsizetype length = MAX_QUOTED_LENGTH;
            if (text.at(length - 1).isHighSurrogate())
            {
                --length;
            }
            part = text.left(length).toString() + QChar(0x2026);
        }
        else
        {
            part = text.toString();
        }
        return QChar(0x2068) + part + QChar(0x2069);
    }

    /// Sets the error of a part, which is not a number
    static void setConvertError(PDFPageRange::Result& result, QStringView part)
    {
        result.errorMessage = PDFPageRange::tr("Can't convert '%1' to a number.").arg(quoted(part));
    }

    /// Returns true, if the document has the page; otherwise sets the error of the missing page
    static bool checkPageExists(PDFPageRange::Result& result, qint64 page, int pageCount)
    {
        if (page < 1 || page > pageCount)
        {
            result.errorMessage = PDFPageRange::getMissingPageText(page, pageCount);
            return false;
        }
        return true;
    }

    /// Comparator of std::upper_bound: the page lies before the interval
    static bool isBeforeInterval(int page, const PDFPageRange::Interval& interval)
    {
        return page < interval.first;
    }

    /// Returns true, if the interval has no page (it is left out of the normalized intervals)
    static bool isInvalidInterval(const PDFPageRange::Interval& interval)
    {
        return interval.first < 1 || interval.last < interval.first;
    }

    /// Order of the intervals: by the first page, then by the last page
    static bool isIntervalLess(const PDFPageRange::Interval& left, const PDFPageRange::Interval& right)
    {
        return left.first < right.first || (left.first == right.first && left.last < right.last);
    }
};

bool PDFPageRange::isListSeparator(QChar character)
{
    switch (character.unicode())
    {
        case u',':
        case 0x060C:    // ARABIC COMMA
        case 0x3001:    // IDEOGRAPHIC COMMA
        case 0xFF0C:    // FULLWIDTH COMMA
            return true;

        default:
            return false;
    }
}

bool PDFPageRange::isRangeSeparator(QChar character)
{
    switch (character.unicode())
    {
        case u'-':
        case 0x2010:    // HYPHEN
        case 0x2011:    // NON-BREAKING HYPHEN
        case 0x2013:    // EN DASH
        case 0x2212:    // MINUS SIGN
        case 0xFF0D:    // FULLWIDTH HYPHEN-MINUS
            return true;

        default:
            return false;
    }
}

qint64 PDFPageRange::Result::getCount() const
{
    // An invalid interval (first > last) counts no page
    qint64 count = 0;
    for (const Interval& interval : intervals)
    {
        count += qMax<qint64>(0, static_cast<qint64>(interval.last) - interval.first + 1);
    }
    return count;
}

bool PDFPageRange::Result::contains(int page) const
{
    // The intervals are sorted and disjoint
    auto it = std::upper_bound(intervals.cbegin(), intervals.cend(), page, &PDFPageRangeHelper::isBeforeInterval);
    if (it == intervals.cbegin())
    {
        return false;
    }

    --it;
    return page >= it->first && page <= it->last;
}

QList<int> PDFPageRange::Result::getPages() const
{
    // At most MAX_PAGE_COUNT pages (intervals, which were not normalized, may be huge)
    QList<int> pages;
    pages.reserve(static_cast<qsizetype>(qMin<qint64>(getCount(), MAX_PAGE_COUNT)));
    for (const Interval& interval : intervals)
    {
        for (qint64 page = interval.first; page <= interval.last && pages.size() < MAX_PAGE_COUNT; ++page)
        {
            pages << static_cast<int>(page);
        }
    }
    return pages;
}

QList<PDFPageRange::Interval> PDFPageRange::normalized(QList<Interval> intervals)
{
    intervals.removeIf(&PDFPageRangeHelper::isInvalidInterval);
    std::sort(intervals.begin(), intervals.end(), &PDFPageRangeHelper::isIntervalLess);

    QList<Interval> result;
    result.reserve(intervals.size());
    for (const Interval& interval : std::as_const(intervals))
    {
        // Overlapping and adjacent intervals are joined (in 64 bits, the last page may be INT_MAX)
        if (!result.isEmpty() && static_cast<qint64>(interval.first) <= static_cast<qint64>(result.back().last) + 1)
        {
            result.back().last = qMax(result.back().last, interval.last);
        }
        else
        {
            result << interval;
        }
    }
    return result;
}

QString PDFPageRange::format(const QList<Interval>& intervals)
{
    QStringList parts;
    for (const Interval& interval : intervals)
    {
        parts << (interval.first == interval.last ? QString::number(interval.first) : QStringLiteral("%1-%2").arg(interval.first).arg(interval.last));
    }
    return parts.join(QStringLiteral(", "));
}

QString PDFPageRange::getMissingPageText(qint64 page, int pageCount)
{
    // The page is the number typed by the user (as it is), the count of the pages
    // is formatted by the locale; the singular has its own text, so the English
    // texts are right also without a translation (a translation gives the plural
    // forms of its language to both texts)
    pageCount = qMax(0, pageCount);
    const QString message = pageCount == 1 ? tr("Page %1 does not exist – the document has %Ln page", nullptr, pageCount)
                                           : tr("Page %1 does not exist – the document has %Ln pages", nullptr, pageCount);
    return message.arg(page);
}

PDFPageRange::Result PDFPageRange::parse(const QString& text, int pageCount)
{
    using Helper = PDFPageRangeHelper;

    Result result;
    pageCount = qBound(0, pageCount, MAX_PAGE_COUNT);

    // The parts are separated by the commas of any script
    QList<QStringView> parts;
    const QStringView view(text);
    qsizetype partStart = 0;
    for (qsizetype i = 0; i <= view.size(); ++i)
    {
        if (i == view.size() || isListSeparator(view.at(i)))
        {
            parts << view.mid(partStart, i - partStart);
            partStart = i + 1;
        }
    }

    QList<Interval> intervals;
    for (QStringView part : std::as_const(parts))
    {
        part = part.trimmed();
        if (part.isEmpty())
        {
            continue;
        }

        qsizetype separator = -1;
        for (qsizetype i = 0; i < part.size(); ++i)
        {
            if (isRangeSeparator(part.at(i)))
            {
                separator = i;
                break;
            }
        }

        if (separator < 0)
        {
            qint64 page = 0;
            if (!Helper::parseNumber(part, page))
            {
                Helper::setConvertError(result, part);
                break;
            }
            if (!Helper::checkPageExists(result, page, pageCount))
            {
                break;
            }

            intervals << Interval{ static_cast<int>(page), static_cast<int>(page) };
            continue;
        }

        const QStringView lowerText = part.left(separator).trimmed();
        const QStringView upperText = part.mid(separator + 1).trimmed();
        const bool isLowerDefined = !lowerText.isEmpty();
        const bool isUpperDefined = !upperText.isEmpty();

        qint64 lower = 1;
        qint64 upper = pageCount;

        if (isLowerDefined && !Helper::parseNumber(lowerText, lower))
        {
            Helper::setConvertError(result, lowerText);
            break;
        }
        if (isUpperDefined && !Helper::parseNumber(upperText, upper))
        {
            Helper::setConvertError(result, upperText);
            break;
        }
        if ((isLowerDefined && !Helper::checkPageExists(result, lower, pageCount)) || (isUpperDefined && !Helper::checkPageExists(result, upper, pageCount)))
        {
            break;
        }

        if (!isLowerDefined && !isUpperDefined)
        {
            // "-" is the whole document (no page of an empty document)
            if (pageCount > 0)
            {
                intervals << Interval{ 1, pageCount };
            }
            continue;
        }

        if (lower > upper)
        {
            result.errorMessage = tr("Closed interval [%1, %2] is invalid.").arg(lower).arg(upper);
            break;
        }

        intervals << Interval{ static_cast<int>(lower), static_cast<int>(upper) };
    }

    if (result.errorMessage.isEmpty())
    {
        result.intervals = normalized(std::move(intervals));
    }

    return result;
}

// ------------------------------------------------------------ Page selection

/// Helper of the page selection
class PDFPageSelectionHelper
{
public:
    PDFPageSelectionHelper() = delete;

    static constexpr PDFPageSelection::Filter FILTERS[4] =
    {
        PDFPageSelection::Filter::Even,
        PDFPageSelection::Filter::Odd,
        PDFPageSelection::Filter::Portrait,
        PDFPageSelection::Filter::Landscape
    };

    static int getFilterIndex(PDFPageSelection::Filter filter)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (FILTERS[i] == filter)
            {
                return i;
            }
        }
        return -1;
    }

    static PDFPageSelection::Filters validFilters(PDFPageSelection::Filters filters)
    {
        return filters & (PDFPageSelection::Filter::Even | PDFPageSelection::Filter::Odd | PDFPageSelection::Filter::Portrait | PDFPageSelection::Filter::Landscape);
    }

    static bool isValidMode(PDFPageSelection::Mode mode)
    {
        switch (mode)
        {
            case PDFPageSelection::Mode::All:
            case PDFPageSelection::Mode::Current:
            case PDFPageSelection::Mode::Selected:
            case PDFPageSelection::Mode::Range:
                return true;
        }
        return false;
    }

    /// Returns the filters, which narrow the result (both filters of one kind are no filter)
    static PDFPageSelection::Filters effectiveFilters(PDFPageSelection::Filters filters)
    {
        using Filter = PDFPageSelection::Filter;
        if (filters.testFlag(Filter::Even) && filters.testFlag(Filter::Odd))
        {
            filters &= ~PDFPageSelection::Filters(Filter::Even | Filter::Odd);
        }
        if (filters.testFlag(Filter::Portrait) && filters.testFlag(Filter::Landscape))
        {
            filters &= ~PDFPageSelection::Filters(Filter::Portrait | Filter::Landscape);
        }
        return filters;
    }

    /// Returns the first page of the parity in the interval (0, if there is none)
    static qint64 firstOfParity(qint64 first, qint64 last, bool isEven)
    {
        const qint64 page = ((first % 2 == 0) == isEven) ? first : first + 1;
        return page <= last ? page : 0;
    }

    /// Returns the last page of the parity in the interval (0, if there is none)
    static qint64 lastOfParity(qint64 first, qint64 last, bool isEven)
    {
        const qint64 page = ((last % 2 == 0) == isEven) ? last : last - 1;
        return page >= first ? page : 0;
    }

    /// Sets the accessible name of a nested widget, unless the application has
    /// set its own (the last default name is kept in \p name)
    static void replaceAccessibleName(QWidget* widget, QString& name, const QString& newName)
    {
        if (widget && (widget->accessibleName().isEmpty() || widget->accessibleName() == name))
        {
            widget->setAccessibleName(newName);
        }
        name = newName;
    }
};

PDFPageSelection::PDFPageSelection(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setFocusPolicy(Qt::NoFocus);

    // The children are created in the order of Tab: modes, range, filters
    m_modes = new PDFSegmentedControl(this);
    for (int i = 0; i < 4; ++i)
    {
        m_modes->addSegment(QString());
    }
    connect(m_modes, &PDFSegmentedControl::currentIndexChanged, this, &PDFPageSelection::onModeIndexChanged);

    m_rangeField = new PDFLineEdit(this);
    m_rangeField->setVisible(false);
    m_rangeField->setMaxLength(MAX_RANGE_LENGTH);
    connect(m_rangeField, &QLineEdit::textChanged, this, &PDFPageSelection::onRangeTextChanged);

    for (int i = 0; i < 4; ++i)
    {
        QCheckBox* box = new QCheckBox(this);
        m_filterBoxes[i] = box;
        connect(box, &QCheckBox::toggled, this, &PDFPageSelection::onFilterToggled);
    }

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFPageSelection::onThemeChanged);

    retranslate();
}

PDFPageSelection::~PDFPageSelection() = default;

void PDFPageSelection::onModeIndexChanged(int index)
{
    // An index, which is not a mode (a segment added by the application), is
    // refused: the control shows the current mode again
    if (!PDFPageSelectionHelper::isValidMode(static_cast<Mode>(index)))
    {
        if (m_modes && m_modes->getCurrentIndex() != static_cast<int>(m_mode))
        {
            m_modes->setCurrentIndex(static_cast<int>(m_mode));
        }
        return;
    }

    setMode(static_cast<Mode>(index));
}

void PDFPageSelection::onRangeTextChanged(const QString& text)
{
    if (m_range != text)
    {
        // A copy: the text is a member of the field, which a slot of the
        // signals may delete before the next slot reads it
        const QString range = text;
        m_range = range;
        QPointer<PDFPageSelection> guard(this);
        updateResult(UpdateRange);
        if (guard)
        {
            Q_EMIT rangeChanged(range);
        }
    }
}

void PDFPageSelection::onFilterToggled()
{
    // The filters are the checked boxes (a box deleted by the application is no filter)
    Filters filters = Filter::None;
    for (int index = 0; index < 4; ++index)
    {
        if (m_filterBoxes[index] && m_filterBoxes[index]->isChecked())
        {
            filters |= PDFPageSelectionHelper::FILTERS[index];
        }
    }
    setFilters(filters);
}

void PDFPageSelection::onThemeChanged()
{
    updateGeometry();
    layoutChildren();
    update();
}

QStringList PDFPageSelection::getModeTexts()
{
    return QStringList{ tr("All"), tr("Current"), tr("Selected"), tr("Range") };
}

QStringList PDFPageSelection::getFilterTexts()
{
    return QStringList{ tr("Even pages"), tr("Odd pages"), tr("Portrait"), tr("Landscape") };
}

void PDFPageSelection::retranslate()
{
    if (m_isLabelDefault)
    {
        m_label = tr("Pages");
    }

    // Accessible names, which the application has set, are kept
    if (m_modes)
    {
        PDFPageSelectionHelper::replaceAccessibleName(m_modes, m_modesAccessibleName, tr("Page selection mode"));
        const QList<QAbstractButton*> segments = m_modes->findChildren<QAbstractButton*>(Qt::FindDirectChildrenOnly);
        const QStringList texts = getModeTexts();
        for (int i = 0; i < segments.size() && i < texts.size(); ++i)
        {
            segments.at(i)->setText(texts.at(i));
        }
    }

    if (m_rangeField)
    {
        m_rangeField->setPlaceholderText(tr("e.g. 1-3, 8, 10-12"));
        PDFPageSelectionHelper::replaceAccessibleName(m_rangeField, m_rangeAccessibleName, tr("Page range"));
    }

    const QStringList filterTexts = getFilterTexts();
    for (int i = 0; i < 4; ++i)
    {
        if (m_filterBoxes[i])
        {
            m_filterBoxes[i]->setText(filterTexts.at(i));
        }
    }

    updateGeometry();
    layoutChildren();
    update();

    // Error messages and the status are translated texts as well (the signal is the last)
    updateResult(UpdateRange | UpdateStatus);
}

void PDFPageSelection::setPageCount(int pageCount)
{
    pageCount = qBound(0, pageCount, PDFPageRange::MAX_PAGE_COUNT);
    if (m_pageCount != pageCount)
    {
        // The count of the header changes the minimal width; the signal is the last
        m_pageCount = pageCount;
        updateGeometry();
        update();
        updateResult(UpdateAll);
    }
}

void PDFPageSelection::setCurrentPage(int page)
{
    page = qMax(0, page);
    if (m_currentPage != page)
    {
        // Only the mode Current uses the page
        m_currentPage = page;
        if (m_mode == Mode::Current)
        {
            updateResult(UpdateResult);
        }
    }
}

void PDFPageSelection::setSelectedPages(const QList<int>& pages)
{
    QList<int> sortedPages;
    sortedPages.reserve(qMin<qsizetype>(pages.size(), PDFPageRange::MAX_PAGE_COUNT));
    for (const int page : pages)
    {
        if (page >= 1 && page <= PDFPageRange::MAX_PAGE_COUNT)
        {
            sortedPages << page;
        }
    }
    std::sort(sortedPages.begin(), sortedPages.end());
    sortedPages.erase(std::unique(sortedPages.begin(), sortedPages.end()), sortedPages.end());

    if (m_selectedPages != sortedPages)
    {
        // Only the mode Selected uses the pages
        m_selectedPages = std::move(sortedPages);
        if (m_mode == Mode::Selected)
        {
            updateResult(UpdateResult);
        }
    }
}

void PDFPageSelection::setPageLabels(const QStringList& labels)
{
    if (m_pageLabels != labels)
    {
        // The labels are shown only in the status
        m_pageLabels = labels;
        updateResult(UpdateStatus);
    }
}

void PDFPageSelection::setPageSizes(const QList<QSizeF>& sizes)
{
    if (m_pageSizes != sizes)
    {
        // Only an orientation filter uses the sizes
        m_pageSizes = sizes;
        if (isOrientationFilter())
        {
            updateResult(UpdateResult | UpdateSizes);
        }
    }
}

void PDFPageSelection::setMode(Mode mode)
{
    if (!PDFPageSelectionHelper::isValidMode(mode) || m_mode == mode)
    {
        return;
    }

    m_mode = mode;
    if (m_modes && m_modes->getCurrentIndex() != static_cast<int>(mode))
    {
        m_modes->setCurrentIndex(static_cast<int>(mode));
    }

    // The range field is shown in the mode Range only, the widget changes its
    // height; the children are placed before the signals
    updateGeometry();
    layoutChildren();
    update();

    QPointer<PDFPageSelection> guard(this);
    updateResult(UpdateResult);
    if (guard)
    {
        Q_EMIT modeChanged(mode);
    }
}

void PDFPageSelection::setRange(const QString& range)
{
    // QLineEdit cuts a longer text to its maximal length; the widget does the same without the field
    const QString text = range.left(MAX_RANGE_LENGTH);
    if (m_range == text)
    {
        return;
    }

    if (m_rangeField)
    {
        // The field reports the change
        m_rangeField->setText(text);
        return;
    }

    m_range = text;
    QPointer<PDFPageSelection> guard(this);
    updateResult(UpdateRange);
    if (guard)
    {
        Q_EMIT rangeChanged(text);
    }
}

void PDFPageSelection::setFilters(Filters filters)
{
    filters = PDFPageSelectionHelper::validFilters(filters);

    // The check boxes show the filters (without a recursion of their signals)
    for (int i = 0; i < 4; ++i)
    {
        if (m_filterBoxes[i])
        {
            const QSignalBlocker blocker(m_filterBoxes[i]);
            m_filterBoxes[i]->setChecked(filters.testFlag(PDFPageSelectionHelper::FILTERS[i]));
        }
    }

    if (m_filters == filters)
    {
        return;
    }

    m_filters = filters;
    QPointer<PDFPageSelection> guard(this);
    updateResult(UpdateResult);
    if (guard)
    {
        Q_EMIT filtersChanged(filters);
    }
}

void PDFPageSelection::setLabel(const QString& label)
{
    m_isLabelDefault = false;
    if (m_label != label)
    {
        // The label is the header and the accessible name; the signal is the last
        m_label = label;
        updateGeometry();
        update();
        updateResult(UpdateTexts);
    }
}

void PDFPageSelection::setCustomError(const QString& message)
{
    if (m_customError != message)
    {
        m_customError = message;
        updateResult(UpdateTexts);
    }
}

QString PDFPageSelection::getError() const
{
    if (m_mode == Mode::Range && !m_rangeError.isEmpty())
    {
        return m_rangeError;
    }

    return m_customError;
}

bool PDFPageSelection::isParityFilter() const
{
    return m_filters.testFlag(Filter::Even) != m_filters.testFlag(Filter::Odd);
}

bool PDFPageSelection::isOrientationFilter() const
{
    return m_filters.testFlag(Filter::Portrait) != m_filters.testFlag(Filter::Landscape);
}

const QList<PDFPageRange::Interval>& PDFPageSelection::getResultIntervals() const
{
    if (m_isResultBuilt)
    {
        return m_result;
    }

    // The intervals of the mode narrowed by the filters
    QList<PDFPageRange::Interval> result;
    if (!isParityFilter() && !isOrientationFilter())
    {
        result = m_base;
    }
    else if (!isOrientationFilter())
    {
        // Every second page
        const bool isEven = m_filters.testFlag(Filter::Even);
        result.reserve(static_cast<qsizetype>(qMin<qint64>(m_summary.count, PDFPageRange::MAX_PAGE_COUNT)));
        for (const PDFPageRange::Interval& interval : m_base)
        {
            const qint64 first = PDFPageSelectionHelper::firstOfParity(interval.first, interval.last, isEven);
            for (qint64 page = first; first > 0 && page <= interval.last; page += 2)
            {
                result << PDFPageRange::Interval{ static_cast<int>(page), static_cast<int>(page) };
            }
        }
    }
    else
    {
        // The pages of known sizes (a page of an unknown size passes no orientation filter)
        const qint64 knownPages = m_pageSizes.size();
        for (const PDFPageRange::Interval& interval : m_base)
        {
            const qint64 last = qMin<qint64>(interval.last, knownPages);
            for (qint64 page = interval.first; page <= last; ++page)
            {
                if (!passesFilters(static_cast<int>(page)))
                {
                    continue;
                }

                if (!result.isEmpty() && static_cast<qint64>(result.back().last) + 1 == page)
                {
                    result.back().last = static_cast<int>(page);
                }
                else
                {
                    result << PDFPageRange::Interval{ static_cast<int>(page), static_cast<int>(page) };
                }
            }
        }
    }

    m_result = std::move(result);
    m_isResultBuilt = true;
    return m_result;
}

QList<int> PDFPageSelection::getResultPages() const
{
    PDFPageRange::Result result;
    result.intervals = getResultIntervals();
    return result.getPages();
}

QCheckBox* PDFPageSelection::getFilterCheckBox(Filter filter) const
{
    const int index = PDFPageSelectionHelper::getFilterIndex(filter);
    return index >= 0 ? m_filterBoxes[index].data() : nullptr;
}

QString PDFPageSelection::getPageText(int page) const
{
    // The label is shown next to the number of the page ("3 (iii)"), unless it is the number
    const QString number = QLocale().toString(page);
    const QString label = m_pageLabels.value(page - 1);
    return (label.isEmpty() || label == number || label == QString::number(page)) ? number : tr("%1 (%2)").arg(number, label);
}

bool PDFPageSelection::passesFilters(int page) const
{
    const bool even = m_filters.testFlag(Filter::Even);
    const bool odd = m_filters.testFlag(Filter::Odd);
    if (even != odd && (page % 2 == 0) != even)
    {
        return false;
    }

    const bool portrait = m_filters.testFlag(Filter::Portrait);
    const bool landscape = m_filters.testFlag(Filter::Landscape);
    if (portrait != landscape)
    {
        // A page of an unknown size passes no orientation filter
        const QSizeF size = m_pageSizes.value(page - 1);
        if (!size.isValid() || size.isEmpty() || !qIsFinite(size.width()) || !qIsFinite(size.height()))
        {
            return false;
        }

        const bool isLandscape = size.width() > size.height();
        if (isLandscape != landscape)
        {
            return false;
        }
    }

    return true;
}

QList<PDFPageRange::Interval> PDFPageSelection::computeBase() const
{
    QList<PDFPageRange::Interval> base;
    switch (m_mode)
    {
        case Mode::All:
            if (m_pageCount > 0)
            {
                base << PDFPageRange::Interval{ 1, m_pageCount };
            }
            break;

        case Mode::Current:
            if (m_currentPage >= 1 && m_currentPage <= m_pageCount)
            {
                base << PDFPageRange::Interval{ m_currentPage, m_currentPage };
            }
            break;

        case Mode::Selected:
            for (const int page : m_selectedPages)
            {
                if (page > m_pageCount)
                {
                    break;
                }
                base << PDFPageRange::Interval{ page, page };
            }
            base = PDFPageRange::normalized(std::move(base));
            break;

        case Mode::Range:
            base = m_rangeIntervals;
            break;
    }
    return base;
}

PDFPageSelection::Summary PDFPageSelection::computeSummary(const QList<PDFPageRange::Interval>& base) const
{
    Summary summary;
    if (!isParityFilter() && !isOrientationFilter())
    {
        for (const PDFPageRange::Interval& interval : base)
        {
            addToSummary(summary, interval.first, interval.last, static_cast<qint64>(interval.last) - interval.first + 1);
        }
    }
    else if (!isOrientationFilter())
    {
        // Every second page, counted without a loop over the pages
        const bool isEven = m_filters.testFlag(Filter::Even);
        for (const PDFPageRange::Interval& interval : base)
        {
            const qint64 first = PDFPageSelectionHelper::firstOfParity(interval.first, interval.last, isEven);
            const qint64 last = PDFPageSelectionHelper::lastOfParity(interval.first, interval.last, isEven);
            if (first > 0 && last >= first)
            {
                addToSummary(summary, first, last, (last - first) / 2 + 1);
            }
        }
    }
    else
    {
        // Only the pages of known sizes can pass (the loop is bounded by the sizes of the application)
        const qint64 knownPages = m_pageSizes.size();
        for (const PDFPageRange::Interval& interval : base)
        {
            const qint64 last = qMin<qint64>(interval.last, knownPages);
            for (qint64 page = interval.first; page <= last; ++page)
            {
                if (passesFilters(static_cast<int>(page)))
                {
                    addToSummary(summary, page, page, 1);
                }
            }
        }
    }

    return summary;
}

void PDFPageSelection::addToSummary(Summary& summary, qint64 first, qint64 last, qint64 count)
{
    // The pages come in the ascending order: the first one is kept, the last one moves
    if (count <= 0)
    {
        return;
    }
    summary.first = summary.count == 0 ? static_cast<int>(first) : summary.first;
    summary.last = static_cast<int>(last);
    summary.count += count;
}

QString PDFPageSelection::computeStatus() const
{
    // The plural forms of the language, the numbers of the locale; the singular
    // has its own text, so the English text is right also without a translation
    const int count = static_cast<int>(qMin<qint64>(qMax<qint64>(0, m_summary.count), PDFPageRange::MAX_PAGE_COUNT));
    if (count <= 0)
    {
        return tr("No pages selected");
    }
    if (count == 1)
    {
        return tr("%Ln page selected · %1", nullptr, count).arg(getPageText(m_summary.first));
    }
    return tr("%Ln pages selected · %1 – %2", nullptr, count).arg(getPageText(m_summary.first), getPageText(m_summary.last));
}

void PDFPageSelection::updateResult(int flags)
{
    bool isChanged = false;

    if (flags & UpdateRange)
    {
        // The text of the range is validated in each mode (the field shows its error)
        PDFPageRange::Result range = PDFPageRange::parse(m_range, m_pageCount);
        m_rangeError = range.errorMessage;
        if (range.intervals != m_rangeIntervals)
        {
            m_rangeIntervals = std::move(range.intervals);
            if (m_mode == Mode::Range)
            {
                flags |= UpdateResult;
            }
        }
    }

    if (flags & UpdateResult)
    {
        // The base intervals, the filters and the summary describe the result
        // (it is built only when it is asked for, a huge document is not materialized)
        QList<PDFPageRange::Interval> base = computeBase();
        const Summary summary = computeSummary(base);
        const Filters filters = PDFPageSelectionHelper::effectiveFilters(m_filters);
        const bool isResultChanged = (flags & UpdateSizes) || base != m_base || filters != m_resultFilters || summary != m_summary;
        if (isResultChanged)
        {
            m_base = std::move(base);
            m_resultFilters = filters;
            m_summary = summary;
            m_result.clear();
            m_isResultBuilt = false;
            isChanged = true;
        }
        flags |= UpdateStatus;
    }

    if (flags & UpdateStatus)
    {
        const QString status = computeStatus();
        if (m_statusText != status)
        {
            m_statusText = status;
            isChanged = true;
        }
    }

    const QString previousError = m_shownError;
    const QString error = getError();
    const bool isErrorChanged = previousError != error;
    m_shownError = error;

    // The field shows the error of its own text
    if (m_rangeField)
    {
        m_rangeField->setInvalid(!m_rangeError.isEmpty(), m_rangeError);
    }

    // The status and the error are drawn, assistive technologies read them as the description
    PDFComponentPanelsHelper::replaceAccessibleTexts(this, m_accessibleName, m_label, m_accessibleDescription,
                                                     error.isEmpty() ? m_statusText : QStringLiteral("%1\n%2").arg(m_statusText, error));

#if QT_CONFIG(accessibility) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (!error.isEmpty() && isErrorChanged && isVisible())
    {
        // The selection, which becomes invalid, is announced assertively; a
        // changed message (typing inside of an invalid range) politely
        QAccessibleAnnouncementEvent announcement(this, error);
        announcement.setPoliteness(previousError.isEmpty() ? QAccessible::AnnouncementPoliteness::Assertive : QAccessible::AnnouncementPoliteness::Polite);
        QAccessible::updateAccessibility(&announcement);
    }
#endif

    if (isErrorChanged)
    {
        // The error row changes the height
        updateGeometry();
        layoutChildren();
    }
    update();

    if (isChanged || isErrorChanged)
    {
        Q_EMIT resultChanged();
    }
}

PDFComponentPainter::PageSelection PDFPageSelection::getContent() const
{
    // The count of the pages: the plural forms of the language, the number of the locale
    PDFComponentPainter::PageSelection content;
    content.label = m_label;
    content.total = m_pageCount == 1 ? tr("%Ln page", nullptr, m_pageCount) : tr("%Ln pages", nullptr, m_pageCount);
    content.filtersLabel = tr("Only");
    content.status = m_statusText;
    content.error = getError();
    content.rangeVisible = m_mode == Mode::Range;
    content.modeLabels = getModeTexts();
    content.filterLabels = getFilterTexts();
    return content;
}

QSize PDFPageSelection::sizeHint() const
{
    return PDFComponentPainter::getPageSelectionSize(getContent());
}

QSize PDFPageSelection::minimumSizeHint() const
{
    return PDFComponentPainter::getPageSelectionMinimumSize(getContent());
}

int PDFPageSelection::heightForWidth(int width) const
{
    return PDFComponentPainter::getPageSelectionHeight(getContent(), width);
}

void PDFPageSelection::layoutChildren()
{
    // The segments are placed by setGeometry (their policy is Ignored), so the
    // minimal width of the page selection (the padding 6 of the segments) may lie
    // below the minimal size hint of PDFSegmentedControl (the padding 14 of the style)
    const PDFComponentPainter::PageSelectionLayout layout = PDFComponentPainter::getPageSelectionLayout(getContent(), width());
    PDFComponentPanelsHelper::placeWidget(m_modes, layoutDirection(), rect(), layout.modes);

    if (m_rangeField)
    {
        PDFComponentPanelsHelper::placeWidget(m_rangeField, layoutDirection(), rect(), layout.range);
        m_rangeField->setVisible(m_mode == Mode::Range);
    }

    for (int i = 0; i < 4; ++i)
    {
        PDFComponentPanelsHelper::placeWidget(m_filterBoxes[i], layoutDirection(), rect(), layout.filters[i]);
    }
}

void PDFPageSelection::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const PDFComponentPainter::State state(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection());
    PDFComponentPainter::paintPageSelection(&painter, QRectF(rect()), getContent(), state);
}

void PDFPageSelection::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutChildren();
}

void PDFPageSelection::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LanguageChange:
            retranslate();
            break;

        case QEvent::FontChange:
        case QEvent::StyleChange:
            updateGeometry();
            layoutChildren();
            update();
            break;

        case QEvent::LayoutDirectionChange:
            layoutChildren();
            update();
            break;

        case QEvent::EnabledChange:
            update();
            break;

        default:
            break;
    }
}

// ------------------------------------------------------------- Continue with

PDFContinueWith::PDFContinueWith(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setFocusPolicy(Qt::NoFocus);

    // The link is before the choices in the order of Tab (the header is the first row)
    m_link = new PDFTextLink(this);
    connect(m_link.data(), &QAbstractButton::clicked, this, &PDFContinueWith::anotherToolRequested);

    // An exclusive group, which can be left empty (Tool choice is deselectable)
    m_group.setExclusive(true);
    for (int i = 0; i < PDFComponentPainter::getContinueWithMaxChoices(); ++i)
    {
        PDFToolChoice* choice = new PDFToolChoice(this);
        choice->setVisible(false);
        choice->installEventFilter(this);
        m_group.addButton(choice, i);
        m_choiceWidgets << choice;
        connect(choice, &QAbstractButton::toggled, this, &PDFContinueWith::onChoiceToggled);
    }

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFContinueWith::onThemeChanged);

    retranslate();
}

void PDFContinueWith::onThemeChanged()
{
    updateGeometry();
    layoutChildren();
    update();
}

PDFContinueWith::~PDFContinueWith()
{
    for (const QPointer<PDFToolChoice>& choice : std::as_const(m_choiceWidgets))
    {
        if (choice)
        {
            choice->removeEventFilter(this);
        }
    }
}

void PDFContinueWith::retranslate()
{
    if (m_isTitleDefault)
    {
        m_title = tr("Then continue with");
    }
    if (m_isNoteDefault)
    {
        m_titleNote = tr("optional");
    }
    if (m_isLinkDefault && m_link)
    {
        m_link->setText(tr("Another tool…"));
    }
    if (m_isCaptionDefault)
    {
        m_caption = tr("Nothing is chosen – the tool just finishes.");
    }
    if (m_isSelectionCaptionDefault)
    {
        m_selectionCaption = getDefaultSelectionCaption();
    }

    updateDescription();
    updateGeometry();
    layoutChildren();
    update();
}

void PDFContinueWith::setTitle(const QString& title)
{
    m_isTitleDefault = false;
    if (m_title != title)
    {
        m_title = title;
        updateDescription();
        updateGeometry();
        layoutChildren();
        update();
    }
}

void PDFContinueWith::setTitleNote(const QString& note)
{
    m_isNoteDefault = false;
    if (m_titleNote != note)
    {
        m_titleNote = note;
        update();
    }
}

QString PDFContinueWith::getLinkText() const
{
    return m_link ? m_link->text() : QString();
}

void PDFContinueWith::setLinkText(const QString& text)
{
    m_isLinkDefault = false;
    if (m_link && m_link->text() != text)
    {
        m_link->setText(text);
        m_link->setVisible(!text.isEmpty());
        updateGeometry();
        layoutChildren();
        update();
    }
}

void PDFContinueWith::setCaption(const QString& caption)
{
    m_isCaptionDefault = false;
    if (m_caption != caption)
    {
        m_caption = caption;
        updateDescription();
        updateGeometry();
        update();
    }
}

void PDFContinueWith::setSelectionCaption(const QString& pattern)
{
    m_isSelectionCaptionDefault = false;
    if (m_selectionCaption != pattern)
    {
        m_selectionCaption = pattern;
        updateDescription();
        updateGeometry();
        update();
    }
}

QString PDFContinueWith::getDefaultSelectionCaption() const
{
    // tools.md 9.3 item 12: "Add Page Numbers starts on the new document after Merge finishes successfully."
    return m_currentToolName.isEmpty() ? tr("%1 starts on the new document after this tool finishes successfully.")
                                       : tr("%1 starts on the new document after %2 finishes successfully.");
}

void PDFContinueWith::setCurrentToolName(const QString& name)
{
    if (m_currentToolName != name)
    {
        m_currentToolName = name;
        if (m_isSelectionCaptionDefault)
        {
            m_selectionCaption = getDefaultSelectionCaption();
        }
        updateDescription();
        updateGeometry();
        update();
    }
}

QString PDFContinueWith::getCurrentCaption() const
{
    if (m_selectedChoice < 0 || m_selectedChoice >= m_choices.size())
    {
        return m_caption;
    }

    // %1 is the selected tool, %2 the current tool; both are replaced in one
    // pass (a name with "%2" in it is not replaced again), other characters are
    // taken as they are
    const QString name = m_choices.at(m_selectedChoice).name;
    QString caption;
    caption.reserve(m_selectionCaption.size() + name.size() + m_currentToolName.size());
    for (qsizetype i = 0; i < m_selectionCaption.size(); ++i)
    {
        const QChar character = m_selectionCaption.at(i);
        if (character == QLatin1Char('%') && i + 1 < m_selectionCaption.size())
        {
            const QChar next = m_selectionCaption.at(i + 1);
            if (next == QLatin1Char('1') || next == QLatin1Char('2'))
            {
                caption += next == QLatin1Char('1') ? name : m_currentToolName;
                ++i;
                continue;
            }
        }
        caption += character;
    }
    return caption;
}

void PDFContinueWith::setChoices(const QList<Choice>& choices)
{
    QList<Choice> shownChoices = choices.mid(0, PDFComponentPainter::getContinueWithMaxChoices());
    for (Choice& choice : shownChoices)
    {
        choice.hue = PDFComponentPainter::normalized(choice.hue);
    }

    bool isEqual = shownChoices.size() == m_choices.size();
    for (qsizetype i = 0; isEqual && i < shownChoices.size(); ++i)
    {
        const Choice& left = shownChoices.at(i);
        const Choice& right = m_choices.at(i);
        isEqual = left.name == right.name && left.iconName == right.iconName && left.hue == right.hue;
    }
    if (isEqual)
    {
        return;
    }

    // Other tools: the new choices are installed first (a slot of the signal
    // sees them), the stop of Tab is the first one again
    QPointer<PDFToolChoice> selectedWidget = getChoiceWidget(m_selectedChoice);
    const bool hadSelection = m_selectedChoice >= 0;
    m_choices = std::move(shownChoices);
    m_tabStop = nullptr;
    for (int i = 0; i < m_choiceWidgets.size(); ++i)
    {
        PDFToolChoice* widget = m_choiceWidgets.at(i);
        if (!widget)
        {
            continue;
        }

        if (i < m_choices.size())
        {
            const Choice& choice = m_choices.at(i);
            widget->setName(choice.name);
            widget->setIconName(choice.iconName);
            widget->setHue(choice.hue);
        }
        widget->setVisible(i < m_choices.size());
    }

    updateTabStop();
    updateDescription();
    updateGeometry();
    layoutChildren();
    update();

    // Other tools: nothing is preselected (the signal is the last)
    if (!hadSelection)
    {
        return;
    }

    QPointer<PDFContinueWith> guard(this);
    if (selectedWidget && selectedWidget->isChecked())
    {
        // The handler of the signal toggled updates the state and emits the signal
        selectedWidget->setSelected(false);
    }

    if (guard && m_selectedChoice != -1)
    {
        // The selected widget has been deleted by the application
        m_selectedChoice = -1;
        updateTabStop();
        updateDescription();
        update();
        Q_EMIT selectedChoiceChanged(-1);
    }
}

int PDFContinueWith::getSelectedChoice() const
{
    return m_selectedChoice;
}

void PDFContinueWith::setSelectedChoice(int index)
{
    if (index < -1 || index >= m_choices.size() || index == m_selectedChoice)
    {
        return;
    }

    if (index == -1)
    {
        if (PDFToolChoice* selected = getChoiceWidget(m_selectedChoice))
        {
            // The handler of the signal toggled updates the state
            selected->setSelected(false);
        }
        else
        {
            m_selectedChoice = -1;
            onChoiceToggled();
        }
        return;
    }

    if (PDFToolChoice* choice = getChoiceWidget(index))
    {
        choice->setSelected(true);
    }
}

PDFToolChoice* PDFContinueWith::getChoiceWidget(int index) const
{
    return (index >= 0 && index < m_choices.size() && index < m_choiceWidgets.size()) ? m_choiceWidgets.at(index).data() : nullptr;
}

void PDFContinueWith::onChoiceToggled()
{
    // The checked choice (an exclusive group switches two buttons, the first
    // signal already sees the new one)
    int selected = -1;
    for (int i = 0; i < m_choices.size() && i < m_choiceWidgets.size(); ++i)
    {
        if (m_choiceWidgets.at(i) && m_choiceWidgets.at(i)->isChecked())
        {
            selected = i;
            break;
        }
    }

    if (selected == m_selectedChoice)
    {
        return;
    }

    m_selectedChoice = selected;
    updateTabStop();
    updateDescription();

    // The caption may have another count of lines
    updateGeometry();
    update();
    Q_EMIT selectedChoiceChanged(selected);
}

void PDFContinueWith::updateTabStop()
{
    // The choices are one stop of Tab: the focused one, the selected one, the last focused one, or the first one
    QList<PDFToolChoice*> shown;
    for (int i = 0; i < m_choices.size() && i < m_choiceWidgets.size(); ++i)
    {
        if (m_choiceWidgets.at(i))
        {
            shown << m_choiceWidgets.at(i).data();
        }
    }

    PDFToolChoice* stop = nullptr;
    for (PDFToolChoice* choice : std::as_const(shown))
    {
        if (choice->hasFocus())
        {
            stop = choice;
        }
    }
    if (!stop)
    {
        stop = getChoiceWidget(m_selectedChoice);
    }
    if (!stop && m_tabStop && shown.contains(m_tabStop.data()))
    {
        stop = m_tabStop.data();
    }
    if (!stop && !shown.isEmpty())
    {
        stop = shown.front();
    }

    for (const QPointer<PDFToolChoice>& choice : std::as_const(m_choiceWidgets))
    {
        if (choice)
        {
            choice->setFocusPolicy(choice == stop ? Qt::StrongFocus : Qt::ClickFocus);
        }
    }
}

void PDFContinueWith::updateDescription()
{
    // The header and the caption are drawn, they are the name and the description of the group
    PDFComponentPanelsHelper::replaceAccessibleTexts(this, m_accessibleName, m_title, m_accessibleDescription, getCurrentCaption());
}

PDFComponentPainter::ContinueWith PDFContinueWith::getContent() const
{
    PDFComponentPainter::ContinueWith content;
    content.title = m_title;
    content.note = m_titleNote;
    content.linkText = (m_link && !m_link->isHidden()) ? m_link->text() : QString();
    content.caption = getCurrentCaption();
    content.choices = m_choices;
    content.selectedChoice = m_selectedChoice;
    content.paintNested = false;
    return content;
}

QSize PDFContinueWith::sizeHint() const
{
    return PDFComponentPainter::getContinueWithSize(getContent());
}

QSize PDFContinueWith::minimumSizeHint() const
{
    return PDFComponentPainter::getContinueWithMinimumSize(getContent());
}

int PDFContinueWith::heightForWidth(int width) const
{
    return PDFComponentPainter::getContinueWithHeight(getContent(), width);
}

void PDFContinueWith::layoutChildren()
{
    const PDFComponentPainter::ContinueWithLayout layout = PDFComponentPainter::getContinueWithLayout(getContent(), width());

    if (m_link)
    {
        m_link->setGeometry(QStyle::visualRect(layoutDirection(), rect(), layout.link));
    }

    for (int i = 0; i < layout.choices.size() && i < m_choiceWidgets.size(); ++i)
    {
        if (m_choiceWidgets.at(i))
        {
            m_choiceWidgets.at(i)->setGeometry(QStyle::visualRect(layoutDirection(), rect(), layout.choices.at(i)));
        }
    }
}

void PDFContinueWith::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const PDFComponentPainter::State state(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection());
    PDFComponentPainter::paintContinueWith(&painter, QRectF(rect()), getContent(), state);
}

void PDFContinueWith::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutChildren();
}

void PDFContinueWith::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LanguageChange:
            retranslate();
            break;

        case QEvent::FontChange:
        case QEvent::StyleChange:
            updateGeometry();
            layoutChildren();
            update();
            break;

        case QEvent::LayoutDirectionChange:
            layoutChildren();
            update();
            break;

        case QEvent::EnabledChange:
            update();
            break;

        default:
            break;
    }
}

bool PDFContinueWith::eventFilter(QObject* watched, QEvent* event)
{
    PDFToolChoice* choice = qobject_cast<PDFToolChoice*>(watched);
    const int index = choice ? static_cast<int>(m_choiceWidgets.indexOf(choice)) : -1;

    if (index >= 0 && index < m_choices.size())
    {
        if (event->type() == QEvent::FocusIn)
        {
            // QAbstractButton makes the focused button of the group the stop of Tab
            m_tabStop = choice;
        }
        else if (event->type() == QEvent::KeyPress)
        {
            const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
            const int key = keyEvent->key();
            const bool isNavigation = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Home || key == Qt::Key_End;

            if (isNavigation && (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier)
            {
                // The arrows move the focus, they do not select (nothing is preselected)
                const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
                int step = 0;
                int target = index;
                switch (key)
                {
                    case Qt::Key_Left:
                        step = isRightToLeft ? 1 : -1;
                        break;
                    case Qt::Key_Right:
                        step = isRightToLeft ? -1 : 1;
                        break;
                    case Qt::Key_Up:
                        step = -1;
                        break;
                    case Qt::Key_Down:
                        step = 1;
                        break;
                    case Qt::Key_Home:
                        target = -1;
                        step = 1;
                        break;
                    case Qt::Key_End:
                        target = static_cast<int>(m_choices.size());
                        step = -1;
                        break;
                    default:
                        break;
                }

                for (target += step; target >= 0 && target < m_choices.size(); target += step)
                {
                    PDFToolChoice* targetChoice = getChoiceWidget(target);
                    if (targetChoice && targetChoice->isEnabled() && targetChoice->isVisible())
                    {
                        targetChoice->setFocus(step < 0 ? Qt::BacktabFocusReason : Qt::TabFocusReason);
                        break;
                    }
                }
                return true;
            }
        }
    }

    return BaseClass::eventFilter(watched, event);
}

// ------------------------------------------------------------- Color palette

/// Helper of the palette of the colors
class PDFColorPaletteHelper
{
public:
    PDFColorPaletteHelper() = delete;

    /// Gap between the anchor and the panel of the popover
    static constexpr int POPUP_GAP = 4;

    /// Returns true, if the colors are the same (two invalid colors are the same: no color)
    static bool isSameColor(const QColor& first, const QColor& second)
    {
        if (!first.isValid() || !second.isValid())
        {
            return first.isValid() == second.isValid();
        }
        return first.rgba64() == second.rgba64();
    }

    /// Returns the name of a color, which has none
    static QString getDefaultName(const QColor& color)
    {
        if (!color.isValid())
        {
            return PDFColorPalette::tr("No color");
        }
        return color.alpha() == 255 ? color.name(QColor::HexRgb).toUpper() : color.name(QColor::HexArgb).toUpper();
    }

    /// Returns true, if an entry has the same color
    static bool containsColor(const QList<PDFColorPalette::Entry>& entries, const QColor& color)
    {
        for (const PDFColorPalette::Entry& entry : entries)
        {
            if (isSameColor(entry.color, color))
            {
                return true;
            }
        }
        return false;
    }

    /// Removes the entries of the same color
    static void removeColor(QList<PDFColorPalette::Entry>& entries, const QColor& color)
    {
        for (qsizetype i = entries.size() - 1; i >= 0; --i)
        {
            if (isSameColor(entries.at(i).color, color))
            {
                entries.removeAt(i);
            }
        }
    }
};

PDFColorPalette::PDFColorPalette(QWidget* parent, Qt::WindowFlags flags) :
    BaseClass(parent, flags)
{
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_group.setExclusive(true);

    if (isPopup())
    {
        // Popover of the style (translucent window with the shadow); the palette
        // places its panel by itself
        setProperty(PDFWidgetStyleProperties::POPOVER, true);
        setProperty(PDFWidgetStyleProperties::POPUP_PLACED, true);
    }

    m_more = new PDFTextLink(this);
    connect(m_more.data(), &QAbstractButton::clicked, this, &PDFColorPalette::onMoreClicked);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));

    retranslate();
    rebuild();
}

void PDFColorPalette::onMoreClicked()
{
    // The popover closes first (a filter of the hide may delete it), the signal is the last
    QPointer<PDFColorPalette> guard(this);
    if (isPopup())
    {
        close();
    }
    if (guard)
    {
        Q_EMIT moreColorsRequested();
    }
}

PDFColorPalette::~PDFColorPalette()
{
    watchAnchor(false);

    // An open popover closes, while the palette is still whole: the destructor
    // of QWidget would close it (the popup, the focus returning to the anchor)
    // with the colors and the swatches of the palette already deleted
    if (isPopup() && isVisible())
    {
        hide();
    }
}

void PDFColorPalette::retranslate()
{
    if (m_more)
    {
        m_more->setText(tr("More colors…"));
    }
    setAccessibleName(tr("Colors"));

    // Names of the colors without a name of their own
    rebuild();
}

bool PDFColorPalette::isPopup() const
{
    return windowType() == Qt::Popup;
}

QMargins PDFColorPalette::getPanelMargins() const
{
    // A window of the style has the margins of the shadow (empty, when the
    // windows cannot be translucent); a panel inside of a widget has none
    if (!isWindow() || !PDFWidgetStyle::isActive())
    {
        return QMargins();
    }

    return PDFWidgetStyleImpl::getPopupMargins(this);
}

void PDFColorPalette::setSuggestedColors(const QList<Entry>& colors)
{
    const QList<Entry> limited = colors.mid(0, MAX_SUGGESTED_COLORS);
    if (m_suggested != limited)
    {
        m_suggested = limited;
        if (rebuild())
        {
            Q_EMIT colorsChanged();
        }
    }
}

void PDFColorPalette::setRecentColors(const QList<Entry>& colors)
{
    // Without duplicates, the first occurrence is kept
    QList<Entry> limited;
    for (const Entry& entry : colors)
    {
        if (!PDFColorPaletteHelper::containsColor(limited, entry.color) && limited.size() < MAX_RECENT_COLORS)
        {
            limited << entry;
        }
    }

    if (m_recent != limited)
    {
        m_recent = limited;
        if (rebuild())
        {
            Q_EMIT colorsChanged();
        }
    }
}

void PDFColorPalette::addRecentColor(const Entry& entry)
{
    QList<Entry> colors = m_recent;
    PDFColorPaletteHelper::removeColor(colors, entry.color);
    colors.prepend(entry);
    setRecentColors(colors);
}

void PDFColorPalette::setCurrentColor(const QColor& color)
{
    if (!PDFColorPaletteHelper::isSameColor(m_currentColor, color))
    {
        m_currentColor = color;
        if (updateSelection())
        {
            update();
        }
    }
}

QString PDFColorPalette::getColorName(const QColor& color) const
{
    for (const QList<Entry>* entries : { &m_suggested, &m_recent })
    {
        for (const Entry& entry : *entries)
        {
            if (!entry.name.isEmpty() && PDFColorPaletteHelper::isSameColor(entry.color, color))
            {
                return entry.name;
            }
        }
    }

    return PDFColorPaletteHelper::getDefaultName(color);
}

PDFColorSwatch* PDFColorPalette::getSuggestedSwatch(int index) const
{
    return (index >= 0 && index < m_suggested.size() && index < m_suggestedSwatches.size()) ? m_suggestedSwatches.at(index).data() : nullptr;
}

PDFColorSwatch* PDFColorPalette::getRecentSwatch(int index) const
{
    return (index >= 0 && index < m_recent.size() && index < m_recentSwatches.size()) ? m_recentSwatches.at(index).data() : nullptr;
}

void PDFColorPalette::fillSwatches(const QList<Entry>& entries, QList<QPointer<PDFColorSwatch>>& swatches, bool isSuggested)
{
    // The swatches are reused (a swatch is never deleted, while its signal
    // clicked is handled); the swatches, which are not needed, are hidden
    swatches.removeAll(QPointer<PDFColorSwatch>());

    while (swatches.size() < entries.size())
    {
        PDFColorSwatch* swatch = new PDFColorSwatch(this);
        swatch->installEventFilter(this);
        swatch->setSelectionBehavior(isSuggested ? PDFComponentButton::SelectionBehavior::Exclusive : PDFComponentButton::SelectionBehavior::None);
        if (isSuggested)
        {
            m_group.addButton(swatch);
        }

        connect(swatch, &QAbstractButton::clicked, this, &PDFColorPalette::onSwatchButtonClicked);
        swatches << swatch;
    }

    for (qsizetype i = 0; i < swatches.size(); ++i)
    {
        PDFColorSwatch* swatch = swatches.at(i);
        if (i < entries.size())
        {
            const Entry& entry = entries.at(i);
            swatch->setColor(entry.color);
            swatch->setKind(entry.color.isValid() ? PDFColorSwatch::Kind::Color : PDFColorSwatch::Kind::None);
            swatch->setColorName(entry.name.isEmpty() ? PDFColorPaletteHelper::getDefaultName(entry.color) : entry.name);
            swatch->setVisible(true);
        }
        else
        {
            swatch->setVisible(false);
        }
    }
}

bool PDFColorPalette::rebuild()
{
    fillSwatches(m_suggested, m_suggestedSwatches, true);
    fillSwatches(m_recent, m_recentSwatches, false);

    if (!updateSelection())
    {
        return false;
    }
    updateTabStops(qobject_cast<PDFColorSwatch*>(focusWidget()));
    updateGeometry();

    if (isWindow())
    {
        // Not the virtual sizeHint: rebuild is called also from the constructor
        const QSize size = getPaletteSize();
        if (size != this->size())
        {
            resize(size);
        }
    }

    layoutChildren();
    update();
    return true;
}

bool PDFColorPalette::updateSelection()
{
    // The current color is the selected suggested swatch (the first one of the same color)
    int selected = -1;
    for (int i = 0; i < m_suggested.size(); ++i)
    {
        if (PDFColorPaletteHelper::isSameColor(m_suggested.at(i).color, m_currentColor))
        {
            selected = i;
            break;
        }
    }

    // A swatch emits toggled, whose slot of the application may delete the palette
    QPointer<PDFColorPalette> guard(this);
    for (int i = 0; i < m_suggestedSwatches.size(); ++i)
    {
        if (PDFColorSwatch* swatch = m_suggestedSwatches.at(i))
        {
            swatch->setSelected(i == selected);
            if (!guard)
            {
                return false;
            }
        }
    }
    return true;
}

QList<PDFColorSwatch*> PDFColorPalette::getVisibleSwatches(bool recent) const
{
    QList<PDFColorSwatch*> swatches;
    const QList<QPointer<PDFColorSwatch>>& list = recent ? m_recentSwatches : m_suggestedSwatches;
    const qsizetype count = recent ? m_recent.size() : m_suggested.size();
    for (qsizetype i = 0; i < list.size() && i < count; ++i)
    {
        if (list.at(i))
        {
            swatches << list.at(i).data();
        }
    }
    return swatches;
}

void PDFColorPalette::updateTabStops(PDFColorSwatch* focused)
{
    // Each grid is one stop of Tab: the focused swatch, the selected one, or the first one
    for (const bool recent : { false, true })
    {
        const QList<PDFColorSwatch*> swatches = getVisibleSwatches(recent);
        PDFColorSwatch* stop = swatches.contains(focused) ? focused : nullptr;
        for (PDFColorSwatch* swatch : swatches)
        {
            if (!stop && swatch->isSelected())
            {
                stop = swatch;
            }
        }
        if (!stop && !swatches.isEmpty())
        {
            stop = swatches.front();
        }

        for (PDFColorSwatch* swatch : swatches)
        {
            swatch->setFocusPolicy(swatch == stop ? Qt::StrongFocus : Qt::ClickFocus);
        }
    }
}

bool PDFColorPalette::moveFocus(PDFColorSwatch* swatch, int key)
{
    const QList<PDFColorSwatch*> suggested = getVisibleSwatches(false);
    const QList<PDFColorSwatch*> recent = getVisibleSwatches(true);
    const bool isRecent = recent.contains(swatch);
    const QList<PDFColorSwatch*>& grid = isRecent ? recent : suggested;
    const int index = static_cast<int>(grid.indexOf(swatch));
    const int count = static_cast<int>(grid.size());
    const int columns = PDFComponentPainter::getColorPaletteColumns();

    if (index < 0)
    {
        return false;
    }

    // The arrows go by the picture (mirrored in the right-to-left layout)
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    if (key == Qt::Key_Left || key == Qt::Key_Right)
    {
        key = ((key == Qt::Key_Right) != isRightToLeft) ? Qt::Key_Right : Qt::Key_Left;
    }

    PDFColorSwatch* target = nullptr;
    switch (key)
    {
        case Qt::Key_Right:
            target = index + 1 < count ? grid.at(index + 1) : nullptr;
            break;

        case Qt::Key_Left:
            target = index > 0 ? grid.at(index - 1) : nullptr;
            break;

        case Qt::Key_Down:
            if (index + columns < count)
            {
                target = grid.at(index + columns);
            }
            else if (!isRecent && !recent.isEmpty() && index / columns == (count - 1) / columns)
            {
                // From the last row of the suggested colors to the recent ones
                target = recent.at(qMin(index % columns, static_cast<int>(recent.size()) - 1));
            }
            break;

        case Qt::Key_Up:
            if (index - columns >= 0)
            {
                target = grid.at(index - columns);
            }
            else if (isRecent && !suggested.isEmpty())
            {
                // From the first row of the recent colors to the last row of the suggested ones
                const int lastRow = (static_cast<int>(suggested.size()) - 1) / columns;
                target = suggested.at(qMin(lastRow * columns + index % columns, static_cast<int>(suggested.size()) - 1));
            }
            break;

        case Qt::Key_Home:
            target = grid.front();
            break;

        case Qt::Key_End:
            target = grid.back();
            break;

        default:
            return false;
    }

    if (target && target != swatch)
    {
        target->setFocus(Qt::TabFocusReason);
    }
    return true;
}

void PDFColorPalette::onSwatchClicked(PDFColorSwatch* swatch)
{
    if (!swatch)
    {
        return;
    }

    Entry entry;
    const int suggestedIndex = static_cast<int>(m_suggestedSwatches.indexOf(swatch));
    const int recentIndex = static_cast<int>(m_recentSwatches.indexOf(swatch));
    if (suggestedIndex >= 0 && suggestedIndex < m_suggested.size())
    {
        entry = m_suggested.at(suggestedIndex);
    }
    else if (recentIndex >= 0 && recentIndex < m_recent.size())
    {
        entry = m_recent.at(recentIndex);
    }
    else
    {
        return;
    }

    // The state is changed first, the signal is the last (its slot may delete the palette)
    m_currentColor = entry.color;
    if (entry.name.isEmpty())
    {
        entry.name = getColorName(entry.color);
    }
    QPointer<PDFColorPalette> guard(this);
    addRecentColor(entry);
    if (!guard || !updateSelection())
    {
        return;
    }

    if (isPopup())
    {
        close();
    }
    if (guard)
    {
        Q_EMIT colorSelected(entry.color);
    }
}

void PDFColorPalette::onSwatchButtonClicked()
{
    // The clicked swatch is the sender (the swatches are created and reused by the palette)
    onSwatchClicked(qobject_cast<PDFColorSwatch*>(sender()));
}

void PDFColorPalette::onAnchorDestroyed()
{
    // An anchor deleted by the application closes the popover as well (nothing of the anchor is used)
    if (isVisible())
    {
        close();
    }
}

void PDFColorPalette::onHiddenQueued(quint64 openCount)
{
    // The palette opened again after the hide drops the signal of that hide
    if (m_openCount == openCount && !isVisible())
    {
        Q_EMIT closed();
    }
}

void PDFColorPalette::watchAnchor(bool watch)
{
    for (QWidget* widget : { m_anchor.data(), m_anchorWindow.data() })
    {
        if (widget)
        {
            widget->removeEventFilter(this);
        }
    }

    disconnect(m_anchorDestroyed);
    m_anchorDestroyed = QMetaObject::Connection();

    if (!watch)
    {
        m_anchor = nullptr;
        m_anchorWindow = nullptr;
        return;
    }

    for (QWidget* widget : { m_anchor.data(), m_anchorWindow.data() })
    {
        if (widget)
        {
            widget->installEventFilter(this);
        }
    }

    // An anchor deleted by the application closes the popover as well
    if (m_anchor)
    {
        m_anchorDestroyed = connect(m_anchor.data(), &QObject::destroyed, this, &PDFColorPalette::onAnchorDestroyed);
    }
}

void PDFColorPalette::popup(QWidget* anchor)
{
    if (!isPopup() || !anchor || !anchor->isVisible() || anchor->window() == this)
    {
        return;
    }

    watchAnchor(false);
    m_anchor = anchor;
    m_anchorWindow = anchor->window();
    watchAnchor(true);

    // A pending signal closed of the previous opening is dropped (the palette is open again)
    m_isOpen = true;
    ++m_openCount;

    // The window of the popup takes the direction of its anchor
    setLayoutDirection(anchor->layoutDirection());
    setAttribute(Qt::WA_NoMouseReplay, false);
    ensurePolished();

    const QMargins margins = getPanelMargins();
    const QSize panelSize = PDFComponentPainter::getColorPaletteSize(getContent());
    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;

    // Below the anchor, aligned with its leading edge; above it, when there is no room below
    QRect panel(QPoint(isRightToLeft ? anchorRect.right() + 1 - panelSize.width() : anchorRect.left(), anchorRect.bottom() + 1 + PDFColorPaletteHelper::POPUP_GAP), panelSize);

    // The screen, on which the anchor lies (a window may span more screens)
    const QScreen* screen = QGuiApplication::screenAt(anchorRect.center());
    if (!screen)
    {
        screen = anchor->screen();
    }
    if (screen)
    {
        const QRect available = screen->availableGeometry();
        if (panel.bottom() > available.bottom())
        {
            const QRect above(QPoint(panel.left(), anchorRect.top() - PDFColorPaletteHelper::POPUP_GAP - panelSize.height()), panelSize);
            if (above.top() >= available.top() || anchorRect.top() - available.top() > available.bottom() - anchorRect.bottom())
            {
                panel = above;
            }
        }

        panel.moveLeft(qBound(available.left(), panel.left(), qMax(available.left(), available.right() + 1 - panel.width())));
        panel.moveTop(qBound(available.top(), panel.top(), qMax(available.top(), available.bottom() + 1 - panel.height())));
    }

    resize(panelSize.grownBy(margins));
    move(panel.topLeft() - QPoint(margins.left(), margins.top()));
    layoutChildren();
    QPointer<PDFColorPalette> guard(this);
    show();
    if (!guard)
    {
        // A filter of the events of the application has deleted the palette
        return;
    }

    // The focus goes to the selected color, or to the first one
    QWidget* target = nullptr;
    for (PDFColorSwatch* swatch : getVisibleSwatches(false))
    {
        if (!target || swatch->isSelected())
        {
            target = swatch;
            if (swatch->isSelected())
            {
                break;
            }
        }
    }
    if (!target)
    {
        const QList<PDFColorSwatch*> recentSwatches = getVisibleSwatches(true);
        if (!recentSwatches.isEmpty())
        {
            target = recentSwatches.front();
        }
    }
    if (!target)
    {
        target = m_more.data();
    }
    if (target)
    {
        target->setFocus(Qt::PopupFocusReason);
        updateTabStops(qobject_cast<PDFColorSwatch*>(target));
    }
}

PDFComponentPainter::ColorPalette PDFColorPalette::getContent() const
{
    PDFComponentPainter::ColorPalette content;
    for (const Entry& entry : m_suggested)
    {
        content.suggested << entry.color;
    }
    for (const Entry& entry : m_recent)
    {
        content.recent << entry.color;
    }
    for (int i = 0; i < m_suggested.size(); ++i)
    {
        if (PDFColorPaletteHelper::isSameColor(m_suggested.at(i).color, m_currentColor))
        {
            content.selected = i;
            break;
        }
    }
    content.suggestedLabel = tr("Suggested");
    content.recentLabel = tr("Recently used");
    content.moreText = m_more ? m_more->text() : QString();
    content.paintNested = false;
    return content;
}

QSize PDFColorPalette::sizeHint() const
{
    return getPaletteSize();
}

QSize PDFColorPalette::getPaletteSize() const
{
    return PDFComponentPainter::getColorPaletteSize(getContent()).grownBy(getPanelMargins());
}

QSize PDFColorPalette::minimumSizeHint() const
{
    return sizeHint();
}

void PDFColorPalette::layoutChildren()
{
    const QMargins margins = getPanelMargins();
    const QRect panel = rect().marginsRemoved(margins);
    const PDFComponentPainter::ColorPaletteLayout layout = PDFComponentPainter::getColorPaletteLayout(getContent());

    // The layout is in the coordinates of the panel
    const Qt::LayoutDirection direction = layoutDirection();
    for (int i = 0; i < layout.suggested.size(); ++i)
    {
        PDFComponentPanelsHelper::placeWidget(getSuggestedSwatch(i), direction, panel, layout.suggested.at(i).translated(panel.topLeft()));
    }
    for (int i = 0; i < layout.recent.size(); ++i)
    {
        PDFComponentPanelsHelper::placeWidget(getRecentSwatch(i), direction, panel, layout.recent.at(i).translated(panel.topLeft()));
    }
    PDFComponentPanelsHelper::placeWidget(m_more.data(), direction, panel, layout.more.translated(panel.topLeft()));
}

void PDFColorPalette::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QMargins margins = getPanelMargins();
    const QRectF panel = QRectF(rect()).marginsRemoved(QMarginsF(margins));
    PDFComponentPainter::ColorPalette content = getContent();
    if (isPopup() && PDFWidgetStyle::isActive())
    {
        // The panel of a popover of the style: the shadow 0 / 8 / 24 of a
        // translucent window, a square panel of an opaque window (its corners
        // can not be transparent)
        PDFWidgetStyleImpl::drawPopupPanel(&painter, rect(), this, PDFWidgetStyleImpl::PopupKind::Popover, PDFComponentPainter::getColorPaletteRadius(), PDFThemeColors::BorderDefault);
        content.panelVisible = false;
    }

    const PDFComponentPainter::State state(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection());
    PDFComponentPainter::paintColorPalette(&painter, panel, content, state);
}

void PDFColorPalette::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LanguageChange:
            retranslate();
            break;

        case QEvent::LayoutDirectionChange:
        case QEvent::StyleChange:
            layoutChildren();
            update();
            break;

        default:
            break;
    }
}

void PDFColorPalette::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && isPopup())
    {
        close();
        event->accept();
        return;
    }

    BaseClass::keyPressEvent(event);
}

void PDFColorPalette::mousePressEvent(QMouseEvent* event)
{
    // A click on the anchor closes the popover and it does not open it again
    // (the press is not replayed to the anchor, as QMenu does for its button)
    if (isPopup() && m_anchor && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(event->globalPosition().toPoint()))
    {
        setAttribute(Qt::WA_NoMouseReplay, true);
    }

    BaseClass::mousePressEvent(event);
}

void PDFColorPalette::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutChildren();
}

void PDFColorPalette::hideEvent(QHideEvent* event)
{
    BaseClass::hideEvent(event);

    if (isPopup() && m_isOpen)
    {
        // Any hide closes the opening (also a hide by the system); the signal is
        // queued, so its slot may delete the palette (Qt is still hiding it now).
        // A palette opened again before it is delivered drops it.
        m_isOpen = false;
        watchAnchor(false);
        QMetaObject::invokeMethod(this, &PDFColorPalette::onHiddenQueued, Qt::QueuedConnection, quint64(m_openCount));
    }
}

bool PDFColorPalette::eventFilter(QObject* watched, QEvent* event)
{
    // The owner of the popover is hidden or closed: the popover closes
    if ((watched == m_anchor || watched == m_anchorWindow) && isPopup())
    {
        switch (event->type())
        {
            case QEvent::Hide:
            case QEvent::Close:
            case QEvent::EnabledChange:
                if (isVisible() && (event->type() != QEvent::EnabledChange || (m_anchor && !m_anchor->isEnabled())))
                {
                    close();
                }
                break;

            default:
                break;
        }
        return BaseClass::eventFilter(watched, event);
    }

    PDFColorSwatch* swatch = qobject_cast<PDFColorSwatch*>(watched);
    if (swatch && swatch->parentWidget() == this)
    {
        if (event->type() == QEvent::FocusIn)
        {
            updateTabStops(swatch);
        }
        else if (event->type() == QEvent::KeyPress)
        {
            const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
            const int key = keyEvent->key();
            const bool isNavigation = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Home || key == Qt::Key_End;
            if (isNavigation && (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && moveFocus(swatch, key))
            {
                return true;
            }
        }
    }

    return BaseClass::eventFilter(watched, event);
}

// -------------------------------------------------------------- Color button

PDFColorButton::PDFColorButton(QWidget* parent) :
    BaseClass(FocusRing::Inside, parent)
{
    init();
}

PDFColorButton::PDFColorButton(const QColor& color, const QString& colorName, QWidget* parent) :
    BaseClass(FocusRing::Inside, parent),
    m_color(color),
    m_colorName(colorName)
{
    init();
}

PDFColorButton::~PDFColorButton()
{
    // The palette is deleted now, while the button is still whole (QWidget
    // would delete it later, when only the QWidget of the button is left);
    // its signals do not reach the button any more
    if (PDFColorPalette* palette = m_palette.data())
    {
        palette->disconnect(this);
        m_palette = nullptr;
        if (palette->parent() == this)
        {
            delete palette;
        }
    }
}

void PDFColorButton::init()
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // The result of showPalette is not needed by a click
    connect(this, &QAbstractButton::clicked, this, &PDFColorButton::showPalette);
    updateText();
}

void PDFColorButton::setColor(const QColor& color)
{
    if (PDFColorPaletteHelper::isSameColor(m_color, color))
    {
        return;
    }

    // The explicit name belongs to the previous color
    m_color = color;
    m_colorName.clear();
    updateText();
    update();
    Q_EMIT colorChanged(color);
}

void PDFColorButton::setColorName(const QString& colorName)
{
    if (m_colorName != colorName)
    {
        m_colorName = colorName;
        updateText();
        update();
    }
}

QString PDFColorButton::getShownName() const
{
    if (!m_colorName.isEmpty())
    {
        return m_colorName;
    }

    return m_palette ? m_palette->getColorName(m_color) : PDFColorPaletteHelper::getDefaultName(m_color);
}

void PDFColorButton::updateText()
{
    // The text of the button is the name of the color (its default accessible name)
    setText(getShownName());
}

PDFColorPalette* PDFColorButton::getPalette()
{
    if (!m_palette)
    {
        m_palette = new PDFColorPalette(this, Qt::Popup);
        connect(m_palette.data(), &PDFColorPalette::colorSelected, this, &PDFColorButton::onPaletteColorSelected);
        connect(m_palette.data(), &PDFColorPalette::moreColorsRequested, this, &PDFColorButton::moreColorsRequested);
        connect(m_palette.data(), &PDFColorPalette::closed, this, &PDFColorButton::onPaletteClosed);
        connect(m_palette.data(), &PDFColorPalette::colorsChanged, this, &PDFColorButton::onPaletteColorsChanged);
        connect(m_palette.data(), &QObject::destroyed, this, &PDFColorButton::onPaletteDestroyed);
    }

    return m_palette.data();
}

void PDFColorButton::onPaletteColorSelected(const QColor& color)
{
    // The name of the picked color comes from the palette (getShownName
    // asks it), an explicit name of the previous color is dropped; the
    // color is copied (the reference belongs to the emitter, a slot of
    // colorChanged may delete the palette)
    const QColor selectedColor = color;
    QPointer<PDFColorButton> guard(this);
    m_colorName.clear();
    if (PDFColorPaletteHelper::isSameColor(m_color, selectedColor))
    {
        updateText();
        update();
    }
    else
    {
        setColor(selectedColor);
    }
    if (guard)
    {
        Q_EMIT colorSelected(selectedColor);
    }
}

void PDFColorButton::onPaletteColorsChanged()
{
    // The shown name may come from the palette
    updateText();
    update();
}

bool PDFColorButton::showPalette()
{
    if (!isEnabled() || !isVisible())
    {
        return false;
    }

    QPointer<PDFColorPalette> palette = getPalette();
    if (!palette)
    {
        return false;
    }

    // The palette selects its swatch (toggled) and shows itself: a slot or a
    // filter of the application may delete the palette or this button
    QPointer<PDFColorButton> guard(this);
    palette->setCurrentColor(m_color);
    if (!guard || !palette)
    {
        return false;
    }
    palette->popup(this);
    if (!guard || !palette)
    {
        return false;
    }
    update();
    notifyExpanded();
    return palette->isVisible();
}

void PDFColorButton::notifyExpanded()
{
#if QT_CONFIG(accessibility)
    QAccessible::State changed;
    changed.expanded = true;
    changed.collapsed = true;
    QAccessibleStateChangeEvent event(this, changed);
    QAccessible::updateAccessibility(&event);
#endif
}

void PDFColorButton::hidePalette()
{
    if (m_palette && m_palette->isVisible())
    {
        m_palette->close();
    }
}

bool PDFColorButton::isPaletteVisible() const
{
    return m_palette && m_palette->isVisible();
}

void PDFColorButton::onPaletteDestroyed()
{
    // QWidget emits destroyed in its destructor, after the destructor of the
    // palette has deleted its colors, but before QObject clears the pointers
    // to it: the palette is dropped first, nothing of it is asked any more
    // (the name of the color falls back to the default name)
    m_palette = nullptr;
    updateText();
    update();
}

void PDFColorButton::onPaletteClosed()
{
    // The focus returns to the button (a click outside gives it to the clicked widget after this)
    if (isVisible() && isEnabled() && window()->isActiveWindow())
    {
        setFocus(Qt::PopupFocusReason);
    }
    update();
    notifyExpanded();
}

PDFComponentPainter::ColorButton PDFColorButton::getContent() const
{
    PDFComponentPainter::ColorButton content;
    content.color = m_color;
    content.name = getShownName();
    content.open = isPaletteVisible();
    return content;
}

void PDFColorButton::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintColorButton(painter, QRectF(rect), getContent(), state);
}

QSize PDFColorButton::getContentSizeHint() const
{
    return PDFComponentPainter::getColorButtonSize(getContent());
}

QSize PDFColorButton::getContentMinimumSizeHint() const
{
    return PDFComponentPainter::getColorButtonMinimumSize(getContent());
}

QString PDFColorButton::getDefaultAccessibleName() const
{
    return getShownName();
}

QString PDFColorButton::getDefaultAccessibleDescription() const
{
    // A name of the application ("Fill color") leaves the color for the description
    const QString opens = tr("Opens the palette of colors");
    return accessibleName().isEmpty() ? opens : joinTexts({ getShownName(), opens }, getListPattern());
}

void PDFColorButton::keyPressEvent(QKeyEvent* event)
{
    // The keys of a combo box open the palette
    const bool isAltArrow = (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) && (event->modifiers() & Qt::AltModifier);
    const bool isF4 = event->key() == Qt::Key_F4 && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
    if (isAltArrow || isF4)
    {
        showPalette();
        event->accept();
        return;
    }

    BaseClass::keyPressEvent(event);
}

void PDFColorButton::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::EnabledChange && !isEnabled())
    {
        hidePalette();
    }
    else if (event->type() == QEvent::LanguageChange)
    {
        updateText();
    }
}

void PDFColorButton::hideEvent(QHideEvent* event)
{
    BaseClass::hideEvent(event);
    hidePalette();
}

// ------------------------------------------------------------ Signature card

/// Helper of the signature card
class PDFSignatureCardHelper
{
public:
    PDFSignatureCardHelper() = delete;

    static bool isSameDetail(const PDFSignatureCard::Detail& left, const PDFSignatureCard::Detail& right)
    {
        return left.key == right.key && left.value == right.value;
    }

    static bool isSameCertificate(const PDFSignatureCard::Certificate& left, const PDFSignatureCard::Certificate& right)
    {
        return left.name == right.name && left.info == right.info && left.status == right.status && left.linkText == right.linkText;
    }
};

PDFSignatureCard::PDFSignatureCard(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setFocusPolicy(Qt::NoFocus);

    m_toggle = new PDFIconButton(QStringLiteral("chevron-down"), PDFIconButton::Size::Small, this);
    connect(m_toggle.data(), &QAbstractButton::clicked, this, &PDFSignatureCard::onToggleClicked);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFSignatureCard::onThemeChanged);

    m_card.paintNested = false;
    retranslate();
}

PDFSignatureCard::~PDFSignatureCard() = default;

void PDFSignatureCard::onToggleClicked()
{
    setExpanded(!m_card.expanded);
}

void PDFSignatureCard::onThemeChanged()
{
    updateGeometry();
    layoutChildren();
    update();
}

void PDFSignatureCard::onCertificateLinkClicked()
{
    // The link of a certificate is the sender; a link keeps the index of its
    // certificate (the links are reused, never moved)
    const PDFTextLink* link = qobject_cast<const PDFTextLink*>(sender());
    for (qsizetype i = 0; link && i < m_links.size(); ++i)
    {
        if (m_links.at(i).data() == link)
        {
            Q_EMIT certificateLinkActivated(static_cast<int>(i));
            return;
        }
    }
}

void PDFSignatureCard::retranslate()
{
    if (m_isCaptionDefault)
    {
        m_card.chainCaption = tr("Certificate chain");
    }
    changeContent();
}

void PDFSignatureCard::setSigner(const QString& signer)
{
    if (m_card.signer != signer)
    {
        m_card.signer = signer;
        changeContent();
    }
}

void PDFSignatureCard::setMeta(const QString& meta)
{
    if (m_card.meta != meta)
    {
        m_card.meta = meta;
        changeContent();
    }
}

void PDFSignatureCard::setSummary(const QString& summary)
{
    if (m_card.summary != summary)
    {
        m_card.summary = summary;
        changeContent();
    }
}

void PDFSignatureCard::setStatus(Status status)
{
    status = PDFComponentPainter::normalized(status);
    if (m_card.status != status)
    {
        m_card.status = status;
        changeContent();
    }
}

void PDFSignatureCard::setExpanded(bool expanded)
{
    if (m_card.expanded != expanded)
    {
        m_card.expanded = expanded;
        changeContent();
        Q_EMIT expandedChanged(expanded);
    }
}

void PDFSignatureCard::setDetails(const QList<Detail>& details)
{
    if (details.size() != m_card.details.size() || !std::equal(details.cbegin(), details.cend(), m_card.details.cbegin(), &PDFSignatureCardHelper::isSameDetail))
    {
        m_card.details = details;
        changeContent();
    }
}

void PDFSignatureCard::setCertificates(const QList<Certificate>& certificates)
{
    QList<Certificate> normalizedCertificates = certificates;
    for (Certificate& certificate : normalizedCertificates)
    {
        certificate.status = PDFComponentPainter::normalized(certificate.status);
    }

    if (normalizedCertificates.size() != m_card.certificates.size() || !std::equal(normalizedCertificates.cbegin(), normalizedCertificates.cend(), m_card.certificates.cbegin(), &PDFSignatureCardHelper::isSameCertificate))
    {
        m_card.certificates = std::move(normalizedCertificates);
        changeContent();
    }
}

void PDFSignatureCard::setChainCaption(const QString& caption)
{
    m_isCaptionDefault = false;
    if (m_card.chainCaption != caption)
    {
        m_card.chainCaption = caption;
        changeContent();
    }
}

PDFTextLink* PDFSignatureCard::getCertificateLink(int index) const
{
    if (index < 0 || index >= m_card.certificates.size() || index >= m_links.size() || m_card.certificates.at(index).linkText.isEmpty())
    {
        return nullptr;
    }
    return m_links.at(index).data();
}

bool PDFSignatureCard::isExpandable() const
{
    return !m_card.details.isEmpty() || !m_card.certificates.isEmpty();
}

void PDFSignatureCard::changeContent()
{
    updateToggle();
    updateLinks();
    updateDescription();
    updateGeometry();
    layoutChildren();
    update();
}

void PDFSignatureCard::updateToggle()
{
    if (!m_toggle)
    {
        return;
    }

    // A card without details has nothing to expand; the name of the toggle
    // says the signer (a list of signatures has a toggle in each card)
    const QString text = m_card.expanded ? tr("Hide details") : tr("Show details");
    QString name = text;
    if (!m_card.signer.isEmpty())
    {
        name = m_card.expanded ? tr("Hide details of %1").arg(m_card.signer) : tr("Show details of %1").arg(m_card.signer);
    }
    m_toggle->setIconName(m_card.expanded ? QStringLiteral("chevron-up") : QStringLiteral("chevron-down"));
    m_toggle->setAccessibleName(name);
    m_toggle->setToolTip(text);
    m_toggle->setVisible(isExpandable() || m_card.expanded);
}

void PDFSignatureCard::updateLinks()
{
    // A link of each certificate is reused (a link is not deleted, while its signal is handled)
    // (the slots are kept by the index of the certificate)
    while (m_links.size() < m_card.certificates.size())
    {
        m_links << QPointer<PDFTextLink>();
    }

    for (qsizetype i = 0; i < m_card.certificates.size(); ++i)
    {
        const Certificate& certificate = m_card.certificates.at(i);
        if (!certificate.linkText.isEmpty() && !m_links.at(i))
        {
            PDFTextLink* link = new PDFTextLink(this);
            connect(link, &QAbstractButton::clicked, this, &PDFSignatureCard::onCertificateLinkClicked);
            m_links[i] = link;
        }

        if (PDFTextLink* link = m_links.at(i))
        {
            link->setText(certificate.linkText);
            link->setVisible(m_card.expanded && !certificate.linkText.isEmpty());
        }
    }

    for (qsizetype i = m_card.certificates.size(); i < m_links.size(); ++i)
    {
        if (m_links.at(i))
        {
            m_links.at(i)->setVisible(false);
        }
    }
}

void PDFSignatureCard::updateDescription()
{
    // The texts are drawn, assistive technologies read them as the name and the description
    QStringList lines;
    lines << m_card.meta << PDFComponentPainter::getSignatureStatusText(m_card.status) << m_card.summary;

    if (m_card.expanded)
    {
        for (const Detail& detail : std::as_const(m_card.details))
        {
            lines << tr("%1: %2").arg(detail.key, detail.value);
        }
        if (!m_card.certificates.isEmpty())
        {
            lines << m_card.chainCaption;
        }
        for (const Certificate& certificate : std::as_const(m_card.certificates))
        {
            lines << tr("%1, %2").arg(certificate.name, certificate.info);
        }
    }

    lines.removeAll(QString());
    PDFComponentPanelsHelper::replaceAccessibleTexts(this, m_accessibleName, m_card.signer, m_accessibleDescription, lines.join(QLatin1Char('\n')));
}

PDFComponentPainter::SignatureCard PDFSignatureCard::getContent() const
{
    PDFComponentPainter::SignatureCard content = m_card;
    content.paintNested = false;
    content.toggleVisible = m_toggle && !m_toggle->isHidden();
    return content;
}

QSize PDFSignatureCard::sizeHint() const
{
    return PDFComponentPainter::getSignatureCardSize(getContent());
}

QSize PDFSignatureCard::minimumSizeHint() const
{
    return PDFComponentPainter::getSignatureCardMinimumSize(getContent());
}

int PDFSignatureCard::heightForWidth(int width) const
{
    return PDFComponentPainter::getSignatureCardHeight(getContent(), width);
}

void PDFSignatureCard::layoutChildren()
{
    const PDFComponentPainter::SignatureCardLayout layout = PDFComponentPainter::getSignatureCardLayout(getContent(), width());

    if (m_toggle)
    {
        m_toggle->setGeometry(QStyle::visualRect(layoutDirection(), rect(), layout.toggle));
    }

    for (int i = 0; i < layout.links.size() && i < m_links.size(); ++i)
    {
        if (m_links.at(i) && !layout.links.at(i).isEmpty())
        {
            m_links.at(i)->setGeometry(QStyle::visualRect(layoutDirection(), rect(), layout.links.at(i)));
        }
    }
}

void PDFSignatureCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const PDFComponentPainter::State state(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection());
    PDFComponentPainter::paintSignatureCard(&painter, QRectF(rect()), getContent(), state);
}

void PDFSignatureCard::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutChildren();
}

void PDFSignatureCard::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LanguageChange:
            retranslate();
            break;

        case QEvent::FontChange:
        case QEvent::StyleChange:
            updateGeometry();
            layoutChildren();
            update();
            break;

        case QEvent::LayoutDirectionChange:
            layoutChildren();
            update();
            break;

        case QEvent::EnabledChange:
            update();
            break;

        default:
            break;
    }
}

// ------------------------------------------------------------ Strength meter

PDFStrengthMeter::PDFStrengthMeter(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setFocusPolicy(Qt::NoFocus);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFStrengthMeter::onThemeChanged);

    retranslate();
}

void PDFStrengthMeter::onThemeChanged()
{
    updateGeometry();
    update();
}

void PDFStrengthMeter::retranslate()
{
    if (m_isTitleDefault)
    {
        m_meter.title = tr("Password strength");
    }
    changeContent(true);
}

void PDFStrengthMeter::setLevel(Level level)
{
    level = PDFComponentPainter::normalized(level);
    if (m_meter.level != level)
    {
        m_meter.level = level;
        changeContent(false);

#if QT_CONFIG(accessibility) && QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        if (level != Level::Empty && isVisible())
        {
            // A new level is told politely (the meter changes while the user types)
            QAccessibleAnnouncementEvent announcement(this, getLevelText());
            announcement.setPoliteness(QAccessible::AnnouncementPoliteness::Polite);
            QAccessible::updateAccessibility(&announcement);
        }
#endif

        Q_EMIT levelChanged(level);
    }
}

void PDFStrengthMeter::setTitle(const QString& title)
{
    m_isTitleDefault = false;
    if (m_meter.title != title)
    {
        m_meter.title = title;
        changeContent(false);
    }
}

void PDFStrengthMeter::setDetail(const QString& detail)
{
    if (m_meter.detail != detail)
    {
        m_meter.detail = detail;
        changeContent(false);
    }
}

void PDFStrengthMeter::setDetailVisible(bool visible)
{
    if (m_meter.detailVisible != visible)
    {
        m_meter.detailVisible = visible;
        changeContent(false);
    }
}

void PDFStrengthMeter::setCriteriaVisible(bool visible)
{
    if (m_meter.criteriaVisible != visible)
    {
        m_meter.criteriaVisible = visible;
        changeContent(true);
    }
}

void PDFStrengthMeter::setSatisfiedCriteria(Criteria criteria)
{
    criteria &= (Criterion::Length | Criterion::Case | Criterion::Digits | Criterion::Symbols);
    if (m_meter.satisfied != criteria)
    {
        m_meter.satisfied = criteria;
        changeContent(false);
    }
}

void PDFStrengthMeter::setFilledCells(int cells)
{
    cells = cells < 0 ? -1 : qMin(cells, PDFComponentPainter::getStrengthMeterCellCount());
    if (m_meter.filledCells != cells)
    {
        m_meter.filledCells = cells;
        changeContent(false);
    }
}

void PDFStrengthMeter::setCriterionText(Criterion criterion, const QString& text)
{
    static constexpr Criterion criteria[] = { Criterion::Length, Criterion::Case, Criterion::Digits, Criterion::Symbols };
    for (int i = 0; i < 4; ++i)
    {
        if (criteria[i] != criterion)
        {
            continue;
        }

        while (m_meter.criteriaTexts.size() < 4)
        {
            m_meter.criteriaTexts << QString();
        }

        if (m_meter.criteriaTexts.at(i) != text)
        {
            m_meter.criteriaTexts[i] = text;
            changeContent(true);
        }
        return;
    }
}

QString PDFStrengthMeter::getLevelText() const
{
    return PDFComponentPainter::getStrengthLevelText(m_meter);
}

void PDFStrengthMeter::changeContent(bool isGeometryChanged)
{
    updateAccessibleTexts();
    if (isGeometryChanged)
    {
        updateGeometry();
    }
    update();
}

void PDFStrengthMeter::updateAccessibleTexts()
{
    // The meter is drawn: its level and the criteria are its name and description
    const QString name = m_meter.level == Level::Empty ? m_meter.title : tr("%1: %2").arg(m_meter.title, getLevelText());

    QStringList lines;
    if (m_meter.detailVisible)
    {
        lines << m_meter.detail;
    }
    if (m_meter.criteriaVisible)
    {
        for (const Criterion criterion : { Criterion::Length, Criterion::Case, Criterion::Digits, Criterion::Symbols })
        {
            const QString text = PDFComponentPainter::getStrengthCriterionText(m_meter, criterion);
            lines << (m_meter.satisfied.testFlag(criterion) ? tr("%1: satisfied") : tr("%1: not satisfied")).arg(text);
        }
    }
    lines.removeAll(QString());
    const QString description = lines.join(QLatin1Char('\n'));

    PDFComponentPanelsHelper::replaceAccessibleTexts(this, m_accessibleName, name, m_accessibleDescription, description);
}

QSize PDFStrengthMeter::sizeHint() const
{
    return PDFComponentPainter::getStrengthMeterSize(m_meter);
}

QSize PDFStrengthMeter::minimumSizeHint() const
{
    return PDFComponentPainter::getStrengthMeterMinimumSize(m_meter);
}

int PDFStrengthMeter::heightForWidth(int width) const
{
    return PDFComponentPainter::getStrengthMeterHeight(m_meter, width);
}

void PDFStrengthMeter::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const PDFComponentPainter::State state(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection());
    PDFComponentPainter::paintStrengthMeter(&painter, QRectF(rect()), m_meter, state);
}

void PDFStrengthMeter::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LanguageChange:
            retranslate();
            break;

        case QEvent::FontChange:
        case QEvent::StyleChange:
            updateGeometry();
            update();
            break;

        case QEvent::LayoutDirectionChange:
        case QEvent::EnabledChange:
            update();
            break;

        default:
            break;
    }
}

}   // namespace pdf
