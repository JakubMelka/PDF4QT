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

#include "pdfcomponentwindow.h"

#include <QEvent>
#include <QPainter>

namespace pdf
{

PDFWindowButton::PDFWindowButton(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFWindowButton::PDFWindowButton(Kind kind, Platform platform, QWidget* parent) :
    BaseClass(parent),
    m_kind(PDFComponentPainter::normalized(kind)),
    m_platform(PDFComponentPainter::normalized(platform))
{
    init();
}

void PDFWindowButton::init()
{
    setAttribute(Qt::WA_Hover, true);

    // The buttons of a window are not in the chain of the focus, the window has its system menu
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    updateTexts();
}

void PDFWindowButton::setKind(Kind kind)
{
    const Kind normalizedKind = PDFComponentPainter::normalized(kind);
    if (m_kind != normalizedKind)
    {
        m_kind = normalizedKind;
        updateTexts();
        update();
    }
}

void PDFWindowButton::setPlatform(Platform platform)
{
    const Platform normalizedPlatform = PDFComponentPainter::normalized(platform);
    if (m_platform != normalizedPlatform)
    {
        m_platform = normalizedPlatform;
        updateTexts();
        updateGeometry();
        update();
    }
}

void PDFWindowButton::setActivity(Activity activity)
{
    switch (activity)
    {
        case Activity::FollowWindow:
        case Activity::Active:
        case Activity::Inactive:
            break;

        default:
            activity = Activity::FollowWindow;
            break;
    }

    if (m_activity != activity)
    {
        m_activity = activity;
        update();
    }
}

bool PDFWindowButton::isWindowActive() const
{
    switch (m_activity)
    {
        case Activity::Active:
            return true;
        case Activity::Inactive:
            return false;
        case Activity::FollowWindow:
            break;
    }

    return isActiveWindow();
}

void PDFWindowButton::setExternalHover(bool hover)
{
    if (m_externalHover != hover)
    {
        m_externalHover = hover;
        update();
    }
}

void PDFWindowButton::setExternalPressed(bool pressed)
{
    if (m_externalPressed != pressed)
    {
        m_externalPressed = pressed;
        update();
    }
}

PDFComponentPainter::State PDFWindowButton::getPaintedState() const
{
    QStyle::State flags = QStyle::State_None;
    if (isEnabled())
    {
        flags |= QStyle::State_Enabled;
    }
    if (underMouse() || m_externalHover)
    {
        flags |= QStyle::State_MouseOver;
    }
    if (isDown() || m_externalPressed)
    {
        flags |= QStyle::State_Sunken;
    }
    return PDFComponentPainter::State(flags, layoutDirection());
}

PDFComponentPainter::WindowButton PDFWindowButton::getPaintedButton() const
{
    return PDFComponentPainter::WindowButton{ m_kind, m_platform, isWindowActive() };
}

QString PDFWindowButton::getActionName(Kind kind, Platform platform)
{
    switch (PDFComponentPainter::normalized(kind))
    {
        case Kind::Minimize:
            return tr("Minimize");

        case Kind::Maximize:
            return PDFComponentPainter::normalized(platform) == Platform::MacOS ? tr("Zoom") : tr("Maximize");

        case Kind::Restore:
            return PDFComponentPainter::normalized(platform) == Platform::MacOS ? tr("Zoom") : tr("Restore");

        case Kind::Close:
            return tr("Close");
    }

    return QString();
}

void PDFWindowButton::updateTexts()
{
    // The text is only the name for assistive technologies (it is not drawn);
    // an ampersand of a translation must not create a mnemonic shortcut
    const QString name = getActionName(m_kind, m_platform);
    QString text = name;
    text.replace(QLatin1Char('&'), QStringLiteral("&&"));
    if (BaseClass::text() != text)
    {
        setText(text);
    }

    // Tooltips as the caption buttons of Windows; the traffic lights of macOS have none
    const QString toolTipText = m_platform == Platform::MacOS ? QString() : name;
    if (toolTip().isEmpty() || toolTip() == m_defaultToolTip)
    {
        setToolTip(toolTipText);
    }
    m_defaultToolTip = toolTipText;
}

QSize PDFWindowButton::sizeHint() const
{
    return PDFComponentPainter::getWindowButtonSize(m_platform);
}

QSize PDFWindowButton::minimumSizeHint() const
{
    return sizeHint();
}

void PDFWindowButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintWindowButton(&painter, QRectF(rect()), getPaintedButton(), getPaintedState());
}

void PDFWindowButton::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LanguageChange:
            updateTexts();
            break;

        case QEvent::ActivationChange:
        case QEvent::EnabledChange:
        case QEvent::LayoutDirectionChange:
            update();
            break;

        default:
            break;
    }
}

