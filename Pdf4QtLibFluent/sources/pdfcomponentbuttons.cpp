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

#include "pdfcomponentbuttons.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QEvent>
#include <QFocusFrame>
#include <QGraphicsEffect>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStyle>
#include <QtMath>

namespace pdf
{

PDFIconButton::PDFIconButton(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFIconButton::PDFIconButton(const QString& iconName, Size size, QWidget* parent) :
    BaseClass(parent),
    m_iconName(iconName),
    m_size(size)
{
    init();
}

void PDFIconButton::init()
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // The focus ring of the style lies outside of the shape of this radius
    setProperty(PDFWidgetStyleProperties::RADIUS, getRadius(m_size));
}

void PDFIconButton::setIconName(const QString& iconName)
{
    if (m_iconName != iconName)
    {
        m_iconName = iconName;
        update();
    }
}

void PDFIconButton::setButtonSize(Size size)
{
    if (m_size != size)
    {
        m_size = size;
        setProperty(PDFWidgetStyleProperties::RADIUS, getRadius(m_size));
        updateGeometry();
        update();
    }
}

void PDFIconButton::setIconColor(PDFThemeColors::Color color)
{
    // An invalid value would be drawn in the leak color, the color of the state is used instead
    if (static_cast<int>(color) < 0 || static_cast<int>(color) >= static_cast<int>(PDFThemeColors::ColorCount))
    {
        resetIconColor();
        return;
    }

    if (m_iconColor != color)
    {
        m_iconColor = color;
        update();
    }
}

void PDFIconButton::resetIconColor()
{
    if (m_iconColor.has_value())
    {
        m_iconColor.reset();
        update();
    }
}

PDFThemeColors::Color PDFIconButton::getCurrentIconColor() const
{
    return PDFComponentPainter::getIconButtonColor(getPaintedButton(), getPaintedState());
}

QSize PDFIconButton::sizeHint() const
{
    const int extent = getExtent(m_size);
    return QSize(extent, extent);
}

QSize PDFIconButton::minimumSizeHint() const
{
    return sizeHint();
}

int PDFIconButton::getExtent(Size size)
{
    switch (size)
    {
        case Size::Small:
            return 24;
        case Size::Medium:
            return 28;
        case Size::TabClose:
            return 20;
    }

    return 24;
}

int PDFIconButton::getIconExtent(Size size)
{
    switch (size)
    {
        case Size::Small:
            return 14;
        case Size::Medium:
            return 16;
        case Size::TabClose:
            return 12;
    }

    return 14;
}

qreal PDFIconButton::getRadius(Size size)
{
    return size == Size::TabClose ? 4.0 : 6.0;
}

void PDFIconButton::keyPressEvent(QKeyEvent* event)
{
    // Enter and Return press the button as Space does (as the buttons of the
    // components); the key does not go to the default button of a dialog
    const bool isEnter = (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
    if (isEnter)
    {
        if (!event->isAutoRepeat() && isEnabled())
        {
            click();
        }
        event->accept();
        return;
    }

    BaseClass::keyPressEvent(event);
}

void PDFIconButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Hover and press add a background, the focus ring is drawn by the style outside
    PDFComponentPainter::paintIconButton(&painter, QRectF(rect()), getPaintedButton(), getPaintedState());
}

PDFComponentPainter::IconButton PDFIconButton::getPaintedButton() const
{
    PDFComponentPainter::IconButton button;
    button.iconName = m_iconName;
    button.extent = getExtent(m_size);
    button.iconExtent = getIconExtent(m_size);
    button.radius = getRadius(m_size);
    button.iconColor = m_iconColor;
    return button;
}

PDFComponentPainter::State PDFIconButton::getPaintedState() const
{
    QStyle::State flags = QStyle::State_None;
    if (isEnabled())
    {
        flags |= QStyle::State_Enabled;
    }
    if (underMouse())
    {
        flags |= QStyle::State_MouseOver;
    }
    if (isDown())
    {
        flags |= QStyle::State_Sunken;
    }
    return PDFComponentPainter::State(flags, layoutDirection());
}

/// Geometry of the stamp chip
class PDFStampChipHelper
{
public:
    PDFStampChipHelper() = delete;

    static constexpr int HEIGHT = 26;
    static constexpr int PADDING = 11;      ///< Border 1 and padding 10
    static constexpr qreal RADIUS = 6.0;
};

PDFStampChip::PDFStampChip(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFStampChip::PDFStampChip(const QString& text, PDFAccentHue hue, QWidget* parent) :
    BaseClass(parent),
    m_hue(hue)
{
    setText(text);
    init();
}

void PDFStampChip::init()
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setProperty(PDFWidgetStyleProperties::RADIUS, PDFStampChipHelper::RADIUS);
}

void PDFStampChip::setHue(PDFAccentHue hue)
{
    if (m_hue != hue)
    {
        m_hue = hue;
        update();
    }
}

QFont PDFStampChip::getLabelFont()
{
    QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionSemiBold);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
    return font;
}

QSize PDFStampChip::sizeHint() const
{
    const qreal textWidth = PDFStylePainter::textWidth(text().toUpper(), getLabelFont());
    return QSize(qRound(textWidth) + 2 * PDFStampChipHelper::PADDING, PDFStampChipHelper::HEIGHT);
}

QSize PDFStampChip::minimumSizeHint() const
{
    return sizeHint();
}

void PDFStampChip::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
    {
        updateGeometry();
    }
}

