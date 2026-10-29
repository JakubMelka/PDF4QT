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

#include "pdfappdocumentmenus.h"
#include "pdfapplocale.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappsendmail.h"
#include "pdfsettingdefinitions.h"
#include "pdfshortcutrepository.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentcards.h"
#include "pdffluentwidgets.h"
#include "pdfpreviewpainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QStyleOptionMenuItem>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

/// Context of the translated texts of the panels
class PDFAppDocumentMenusHelper
{
public:
    PDFAppDocumentMenusHelper() = delete;

    static constexpr const char* CONTEXT = "pdf::PDFAppDocumentPanelPopup";

    /// Property of a tile and of a row: the id of its command
    static constexpr const char* COMMAND_PROPERTY = "pdf4qtCommandId";

    static QString translate(const char* sourceText)
    {
        return QCoreApplication::translate(CONTEXT, sourceText);
    }

    /// The control \p widget of the panel \p popup uses Left / Right itself: a
    /// field, a slider, a list (every focusable widget, which is not a button), a
    /// button of a group of choices and a button inside of a composite control
    /// (a row of color swatches)
    static bool usesHorizontalArrows(const QWidget* popup, const QWidget* widget)
    {
        const QAbstractButton* button = qobject_cast<const QAbstractButton*>(widget);
        if (!button)
        {
            return true;
        }
        if (button->group() || button->autoExclusive())
        {
            return true;
        }

        // A button of the panel lies in the panel or in a plain container (a row, a section)
        const QWidget* parent = button->parentWidget();
        return parent && parent != popup && parent->metaObject() != &QWidget::staticMetaObject;
    }

    /// The nearest focusable control in the row of \p focus at the side given by
    /// \p isRightward (the geometry, not the order), nullptr at the edge of the row
    static QWidget* findRowNeighbor(QWidget* popup, QWidget* focus, bool isRightward)
    {
        const QRect focusRect(focus->mapTo(popup, QPoint(0, 0)), focus->size());
        QWidget* neighbor = nullptr;
        int neighborDistance = 0;

        const QList<QWidget*> widgets = popup->findChildren<QWidget*>();
        for (QWidget* widget : widgets)
        {
            if (widget == focus || !(widget->focusPolicy() & Qt::TabFocus) || !widget->isEnabled() || !widget->isVisibleTo(popup) || widget->focusProxy())
            {
                continue;
            }

            // A part of a composite control is reached through the control
            const QWidget* parent = widget->parentWidget();
            if (parent && parent != popup && parent->metaObject() != &QWidget::staticMetaObject)
            {
                continue;
            }

            const QRect rect(widget->mapTo(popup, QPoint(0, 0)), widget->size());
            const bool isSameRow = (rect.center().y() >= focusRect.top() && rect.center().y() <= focusRect.bottom()) ||
                                   (focusRect.center().y() >= rect.top() && focusRect.center().y() <= rect.bottom());
            const int distance = isRightward ? rect.center().x() - focusRect.center().x() : focusRect.center().x() - rect.center().x();
            if (!isSameRow || distance <= 0)
            {
                continue;
            }

            if (!neighbor || distance < neighborDistance)
            {
                neighbor = widget;
                neighborDistance = distance;
            }
        }

        return neighbor;
    }
};

// ------------------------------------------------------------ PDFAppPanelMenuItem

PDFAppPanelMenuItem::PDFAppPanelMenuItem(const QString& iconName, QWidget* parent) :
    BaseClass(parent),
    m_iconName(iconName)
{
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (!iconName.isEmpty())
    {
        setIcon(PDFThemeIcons::getIcon(iconName));
    }
}

void PDFAppPanelMenuItem::setShortcutText(const QString& text)
{
    if (m_shortcutText != text)
    {
        m_shortcutText = text;
        updateGeometry();
        update();
    }
}

QSize PDFAppPanelMenuItem::sizeHint() const
{
    // Padding 10, icon 16, gap 10, the label, the shortcut and the check mark
    const QFontMetrics metrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::Body));
    const QFontMetrics smallMetrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::Small));
    int width = 10 + 16 + 10 + metrics.horizontalAdvance(text()) + 10;
    if (!m_shortcutText.isEmpty())
    {
        width += smallMetrics.horizontalAdvance(m_shortcutText) + 10;
    }
    if (isCheckable())
    {
        width += 16 + 10;
    }
    return QSize(width, HEIGHT);
}

QSize PDFAppPanelMenuItem::minimumSizeHint() const
{
    return QSize(10 + 16 + 10 + 10, HEIGHT);
}

void PDFAppPanelMenuItem::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // The row is an item of a menu of the style (component Menu item)
    QStyleOptionMenuItem option;
    option.initFrom(this);
    option.menuItemType = QStyleOptionMenuItem::Normal;
    option.checkType = isCheckable() ? QStyleOptionMenuItem::NonExclusive : QStyleOptionMenuItem::NotCheckable;
    option.checked = isChecked();
    option.icon = icon();
    option.maxIconWidth = 16;
    option.text = m_shortcutText.isEmpty() ? text() : text() + QLatin1Char('\t') + m_shortcutText;
    option.state &= ~(QStyle::State_Selected | QStyle::State_Sunken);
    if (isEnabled() && (underMouse() || hasFocus()))
    {
        option.state |= QStyle::State_Selected;
    }
    if (isEnabled() && isDown())
    {
        option.state |= QStyle::State_Sunken;
    }

    QPainter painter(this);
    style()->drawControl(QStyle::CE_MenuItem, &option, &painter, this);
}

void PDFAppPanelMenuItem::keyPressEvent(QKeyEvent* event)
{
    // Enter and Return run the row as Space does
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers() == Qt::NoModifier)
    {
        event->accept();
        animateClick();
        return;
    }

    BaseClass::keyPressEvent(event);
}

bool PDFAppPanelMenuItem::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            update();
            break;

        default:
            break;
    }

    return BaseClass::event(event);
}

// ------------------------------------------------------ PDFAppDocumentPanelPopup