bool PDFWindowButton::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
            update();
            break;

        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
            // The window passes the change of its activity to its children by
            // these events (ActivationChange is sent to the window only); the
            // palettes of the groups are equal, so Qt itself repaints nothing
            if (m_activity == Activity::FollowWindow)
            {
                update();
            }
            break;

        default:
            break;
    }

    return BaseClass::event(event);
}

PDFWindowControls::PDFWindowControls(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFWindowControls::PDFWindowControls(Platform platform, QWidget* parent) :
    BaseClass(parent),
    m_platform(PDFComponentPainter::normalized(platform))
{
    init();
}

PDFWindowControls::~PDFWindowControls() = default;

void PDFWindowControls::init()
{
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // The connections have the group as their context, they are cut with it
    const Kind kinds[] = { Kind::Minimize, Kind::Maximize, Kind::Close };
    for (size_t i = 0; i < m_buttons.size(); ++i)
    {
        m_buttons[i] = new PDFWindowButton(kinds[i], m_platform, this);
    }
    connect(m_buttons[0], &QAbstractButton::clicked, this, &PDFWindowControls::onMinimizeClicked);
    connect(m_buttons[1], &QAbstractButton::clicked, this, &PDFWindowControls::onMaximizeClicked);
    connect(m_buttons[2], &QAbstractButton::clicked, this, &PDFWindowControls::onCloseClicked);

    updateAccessibleName();
    updateButtons();
}

int PDFWindowControls::getButtonIndex(Kind kind)
{
    switch (kind)
    {
        case Kind::Minimize:
            return 0;
        case Kind::Maximize:
        case Kind::Restore:
            return 1;
        case Kind::Close:
            return 2;
    }

    return -1;
}

void PDFWindowControls::setPlatform(Platform platform)
{
    const Platform normalizedPlatform = PDFComponentPainter::normalized(platform);
    if (m_platform != normalizedPlatform)
    {
        m_platform = normalizedPlatform;
        updateButtons();
        updateGeometry();
    }
}

void PDFWindowControls::setMaximized(bool maximized)
{
    if (m_maximized != maximized)
    {
        m_maximized = maximized;
        updateButtons();
    }
}

void PDFWindowControls::setResizable(bool resizable)
{
    if (m_resizable != resizable)
    {
        m_resizable = resizable;
        updateButtons();
    }
}

void PDFWindowControls::setActivity(Activity activity)
{
    switch (activity)
    {
        case Activity::FollowWindow:
        case Activity::Active:
        case Activity::Inactive:
            break;

        default:
            activity = Activity::FollowWindow;
            break;
    }

    if (m_activity != activity)
    {
        m_activity = activity;
        updateButtons();
    }
}

void PDFWindowControls::setSystemButtons(bool systemButtons)
{
    if (m_systemButtons != systemButtons)
    {
        m_systemButtons = systemButtons;
        updateButtons();
    }
}

bool PDFWindowControls::hasOwnButtons() const
{
    return !(m_systemButtons && m_platform == Platform::MacOS);
}

PDFWindowButton* PDFWindowControls::getButton(Kind kind) const
{
    const int index = getButtonIndex(kind);
    if (index < 0 || !hasOwnButtons())
    {
        return nullptr;
    }

    return m_buttons[index].data();
}

QList<PDFWindowButton*> PDFWindowControls::getButtons() const
{
    QList<PDFWindowButton*> buttons;
    if (!hasOwnButtons())
    {
        return buttons;
    }

    // The logical order of the platform (macOS: close, minimize, zoom)
    for (const Kind kind : PDFComponentPainter::getWindowControlsButtons(getPaintedControls()))
    {
        if (PDFWindowButton* button = getButton(kind))
        {
            buttons.append(button);
        }
    }
    return buttons;
}

QRect PDFWindowControls::getButtonRect(Kind kind) const
{
    // A button of a hidden group (or of a hidden parent of the group) is not
    // under the pointer, the window must not answer HTMAXBUTTON over it
    const PDFWindowButton* button = getButton(kind);
    const QWidget* windowWidget = window();
    if (!button || button->parentWidget() != this || !button->isVisibleTo(windowWidget))
    {
        return QRect();
    }

    return QRect(button->mapTo(windowWidget, QPoint(0, 0)), button->size());
}

std::optional<PDFWindowControls::Kind> PDFWindowControls::getButtonAt(const QPoint& windowPosition) const
{
    for (const PDFWindowButton* button : getButtons())
    {
        if (getButtonRect(button->getKind()).contains(windowPosition))
        {
            return button->getKind();
        }
    }

    return std::nullopt;
}

void PDFWindowControls::setHoveredButton(std::optional<Kind> kind)
{
    if (kind.has_value() && getButtonIndex(*kind) < 0)
    {
        kind.reset();
    }

    if (m_hoveredButton != kind)
    {
        m_hoveredButton = kind;
        updateButtons();
    }
}

void PDFWindowControls::setPressedButton(std::optional<Kind> kind)
{
    if (kind.has_value() && getButtonIndex(*kind) < 0)
    {
        kind.reset();
    }

    if (m_pressedButton != kind)
    {
        m_pressedButton = kind;
        updateButtons();
    }
}

bool PDFWindowControls::activate(Kind kind)
{
    const PDFWindowButton* button = getButton(kind);
    if (!button || !button->isEnabled() || button->isHidden())
    {
        return false;
    }

    // The slot of the signal may delete the group, nothing follows the emit
    switch (getButtonIndex(kind))
    {
        case 0:
            Q_EMIT minimizeRequested();
            return true;
        case 1:
            Q_EMIT maximizeRestoreRequested();
            return true;
        case 2:
            Q_EMIT closeRequested();
            return true;
        default:
            break;
    }

    return false;
}

PDFComponentPainter::WindowControls PDFWindowControls::getPaintedControls() const
{
    PDFComponentPainter::WindowControls controls;
    controls.platform = m_platform;
    controls.maximized = m_maximized;
    controls.resizable = m_resizable;
    controls.hovered = m_hoveredButton;
    controls.pressed = m_pressedButton;

    switch (m_activity)
    {
        case Activity::Active:
            controls.windowActive = true;
            break;
        case Activity::Inactive:
            controls.windowActive = false;
            break;
        case Activity::FollowWindow:
            controls.windowActive = isActiveWindow();
            break;
    }

    return controls;
}

QSize PDFWindowControls::sizeHint() const
{
    return PDFComponentPainter::getWindowControlsSize(m_platform);
}

QSize PDFWindowControls::minimumSizeHint() const
{
    return sizeHint();
}

void PDFWindowControls::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutButtons();
}