void PDFStampChip::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    using Role = PDFAccentHueColors::Role;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF shape(rect());
    const PDFStylePainter::Radii radii(PDFStampChipHelper::RADIUS);

    Role fill = Role::Background;
    if (isEnabled() && isDown())
    {
        fill = Role::BackgroundPressed;
    }
    else if (isEnabled() && underMouse())
    {
        fill = Role::BackgroundHover;
    }

    PDFStylePainter::fill(&painter, shape, radii, PDFAccentHueColors::getColor(m_hue, fill));

    // The focus ring outside of the shape replaces the border of the hue
    if (!PDFWidgetStyle::isFocusRingVisible(this))
    {
        PDFStylePainter::strokeInside(&painter, shape, radii, PDFAccentHueColors::getColor(m_hue, Role::BorderStrong), 1.0);
    }

    const PDFThemeColors::Color textColor = isEnabled() ? PDFAccentHueColors::getColor(m_hue, Role::Icon) : PDFThemeColors::TextDisabled;
    const QRectF textRect = shape.adjusted(PDFStampChipHelper::PADDING, 0.0, -PDFStampChipHelper::PADDING, 0.0);
    PDFStylePainter::text(&painter, textRect, Qt::AlignCenter | Qt::TextSingleLine, text(), getLabelFont(), PDFStylePainter::color(textColor));
}

#if QT_CONFIG(accessibility)
/// Accessible interface of the buttons of the components. The role follows the
/// checkable behaviour of the button, the name falls back to the default name
/// of the button (the title, the name of a color, the state of a star).
class PDFComponentButtonAccessible : public QAccessibleWidget
{
public:
    explicit PDFComponentButtonAccessible(PDFComponentButton* button) :
        QAccessibleWidget(button, QAccessible::Button)
    {

    }

    PDFComponentButton* button() const { return qobject_cast<PDFComponentButton*>(widget()); }

    virtual QAccessible::Role role() const override
    {
        const PDFComponentButton* componentButton = button();
        if (!componentButton)
        {
            return QAccessible::Button;
        }

        if (!componentButton->isInteractive())
        {
            return QAccessible::StaticText;
        }

        switch (componentButton->getSelectionBehavior())
        {
            case PDFComponentButton::SelectionBehavior::Exclusive:
            case PDFComponentButton::SelectionBehavior::ExclusiveStatic:
            case PDFComponentButton::SelectionBehavior::Deselectable:
                return QAccessible::RadioButton;

            case PDFComponentButton::SelectionBehavior::None:
            case PDFComponentButton::SelectionBehavior::Toggle:
                break;
        }

        return QAccessible::Button;
    }

    virtual QAccessible::State state() const override
    {
        QAccessible::State state = QAccessibleWidget::state();

        if (const PDFComponentButton* componentButton = button())
        {
            state.checkable = componentButton->isCheckable();
            state.checked = componentButton->isCheckable() && componentButton->isChecked();
            state.pressed = componentButton->isDown();

            // The menu button opens a menu
            if (const PDFMoreButton* moreButton = qobject_cast<const PDFMoreButton*>(componentButton))
            {
                state.hasPopup = moreButton->getMenu() != nullptr;
            }
        }

        return state;
    }

    virtual QString text(QAccessible::Text textType) const override
    {
        const PDFComponentButton* componentButton = button();

        if (componentButton && textType == QAccessible::Name)
        {
            return componentButton->getAccessibleText();
        }

        if (componentButton && textType == QAccessible::Description && componentButton->accessibleDescription().isEmpty())
        {
            // The tooltip describes the button, unless it only repeats its name ("Unpin")
            const QString toolTip = componentButton->toolTip();
            return toolTip == componentButton->getAccessibleText() ? QString() : toolTip;
        }

        return QAccessibleWidget::text(textType);
    }

    virtual QStringList actionNames() const override
    {
        const PDFComponentButton* componentButton = button();
        if (!componentButton || !componentButton->isActivatable())
        {
            return QAccessibleWidget::actionNames();
        }

        QStringList names;
        names << (componentButton->isCheckable() ? toggleAction() : pressAction());
        names << QAccessibleWidget::actionNames();
        return names;
    }

    virtual void doAction(const QString& actionName) override
    {
        PDFComponentButton* componentButton = button();

        if (componentButton && (actionName == pressAction() || actionName == toggleAction()))
        {
            // The selected item of a static choice and a label do nothing
            if (componentButton->isActivatable())
            {
                componentButton->click();
            }
            return;
        }

        QAccessibleWidget::doAction(actionName);
    }

    static QAccessibleInterface* create(const QString& className, QObject* object)
    {
        if (className != QLatin1String(PDFComponentButton::staticMetaObject.className()))
        {
            return nullptr;
        }

        PDFComponentButton* componentButton = qobject_cast<PDFComponentButton*>(object);
        return componentButton ? new PDFComponentButtonAccessible(componentButton) : nullptr;
    }
};

/// Registration of the accessible interface of the buttons of the components.
/// The factory is removed, when the library is unloaded while the application
/// still runs (a library loaded by a plugin); at the exit of the application
/// the registry of Qt is not touched any more (the application object is gone).
class PDFComponentButtonAccessibleRegistration
{
public:
    PDFComponentButtonAccessibleRegistration()
    {
        QAccessible::installFactory(&PDFComponentButtonAccessible::create);
    }

    ~PDFComponentButtonAccessibleRegistration()
    {
        if (QCoreApplication::instance())
        {
            QAccessible::removeFactory(&PDFComponentButtonAccessible::create);
        }
    }

    PDFComponentButtonAccessibleRegistration(const PDFComponentButtonAccessibleRegistration&) = delete;
    PDFComponentButtonAccessibleRegistration& operator=(const PDFComponentButtonAccessibleRegistration&) = delete;
};
#endif

/// Helper of the buttons of the components
class PDFComponentButtonHelper
{
public:
    PDFComponentButtonHelper() = delete;

    /// Dynamic property of a nested widget, which got Qt::WA_NoMousePropagation from the button
    static constexpr const char* NO_MOUSE_PROPAGATION_PROPERTY = "_pdf4qt_trailingNoMousePropagation";

    static void installAccessibility()
    {
#if QT_CONFIG(accessibility)
        // Installed once, removed when the library is unloaded
        static PDFComponentButtonAccessibleRegistration registration;
        Q_UNUSED(registration);
#endif
    }