PDFAppDocumentPanelPopup::PDFAppDocumentPanelPopup(PDFAppDocumentView* view, int panelWidth, QWidget* parent) :
    BaseClass(parent, Qt::Popup),
    m_view(view),
    m_panelWidth(panelWidth)
{
    // Popover of the style (translucent window with the shadow); the panel places
    // itself (popup), the style does not move it
    setProperty(PDFWidgetStyleProperties::POPOVER, true);
    setProperty(PDFWidgetStyleProperties::POPUP_PLACED, true);
    setAttribute(Qt::WA_WindowPropagation, true);

    m_contentLayout = new QVBoxLayout(this);
    m_contentLayout->setSpacing(SECTION_GAP);
    m_contentLayout->setSizeConstraint(QLayout::SetNoConstraint);

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppDocumentPanelPopup::update));
    if (view)
    {
        connect(view, &PDFAppDocumentView::commandStatesChanged, this, &PDFAppDocumentPanelPopup::onViewStateChanged);
        connect(view, &PDFAppDocumentView::viewChanged, this, &PDFAppDocumentPanelPopup::onViewStateChanged);
    }
}

PDFAppDocumentPanelPopup::~PDFAppDocumentPanelPopup()
{
    watchAnchor(false);

    // An open panel closes, while it is whole (the destructor of QWidget would
    // hide it with the children of the subclass already deleted)
    if (isVisible())
    {
        hide();
    }
}

PDFAppDocumentView* PDFAppDocumentPanelPopup::getView() const
{
    return m_view.data();
}

QWidget* PDFAppDocumentPanelPopup::getAnchor() const
{
    return isVisible() ? m_anchor.data() : nullptr;
}

QRect PDFAppDocumentPanelPopup::getPanelRect() const
{
    return rect().marginsRemoved(PDFWidgetStyle::getPopoverMargins(this));
}

QAbstractButton* PDFAppDocumentPanelPopup::getCommandButton(const QString& commandId) const
{
    auto it = m_commandButtons.find(commandId);
    return it != m_commandButtons.end() ? it->second.data() : nullptr;
}

void PDFAppDocumentPanelPopup::popup(QWidget* anchor, int globalTop)
{
    if (!anchor || !anchor->isVisible())
    {
        return;
    }

    watchAnchor(false);
    m_anchor = anchor;
    m_anchorWindow = anchor->window();
    watchAnchor(true);
    m_isOpen = true;
    ++m_openCount;

    // The window of the popup takes the direction of its anchor
    setLayoutDirection(anchor->layoutDirection());
    setAttribute(Qt::WA_NoMouseReplay, false);
    ensurePolished();
    updateState();
    updateTileToolTips();

    // The content lies inside of the border and of the padding; the window has
    // the margins of the shadow around the panel
    const QMargins margins = PDFWidgetStyle::getPopoverMargins(this);
    const QMargins padding(PADDING + 1, PADDING + 1, PADDING + 1, PADDING + 1);
    m_contentLayout->setContentsMargins(margins + padding);
    m_contentLayout->activate();

    const int windowWidth = m_panelWidth + margins.left() + margins.right();
    const int windowHeight = m_contentLayout->hasHeightForWidth() ? m_contentLayout->totalHeightForWidth(windowWidth) : m_contentLayout->totalSizeHint().height();
    const QSize panelSize(m_panelWidth, qMax(0, windowHeight - margins.top() - margins.bottom()));

    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    QRect panel(QPoint(isRightToLeft ? anchorRect.right() + 1 - panelSize.width() : anchorRect.left(), globalTop), panelSize);

    // The screen, on which the anchor lies; the panel stays inside of it
    const QScreen* screen = QGuiApplication::screenAt(anchorRect.center());
    if (!screen)
    {
        screen = anchor->screen();
    }
    if (screen)
    {
        const QRect available = screen->availableGeometry();
        panel.moveLeft(qBound(available.left(), panel.left(), qMax(available.left(), available.right() + 1 - panel.width())));
        panel.moveTop(qBound(available.top(), panel.top(), qMax(available.top(), available.bottom() + 1 - panel.height())));
        if (panel.height() > available.height())
        {
            panel.setHeight(available.height());
        }
    }

    setFixedSize(panel.size().grownBy(margins));
    move(panel.topLeft() - QPoint(margins.left(), margins.top()));

    // Left / Right reach the panel before its controls (a tool button moves the
    // focus by them itself); a filter installed again moves to the front, also the
    // controls created since the last opening get it
    const QList<QWidget*> controls = findChildren<QWidget*>();
    for (QWidget* control : controls)
    {
        control->installEventFilter(this);
    }

    QPointer<PDFAppDocumentPanelPopup> guard(this);
    show();
    if (!guard)
    {
        // A filter of the events of the application has deleted the panel
        return;
    }

    // The keyboard starts at the first control of the panel, also when the panel is
    // opened again: the window remembers its last focused control, after which
    // focusNextPrevChild would continue - the remembered control is forgotten first
    activateWindow();
    if (QWidget* remembered = focusWidget())
    {
        remembered->clearFocus();
        if (!guard)
        {
            // A slot of the loss of the focus has deleted the panel
            return;
        }
    }
    focusNextPrevChild(true);
}

void PDFAppDocumentPanelPopup::onViewStateChanged()
{
    // A closed panel reads the states, when it opens (popup): the Go to panel
    // would read the database at every scroll step of the view
    if (isVisible())
    {
        updateState();
    }
}

void PDFAppDocumentPanelPopup::updateState()
{
    PDFAppDocumentView* view = m_view.data();
    for (const auto& item : m_commandButtons)
    {
        QAbstractButton* button = item.second.data();
        if (!button)
        {
            continue;
        }

        const QSignalBlocker blocker(button);
        button->setEnabled(view && view->isCommandEnabled(item.first));
        if (button->isCheckable())
        {
            button->setChecked(view && view->isCommandChecked(item.first));
        }
    }
}

PDFLabel* PDFAppDocumentPanelPopup::createCaption(const char* sourceText)
{
    PDFLabel* label = new PDFLabel(QString(), PDFThemeFonts::Role::Caption, PDFThemeColors::TextMuted, this);
    addText(label, sourceText);
    return label;
}