void PDFWindowControls::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LayoutDirectionChange:
            layoutButtons();
            break;

        case QEvent::LanguageChange:
            updateAccessibleName();
            break;

        default:
            break;
    }
}

void PDFWindowControls::updateButtons()
{
    const PDFComponentPainter::WindowControls controls = getPaintedControls();
    const QList<Kind> kinds = PDFComponentPainter::getWindowControlsButtons(controls);
    const bool isShown = hasOwnButtons();

    for (const Kind kind : kinds)
    {
        const int index = getButtonIndex(kind);
        PDFWindowButton* button = index >= 0 ? m_buttons[index].data() : nullptr;
        if (!button)
        {
            // Deleted by the application
            continue;
        }

        button->setPlatform(m_platform);
        button->setKind(kind);
        button->setActivity(m_activity);
        button->setExternalHover(m_hoveredButton.has_value() && getButtonIndex(*m_hoveredButton) == index);
        button->setExternalPressed(m_pressedButton.has_value() && getButtonIndex(*m_pressedButton) == index);

        // Only the system buttons of macOS hide the buttons, a button hidden by
        // the application stays hidden
        if (!isShown && !button->isHidden())
        {
            m_hiddenByGroup[index] = true;
            button->setHidden(true);
        }
        else if (isShown && m_hiddenByGroup[index])
        {
            m_hiddenByGroup[index] = false;
            button->setHidden(false);
        }

        // Only the middle button follows the group (a window of a fixed size),
        // the application may disable the others itself
        if (index == 1)
        {
            button->setEnabled(m_resizable);
        }
    }

    layoutButtons();
}

void PDFWindowControls::layoutButtons()
{
    const PDFComponentPainter::WindowControls controls = getPaintedControls();
    const PDFComponentPainter::State state(QStyle::State_Enabled, layoutDirection());
    const QRectF area(QPointF(0, 0), QSizeF(size()));

    for (const Kind kind : PDFComponentPainter::getWindowControlsButtons(controls))
    {
        const int index = getButtonIndex(kind);
        PDFWindowButton* button = index >= 0 ? m_buttons[index].data() : nullptr;
        if (button && button->parentWidget() == this)
        {
            button->setGeometry(PDFComponentPainter::getWindowControlsButtonRect(area, controls, kind, state).toRect());
        }
    }
}

void PDFWindowControls::updateAccessibleName()
{
    // A name set by the application is kept
    const QString name = tr("Window controls");
    if (accessibleName().isEmpty() || accessibleName() == m_defaultName)
    {
        setAccessibleName(name);
    }
    m_defaultName = name;
}

void PDFWindowControls::onMinimizeClicked()
{
    Q_EMIT minimizeRequested();
}

void PDFWindowControls::onMaximizeClicked()
{
    Q_EMIT maximizeRestoreRequested();
}

void PDFWindowControls::onCloseClicked()
{
    Q_EMIT closeRequested();
}

}   // namespace pdf