    /// Returns the size of a visible child widget, which is placed by its parent
    static QSize getChildSize(const QWidget* widget)
    {
        QSize size = widget->sizeHint();
        if (!size.isValid())
        {
            size = widget->size();
        }

        return size.expandedTo(widget->minimumSize()).boundedTo(widget->maximumSize()).expandedTo(QSize(0, 0));
    }

    static bool isEnterKey(const QKeyEvent* event)
    {
        return (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
               (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier;
    }

    /// Keys, which press a button (the default of the platform themes)
    static bool isButtonPressKey(const QKeyEvent* event)
    {
        return event->key() == Qt::Key_Space || event->key() == Qt::Key_Select;
    }

    /// Returns true, if the widget is a nested widget placed by the button
    static bool isPlacedTrailingWidget(const QPointer<QWidget>& widget, const QWidget* button)
    {
        return widget && widget->parentWidget() == button && !widget->isHidden();
    }
};

PDFComponentButton::PDFComponentButton(FocusRing focusRing, QWidget* parent) :
    BaseClass(parent),
    m_focusRing(focusRing == FocusRing::Outside ? FocusRing::Outside : FocusRing::Inside)
{
    PDFComponentButtonHelper::installAccessibility();

    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);

    if (m_focusRing == FocusRing::Outside)
    {
        // The focus frame of the style draws the ring only around a widget with
        // the radius; a subclass, which does not set its own, has a sharp ring
        setProperty(PDFWidgetStyleProperties::RADIUS, 0.0);
    }

    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFComponentButton::onThemeChanged);
}

PDFComponentButton::~PDFComponentButton()
{
    for (const QPointer<QWidget>& widget : std::as_const(m_trailingWidgets))
    {
        if (widget)
        {
            widget->removeEventFilter(this);
        }
    }
}

void PDFComponentButton::setText(const QString& text)
{
    if (text == this->text())
    {
        return;
    }

    // QAbstractButton replaces the shortcut by the mnemonic of the text. The text
    // is data, so its mnemonic is dropped; a shortcut of the application is kept.
    const QKeySequence previousShortcut = shortcut();
    const bool isPreviousMnemonic = previousShortcut == QKeySequence::mnemonic(this->text());
    BaseClass::setText(text);
    setShortcut(isPreviousMnemonic ? QKeySequence() : previousShortcut);
}

void PDFComponentButton::removeMnemonicShortcut()
{
    const QKeySequence mnemonic = QKeySequence::mnemonic(text());
    if (!mnemonic.isEmpty() && shortcut() == mnemonic)
    {
        setShortcut(QKeySequence());
    }
}

void PDFComponentButton::setFocusRingRadius(qreal radius)
{
    // A negative or invalid radius is a sharp corner
    const qreal validRadius = (qIsFinite(radius) && radius > 0.0) ? radius : 0.0;

    if (qFuzzyCompare(m_focusRingRadius + 1.0, validRadius + 1.0))
    {
        return;
    }

    m_focusRingRadius = validRadius;

    if (m_focusRing == FocusRing::Outside)
    {
        // The focus frame of the style draws the ring around the shape of this radius
        setProperty(PDFWidgetStyleProperties::RADIUS, validRadius);
        updateFocusFrame();
    }

    update();
}

void PDFComponentButton::updateFocusFrame()
{
    // A ring, which is shown, takes the new shape
    const QList<QFocusFrame*> frames = window()->findChildren<QFocusFrame*>();
    for (QFocusFrame* frame : frames)
    {
        if (frame->widget() == this)
        {
            frame->update();
        }
    }
}

void PDFComponentButton::setSelectionBehavior(SelectionBehavior behavior)
{
    switch (behavior)
    {
        case SelectionBehavior::None:
        case SelectionBehavior::Toggle:
        case SelectionBehavior::Exclusive:
        case SelectionBehavior::ExclusiveStatic:
        case SelectionBehavior::Deselectable:
            break;

        default:
            behavior = SelectionBehavior::None;
            break;
    }

    if (m_selectionBehavior == behavior)
    {
        return;
    }

    m_selectionBehavior = behavior;

    if (behavior == SelectionBehavior::None)
    {
        // Unchecked with the signal toggled, also as the checked button of an
        // exclusive group (QAbstractButton::setCheckable would clear the state silently)
        QPointer<PDFComponentButton> guard(this);
        uncheck();
        if (!guard)
        {
            return;
        }
    }

    // The exclusivity comes from an exclusive QButtonGroup only (the auto
    // exclusivity of Qt groups all buttons anywhere below the parent widget)
    setCheckable(behavior != SelectionBehavior::None);
    update();
}

void PDFComponentButton::uncheck()
{
    if (!isCheckable() || !isChecked())
    {
        return;
    }

    // Qt refuses to uncheck the checked button of an exclusive group, the
    // exclusivity is lifted for the change and restored after it
    QPointer<PDFComponentButton> guard(this);
    QPointer<QButtonGroup> buttonGroup = group();
    const bool isGroupExclusive = buttonGroup && buttonGroup->exclusive();
    const bool isAutoExclusive = !buttonGroup && autoExclusive();

    if (isGroupExclusive)
    {
        buttonGroup->setExclusive(false);
    }
    if (isAutoExclusive)
    {
        setAutoExclusive(false);
    }

    setChecked(false);

    if (isGroupExclusive && buttonGroup)
    {
        buttonGroup->setExclusive(true);
    }
    if (isAutoExclusive && guard)
    {
        setAutoExclusive(true);
    }
}

bool PDFComponentButton::isActivatable() const
{
    const bool isStaticSelection = m_selectionBehavior == SelectionBehavior::ExclusiveStatic && isCheckable() && isChecked();
    return isEnabled() && isInteractive() && !isStaticSelection;
}