QToolButton* PDFAppDocumentPanelPopup::createTile(const QString& commandId, const QString& iconName, const char* sourceText)
{
    QToolButton* tile = new QToolButton(this);
    tile->setObjectName(QStringLiteral("tile:") + commandId);
    tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tile->setIcon(PDFThemeIcons::getIcon(iconName));
    tile->setIconSize(QSize(18, 18));
    tile->setFocusPolicy(Qt::TabFocus);
    tile->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    tile->setFixedHeight(TILE_HEIGHT);
    tile->setProperty(PDFAppDocumentMenusHelper::COMMAND_PROPERTY, commandId);

    const PDFAppContext* context = m_view ? m_view->getContext() : nullptr;
    const PDFAppCommand* command = context ? context->getCommandRegistry()->findCommand(commandId) : nullptr;
    tile->setCheckable(command && command->isCheckable);

    addText(tile, sourceText);
    connect(tile, &QToolButton::clicked, this, &PDFAppDocumentPanelPopup::onCommandButtonClicked);
    m_commandButtons[commandId] = tile;
    return tile;
}

PDFAppPanelMenuItem* PDFAppDocumentPanelPopup::createMenuItem(const QString& commandId, const QString& iconName, const char* sourceText, bool showShortcut)
{
    PDFAppPanelMenuItem* item = createActionItem(iconName, sourceText);
    item->setObjectName(QStringLiteral("item:") + commandId);
    item->setProperty(PDFAppDocumentMenusHelper::COMMAND_PROPERTY, commandId);

    const PDFAppContext* context = m_view ? m_view->getContext() : nullptr;
    const PDFAppCommand* command = context ? context->getCommandRegistry()->findCommand(commandId) : nullptr;
    item->setCheckable(command && command->isCheckable);

    if (showShortcut && command)
    {
        // The effective shortcut of the user (the action of the window has it)
        const PDFAppMainWindow* window = m_view ? qobject_cast<const PDFAppMainWindow*>(m_view->window()) : nullptr;
        const QAction* action = window ? window->getCommandAction(commandId) : nullptr;
        const QKeySequence shortcut = action ? action->shortcut() : command->defaultShortcut;
        item->setShortcutText(shortcut.toString(QKeySequence::NativeText));
    }

    connect(item, &QAbstractButton::clicked, this, &PDFAppDocumentPanelPopup::onCommandButtonClicked);
    m_commandButtons[commandId] = item;
    return item;
}

PDFAppPanelMenuItem* PDFAppDocumentPanelPopup::createActionItem(const QString& iconName, const char* sourceText)
{
    PDFAppPanelMenuItem* item = new PDFAppPanelMenuItem(iconName, this);
    addText(item, sourceText);
    return item;
}

QWidget* PDFAppDocumentPanelPopup::createTileRow(const std::vector<QToolButton*>& tiles, int columns)
{
    QWidget* row = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(TILE_GAP);

    for (QToolButton* tile : tiles)
    {
        tile->setParent(row);
        layout->addWidget(tile, 1);
    }

    // Empty places of a row with fewer tiles (Line, Bookmarks)
    for (int i = int(tiles.size()); i < columns; ++i)
    {
        QWidget* empty = new QWidget(row);
        empty->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        layout->addWidget(empty, 1);
    }
    return row;
}

QWidget* PDFAppDocumentPanelPopup::createDivider()
{
    // divider 196:2605: 1 px border/divider over the whole width
    return new PDFAppToolbarDivider(Qt::Horizontal, 0, 0, PDFThemeColors::BorderDivider, this);
}

void PDFAppDocumentPanelPopup::addText(QWidget* widget, const char* sourceText)
{
    if (widget && sourceText)
    {
        m_texts.emplace_back(widget, sourceText);
        updateTexts();
    }
}

void PDFAppDocumentPanelPopup::setTitleText(const char* sourceText)
{
    m_titleText = sourceText;
    updateTexts();
}

void PDFAppDocumentPanelPopup::updateTexts()
{
    if (m_titleText)
    {
        setAccessibleName(PDFAppDocumentMenusHelper::translate(m_titleText));
        setWindowTitle(accessibleName());
    }

    for (const auto& item : m_texts)
    {
        QWidget* widget = item.first.data();
        const QString text = PDFAppDocumentMenusHelper::translate(item.second);
        if (QAbstractButton* button = qobject_cast<QAbstractButton*>(widget))
        {
            button->setText(text);
            button->setAccessibleName(text);
        }
        else if (QLabel* label = qobject_cast<QLabel*>(widget))
        {
            label->setText(text);
        }
    }
}

void PDFAppDocumentPanelPopup::updateTileToolTips()
{
    // A tile shows its title, its tooltip the shortcut of the user (also of a
    // Canvas command - the action of the window has none), kept by the view
    const PDFAppDocumentView* view = m_view.data();
    if (!view)
    {
        return;
    }

    for (const auto& item : m_commandButtons)
    {
        QAbstractButton* button = item.second.data();
        if (button && button->objectName().startsWith(QLatin1String("tile:")))
        {
            button->setToolTip(view->getCommandToolTip(item.first, true));
        }
    }
}

void PDFAppDocumentPanelPopup::retranslate()
{
    updateTexts();
}

void PDFAppDocumentPanelPopup::runCommand(const QString& commandId)
{
    // The panel closes first; the command runs from the event loop (it may delete
    // the tab, the toolbar and this panel)
    QPointer<PDFAppDocumentView> view = m_view;
    close();
    if (view)
    {
        view->requestCommand(commandId);
    }
}

void PDFAppDocumentPanelPopup::openSettings(const QString& section)
{
    QPointer<PDFAppDocumentView> view = m_view;
    close();
    if (view)
    {
        view->openSettings(section);
    }
}

void PDFAppDocumentPanelPopup::onCommandButtonClicked()
{
    // The tiles and the rows do not identify themselves in the signal
    QAbstractButton* button = qobject_cast<QAbstractButton*>(sender());
    if (!button)
    {
        return;
    }

    const QString commandId = button->property(PDFAppDocumentMenusHelper::COMMAND_PROPERTY).toString();
    if (!commandId.isEmpty())
    {
        runCommand(commandId);
    }
}

void PDFAppDocumentPanelPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    PDFWidgetStyle::drawPopoverPanel(&painter, this, RADIUS);
}

void PDFAppDocumentPanelPopup::keyPressEvent(QKeyEvent* event)
{
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const bool isLineEdit = qobject_cast<QLineEdit*>(focusWidget()) != nullptr;

    switch (event->key())
    {
        case Qt::Key_Escape:
            close();
            event->accept();
            return;

        case Qt::Key_Down:
            focusNextPrevChild(true);
            event->accept();
            return;

        case Qt::Key_Up:
            focusNextPrevChild(false);
            event->accept();
            return;

        case Qt::Key_Right:
        case Qt::Key_Left:
            if ((event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && handleHorizontalArrow(focusWidget(), event->key()))
            {
                // The panel may be deleted by a slot of neighborRequested
                event->accept();
                return;
            }
            if (!isLineEdit)
            {
                // A group of choices at its edge passes the arrow on
                const bool isForward = (event->key() == Qt::Key_Right) != isRightToLeft;
                focusNextPrevChild(isForward);
                event->accept();
                return;
            }
            break;

        default:
            break;
    }

    BaseClass::keyPressEvent(event);
}

void PDFAppDocumentPanelPopup::mousePressEvent(QMouseEvent* event)
{
    // A click on the anchor closes the panel and it does not open it again (the
    // press is not replayed to the anchor)
    if (m_anchor && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(event->globalPosition().toPoint()))
    {
        setAttribute(Qt::WA_NoMouseReplay, true);
    }

    BaseClass::mousePressEvent(event);
}

void PDFAppDocumentPanelPopup::hideEvent(QHideEvent* event)
{
    BaseClass::hideEvent(event);

    if (m_isOpen)
    {
        // Any hide closes the opening; the signal is queued, so its slot may
        // delete the panel. A panel opened again before it arrives drops it.
        m_isOpen = false;
        watchAnchor(false);
        QMetaObject::invokeMethod(this, &PDFAppDocumentPanelPopup::onHiddenQueued, Qt::QueuedConnection, quint64(m_openCount));
    }
}

void PDFAppDocumentPanelPopup::onHiddenQueued(quint64 openCount)
{
    if (m_openCount == openCount && !isVisible())
    {
        Q_EMIT closed();
    }
}

void PDFAppDocumentPanelPopup::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

bool PDFAppDocumentPanelPopup::eventFilter(QObject* watched, QEvent* event)
{
    // The owner of the panel is hidden or closed: the panel closes
    if (watched == m_anchor || watched == m_anchorWindow)
    {
        switch (event->type())
        {
            case QEvent::Hide:
            case QEvent::Close:
                if (isVisible())
                {
                    close();
                }
                break;

            default:
                break;
        }
    }
    else if (event->type() == QEvent::KeyPress && watched != this && watched->isWidgetType() && isVisible())
    {
        // Left / Right of the focused control (not the event passed on to its parents)
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        QWidget* widget = static_cast<QWidget*>(watched);
        const bool isHorizontalArrow = keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right;
        if (isHorizontalArrow && (keyEvent->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && widget->hasFocus() && isAncestorOf(widget) &&
            handleHorizontalArrow(widget, keyEvent->key()))
        {
            // The panel may be deleted by a slot of neighborRequested
            keyEvent->accept();
            return true;
        }
    }

    return BaseClass::eventFilter(watched, event);
}

bool PDFAppDocumentPanelPopup::handleHorizontalArrow(QWidget* focus, int key)
{
    using H = PDFAppDocumentMenusHelper;

    if (focus && focus != this && (!isAncestorOf(focus) || H::usesHorizontalArrows(this, focus)))
    {
        return false;
    }

    // A row of buttons (tiles, chips, check boxes): the arrow moves to the neighbor
    if (focus && focus != this)
    {
        if (QWidget* neighbor = H::findRowNeighbor(this, focus, key == Qt::Key_Right))
        {
            neighbor->setFocus(Qt::TabFocusReason);
            return true;
        }
    }

    // The edge of a row, a list of rows: the neighboring panel (menu bar)
    const bool isNext = (key == Qt::Key_Right) != (layoutDirection() == Qt::RightToLeft);
    Q_EMIT neighborRequested(isNext);
    return true;
}

void PDFAppDocumentPanelPopup::onAnchorDestroyed()
{
    // The anchor is not whole any more: only the panel closes
    if (isVisible())
    {
        close();
    }
}

void PDFAppDocumentPanelPopup::watchAnchor(bool watch)
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

    if (m_anchor)
    {
        m_anchorDestroyed = connect(m_anchor.data(), &QObject::destroyed, this, &PDFAppDocumentPanelPopup::onAnchorDestroyed);
    }
}

// ------------------------------------------------------- PDFAppDocumentFilePanel

PDFAppDocumentFilePanel::PDFAppDocumentFilePanel(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(view, 300, parent)
{
    setObjectName(QStringLiteral("filePanel"));
    setTitleText(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "File"));
    using C = PDFAppCommands;

    QVBoxLayout* layout = getContentLayout();
    layout->addWidget(createTileRow({ createTile(C::save(), QStringLiteral("save"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Save")),
                                      createTile(C::saveAs(), QStringLiteral("save-as"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Save as")),
                                      createTile(C::print(), QStringLiteral("printer"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Print")),
                                      createTile(C::sendMail(), QStringLiteral("mail"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "E-mail")) }));
    layout->addWidget(createDivider());

    QWidget* items = new QWidget(this);
    QVBoxLayout* itemsLayout = new QVBoxLayout(items);
    itemsLayout->setContentsMargins(0, 0, 0, 0);
    itemsLayout->setSpacing(1);
    itemsLayout->addWidget(createMenuItem(C::properties(), QStringLiteral("info"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Properties…")));
    itemsLayout->addWidget(createMenuItem(C::autoRefresh(), QStringLiteral("refresh-cw"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Automatic refresh")));
    itemsLayout->addWidget(createMenuItem(C::lockDocument(), QStringLiteral("lock"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Lock document")));
    PDFAppPanelMenuItem* settings = createActionItem(QStringLiteral("settings"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Settings…"));
    settings->setObjectName(QStringLiteral("item:settings"));
    connect(settings, &QAbstractButton::clicked, this, &PDFAppDocumentFilePanel::onSettingsClicked);
    itemsLayout->addWidget(settings);
    layout->addWidget(items);
    layout->addWidget(createDivider());

    // Close document is the command Close tab of the window
    m_closeItem = createActionItem(QStringLiteral("x-circle"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Close document"));
    m_closeItem->setObjectName(QStringLiteral("item:") + C::closeTab());
    connect(m_closeItem.data(), &QAbstractButton::clicked, this, &PDFAppDocumentFilePanel::onCloseClicked);
    layout->addWidget(m_closeItem);
}

void PDFAppDocumentFilePanel::updateState()
{
    BaseClass::updateState();

    // E-mail is hidden on a platform without it (D2: macOS); the other tiles of
    // the row share its place
    if (QAbstractButton* mailTile = getCommandButton(PDFAppCommands::sendMail()))
    {
        mailTile->setVisible(PDFAppSendMail::isAvailable());
    }

    if (m_closeItem)
    {
        // The effective shortcut of the user
        const PDFAppMainWindow* window = getView() ? qobject_cast<const PDFAppMainWindow*>(getView()->window()) : nullptr;
        const QAction* action = window ? window->getCommandAction(PDFAppCommands::closeTab()) : nullptr;
        m_closeItem->setShortcutText(action ? action->shortcut().toString(QKeySequence::NativeText) : QKeySequence(Qt::CTRL | Qt::Key_W).toString(QKeySequence::NativeText));
    }
}

void PDFAppDocumentFilePanel::onSettingsClicked()
{
    openSettings(QString());
}

void PDFAppDocumentFilePanel::onCloseClicked()
{
    runCommand(PDFAppCommands::closeTab());
}

// ------------------------------------------------------- PDFAppDocumentViewPanel

PDFAppDocumentViewPanel::PDFAppDocumentViewPanel(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(view, 700, parent)
{
    setObjectName(QStringLiteral("viewPanel"));
    setTitleText(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "View"));
    using C = PDFAppCommands;
    using Preview = PDFPreviewPainter::Preview;

    QVBoxLayout* layout = getContentLayout();

    // Columns: page layout (2 cards in a row) and page colors (3 cards in a row)
    QWidget* columns = new QWidget(this);
    QHBoxLayout* columnsLayout = new QHBoxLayout(columns);
    columnsLayout->setContentsMargins(0, 0, 0, 0);
    columnsLayout->setSpacing(24);

    QButtonGroup* layoutGroup = new QButtonGroup(this);
    layoutGroup->setExclusive(true);
    QWidget* layoutSection = new QWidget(columns);
    QVBoxLayout* layoutSectionLayout = new QVBoxLayout(layoutSection);
    layoutSectionLayout->setContentsMargins(0, 0, 0, 0);
    layoutSectionLayout->setSpacing(8);
    layoutSectionLayout->addWidget(createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Page layout")));
    QGridLayout* layoutCards = new QGridLayout();
    layoutCards->setSpacing(TILE_GAP);
    layoutCards->addWidget(createCard(C::layoutSinglePage(), int(Preview::LayoutSinglePage), layoutGroup), 0, 0);
    layoutCards->addWidget(createCard(C::layoutContinuous(), int(Preview::LayoutContinuous), layoutGroup), 0, 1);
    layoutCards->addWidget(createCard(C::layoutTwoPages(), int(Preview::LayoutTwoPages), layoutGroup), 1, 0);
    layoutCards->addWidget(createCard(C::layoutTwoColumns(), int(Preview::LayoutTwoColumns), layoutGroup), 1, 1);
    layoutSectionLayout->addLayout(layoutCards);
    layoutSectionLayout->addStretch(1);
    columnsLayout->addWidget(layoutSection);

    QButtonGroup* colorGroup = new QButtonGroup(this);
    colorGroup->setExclusive(true);
    QWidget* colorSection = new QWidget(columns);
    QVBoxLayout* colorSectionLayout = new QVBoxLayout(colorSection);
    colorSectionLayout->setContentsMargins(0, 0, 0, 0);
    colorSectionLayout->setSpacing(8);
    colorSectionLayout->addWidget(createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Page colors")));
    QGridLayout* colorCards = new QGridLayout();
    colorCards->setSpacing(TILE_GAP);
    colorCards->addWidget(createCard(C::colorModeNormal(), int(Preview::ModeNormal), colorGroup), 0, 0);
    colorCards->addWidget(createCard(C::colorModeInverted(), int(Preview::ModeInverted), colorGroup), 0, 1);
    colorCards->addWidget(createCard(C::colorModeGrayscale(), int(Preview::ModeGrayscale), colorGroup), 0, 2);
    colorCards->addWidget(createCard(C::colorModeHighContrast(), int(Preview::ModeHighContrast), colorGroup), 1, 0);
    colorCards->addWidget(createCard(C::colorModeBitonal(), int(Preview::ModeBitonal), colorGroup), 1, 1);
    colorCards->addWidget(createCard(C::colorModeCustom(), int(Preview::ModeCustomColors), colorGroup), 1, 2);
    colorSectionLayout->addLayout(colorCards);
    colorSectionLayout->addStretch(1);
    columnsLayout->addWidget(colorSection);
    columnsLayout->addStretch(1);
    layout->addWidget(columns);

    // Actions: zoom and rotation (5 tiles) and tools (2 tiles, 186 px)
    QWidget* actions = new QWidget(this);
    QHBoxLayout* actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(24);

    QWidget* zoomSection = new QWidget(actions);
    QVBoxLayout* zoomSectionLayout = new QVBoxLayout(zoomSection);
    zoomSectionLayout->setContentsMargins(0, 0, 0, 0);
    zoomSectionLayout->setSpacing(8);
    zoomSectionLayout->addWidget(createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Zoom and rotation")));
    zoomSectionLayout->addWidget(createTileRow({ createTile(C::fitPage(), QStringLiteral("maximize"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Fit page")),
                                                 createTile(C::fitWidth(), QStringLiteral("fit-width"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Fit width")),
                                                 createTile(C::fitHeight(), QStringLiteral("fit-height"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Fit height")),
                                                 createTile(C::rotateViewLeft(), QStringLiteral("rotate-ccw"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Rotate left")),
                                                 createTile(C::rotateViewRight(), QStringLiteral("rotate-cw"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Rotate right")) }));
    actionsLayout->addWidget(zoomSection, 1);

    QWidget* toolsSection = new QWidget(actions);
    toolsSection->setFixedWidth(186);
    QVBoxLayout* toolsSectionLayout = new QVBoxLayout(toolsSection);
    toolsSectionLayout->setContentsMargins(0, 0, 0, 0);
    toolsSectionLayout->setSpacing(8);
    toolsSectionLayout->addWidget(createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Tools")));
    toolsSectionLayout->addWidget(createTileRow({ createTile(C::magnifier(), QStringLiteral("magnifier"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Magnifier")),
                                                  createTile(C::fullScreen(), QStringLiteral("expand"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Full screen")) }));
    actionsLayout->addWidget(toolsSection);
    layout->addWidget(actions);
    layout->addWidget(createDivider());

    // Footer: the check boxes and the link to the settings of the rendering
    QWidget* footer = new QWidget(this);
    QHBoxLayout* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(24);

    m_showAnnotations = new QCheckBox(footer);
    m_showAnnotations->setObjectName(QStringLiteral("check:") + C::showAnnotations());
    addText(m_showAnnotations, QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Show annotations"));
    connect(m_showAnnotations.data(), &QCheckBox::clicked, this, &PDFAppDocumentViewPanel::onCheckBoxClicked);
    footerLayout->addWidget(m_showAnnotations);

    m_firstPageRight = new QCheckBox(footer);
    m_firstPageRight->setObjectName(QStringLiteral("check:") + C::firstPageRight());
    addText(m_firstPageRight, QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "First page on the right side"));
    connect(m_firstPageRight.data(), &QCheckBox::clicked, this, &PDFAppDocumentViewPanel::onCheckBoxClicked);
    footerLayout->addWidget(m_firstPageRight);
    footerLayout->addStretch(1);

    // rendering-settings 196:3045: the icon 14 and the link, 6 px apart
    PDFIconBadge* settingsIcon = new PDFIconBadge(QStringLiteral("settings"), 14, 14, footer);
    settingsIcon->setColors(std::nullopt, PDFThemeColors::IconAccent);
    m_renderingSettings = new PDFTextLink(footer);
    m_renderingSettings->setObjectName(QStringLiteral("renderingSettings"));
    addText(m_renderingSettings, QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Rendering settings"));
    connect(m_renderingSettings.data(), &QAbstractButton::clicked, this, &PDFAppDocumentViewPanel::onRenderingSettingsClicked);
    QHBoxLayout* linkLayout = new QHBoxLayout();
    linkLayout->setContentsMargins(0, 0, 0, 0);
    linkLayout->setSpacing(6);
    linkLayout->addWidget(settingsIcon);
    linkLayout->addWidget(m_renderingSettings);
    footerLayout->addLayout(linkLayout);
    layout->addWidget(footer);
}

PDFPreviewCard* PDFAppDocumentViewPanel::createCard(const QString& commandId, int preview, QButtonGroup* group)
{
    PDFPreviewCard* card = new PDFPreviewCard(static_cast<PDFPreviewPainter::Preview>(preview), this);
    card->setObjectName(QStringLiteral("card:") + commandId);
    card->setProperty(PDFAppDocumentMenusHelper::COMMAND_PROPERTY, commandId);
    group->addButton(card);
    connect(card, &QAbstractButton::clicked, this, &PDFAppDocumentViewPanel::onCardClicked);
    m_cards[commandId] = card;
    return card;
}

void PDFAppDocumentViewPanel::updateState()
{
    BaseClass::updateState();

    PDFAppDocumentView* view = getView();
    for (const auto& item : m_cards)
    {
        if (PDFPreviewCard* card = item.second.data())
        {
            const QSignalBlocker blocker(card);
            card->setEnabled(view && view->isCommandEnabled(item.first));
            card->setSelected(view && view->isCommandChecked(item.first));
        }
    }

    for (QCheckBox* checkBox : { m_showAnnotations.data(), m_firstPageRight.data() })
    {
        if (checkBox)
        {
            const QString commandId = checkBox == m_showAnnotations ? PDFAppCommands::showAnnotations() : PDFAppCommands::firstPageRight();
            const QSignalBlocker blocker(checkBox);
            checkBox->setEnabled(view && view->isCommandEnabled(commandId));
            checkBox->setChecked(view && view->isCommandChecked(commandId));
        }
    }
}

PDFPreviewCard* PDFAppDocumentViewPanel::getCard(const QString& commandId) const
{
    auto it = m_cards.find(commandId);
    return it != m_cards.end() ? it->second.data() : nullptr;
}

QCheckBox* PDFAppDocumentViewPanel::getShowAnnotationsCheckBox() const
{
    return m_showAnnotations.data();
}

QCheckBox* PDFAppDocumentViewPanel::getFirstPageRightCheckBox() const
{
    return m_firstPageRight.data();
}

PDFTextLink* PDFAppDocumentViewPanel::getRenderingSettingsLink() const
{
    return m_renderingSettings.data();
}

void PDFAppDocumentViewPanel::onCardClicked()
{
    // The cards do not identify themselves in the signal; the panel stays open
    PDFPreviewCard* card = qobject_cast<PDFPreviewCard*>(sender());
    PDFAppDocumentView* view = getView();
    if (card && view)
    {
        view->requestCommand(card->property(PDFAppDocumentMenusHelper::COMMAND_PROPERTY).toString());
    }
}

void PDFAppDocumentViewPanel::onCheckBoxClicked()
{
    // The check box shows the state of the command, which is toggled (the panel stays open)
    QCheckBox* checkBox = qobject_cast<QCheckBox*>(sender());
    PDFAppDocumentView* view = getView();
    if (checkBox && view)
    {
        view->requestCommand(checkBox == m_showAnnotations ? PDFAppCommands::showAnnotations() : PDFAppCommands::firstPageRight());
    }
}

void PDFAppDocumentViewPanel::onRenderingSettingsClicked()
{
    openSettings(PDFSettingDefinitions::sectionViewing());
}

// ------------------------------------------------------- PDFAppDocumentGoToPanel

PDFAppDocumentGoToPanel::PDFAppDocumentGoToPanel(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(view, 340, parent)
{
    setObjectName(QStringLiteral("goToPanel"));
    setTitleText(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Go to"));
    using C = PDFAppCommands;

    QVBoxLayout* layout = getContentLayout();

    // Page: tiles and the field Go to page
    QWidget* pageSection = new QWidget(this);
    QVBoxLayout* pageLayout = new QVBoxLayout(pageSection);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(8);
    pageLayout->addWidget(createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Page")));
    pageLayout->addWidget(createTileRow({ createTile(C::firstPage(), QStringLiteral("chevron-first"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "First")),
                                          createTile(C::previousPage(), QStringLiteral("chevron-left"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Previous")),
                                          createTile(C::nextPage(), QStringLiteral("chevron-right"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Next")),
                                          createTile(C::lastPage(), QStringLiteral("chevron-last"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Last")) }));

    // go-to-page 196:3461: the label, the field 72 x 34, "of N", the button Go (2 px above)
    QWidget* goToRow = new QWidget(pageSection);
    QHBoxLayout* goToLayout = new QHBoxLayout(goToRow);
    goToLayout->setContentsMargins(0, 2, 0, 0);
    goToLayout->setSpacing(8);
    PDFLabel* goToLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Body, PDFThemeColors::TextSecondary, goToRow);
    addText(goToLabel, QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Go to page"));
    goToLayout->addWidget(goToLabel);
    m_pageField = new PDFLineEdit(goToRow);
    m_pageField->setObjectName(QStringLiteral("goToPageField"));
    m_pageField->setFixedWidth(72);
    m_pageField->setMaxLength(256);
    goToLabel->setBuddy(m_pageField);
    connect(m_pageField.data(), &QLineEdit::returnPressed, this, &PDFAppDocumentGoToPanel::onGoClicked);
    connect(m_pageField.data(), &QLineEdit::textEdited, this, &PDFAppDocumentGoToPanel::onPageTextEdited);
    goToLayout->addWidget(m_pageField);
    m_pageCount = new PDFLabel(QString(), PDFThemeFonts::Role::Body, PDFThemeColors::TextMuted, goToRow);
    goToLayout->addWidget(m_pageCount);
    goToLayout->addStretch(1);
    m_goButton = new QPushButton(goToRow);
    m_goButton->setObjectName(QStringLiteral("goToPageButton"));
    m_goButton->setAutoDefault(false);
    addText(m_goButton, QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Go"));
    connect(m_goButton.data(), &QPushButton::clicked, this, &PDFAppDocumentGoToPanel::onGoClicked);
    goToLayout->addWidget(m_goButton);
    pageLayout->addWidget(goToRow);
    layout->addWidget(pageSection);

    // Line
    QWidget* lineSection = new QWidget(this);
    QVBoxLayout* lineLayout = new QVBoxLayout(lineSection);
    lineLayout->setContentsMargins(0, 0, 0, 0);
    lineLayout->setSpacing(8);
    lineLayout->addWidget(createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Line")));
    lineLayout->addWidget(createTileRow({ createTile(C::previousLine(), QStringLiteral("match-prev"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Previous line")),
                                          createTile(C::nextLine(), QStringLiteral("match-next"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Next line")) }, 4));
    layout->addWidget(lineSection);

    // Bookmarks (the commands of package C)
    QWidget* bookmarksSection = new QWidget(this);
    QVBoxLayout* bookmarksLayout = new QVBoxLayout(bookmarksSection);
    bookmarksLayout->setContentsMargins(0, 0, 0, 0);
    bookmarksLayout->setSpacing(8);
    bookmarksLayout->addWidget(createCaption(QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Bookmarks")));
    bookmarksLayout->addWidget(createTileRow({ createTile(C::addBookmark(), QStringLiteral("bookmark-plus"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Add")),
                                               createTile(C::previousBookmark(), QStringLiteral("chevron-left"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Previous")),
                                               createTile(C::nextBookmark(), QStringLiteral("chevron-right"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Next")) }, 4));
    layout->addWidget(bookmarksSection);
    layout->addWidget(createDivider());

    QWidget* items = new QWidget(this);
    QVBoxLayout* itemsLayout = new QVBoxLayout(items);
    itemsLayout->setContentsMargins(0, 0, 0, 0);
    itemsLayout->setSpacing(1);
    itemsLayout->addWidget(createMenuItem(C::exportBookmarks(), QStringLiteral("download"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Export bookmarks…")));
    itemsLayout->addWidget(createMenuItem(C::importBookmarks(), QStringLiteral("folder-open"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Import bookmarks…")));
    m_autoGenerate = createActionItem(QStringLiteral("zap"), QT_TRANSLATE_NOOP("pdf::PDFAppDocumentPanelPopup", "Generate automatically"));
    m_autoGenerate->setObjectName(QStringLiteral("item:autoGenerateBookmarks"));
    m_autoGenerate->setCheckable(true);
    connect(m_autoGenerate.data(), &QAbstractButton::clicked, this, &PDFAppDocumentGoToPanel::onAutoGenerateClicked);
    itemsLayout->addWidget(m_autoGenerate);
    layout->addWidget(items);

    retranslate();
}

PDFLineEdit* PDFAppDocumentGoToPanel::getPageField() const
{
    return m_pageField.data();
}

QPushButton* PDFAppDocumentGoToPanel::getGoButton() const
{
    return m_goButton.data();
}

PDFAppPanelMenuItem* PDFAppDocumentGoToPanel::getAutoGenerateItem() const
{
    return m_autoGenerate.data();
}

void PDFAppDocumentGoToPanel::updateState()
{
    BaseClass::updateState();

    PDFAppDocumentView* view = getView();
    const bool hasPages = view && view->getPageCount() > 0;
    if (m_pageField)
    {
        m_pageField->setEnabled(hasPages);
    }
    if (m_goButton)
    {
        m_goButton->setEnabled(hasPages);
    }
    if (m_pageCount)
    {
        m_pageCount->setText(tr("of %1").arg(PDFAppLocale::formatNumber(view ? view->getPageCount() : 0)));
    }

    if (m_autoGenerate)
    {
        const PDFAppContext* context = view ? view->getContext() : nullptr;
        const QSignalBlocker blocker(m_autoGenerate.data());
        m_autoGenerate->setEnabled(context && context->getDatabase());
        m_autoGenerate->setChecked(context && context->getDatabase() && context->getSettings().getBool(PDFSettingKeys::bookmarksAutoGenerate()));
    }
}

void PDFAppDocumentGoToPanel::retranslate()
{
    BaseClass::retranslate();
    updateState();
}

void PDFAppDocumentGoToPanel::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The field shows the current page, when the panel opens
    PDFAppDocumentView* view = getView();
    if (m_pageField && view)
    {
        m_pageField->setInvalid(false);
        const PDFInteger pageIndex = view->getDisplayedPageIndex();
        m_pageField->setText(pageIndex >= 0 ? view->getPageLabel(pageIndex) : QString());
    }
}

void PDFAppDocumentGoToPanel::focusPageField()
{
    if (m_pageField && m_pageField->isEnabled())
    {
        m_pageField->setFocus(Qt::ShortcutFocusReason);
        m_pageField->selectAll();
    }
}

void PDFAppDocumentGoToPanel::onPageTextEdited()
{
    if (m_pageField)
    {
        m_pageField->setInvalid(false);
    }
}

void PDFAppDocumentGoToPanel::onGoClicked()
{
    PDFAppDocumentView* view = getView();
    if (!view || !m_pageField)
    {
        return;
    }

    const std::optional<PDFInteger> pageIndex = view->findPageIndex(m_pageField->text());
    if (!pageIndex)
    {
        // The field says the error, the panel stays open
        m_pageField->setInvalid(true, tr("The document has no page “%1”.").arg(m_pageField->text().trimmed().left(64)));
        m_pageField->selectAll();
        return;
    }

    QPointer<PDFAppDocumentView> guard(view);
    close();
    if (guard)
    {
        guard->goToPage(*pageIndex);
    }
}

void PDFAppDocumentGoToPanel::onAutoGenerateClicked()
{
    PDFAppDocumentView* view = getView();
    PDFAppContext* context = view ? view->getContext() : nullptr;
    if (!context || !context->getDatabase() || !m_autoGenerate)
    {
        return;
    }

    // The setting of package C (personal bookmarks); a refused value keeps the old one
    const PDFDatabaseError error = context->getSettings().setValue(PDFSettingKeys::bookmarksAutoGenerate(), m_autoGenerate->isChecked());
    if (error.isError())
    {
        qWarning("The setting of the generated bookmarks cannot be stored: %s", qPrintable(error.toString()));
    }
    updateState();
}

// ------------------------------------------------------ PDFAppDocumentSelectMenu

PDFAppDocumentSelectMenu::PDFAppDocumentSelectMenu(PDFAppDocumentView* view, QWidget* parent) :
    BaseClass(parent),
    m_view(view)
{
    setObjectName(QStringLiteral("selectMenu"));
    using C = PDFAppCommands;

    // Hand is the first row (owner 2026-10-25: the pan of the pages by a left drag of
    // the old applications); its row has no icon until "hand" exists in the library
    const std::pair<QString, QString> items[] =
    {
        { C::handTool(), PDFAppCommands::getHandIconName() },
        { C::selectText(), QStringLiteral("text-cursor") },
        { C::selectTable(), QStringLiteral("table") },
        { C::screenshot(), QStringLiteral("camera") },
        { C::extractImage(), QStringLiteral("image") }
    };

    for (const std::pair<QString, QString>& item : items)
    {
        QAction* action = addAction(PDFThemeIcons::getIcon(item.second), QString());
        action->setObjectName(item.first);
        action->setData(item.first);
        action->setCheckable(true);
        m_actions[item.first] = action;
    }

    // One tool is the choice, the handler keeps the exclusivity: the actions are
    // not in an exclusive group, so the check mark lies at the trailing side and
    // the icon stays (Menu item 198:5518)

    connect(this, &QMenu::aboutToShow, this, &PDFAppDocumentSelectMenu::onAboutToShow);
    connect(this, &QMenu::triggered, this, &PDFAppDocumentSelectMenu::onActionTriggered);
    retranslate();
}

QAction* PDFAppDocumentSelectMenu::getCommandAction(const QString& commandId) const
{
    auto it = m_actions.find(commandId);
    return it != m_actions.end() ? it->second.data() : nullptr;
}

void PDFAppDocumentSelectMenu::updateState()
{
    for (const auto& item : m_actions)
    {
        if (QAction* action = item.second.data())
        {
            const QSignalBlocker blocker(action);
            action->setEnabled(m_view && m_view->isCommandEnabled(item.first));
            action->setChecked(m_view && m_view->isCommandChecked(item.first));
        }
    }
}

void PDFAppDocumentSelectMenu::retranslate()
{
    using C = PDFAppCommands;
    const std::pair<QString, const char*> texts[] =
    {
        { C::handTool(), QT_TR_NOOP("Hand") },
        { C::selectText(), QT_TR_NOOP("Select text") },
        { C::selectTable(), QT_TR_NOOP("Select table") },
        { C::screenshot(), QT_TR_NOOP("Screenshot") },
        { C::extractImage(), QT_TR_NOOP("Extract image") }
    };

    for (const std::pair<QString, const char*>& text : texts)
    {
        if (QAction* action = getCommandAction(text.first))
        {
            action->setText(tr(text.second));
        }
    }
    setTitle(tr("Select"));
}

void PDFAppDocumentSelectMenu::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
}

void PDFAppDocumentSelectMenu::showEvent(QShowEvent* event)
{
    // The style has placed the window at its anchor (the event filter of the style
    // runs before this handler), the menu moves it below the toolbar
    BaseClass::showEvent(event);
    placeBelowToolbar();
}

void PDFAppDocumentSelectMenu::placeBelowToolbar()
{
    QWidget* button = parentWidget();
    const PDFAppDocumentToolbar* toolbar = m_view ? m_view->getToolbar() : nullptr;
    if (!button || !toolbar || !button->isVisible() || !toolbar->isVisible())
    {
        return;
    }

    // Only a menu below its button moves (a menu above the button at the bottom of
    // the screen, or at the pointer, stays)
    const QMargins margins = PDFWidgetStyle::getPopoverMargins(this);
    const int panelTop = y() + margins.top();
    const int buttonBottom = button->mapToGlobal(QPoint(0, button->height())).y();
    if (qAbs(panelTop - buttonBottom) > 2)
    {
        return;
    }

    const int targetTop = toolbar->mapToGlobal(QPoint(0, toolbar->height())).y() + PDFAppDocumentToolbar::PANEL_GAP;
    move(x(), targetTop - margins.top());
}

void PDFAppDocumentSelectMenu::onAboutToShow()
{
    // menu-select 198:5517: the panel is 232 px wide (the window holds the shadow too)
    const QMargins margins = PDFWidgetStyle::getPopoverMargins(this);
    setMinimumWidth(PANEL_WIDTH + margins.left() + margins.right());

    updateState();
}

void PDFAppDocumentSelectMenu::onActionTriggered(QAction* action)
{
    // The command runs from the event loop (the menu is closing now)
    if (action && m_view)
    {
        m_view->requestCommand(action->data().toString());
    }
    updateState();
}

}   // namespace pdf