PDFComponentPainter::State PDFComponentButton::getComponentState() const
{
    QStyle::State flags = QStyle::State_None;
    const bool enabled = isEnabled();
    const bool checked = isCheckable() && isChecked();

    if (enabled)
    {
        flags |= QStyle::State_Enabled;
    }

    if (isActiveWindow())
    {
        flags |= QStyle::State_Active;
    }

    // A disabled button and the selected item of a static group do not react to the pointer
    const bool reactsToPointer = isActivatable();
    if (reactsToPointer && underMouse())
    {
        flags |= QStyle::State_MouseOver;
    }
    if (reactsToPointer && isDown())
    {
        flags |= QStyle::State_Sunken;
    }

    if (isCheckable())
    {
        flags |= checked ? QStyle::State_On : QStyle::State_Off;
    }

    if (m_focusRing == FocusRing::Inside && PDFWidgetStyle::isFocusRingVisible(this))
    {
        flags |= QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
    }

    return PDFComponentPainter::State(flags, layoutDirection());
}

QString PDFComponentButton::getAccessibleText() const
{
    const QString name = accessibleName();
    return name.isEmpty() ? getDefaultAccessibleName() : name;
}

QString PDFComponentButton::getDefaultAccessibleName() const
{
    // The text is data, it is announced as it is drawn ("Protect & Sign")
    return text();
}

void PDFComponentButton::addTrailingWidget(QWidget* widget)
{
    if (!widget || widget == this || m_trailingWidgets.contains(widget) || widget->isAncestorOf(this))
    {
        return;
    }

    // A widget hidden by the application stays hidden; QWidget::setParent hides
    // the widget, which is shown otherwise
    const bool isExplicitlyHidden = widget->isHidden() && widget->testAttribute(Qt::WA_WState_ExplicitShowHide);
    if (widget->parentWidget() != this)
    {
        widget->setParent(this);
    }
    if (!isExplicitlyHidden)
    {
        widget->show();
    }

    // A click on a disabled nested widget is not a click on the button
    if (!widget->testAttribute(Qt::WA_NoMousePropagation))
    {
        widget->setAttribute(Qt::WA_NoMousePropagation, true);
        widget->setProperty(PDFComponentButtonHelper::NO_MOUSE_PROPAGATION_PROPERTY, true);
    }

    widget->installEventFilter(this);
    m_trailingWidgets.append(widget);
    requestTrailingLayout();
}

void PDFComponentButton::removeTrailingWidget(QWidget* widget)
{
    if (!widget || !m_trailingWidgets.removeOne(widget))
    {
        return;
    }

    widget->removeEventFilter(this);
    if (widget->property(PDFComponentButtonHelper::NO_MOUSE_PROPAGATION_PROPERTY).toBool())
    {
        widget->setAttribute(Qt::WA_NoMousePropagation, false);
        widget->setProperty(PDFComponentButtonHelper::NO_MOUSE_PROPAGATION_PROPERTY, QVariant());
    }
    requestTrailingLayout();
}

void PDFComponentButton::pruneTrailingWidgets(const QObject* removedChild)
{
    // Deleted widgets, the removed child and widgets of another parent are forgotten
    bool isChanged = false;
    for (qsizetype i = m_trailingWidgets.size() - 1; i >= 0; --i)
    {
        const QPointer<QWidget>& widget = m_trailingWidgets.at(i);
        if (!widget)
        {
            m_trailingWidgets.removeAt(i);
            isChanged = true;
        }
        else if (widget.data() == removedChild || widget->parentWidget() != this)
        {
            QWidget* removedWidget = widget.data();
            removeTrailingWidget(removedWidget);
            isChanged = true;
        }
    }

    if (isChanged)
    {
        requestTrailingLayout();
    }
}

QList<QWidget*> PDFComponentButton::getTrailingWidgets() const
{
    QList<QWidget*> widgets;
    for (const QPointer<QWidget>& widget : m_trailingWidgets)
    {
        if (widget && widget->parentWidget() == this)
        {
            widgets.append(widget.data());
        }
    }
    return widgets;
}

void PDFComponentButton::setTrailingMargins(const QMargins& margins)
{
    if (m_trailingMargins != margins)
    {
        m_trailingMargins = margins;
        requestTrailingLayout();
    }
}

void PDFComponentButton::setTrailingSpacing(int spacing)
{
    spacing = qMax(0, spacing);
    if (m_trailingSpacing != spacing)
    {
        m_trailingSpacing = spacing;
        requestTrailingLayout();
    }
}

void PDFComponentButton::setTrailingOrientation(Qt::Orientation orientation)
{
    orientation = orientation == Qt::Vertical ? Qt::Vertical : Qt::Horizontal;
    if (m_trailingOrientation != orientation)
    {
        m_trailingOrientation = orientation;
        requestTrailingLayout();
    }
}

QRect PDFComponentButton::getTrailingRect() const
{
    int width = 0;
    int height = 0;
    int count = 0;

    for (const QPointer<QWidget>& widget : m_trailingWidgets)
    {
        if (!PDFComponentButtonHelper::isPlacedTrailingWidget(widget, this))
        {
            continue;
        }

        const QSize size = PDFComponentButtonHelper::getChildSize(widget);
        if (m_trailingOrientation == Qt::Horizontal)
        {
            width += size.width() + (count > 0 ? m_trailingSpacing : 0);
            height = qMax(height, size.height());
        }
        else
        {
            width = qMax(width, size.width());
            height += size.height() + (count > 0 ? m_trailingSpacing : 0);
        }
        ++count;
    }

    if (count == 0)
    {
        return QRect();
    }

    const QRect area = rect().marginsRemoved(m_trailingMargins);
    const int left = area.right() + 1 - width;
    const int top = m_trailingOrientation == Qt::Horizontal ? area.top() + (area.height() - height) / 2 : area.top();
    return QRect(left, top, width, height);
}

QRect PDFComponentButton::getContentRect(const QMargins& padding, int trailingGap) const
{
    QRect content = rect().marginsRemoved(padding);
    const QRect trailingRect = getTrailingRect();

    if (!trailingRect.isNull())
    {
        content.setRight(qMin(content.right(), trailingRect.left() - 1 - qMax(0, trailingGap)));
    }

    if (content.width() < 0 || content.height() < 0)
    {
        content.setSize(content.size().expandedTo(QSize(0, 0)));
    }

    return content;
}

void PDFComponentButton::layoutTrailingWidgets()
{
    const QRect trailingRect = getTrailingRect();
    if (trailingRect.isNull())
    {
        return;
    }

    int position = m_trailingOrientation == Qt::Horizontal ? trailingRect.left() : trailingRect.top();
    for (const QPointer<QWidget>& widget : std::as_const(m_trailingWidgets))
    {
        if (!PDFComponentButtonHelper::isPlacedTrailingWidget(widget, this))
        {
            continue;
        }

        const QSize size = PDFComponentButtonHelper::getChildSize(widget);
        QRect logical;
        if (m_trailingOrientation == Qt::Horizontal)
        {
            logical = QRect(position, trailingRect.top() + (trailingRect.height() - size.height()) / 2, size.width(), size.height());
            position += size.width() + m_trailingSpacing;
        }
        else
        {
            logical = QRect(trailingRect.left() + (trailingRect.width() - size.width()) / 2, position, size.width(), size.height());
            position += size.height() + m_trailingSpacing;
        }

        widget->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logical));
    }
}

void PDFComponentButton::requestTrailingLayout()
{
    layoutTrailingWidgets();
    update();

    // The size hint of a subclass may include the nested widgets
    const QSize trailingSize = getTrailingRect().size();
    if (trailingSize != m_trailingSize)
    {
        m_trailingSize = trailingSize;
        updateGeometry();
    }
}

void PDFComponentButton::replaceAccessibleDescription(const QString& previousText, const QString& text)
{
    if (accessibleDescription().isEmpty() || accessibleDescription() == previousText)
    {
        setAccessibleDescription(text);
    }
}

void PDFComponentButton::replaceToolTip(const QString& previousText, const QString& text)
{
    if (toolTip().isEmpty() || toolTip() == previousText)
    {
        setToolTip(text);
    }
}

QSize PDFComponentButton::sizeHint() const
{
    return getContentSizeHint().expandedTo(QSize(0, 0));
}

QSize PDFComponentButton::minimumSizeHint() const
{
    return getContentMinimumSizeHint().expandedTo(QSize(0, 0));
}

void PDFComponentButton::onThemeChanged()
{
    // Colors and fonts are taken at the time of drawing, the size may change with the fonts
    updateGeometry();
    requestTrailingLayout();
}

bool PDFComponentButton::event(QEvent* event)
{
    switch (event->type())
    {
        case QEvent::Enter:
        case QEvent::Leave:
        case QEvent::HoverEnter:
        case QEvent::HoverLeave:
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            update();
            break;

        case QEvent::LayoutRequest:
            // A nested widget has changed its size
            requestTrailingLayout();
            break;

        case QEvent::Show:
            // Nested widgets may have changed their sizes, while the button was hidden
            layoutTrailingWidgets();
            break;

        case QEvent::ChildRemoved:
            // A nested widget has been deleted or moved to another parent
            pruneTrailingWidgets(static_cast<QChildEvent*>(event)->child());
            break;

        case QEvent::Polish:
            // The text may have been set through QAbstractButton::setText
            removeMnemonicShortcut();
            break;

        case QEvent::Shortcut:
        {
            // The mnemonic of a text set through QAbstractButton::setText does nothing
            const QShortcutEvent* shortcutEvent = static_cast<const QShortcutEvent*>(event);
            const QKeySequence mnemonic = QKeySequence::mnemonic(text());
            if (!mnemonic.isEmpty() && shortcutEvent->key() == mnemonic && shortcut() == mnemonic)
            {
                setShortcut(QKeySequence());
                event->accept();
                return true;
            }
            break;
        }

        default:
            break;
    }

    return BaseClass::event(event);
}

bool PDFComponentButton::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
        case QEvent::ShowToParent:
        case QEvent::HideToParent:
        case QEvent::LayoutRequest:
            if (QWidget* widget = qobject_cast<QWidget*>(watched); widget && m_trailingWidgets.contains(widget))
            {
                requestTrailingLayout();
            }
            break;

        case QEvent::ParentChange:
            if (QWidget* widget = qobject_cast<QWidget*>(watched); widget && widget->parentWidget() != this)
            {
                removeTrailingWidget(widget);
            }
            break;

        default:
            break;
    }

    return BaseClass::eventFilter(watched, event);
}

void PDFComponentButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintContent(&painter, rect(), getComponentState());
}

void PDFComponentButton::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutTrailingWidgets();
}

void PDFComponentButton::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    switch (event->type())
    {
        case QEvent::LayoutDirectionChange:
            requestTrailingLayout();
            break;

        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::LanguageChange:
            updateGeometry();
            requestTrailingLayout();
            break;

        case QEvent::EnabledChange:
        case QEvent::ActivationChange:
            update();
            break;

        default:
            break;
    }
}

void PDFComponentButton::keyPressEvent(QKeyEvent* event)
{
    const bool isEnter = PDFComponentButtonHelper::isEnterKey(event);
    const bool isPress = PDFComponentButtonHelper::isButtonPressKey(event);

    if ((isEnter || isPress) && isInteractive() && !isActivatable())
    {
        // The selected item of a static choice (or a disabled button) does nothing,
        // the key does not go to the default button of a dialog either
        event->accept();
        return;
    }

    if (isPress && !isInteractive())
    {
        // A label is not pressed by Space
        event->ignore();
        return;
    }

    const bool isArrow = event->key() == Qt::Key_Up || event->key() == Qt::Key_Down || event->key() == Qt::Key_Left || event->key() == Qt::Key_Right;
    if (isArrow && !isInteractive())
    {
        // A label does not move the focus by the arrows (QAbstractButton would);
        // an arrow ignored by a nested field (a line edit) would take the focus
        // out of it. The key goes on to the parent widget.
        event->ignore();
        return;
    }

    // Enter and Return activate the button as Space does
    if (isEnter && isInteractive())
    {
        if (!event->isAutoRepeat())
        {
            click();
        }
        event->accept();
        return;
    }

    BaseClass::keyPressEvent(event);
}

QPointer<QButtonGroup> PDFComponentButton::liftGroupExclusivity()
{
    // Qt keeps the checked button of an exclusive group checked on a click by
    // the mouse or by Space (QAbstractButtonPrivate::click does not call
    // nextCheckState then). For the time of the click of the selected
    // Deselectable button the group is not exclusive; nextCheckState restores
    // it at once after the button is unchecked.
    QButtonGroup* buttonGroup = group();
    if (m_selectionBehavior != SelectionBehavior::Deselectable || !isCheckable() || !isChecked() || !buttonGroup || !buttonGroup->exclusive() || !isActivatable())
    {
        return nullptr;
    }

    m_liftedGroup = buttonGroup;
    buttonGroup->setExclusive(false);
    return m_liftedGroup;
}

void PDFComponentButton::keyReleaseEvent(QKeyEvent* event)
{
    QPointer<PDFComponentButton> guard(this);
    QPointer<QButtonGroup> liftedGroup = (PDFComponentButtonHelper::isButtonPressKey(event) && isDown()) ? liftGroupExclusivity() : nullptr;

    BaseClass::keyReleaseEvent(event);

    if (liftedGroup && (!guard || guard->m_liftedGroup == liftedGroup))
    {
        liftedGroup->setExclusive(true);
    }
    if (guard)
    {
        m_liftedGroup = nullptr;
    }
}

void PDFComponentButton::mouseReleaseEvent(QMouseEvent* event)
{
    QPointer<PDFComponentButton> guard(this);
    QPointer<QButtonGroup> liftedGroup = (event->button() == Qt::LeftButton && isDown()) ? liftGroupExclusivity() : nullptr;

    BaseClass::mouseReleaseEvent(event);

    if (liftedGroup && (!guard || guard->m_liftedGroup == liftedGroup))
    {
        liftedGroup->setExclusive(true);
    }
    if (guard)
    {
        m_liftedGroup = nullptr;
    }
}

bool PDFComponentButton::hitButton(const QPoint& pos) const
{
    // The selected item of a static choice and a label ignore the mouse
    return isActivatable() && BaseClass::hitButton(pos);
}

void PDFComponentButton::nextCheckState()
{
    if (!isInteractive())
    {
        return;
    }

    if (!isCheckable() || !isChecked())
    {
        BaseClass::nextCheckState();
        return;
    }

    // An exclusive group unchecks its previous button by this function, when
    // another button of the group is checked
    QButtonGroup* buttonGroup = group();
    if (buttonGroup && buttonGroup->exclusive() && buttonGroup->checkedButton() != this)
    {
        BaseClass::nextCheckState();
        return;
    }

    switch (m_selectionBehavior)
    {
        case SelectionBehavior::Exclusive:
        case SelectionBehavior::ExclusiveStatic:
            // A click on the selected item keeps it selected (also in a group,
            // which is not exclusive)
            return;

        case SelectionBehavior::Deselectable:
        {
            QPointer<QButtonGroup> liftedGroup = m_liftedGroup;
            m_liftedGroup = nullptr;

            uncheck();

            // The exclusivity lifted for the click is restored before the
            // signals released and clicked
            if (liftedGroup)
            {
                liftedGroup->setExclusive(true);
            }
            return;
        }

        case SelectionBehavior::None:
        case SelectionBehavior::Toggle:
            break;
    }

    BaseClass::nextCheckState();
}

PDFToolBadge::PDFToolBadge(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

PDFToolBadge::PDFToolBadge(const QString& iconName, PDFAccentHue hue, BadgeSize size, QWidget* parent) :
    PDFToolBadge(parent)
{
    m_badge.iconName = iconName;
    m_badge.hue = PDFComponentPainter::normalized(hue);
    m_badge.size = PDFComponentPainter::normalized(size);
}

void PDFToolBadge::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_badge.hue != hue)
    {
        m_badge.hue = hue;
        update();
    }
}

void PDFToolBadge::setIconName(const QString& iconName)
{
    if (m_badge.iconName != iconName)
    {
        m_badge.iconName = iconName;
        update();
    }
}

void PDFToolBadge::setBadgeSize(BadgeSize size)
{
    size = PDFComponentPainter::normalized(size);
    if (m_badge.size != size)
    {
        m_badge.size = size;
        updateGeometry();
        update();
    }
}

QSize PDFToolBadge::sizeHint() const
{
    return PDFComponentPainter::getToolBadgeSize(m_badge);
}

QSize PDFToolBadge::minimumSizeHint() const
{
    return sizeHint();
}

void PDFToolBadge::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    PDFComponentPainter::paintToolBadge(&painter, QRectF(rect()), m_badge, PDFComponentPainter::State(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection()));
}

void PDFToolBadge::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::EnabledChange)
    {
        update();
    }
}

PDFTag::PDFTag(QWidget* parent) :
    BaseClass(FocusRing::Inside, parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Only a label by default: the mouse passes to the row below it
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

PDFTag::PDFTag(const QString& text, QWidget* parent) :
    PDFTag(parent)
{
    setText(text);
}

void PDFTag::setClickable(bool isClickable)
{
    if (m_isClickable == isClickable)
    {
        return;
    }

    m_isClickable = isClickable;
    setFocusPolicy(isClickable ? Qt::StrongFocus : Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents, !isClickable);

    if (isClickable)
    {
        setCursor(Qt::PointingHandCursor);
    }
    else
    {
        unsetCursor();
        setDown(false);

        // A label takes no focus
        if (hasFocus())
        {
            clearFocus();
        }
    }

#if QT_CONFIG(accessibility)
    // The role changes between a text and a button
    QAccessibleEvent roleEvent(this, QAccessible::RoleChanged);
    QAccessible::updateAccessibility(&roleEvent);
#endif

    update();
}

void PDFTag::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintTag(painter, QRectF(rect), PDFComponentPainter::Tag{ text() }, state);
}

QSize PDFTag::getContentSizeHint() const
{
    return PDFComponentPainter::getTagSize(PDFComponentPainter::Tag{ text() });
}

PDFTextLink::PDFTextLink(QWidget* parent) :
    BaseClass(FocusRing::Outside, parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    // A click on a link does not take the focus (as the icon buttons)
    setFocusPolicy(Qt::TabFocus);
    setFocusRingRadius(PDFComponentPainter::getTextLinkRadius());
}

PDFTextLink::PDFTextLink(const QString& text, QWidget* parent) :
    PDFTextLink(parent)
{
    setText(text);
}

void PDFTextLink::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintTextLink(painter, QRectF(rect), PDFComponentPainter::TextLink{ text() }, state);
}

QSize PDFTextLink::getContentSizeHint() const
{
    return PDFComponentPainter::getTextLinkSize(PDFComponentPainter::TextLink{ text() });
}

PDFStarButton::PDFStarButton(QWidget* parent) :
    PDFStarButton(StarSize::Medium, parent)
{

}

PDFStarButton::PDFStarButton(StarSize size, QWidget* parent) :
    BaseClass(FocusRing::Outside, parent),
    m_size(PDFComponentPainter::normalized(size))
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFocusPolicy(Qt::TabFocus);
    setFocusRingRadius(PDFComponentPainter::getStarRadius());
    setSelectionBehavior(SelectionBehavior::Toggle);

    connect(this, &QAbstractButton::toggled, this, &PDFStarButton::onToggled);
    updateStateText();
}

void PDFStarButton::setPinned(bool isPinned)
{
    // QAbstractButton emits toggled only on a real change
    setChecked(isPinned);
}

void PDFStarButton::setStarSize(StarSize size)
{
    size = PDFComponentPainter::normalized(size);
    if (m_size != size)
    {
        m_size = size;
        updateGeometry();
        update();
    }
}

QString PDFStarButton::getStateText() const
{
    return isChecked() ? tr("Unpin") : tr("Pin file");
}

void PDFStarButton::updateStateText()
{
    const QString previousText = m_stateText;
    m_stateText = getStateText();
    replaceToolTip(previousText, m_stateText);

#if QT_CONFIG(accessibility)
    if (!previousText.isEmpty() && m_stateText != previousText && accessibleName().isEmpty())
    {
        // The name of the star is its state
        QAccessibleEvent nameEvent(this, QAccessible::NameChanged);
        QAccessible::updateAccessibility(&nameEvent);
    }
#endif
}

void PDFStarButton::onToggled(bool isChecked)
{
    QPointer<PDFStarButton> guard(this);
    updateStateText();

    if (guard)
    {
        Q_EMIT pinnedChanged(isChecked);
    }
}

void PDFStarButton::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateStateText();
    }
}

QString PDFStarButton::getDefaultAccessibleName() const
{
    return getStateText();
}

void PDFStarButton::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintStar(painter, QRectF(rect), m_size, state);
}

QSize PDFStarButton::getContentSizeHint() const
{
    return PDFComponentPainter::getStarSize(m_size);
}

PDFMoreButton::PDFMoreButton(QWidget* parent) :
    BaseClass(FocusRing::Outside, parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // A click does not take the focus (as the icon buttons), Tab reaches the button
    setFocusPolicy(Qt::TabFocus);
    setFocusRingRadius(PDFComponentPainter::getMoreButtonRadius());
    updateText();

    connect(this, &QAbstractButton::clicked, this, [this]()
    {
        QPointer<PDFMoreButton> guard(this);
        Q_EMIT menuRequested();
        if (guard)
        {
            showMenu();
        }
    });
}

void PDFMoreButton::setMenu(QMenu* menu)
{
    m_menu = menu;
}

bool PDFMoreButton::showMenu()
{
    QMenu* menu = m_menu.data();
    if (!menu || !isEnabled())
    {
        return false;
    }

    // Below the button, aligned with its leading edge, or above it, when there
    // is no room below (as QPushButton places its menu). The point is the top
    // left corner of the menu in both directions.
    const QSize menuSize = menu->sizeHint();
    const QPoint topLeft = mapToGlobal(QPoint(0, 0));
    QPoint position(topLeft.x(), topLeft.y() + height());
    if (isRightToLeft())
    {
        position.rx() += width() - menuSize.width();
    }
    if (const QScreen* buttonScreen = screen())
    {
        const QRect available = buttonScreen->availableGeometry();
        if (position.y() + menuSize.height() > available.bottom() + 1 && topLeft.y() - menuSize.height() >= available.top())
        {
            position.setY(topLeft.y() - menuSize.height());
        }
    }

    // The style aligns the panel of the menu (without its shadow) with the button
    menu->setProperty(PDFWidgetStyleProperties::POPUP_ANCHOR, QRect(topLeft, size()));

    // The press, which closes the menu over the button, does not open it again
    menu->setNoReplayFor(this);

    // The button stays pressed, while its menu is shown
    setDown(true);
    connect(menu, &QMenu::aboutToHide, this, [this]() { setDown(false); }, Qt::SingleShotConnection);

    QPointer<PDFMoreButton> guard(this);
    menu->popup(position);
    if (guard && (!m_menu || !m_menu->isVisible()))
    {
        setDown(false);
    }
    return true;
}

bool PDFMoreButton::showMenu(const QPoint& globalPosition)
{
    QMenu* menu = m_menu.data();
    if (!menu || !isEnabled())
    {
        return false;
    }

    // A context menu at the pointer: QMenu places itself by the direction of its layout
    menu->setProperty(PDFWidgetStyleProperties::POPUP_ANCHOR, QVariant());
    menu->popup(globalPosition);
    return true;
}

void PDFMoreButton::updateText()
{
    const QString previousText = m_text;
    m_text = tr("More actions");
    replaceToolTip(previousText, m_text);
}

void PDFMoreButton::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateText();
    }
}

QString PDFMoreButton::getDefaultAccessibleName() const
{
    return m_text;
}

void PDFMoreButton::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    // The icon is darker also when the row or the card, in which it lies, is under the pointer
    PDFComponentPainter::State paintedState = state;
    if (const PDFComponentButton* parentButton = qobject_cast<const PDFComponentButton*>(parentWidget()))
    {
        const PDFComponentPainter::State parentState = parentButton->getComponentState();
        if (state.isEnabled() && (parentState.isHovered() || parentState.isPressed()))
        {
            paintedState.flags |= QStyle::State_MouseOver;
        }
    }

    PDFComponentPainter::paintMoreButton(painter, QRectF(rect), paintedState);
}

QSize PDFMoreButton::getContentSizeHint() const
{
    return PDFComponentPainter::getMoreButtonSize();
}

PDFColorSwatch::PDFColorSwatch(QWidget* parent) :
    BaseClass(FocusRing::Outside, parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFocusRingRadius(PDFComponentPainter::getColorSwatchRadius());
    setSelectionBehavior(SelectionBehavior::Exclusive);
}

PDFColorSwatch::PDFColorSwatch(const QColor& color, const QString& colorName, QWidget* parent) :
    PDFColorSwatch(parent)
{
    m_swatch.color = color;
    setColorName(colorName);
}

void PDFColorSwatch::notifyNameChanged(const QString& previousName)
{
#if QT_CONFIG(accessibility)
    if (accessibleName().isEmpty() && getDefaultAccessibleName() != previousName)
    {
        QAccessibleEvent nameEvent(this, QAccessible::NameChanged);
        QAccessible::updateAccessibility(&nameEvent);
    }
#else
    Q_UNUSED(previousName);
#endif
}

void PDFColorSwatch::setColor(const QColor& color)
{
    if (m_swatch.color != color)
    {
        const QString previousName = getDefaultAccessibleName();
        m_swatch.color = color;
        notifyNameChanged(previousName);
        update();
    }
}

void PDFColorSwatch::setKind(Kind kind)
{
    kind = PDFComponentPainter::normalized(kind);
    if (m_swatch.kind != kind)
    {
        const QString previousName = getDefaultAccessibleName();
        m_swatch.kind = kind;
        notifyNameChanged(previousName);
        update();
    }
}

void PDFColorSwatch::setSelected(bool isSelected)
{
    if (isSelected)
    {
        setChecked(true);
    }
    else
    {
        // Also the selected swatch of an exclusive group
        uncheck();
    }
}

void PDFColorSwatch::setColorName(const QString& colorName)
{
    if (m_colorName != colorName)
    {
        const QString previousName = getDefaultAccessibleName();
        replaceToolTip(m_colorName, colorName);
        m_colorName = colorName;
        notifyNameChanged(previousName);
    }
}

QString PDFColorSwatch::getDefaultAccessibleName() const
{
    if (!m_colorName.isEmpty())
    {
        return m_colorName;
    }

    if (m_swatch.kind == Kind::None)
    {
        return tr("No color");
    }

    return m_swatch.color.isValid() ? m_swatch.color.name(QColor::HexRgb) : tr("Color");
}

void PDFColorSwatch::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::paintColorSwatch(painter, QRectF(rect), m_swatch, state);
}

QSize PDFColorSwatch::getContentSizeHint() const
{
    return PDFComponentPainter::getColorSwatchSize();
}

/// Drop shadow of a page thumbnail. It is drawn around the widget, outside of
/// it (into its parent), so the widget keeps the size of the frame. The color
/// is taken at the time of drawing.
class PDFPageThumbnailShadowEffect : public QGraphicsEffect
{
public:
    explicit PDFPageThumbnailShadowEffect(QObject* parent) :
        QGraphicsEffect(parent)
    {

    }

    virtual QRectF boundingRectFor(const QRectF& rect) const override
    {
        return rect.marginsAdded(QMarginsF(PDFComponentPainter::getPageThumbnailShadowMargins()));
    }

protected:
    virtual void draw(QPainter* painter) override
    {
        // The painter is in the logical coordinates of the widget
        PDFComponentPainter::paintPageThumbnailShadow(painter, sourceBoundingRect(Qt::LogicalCoordinates));

        // The widget is drawn from its pixmap (in the resolution of the device):
        // QGraphicsEffect::drawSource would move it by its position once more,
        // when the parent is rendered by QWidget::render with a painter
        QPoint offset;
        const QPixmap pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset, QGraphicsEffect::NoPad);
        if (!pixmap.isNull())
        {
            painter->drawPixmap(offset, pixmap);
        }
    }
};

PDFPageThumbnail::PDFPageThumbnail(QWidget* parent) :
    PDFPageThumbnail(Size::Small, parent)
{

}

PDFPageThumbnail::PDFPageThumbnail(Size size, QWidget* parent) :
    BaseClass(parent)
{
    m_thumbnail.size = PDFComponentPainter::normalized(size);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setGraphicsEffect(new PDFPageThumbnailShadowEffect(this));
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void PDFPageThumbnail::setThumbnailSize(Size size)
{
    size = PDFComponentPainter::normalized(size);
    if (m_thumbnail.size != size)
    {
        m_thumbnail.size = size;
        updateGeometry();
        update();
    }
}

void PDFPageThumbnail::setPixmap(const QPixmap& pixmap)
{
    if (m_thumbnail.pixmap.cacheKey() != pixmap.cacheKey())
    {
        m_thumbnail.pixmap = pixmap;
        update();
    }
}

QSize PDFPageThumbnail::sizeHint() const
{
    // The size of the frame, the shadow takes no space
    return PDFComponentPainter::getPageThumbnailSize(m_thumbnail.size);
}

QSize PDFPageThumbnail::minimumSizeHint() const
{
    return sizeHint();
}

void PDFPageThumbnail::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    PDFComponentPainter::paintPageThumbnailFrame(&painter, QRectF(rect()), m_thumbnail, PDFComponentPainter::State(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection()));
}

void PDFPageThumbnail::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::EnabledChange)
    {
        update();
    }
}

}   // namespace pdf
