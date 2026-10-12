#include "pdfwidgetstyleimpl.h"
#include "pdfthememanager.h"
#include "pdfthemeiconengine.h"
#include "pdffluentwidgets.h"
#include "pdftooltip.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QFocusFrame>
#include <QFontMetricsF>
#include <QGroupBox>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QStyleHints>
#include <QTimer>
#include <QTimerEvent>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QWindow>
#include <QtMath>

#include <algorithm>

namespace pdf
{

/// Names of the internal properties, by which the style remembers,
/// what it has changed on a widget.
class PDFWidgetStyleInternal
{
public:
    PDFWidgetStyleInternal() = delete;

    static constexpr const char* TRANSLUCENT = "_pdf4qt_translucent";
    static constexpr const char* SAVED_AUTOFILL = "_pdf4qt_autofill";
    static constexpr const char* SAVED_HOVER = "_pdf4qt_hover";
    static constexpr const char* SAVED_STYLED = "_pdf4qt_styled";
    static constexpr const char* SAVED_MARGINS = "_pdf4qt_margins";
    static constexpr const char* SAVED_FLAGS = "_pdf4qt_flags";
    static constexpr const char* POPUP_MOVED = "_pdf4qt_moved";
    static constexpr const char* SAVED_POPUP_OPAQUE = "_pdf4qt_popupOpaque";
    static constexpr const char* SAVED_POPUP_NO_BACKGROUND = "_pdf4qt_popupNoBackground";
};

QPointer<PDFWidgetStyleImpl> PDFWidgetStyleImpl::s_applicationImpl;

bool PDFWidgetStyleArea::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    Q_UNUSED(element);
    Q_UNUSED(option);
    Q_UNUSED(painter);
    Q_UNUSED(widget);
    return false;
}

bool PDFWidgetStyleArea::drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    Q_UNUSED(element);
    Q_UNUSED(option);
    Q_UNUSED(painter);
    Q_UNUSED(widget);
    return false;
}

bool PDFWidgetStyleArea::drawComplexControl(QStyle::ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    Q_UNUSED(control);
    Q_UNUSED(option);
    Q_UNUSED(painter);
    Q_UNUSED(widget);
    return false;
}

bool PDFWidgetStyleArea::subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const
{
    Q_UNUSED(element);
    Q_UNUSED(option);
    Q_UNUSED(widget);
    Q_UNUSED(rect);
    return false;
}

bool PDFWidgetStyleArea::subControlRect(QStyle::ComplexControl control, const QStyleOptionComplex* option, QStyle::SubControl subControl, const QWidget* widget, QRect& rect) const
{
    Q_UNUSED(control);
    Q_UNUSED(option);
    Q_UNUSED(subControl);
    Q_UNUSED(widget);
    Q_UNUSED(rect);
    return false;
}

bool PDFWidgetStyleArea::sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const
{
    Q_UNUSED(type);
    Q_UNUSED(option);
    Q_UNUSED(contentsSize);
    Q_UNUSED(widget);
    Q_UNUSED(size);
    return false;
}

void PDFWidgetStyleArea::polish(QWidget* widget)
{
    Q_UNUSED(widget);
}

void PDFWidgetStyleArea::unpolish(QWidget* widget)
{
    Q_UNUSED(widget);
}

const QStyle* PDFWidgetStyleArea::style() const
{
    return m_impl->getStyle()->proxy();
}

PDFWidgetStyleImpl::PDFWidgetStyleImpl(PDFWidgetStyle* style) :
    QObject(nullptr),
    m_style(style)
{
    m_areas.push_back(PDFWidgetStyleAreas::createButtons(this));
    m_areas.push_back(PDFWidgetStyleAreas::createInputs(this));
    m_areas.push_back(PDFWidgetStyleAreas::createRanges(this));
    m_areas.push_back(PDFWidgetStyleAreas::createMenus(this));
    m_areas.push_back(PDFWidgetStyleAreas::createTabs(this));
    m_areas.push_back(PDFWidgetStyleAreas::createItemViews(this));
    m_areas.push_back(PDFWidgetStyleAreas::createContainers(this));

    m_clock.start();
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, &PDFWidgetStyleImpl::onThemeChanged);
}

PDFWidgetStyleImpl::~PDFWidgetStyleImpl()
{
    m_animationTimer.stop();

    if (m_focusFrame)
    {
        m_focusFrame->setWidget(nullptr);
        delete m_focusFrame.data();
    }
}

PDFStyleState PDFWidgetStyleImpl::getState(const QStyleOption* option, const QWidget* widget) const
{
    PDFStyleState state;

    if (!option)
    {
        return state;
    }

    const QStyle::State flags = option->state;
    state.enabled = flags.testFlag(QStyle::State_Enabled);
    state.hover = state.enabled && flags.testFlag(QStyle::State_MouseOver);
    state.pressed = state.enabled && flags.testFlag(QStyle::State_Sunken);
    state.hasFocus = flags.testFlag(QStyle::State_HasFocus);
    state.focusVisible = isFocusVisible(option, widget);
    state.on = flags.testFlag(QStyle::State_On);
    state.partial = flags.testFlag(QStyle::State_NoChange);
    state.selected = flags.testFlag(QStyle::State_Selected);
    state.active = flags.testFlag(QStyle::State_Active);
    state.rightToLeft = option->direction == Qt::RightToLeft;
    return state;
}

bool PDFWidgetStyleImpl::isFocusVisible(const QStyleOption* option, const QWidget* widget) const
{
    if (!option || !option->state.testFlag(QStyle::State_HasFocus) || !option->state.testFlag(QStyle::State_Enabled))
    {
        return false;
    }

    // Qt follows only the tab key and shortcuts and it keeps its flag until the
    // focus is changed. The style follows each key and each click, the same
    // condition decides about the focus frame outside of the widget. The flag
    // of Qt is used only for an option without a widget.
    return widget ? m_keyboardModality : option->state.testFlag(QStyle::State_KeyboardFocusChange);
}

bool PDFWidgetStyleImpl::isApplicationStyle()
{
    return !s_applicationImpl.isNull();
}

void PDFWidgetStyleImpl::requestFocusFrameUpdate()
{
    if (s_applicationImpl)
    {
        QMetaObject::invokeMethod(s_applicationImpl.data(), &PDFWidgetStyleImpl::updateFocusFrame, Qt::QueuedConnection);
    }
}

bool PDFWidgetStyleImpl::isMnemonicVisible(const QWidget* widget) const
{
    if (!widget)
    {
        return false;
    }

    if (qobject_cast<const QMenu*>(widget))
    {
        // A menu opened by the keyboard is controlled by the mnemonics
        return m_altDown || m_keyboardModality;
    }

    const QWidget* window = widget->window();
    const bool seenAlt = std::any_of(m_seenAlt.cbegin(), m_seenAlt.cend(), [window](const QPointer<QWidget>& current) { return current.data() == window; });

    if (qobject_cast<const QMenuBar*>(widget))
    {
        return m_altDown || seenAlt;
    }

    return seenAlt;
}

int PDFWidgetStyleImpl::getMnemonicFlag(const QWidget* widget) const
{
    return isMnemonicVisible(widget) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
}

qint64 PDFWidgetStyleImpl::getAnimationTime() const
{
    return m_animationTime >= 0 ? m_animationTime : m_clock.elapsed();
}

void PDFWidgetStyleImpl::setAnimationTime(qint64 milliseconds)
{
    m_animationTime = milliseconds;
}

bool PDFWidgetStyleImpl::isAnimated() const
{
    return !PDFThemeManager::getInstance()->isReducedMotion();
}

void PDFWidgetStyleImpl::animate(const QWidget* widget) const
{
    if (!widget || !isAnimated() || m_animationTime >= 0)
    {
        return;
    }

    QWidget* target = const_cast<QWidget*>(widget);
    const bool isRegistered = std::any_of(m_animatedWidgets.cbegin(), m_animatedWidgets.cend(), [target](const QPointer<QWidget>& current) { return current.data() == target; });

    if (!isRegistered)
    {
        m_animatedWidgets.push_back(target);
    }

    if (!m_animationTimer.isActive())
    {
        m_animationTimer.start(16, const_cast<PDFWidgetStyleImpl*>(this));
    }
}

bool PDFWidgetStyleImpl::hasProperty(const QWidget* widget, const char* name)
{
    return PDFWidgetStyleProperties::isSet(widget, name);
}

bool PDFWidgetStyleImpl::inherits(const QWidget* widget, const char* className)
{
    return widget && widget->inherits(className);
}

QFont PDFWidgetStyleImpl::getFont(const QWidget* widget, Font role)
{
    QFont font = PDFThemeFonts::getFont(role);

    if (widget && widget->testAttribute(Qt::WA_SetFont) && !widget->inherits("QTabBar"))
    {
        // The application has chosen a font for this widget, keep what it has chosen
        const QFont widgetFont = widget->font();
        const uint mask = widgetFont.resolveMask();

        if (mask & QFont::FamilyResolved)
        {
            font.setFamilies(widgetFont.families());
        }
        if (mask & QFont::SizeResolved)
        {
            if (widgetFont.pixelSize() > 0)
            {
                font.setPixelSize(widgetFont.pixelSize());
            }
            else
            {
                font.setPointSizeF(widgetFont.pointSizeF());
            }
        }
        if (mask & QFont::WeightResolved)
        {
            font.setWeight(widgetFont.weight());
        }
        if (mask & QFont::StyleResolved)
        {
            font.setStyle(widgetFont.style());
        }
        if (mask & QFont::UnderlineResolved)
        {
            font.setUnderline(widgetFont.underline());
        }
        if (mask & QFont::StrikeOutResolved)
        {
            font.setStrikeOut(widgetFont.strikeOut());
        }
    }

    return font;
}

void PDFWidgetStyleImpl::drawStateOverlay(QPainter* painter, const QRectF& rect, const Radii& radii, const PDFStyleState& state)
{
    if (!state.enabled)
    {
        return;
    }

    if (state.pressed)
    {
        PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BgPressed);
    }
    else if (state.hover)
    {
        PDFStylePainter::fill(painter, rect, radii, PDFThemeColors::BgHover);
    }
}

void PDFWidgetStyleImpl::drawIcon(QPainter* painter, const QRectF& rect, const QIcon& icon, const QColor& color, const PDFStyleState& state)
{
    if (icon.isNull() || rect.isEmpty())
    {
        return;
    }

    const QString name = PDFThemeIcons::getIconName(icon);

    if (!name.isEmpty())
    {
        PDFStylePainter::icon(painter, rect, name, color);
        return;
    }

    QIcon::Mode mode = QIcon::Normal;
    if (!state.enabled)
    {
        mode = QIcon::Disabled;
    }
    else if (state.selected)
    {
        mode = QIcon::Selected;
    }
    else if (state.hover || state.pressed)
    {
        mode = QIcon::Active;
    }

    icon.paint(painter, rect.toAlignedRect(), Qt::AlignCenter, mode, state.on ? QIcon::On : QIcon::Off);
}

void PDFWidgetStyleImpl::drawText(QPainter* painter, const QRectF& rect, int alignment, const QString& text, const QFont& font, const QColor& color, const QWidget* widget) const
{
    PDFStylePainter::text(painter, rect, alignment | Qt::TextSingleLine | getMnemonicFlag(widget), text, font, color);
}

int PDFWidgetStyleImpl::getTextWidth(const QString& text, const QFont& font)
{
    if (text.isEmpty())
    {
        return 0;
    }

    return qCeil(PDFStylePainter::textWidth(text, font, Qt::TextHideMnemonic | Qt::TextSingleLine));
}

QString PDFWidgetStyleImpl::getElidedText(const QString& text, const QFont& font, qreal width, Qt::TextElideMode mode)
{
    const QFontMetricsF metrics(font);
    return metrics.elidedText(text, mode, qMax(0.0, width), Qt::TextShowMnemonic);
}

QRect PDFWidgetStyleImpl::visualRect(const QStyleOption* option, const QRect& outer, const QRect& rect)
{
    return QStyle::visualRect(option ? option->direction : Qt::LeftToRight, outer, rect);
}

QRectF PDFWidgetStyleImpl::visualRect(bool rightToLeft, const QRectF& outer, const QRectF& rect)
{
    if (!rightToLeft)
    {
        return rect;
    }

    return QRectF(outer.left() + outer.right() - rect.right(), rect.top(), rect.width(), rect.height());
}

bool PDFWidgetStyleImpl::isTranslucencyAvailable()
{
    static const bool available = []()
    {
        if (qEnvironmentVariableIsSet("PDF4QT_OPAQUE_POPUPS"))
        {
            return false;
        }

        const QString platform = QGuiApplication::platformName();
        for (const char* opaquePlatform : { "minimal", "linuxfb", "eglfs", "vnc", "minimalegl" })
        {
            if (platform == QLatin1String(opaquePlatform))
            {
                return false;
            }
        }

        return true;
    }();

    return available;
}

PDFWidgetStyleImpl::PopupKind PDFWidgetStyleImpl::getPopupKind(const QWidget* widget)
{
    if (!widget)
    {
        return PopupKind::None;
    }

    if (qobject_cast<const QMenu*>(widget))
    {
        if (widget->windowType() != Qt::Popup)
        {
            // Torn off menu
            return PopupKind::None;
        }

        return hasProperty(widget, PDFWidgetStyleProperties::POPOVER) ? PopupKind::Popover : PopupKind::Menu;
    }

    if (widget->inherits("QComboBoxPrivateContainer") || widget->inherits("QCalendarPopup"))
    {
        return PopupKind::Popover;
    }

    // The tooltip of Qt (QTipLabel) has no margins for a shadow: it computes
    // its size and position by itself. It is an opaque panel (PE_PanelTipLabel).
    if (widget->inherits("pdf::PDFToolTipWindow"))
    {
        return PopupKind::ToolTip;
    }

    if (widget->isWindow() && hasProperty(widget, PDFWidgetStyleProperties::POPOVER))
    {
        return PopupKind::Popover;
    }

    return PopupKind::None;
}

QMargins PDFWidgetStyleImpl::getPopupMargins(const QWidget* widget)
{
    if (!widget)
    {
        return QMargins();
    }

    // A popup asks for its geometry before it is prepared by the style
    const QVariant translucent = widget->property(PDFWidgetStyleInternal::TRANSLUCENT);
    const bool isTranslucent = translucent.isValid() ? translucent.toBool() : isTranslucencyAvailable();
    return isTranslucent ? getShadowMargins(getPopupKind(widget)) : QMargins();
}

QMargins PDFWidgetStyleImpl::getShadowMargins(PopupKind kind)
{
    switch (kind)
    {
        case PopupKind::Menu:
            // Shadow 0 / 4 / 16
            return QMargins(16, 12, 16, 20);

        case PopupKind::Popover:
        case PopupKind::ToolTip:
            // Shadow 0 / 8 / 24
            return QMargins(24, 16, 24, 32);

        case PopupKind::None:
            break;
    }

    return QMargins();
}

void PDFWidgetStyleImpl::drawPopupPanel(QPainter* painter, const QRect& rect, const QWidget* widget, PopupKind kind, qreal radius, Color border)
{
    const QMargins margins = getPopupMargins(widget);

    if (margins.isNull())
    {
        // Opaque window: the panel fills the window, corners can not be rounded
        PDFStylePainter::fillRect(painter, rect, PDFThemeColors::BgSurface);
        PDFStylePainter::strokeInside(painter, rect, Radii(0.0), border, 1.0);
        return;
    }

    const QRectF panel = QRectF(rect).marginsRemoved(QMarginsF(margins));
    const bool isMenu = kind == PopupKind::Menu;
    PDFStylePainter::shadow(painter, panel, Radii(radius), QPointF(0.0, isMenu ? 4.0 : 8.0), isMenu ? 16.0 : 24.0, color(PDFThemeColors::EffectShadow));
    PDFStylePainter::fill(painter, panel, Radii(radius), PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(painter, panel, Radii(radius), border, 1.0);
}

void PDFWidgetStyleImpl::polish(QWidget* widget)
{
    if (!widget || qobject_cast<QFocusFrame*>(widget))
    {
        return;
    }

    // Hover is a part of the look of the controls. It is not turned on for
    // other widgets: a widget with hover is repainted whole, whenever the
    // pointer enters or leaves it.
    if (needsHover(widget) && !widget->property(PDFWidgetStyleInternal::SAVED_HOVER).isValid())
    {
        widget->setProperty(PDFWidgetStyleInternal::SAVED_HOVER, widget->testAttribute(Qt::WA_Hover));
        widget->setAttribute(Qt::WA_Hover, true);
    }

    preparePopup(widget);

    // The background of a widget is drawn by the style (PE_Widget), not from the palette
    ensureStyledBackground(widget);

    for (const auto& area : m_areas)
    {
        area->polish(widget);
    }
}

void PDFWidgetStyleImpl::ensureStyledBackground(QWidget* widget)
{
    if (!needsStyledBackground(widget))
    {
        return;
    }

    if (!widget->property(PDFWidgetStyleInternal::SAVED_STYLED).isValid())
    {
        widget->setProperty(PDFWidgetStyleInternal::SAVED_STYLED, widget->testAttribute(Qt::WA_StyledBackground));
        widget->setProperty(PDFWidgetStyleInternal::SAVED_AUTOFILL, widget->autoFillBackground());
    }

    widget->setAttribute(Qt::WA_StyledBackground, true);

    if (!widget->isWindow() && widget->autoFillBackground())
    {
        // Otherwise the palette is drawn below the shape of the widget
        widget->setAutoFillBackground(false);
    }
}

void PDFWidgetStyleImpl::unpolish(QWidget* widget)
{
    if (!widget || qobject_cast<QFocusFrame*>(widget))
    {
        return;
    }

    for (const auto& area : m_areas)
    {
        area->unpolish(widget);
    }

    restorePopup(widget);

    const QVariant savedStyled = widget->property(PDFWidgetStyleInternal::SAVED_STYLED);
    if (savedStyled.isValid())
    {
        widget->setAttribute(Qt::WA_StyledBackground, savedStyled.toBool());
        widget->setAutoFillBackground(widget->property(PDFWidgetStyleInternal::SAVED_AUTOFILL).toBool());
        widget->setProperty(PDFWidgetStyleInternal::SAVED_STYLED, QVariant());
        widget->setProperty(PDFWidgetStyleInternal::SAVED_AUTOFILL, QVariant());
    }

    const QVariant savedHover = widget->property(PDFWidgetStyleInternal::SAVED_HOVER);
    if (savedHover.isValid())
    {
        widget->setAttribute(Qt::WA_Hover, savedHover.toBool());
        widget->setProperty(PDFWidgetStyleInternal::SAVED_HOVER, QVariant());
    }

    if (m_focusFrame && m_focusFrame->widget() == widget)
    {
        m_focusFrame->setWidget(nullptr);
    }
}

void PDFWidgetStyleImpl::polish(QApplication* application)
{
    if (!application || m_applicationPolished)
    {
        return;
    }

    m_applicationPolished = true;
    s_applicationImpl = this;
    m_savedPalette = QApplication::palette();
    m_savedToolTipPalette = QToolTip::palette();
    m_savedFont = QApplication::font();

    static constexpr Qt::UIEffect effects[] = { Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip, Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox };
    for (size_t i = 0; i < std::size(effects); ++i)
    {
        // The effects draw a popup through an opaque window, which loses its shadow
        m_savedEffects[i] = QApplication::isEffectEnabled(effects[i]);
        QApplication::setEffectEnabled(effects[i], false);
    }

    PDFThemeFonts::loadFonts();
    QApplication::setFont(PDFThemeFonts::getFont(Font::Body));
    QApplication::setFont(PDFThemeFonts::getFont(Font::BodySemiBold), "QPushButton");
    QApplication::setFont(PDFThemeFonts::getFont(Font::BodyMedium), "QToolButton");
    QApplication::setFont(PDFThemeFonts::getFont(Font::Body), "QMenu");
    QApplication::setFont(PDFThemeFonts::getFont(Font::BodyMedium), "QMenuBar");
    QApplication::setFont(PDFThemeFonts::getFont(Font::Caption), "QHeaderView");
    QApplication::setFont(PDFThemeFonts::getFont(Font::CaptionRegular), "QStatusBar");
    QApplication::setFont(PDFThemeFonts::getFont(Font::SmallMedium), "QTipLabel");
    QApplication::setFont(PDFThemeFonts::getFont(Font::Body), "QMessageBox");
    QApplication::setFont(PDFThemeFonts::getFont(Font::SmallSemiBold), "QTabBar");

    // Group boxes and dock widgets have no font of their own: their children
    // would inherit it. Their titles are drawn by the font of the design.

    applyLeakPalette();

    const PDFThemeColors& theme = PDFThemeManager::getInstance()->getTheme();
    QGuiApplication::styleHints()->setColorScheme(theme.isDark() ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    applyLeakPalette();

    application->installEventFilter(this);
}

void PDFWidgetStyleImpl::unpolish(QApplication* application)
{
    if (!application || !m_applicationPolished)
    {
        return;
    }

    m_applicationPolished = false;
    if (s_applicationImpl == this)
    {
        s_applicationImpl.clear();
    }
    application->removeEventFilter(this);
    m_animationTimer.stop();
    m_animatedWidgets.clear();
    m_seenAlt.clear();

    if (m_focusFrame)
    {
        m_focusFrame->setWidget(nullptr);
    }

    static constexpr Qt::UIEffect effects[] = { Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip, Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox };
    for (size_t i = 0; i < std::size(effects); ++i)
    {
        QApplication::setEffectEnabled(effects[i], m_savedEffects[i]);
    }

    m_paletteGuard = true;
    QGuiApplication::styleHints()->unsetColorScheme();
    QApplication::setPalette(m_savedPalette);
    QToolTip::setPalette(m_savedToolTipPalette);
    m_paletteGuard = false;

    QApplication::setFont(m_savedFont);
    for (const char* className : { "QPushButton", "QToolButton", "QMenu", "QMenuBar", "QHeaderView", "QStatusBar", "QTipLabel", "QMessageBox", "QTabBar" })
    {
        QApplication::setFont(m_savedFont, className);
    }
}

bool PDFWidgetStyleImpl::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type())
    {
        case QEvent::KeyPress:
        {
            const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
            const int key = keyEvent->key();

            if (key == Qt::Key_Alt)
            {
                m_altDown = true;

                if (QWidget* widget = qobject_cast<QWidget*>(watched))
                {
                    updateMnemonics(widget->window());
                }
            }
            else if (key != Qt::Key_Shift && key != Qt::Key_Control && key != Qt::Key_Meta && key != Qt::Key_AltGr)
            {
                setKeyboardModality(true);

                if (navigateVerticalTabBar(watched, key))
                {
                    return true;
                }
            }
            break;
        }

        case QEvent::KeyRelease:
        {
            const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);

            if (keyEvent->key() == Qt::Key_Alt)
            {
                resetAlt(watched);
            }
            break;
        }

        case QEvent::WindowDeactivate:
            // The release of the Alt key goes to another window (Alt + Tab)
            resetAlt(watched);
            break;

        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
        case QEvent::TabletPress:
        {
            setKeyboardModality(false);

            if (event->type() == QEvent::MouseButtonPress && handlePopupMarginEvent(watched, static_cast<QMouseEvent*>(event)))
            {
                return true;
            }
            break;
        }

        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
            if (handlePopupMarginEvent(watched, static_cast<QMouseEvent*>(event)))
            {
                return true;
            }
            break;

        case QEvent::FocusIn:
        case QEvent::FocusOut:
            if (watched->isWidgetType())
            {
                QMetaObject::invokeMethod(this, &PDFWidgetStyleImpl::updateFocusFrame, Qt::QueuedConnection);
            }
            break;

        case QEvent::Show:
        case QEvent::ShowToParent:
        {
            QWidget* widget = qobject_cast<QWidget*>(watched);
            if (widget && event->type() == QEvent::Show && widget->isWindow() && !getPopupMargins(widget).isNull())
            {
                movePopup(widget);
            }

            // Qt fills some widgets from the palette after they have been prepared
            // by the style (a widget set to a scroll area, pages of containers)
            if (widget && !widget->isWindow() && widget->autoFillBackground() && widget->testAttribute(Qt::WA_WState_Polished))
            {
                ensureStyledBackground(widget);
            }
            break;
        }

        case QEvent::Hide:
        {
            QWidget* widget = qobject_cast<QWidget*>(watched);
            if (widget && widget->isWindow())
            {
                widget->setProperty(PDFWidgetStyleInternal::POPUP_MOVED, QVariant());
            }
            break;
        }

        case QEvent::Close:
        {
            QWidget* widget = qobject_cast<QWidget*>(watched);
            if (widget && widget->isWindow())
            {
                m_seenAlt.removeAll(QPointer<QWidget>(widget));
                m_seenAlt.removeAll(QPointer<QWidget>());
            }
            break;
        }

        case QEvent::Paint:
        {
            if (paintLineEditButton(watched))
            {
                return true;
            }

            QWidget* widget = qobject_cast<QWidget*>(watched);
            if (widget && !widget->isWindow() && widget->autoFillBackground() && widget->testAttribute(Qt::WA_WState_Polished))
            {
                // The fill has been turned on later than the widget was shown
                ensureStyledBackground(widget);
            }

            // The popup of a calendar is a plain widget, its panel is drawn here
            if (widget && widget->isWindow() && widget->property(PDFWidgetStyleInternal::TRANSLUCENT).isValid() && widget->inherits("QCalendarPopup"))
            {
                QPainter painter(widget);
                drawPopupPanel(&painter, widget->rect(), widget, PopupKind::Popover, 12.0, Color::BorderDefault);
            }
            break;
        }

        case QEvent::ToolTip:
        {
            // Tooltips are shown by the tooltip of the library
            QWidget* widget = qobject_cast<QWidget*>(watched);
            const QHelpEvent* helpEvent = static_cast<const QHelpEvent*>(event);
            if (widget && PDFToolTip::showForWidget(widget, helpEvent->globalPos(), helpEvent->pos()))
            {
                return true;
            }
            break;
        }

        case QEvent::StyleChange:
        {
            // The toolbar computes the margins of its layout from the metrics again
            QToolBar* toolBar = qobject_cast<QToolBar*>(watched);
            if (toolBar && toolBar->style() == m_style && toolBar->testAttribute(Qt::WA_WState_Polished))
            {
                QMetaObject::invokeMethod(toolBar, [toolBar]() { applyToolBarMargins(toolBar); }, Qt::QueuedConnection);
            }
            break;
        }

        case QEvent::Move:
        case QEvent::Resize:
        {
            QWidget* extension = qobject_cast<QWidget*>(watched);
            if (extension && extension->inherits("QToolBarExtension") && extension->style() == m_style)
            {
                placeToolBarExtension(extension);
            }
            break;
        }

        case QEvent::Enter:
            setSplitterHover(watched, true);
            break;

        case QEvent::Leave:
            setSplitterHover(watched, false);
            break;

        case QEvent::ApplicationPaletteChange:
        case QEvent::ThemeChange:
            if (watched == qApp && !m_paletteGuard)
            {
                QMetaObject::invokeMethod(this, &PDFWidgetStyleImpl::applyLeakPalette, Qt::QueuedConnection);
            }
            break;

        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}

void PDFWidgetStyleImpl::resetAlt(QObject* watched)
{
    if (!m_altDown)
    {
        return;
    }

    m_altDown = false;

    if (QWidget* widget = qobject_cast<QWidget*>(watched))
    {
        const QList<QMenuBar*> menuBars = widget->window()->findChildren<QMenuBar*>();
        for (QMenuBar* menuBar : menuBars)
        {
            menuBar->update();
        }
    }
}

bool PDFWidgetStyleImpl::handlePopupMarginEvent(QObject* watched, QMouseEvent* event)
{
    QWidget* popup = qobject_cast<QWidget*>(watched);
    if (!popup || !popup->isWindow() || m_isForwardingMouseEvent)
    {
        return false;
    }

    const QMargins margins = getPopupMargins(popup);
    if (margins.isNull())
    {
        return false;
    }

    const QPoint position = event->position().toPoint();
    if (!popup->rect().contains(position) || popup->rect().marginsRemoved(margins).contains(position))
    {
        // Inside of the panel, or outside of the window (the popup grabs the mouse)
        return false;
    }

    // The pointer is in the transparent margin with the shadow. The margin is
    // not a part of the popup for the user: the event belongs to the menu or
    // to the menu bar, which lies below the margin (the margin of a submenu
    // covers a strip of its parent menu).
    const QPoint globalPosition = event->globalPosition().toPoint();
    QWidget* target = nullptr;

    const QWidgetList windows = QApplication::topLevelWidgets();
    for (QWidget* window : windows)
    {
        if (window == popup || !window->isVisible())
        {
            continue;
        }

        const QPoint windowPosition = window->mapFromGlobal(globalPosition);

        if (qobject_cast<QMenu*>(window))
        {
            if (window->rect().marginsRemoved(getPopupMargins(window)).contains(windowPosition))
            {
                target = window;
                break;
            }
        }
        else if (!target && window->rect().contains(windowPosition))
        {
            for (QWidget* child = window->childAt(windowPosition); child; child = child->parentWidget())
            {
                if (qobject_cast<QMenuBar*>(child))
                {
                    target = child;
                    break;
                }
            }
        }
    }

    if (target)
    {
        const QPointF targetPosition = target->mapFromGlobal(event->globalPosition());
        const QPointF scenePosition = target->window()->mapFromGlobal(event->globalPosition());
        QMouseEvent forwardedEvent(event->type(), targetPosition, scenePosition, event->globalPosition(), event->button(), event->buttons(), event->modifiers(), event->pointingDevice());

        m_isForwardingMouseEvent = true;
        QCoreApplication::sendEvent(target, &forwardedEvent);
        m_isForwardingMouseEvent = false;
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        // A click into the shadow is a click outside of the popups. A popup can
        // refuse to close, so the popups are not closed in an endless loop.
        for (int i = 0; i < 32; ++i)
        {
            QWidget* activePopup = QApplication::activePopupWidget();
            if (!activePopup || !activePopup->close())
            {
                break;
            }
        }
        return true;
    }

    return false;
}

void PDFWidgetStyleImpl::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != m_animationTimer.timerId())
    {
        QObject::timerEvent(event);
        return;
    }

    // Widgets register themselves again, when they draw the next frame
    const QList<QPointer<QWidget>> widgets = m_animatedWidgets;
    m_animatedWidgets.clear();

    if (widgets.isEmpty())
    {
        m_animationTimer.stop();
        return;
    }

    for (const QPointer<QWidget>& widget : widgets)
    {
        if (widget && widget->isVisible())
        {
            widget->update();
        }
    }
}

void PDFWidgetStyleImpl::onThemeChanged()
{
    if (!m_applicationPolished)
    {
        return;
    }

    const PDFThemeColors& theme = PDFThemeManager::getInstance()->getTheme();

    m_paletteGuard = true;
    QGuiApplication::styleHints()->setColorScheme(theme.isDark() ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    m_paletteGuard = false;
    applyLeakPalette();

    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets)
    {
        QEvent styleChange(QEvent::StyleChange);
        QCoreApplication::sendEvent(widget, &styleChange);
        widget->update();
    }
}

void PDFWidgetStyleImpl::applyLeakPalette()
{
    if (m_paletteGuard || !m_applicationPolished)
    {
        return;
    }

    m_paletteGuard = true;

    const QPalette leakPalette = PDFThemeColors::createLeakPalette();
    if (QApplication::palette() != leakPalette)
    {
        QApplication::setPalette(leakPalette);
    }
    // The tooltip of Qt draws its text by itself (a rich text as well), so it
    // has the palette of a text widget of the library
    const QPalette toolTipPalette = createToolTipPalette();
    if (QToolTip::palette() != toolTipPalette)
    {
        QToolTip::setPalette(toolTipPalette);
    }

    m_paletteGuard = false;
}

QPalette PDFWidgetStyleImpl::createToolTipPalette()
{
    const PDFThemeColors& theme = PDFThemeManager::getInstance()->getTheme();
    QPalette palette = PDFThemeWidgetHelper::createTextPalette(Color::TextPrimary);

    for (QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive, QPalette::Disabled })
    {
        palette.setColor(group, QPalette::ToolTipText, theme.getColor(Color::TextPrimary));
        palette.setColor(group, QPalette::ToolTipBase, theme.getColor(Color::BgSurface));
    }

    return palette;
}

void PDFWidgetStyleImpl::setKeyboardModality(bool keyboardModality)
{
    if (m_keyboardModality == keyboardModality)
    {
        return;
    }

    m_keyboardModality = keyboardModality;

    if (QWidget* focusWidget = QApplication::focusWidget())
    {
        focusWidget->update();
    }

    QMetaObject::invokeMethod(this, &PDFWidgetStyleImpl::updateFocusFrame, Qt::QueuedConnection);
}

bool PDFWidgetStyleImpl::isOutsideFocusRingWidget(const QWidget* widget) const
{
    if (!widget || !widget->isVisible() || !widget->isEnabled())
    {
        return false;
    }

    if (widget->property(PDFWidgetStyleProperties::RADIUS).isValid())
    {
        return true;
    }

    if (const QToolButton* toolButton = qobject_cast<const QToolButton*>(widget))
    {
        // Buttons of a line edit and items of a status bar draw their ring inside of their shape
        return !inherits(toolButton->parentWidget(), "QLineEdit") && !hasProperty(widget, PDFWidgetStyleProperties::STATUS_ITEM);
    }

    if (const QGroupBox* groupBox = qobject_cast<const QGroupBox*>(widget))
    {
        // The check box of a group box has the ring of a check box
        return groupBox->isCheckable();
    }

    return qobject_cast<const QPushButton*>(widget) || qobject_cast<const QCheckBox*>(widget) || qobject_cast<const QRadioButton*>(widget);
}

void PDFWidgetStyleImpl::updateFocusFrame()
{
    if (!m_applicationPolished)
    {
        return;
    }

    QWidget* focusWidget = QApplication::focusWidget();
    const bool isShown = m_keyboardModality && isOutsideFocusRingWidget(focusWidget);

    if (!isShown)
    {
        if (m_focusFrame && m_focusFrame->widget())
        {
            m_focusFrame->widget()->disconnect(m_focusFrame);
            m_focusFrame->setWidget(nullptr);
        }
        return;
    }

    if (!m_focusFrame)
    {
        m_focusFrame = new QFocusFrame();
    }

    if (m_focusFrame->widget() != focusWidget)
    {
        if (m_focusFrame->widget())
        {
            m_focusFrame->widget()->disconnect(m_focusFrame);
        }

        m_focusFrame->setWidget(focusWidget);

        if (QAbstractButton* button = qobject_cast<QAbstractButton*>(focusWidget))
        {
            // The ring of a check box depends on its state
            connect(button, &QAbstractButton::toggled, m_focusFrame, qOverload<>(&QWidget::update));
        }
        else if (QGroupBox* groupBox = qobject_cast<QGroupBox*>(focusWidget))
        {
            connect(groupBox, &QGroupBox::toggled, m_focusFrame, qOverload<>(&QWidget::update));
        }
    }

    m_focusFrame->update();
}

void PDFWidgetStyleImpl::updateMnemonics(QWidget* window)
{
    if (!window)
    {
        return;
    }

    const bool isKnown = std::any_of(m_seenAlt.cbegin(), m_seenAlt.cend(), [window](const QPointer<QWidget>& current) { return current.data() == window; });
    if (!isKnown)
    {
        m_seenAlt.push_back(window);
    }

    const QList<QWidget*> children = window->findChildren<QWidget*>();
    for (QWidget* child : children)
    {
        if (!child->isWindow() && child->isVisible())
        {
            child->update();
        }
    }
}

void PDFWidgetStyleImpl::preparePopup(QWidget* widget)
{
    const PopupKind kind = getPopupKind(widget);

    if (kind == PopupKind::None || !isTranslucencyAvailable() || widget->property(PDFWidgetStyleInternal::TRANSLUCENT).isValid())
    {
        return;
    }

    if (widget->testAttribute(Qt::WA_WState_Created) && widget->windowHandle() && !widget->windowHandle()->format().hasAlpha())
    {
        // The native window exists already and it is opaque
        widget->setProperty(PDFWidgetStyleInternal::TRANSLUCENT, false);
        return;
    }

    widget->setProperty(PDFWidgetStyleInternal::SAVED_FLAGS, static_cast<qulonglong>(widget->windowFlags().toInt()));
    widget->setProperty(PDFWidgetStyleInternal::SAVED_POPUP_OPAQUE, widget->testAttribute(Qt::WA_OpaquePaintEvent));
    widget->setProperty(PDFWidgetStyleInternal::SAVED_POPUP_NO_BACKGROUND, widget->testAttribute(Qt::WA_NoSystemBackground));

    // Changing of the flags must not create or recreate the window
    const bool wasCreated = widget->testAttribute(Qt::WA_WState_Created);
    const bool wasRightToLeft = widget->testAttribute(Qt::WA_RightToLeft);
    widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
    widget->setAttribute(Qt::WA_TranslucentBackground, true);
    widget->setWindowFlag(Qt::FramelessWindowHint, true);
    widget->setWindowFlag(Qt::NoDropShadowWindowHint, true);
    widget->setAttribute(Qt::WA_RightToLeft, wasRightToLeft);
    widget->setAttribute(Qt::WA_WState_Created, wasCreated);
    widget->setProperty(PDFWidgetStyleInternal::TRANSLUCENT, true);

    if (qobject_cast<QMenu*>(widget) || widget->inherits("QCalendarPopup"))
    {
        const QMargins margins = widget->contentsMargins();
        widget->setProperty(PDFWidgetStyleInternal::SAVED_MARGINS, QVariant::fromValue(margins));
        widget->setContentsMargins(getShadowMargins(kind) + (widget->inherits("QCalendarPopup") ? QMargins(9, 9, 9, 9) : QMargins()));
    }
}

void PDFWidgetStyleImpl::restorePopup(QWidget* widget)
{
    const QVariant translucent = widget->property(PDFWidgetStyleInternal::TRANSLUCENT);

    if (!translucent.isValid())
    {
        return;
    }

    if (translucent.toBool())
    {
        const QVariant margins = widget->property(PDFWidgetStyleInternal::SAVED_MARGINS);
        if (margins.isValid())
        {
            widget->setContentsMargins(margins.value<QMargins>());
        }

        const bool wasCreated = widget->testAttribute(Qt::WA_WState_Created);
        widget->setAttribute(Qt::WA_TranslucentBackground, false);
        widget->setAttribute(Qt::WA_NoSystemBackground, widget->property(PDFWidgetStyleInternal::SAVED_POPUP_NO_BACKGROUND).toBool());
        widget->setAttribute(Qt::WA_OpaquePaintEvent, widget->property(PDFWidgetStyleInternal::SAVED_POPUP_OPAQUE).toBool());
        widget->setWindowFlags(Qt::WindowFlags::fromInt(static_cast<int>(widget->property(PDFWidgetStyleInternal::SAVED_FLAGS).toULongLong())));
        widget->setAttribute(Qt::WA_WState_Created, wasCreated);
    }

    widget->setProperty(PDFWidgetStyleInternal::SAVED_POPUP_OPAQUE, QVariant());
    widget->setProperty(PDFWidgetStyleInternal::SAVED_POPUP_NO_BACKGROUND, QVariant());
    widget->setProperty(PDFWidgetStyleInternal::TRANSLUCENT, QVariant());
    widget->setProperty(PDFWidgetStyleInternal::SAVED_MARGINS, QVariant());
    widget->setProperty(PDFWidgetStyleInternal::SAVED_FLAGS, QVariant());
}

void PDFWidgetStyleImpl::movePopup(QWidget* widget)
{
    if (widget->property(PDFWidgetStyleInternal::POPUP_MOVED).toBool())
    {
        return;
    }

    if (widget->inherits("QComboBoxPrivateContainer") || widget->inherits("pdf::PDFToolTipWindow"))
    {
        // The position of these popups is computed with the margins
        return;
    }

    const QMargins margins = getPopupMargins(widget);
    QPoint position = widget->pos();

    if (QMenu* menu = qobject_cast<QMenu*>(widget))
    {
        // A submenu is placed by the metric PM_SubMenuOverlap, only its vertical position is corrected
        const QMenu* parentMenu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        const bool isSubMenu = parentMenu && parentMenu != menu && parentMenu->activeAction() && parentMenu->activeAction()->menu() == menu;

        if (isSubMenu)
        {
            return;
        }
    }

    // Qt has placed the window with its margins as if it were the panel: the
    // panel takes the place of the window. The window is anchored by its top
    // left corner (a menu below the pointer or below its button), unless Qt
    // has put it above or to the left of its anchor (a menu, which does not fit
    // below the pointer, a menu above its button, a popup in the right-to-left
    // direction) or it has pushed it to the edge of the screen. Then the panel
    // keeps the bottom or the right edge of the window.
    const QRect window(position, widget->size());

    // The screen of the widget is the screen, where it was shown the last time
    const QScreen* screen = QGuiApplication::screenAt(window.center());
    if (!screen)
    {
        screen = widget->screen();
    }
    const QRect available = screen ? screen->availableGeometry() : QRect();

    QList<QRect> anchors;
    anchors << QRect(QCursor::pos(), QSize(1, 1));

    for (const QWidget* anchorWidget : { static_cast<const QWidget*>(QApplication::focusWidget()), static_cast<const QWidget*>(widget->parentWidget()) })
    {
        if (anchorWidget && anchorWidget != widget && anchorWidget->isVisible())
        {
            anchors << QRect(anchorWidget->mapToGlobal(QPoint(0, 0)), anchorWidget->size());
        }
    }

    bool isAnchoredByRight = screen && window.right() >= available.right() - 1;
    bool isAnchoredByBottom = screen && window.bottom() >= available.bottom() - 1;

    for (const QRect& anchor : anchors)
    {
        const bool isPoint = anchor.size() == QSize(1, 1);

        // The window ends at the top of the anchor and it does not start below it
        if (qAbs(window.bottom() + 1 - anchor.top()) <= 2 && qAbs(window.top() - (anchor.bottom() + 1)) > 2)
        {
            isAnchoredByBottom = true;
        }

        // The window ends at the pointer, or it is aligned with the right edge of a widget
        const int anchorRight = isPoint ? anchor.left() : anchor.right() + 1;
        if (qAbs(window.right() + 1 - anchorRight) <= 2 && qAbs(window.left() - anchor.left()) > 2)
        {
            isAnchoredByRight = true;
        }
    }

    position += QPoint(isAnchoredByRight ? margins.right() : -margins.left(), isAnchoredByBottom ? margins.bottom() : -margins.top());

    if (screen)
    {
        // Only the panel has to be on the screen, the shadow may leave it
        QRect panel = QRect(position, widget->size()).marginsRemoved(margins);

        if (panel.right() > available.right())
        {
            position.rx() -= panel.right() - available.right();
        }
        if (panel.bottom() > available.bottom())
        {
            position.ry() -= panel.bottom() - available.bottom();
        }

        panel = QRect(position, widget->size()).marginsRemoved(margins);

        if (panel.left() < available.left())
        {
            position.rx() += available.left() - panel.left();
        }
        if (panel.top() < available.top())
        {
            position.ry() += available.top() - panel.top();
        }
    }

    widget->setProperty(PDFWidgetStyleInternal::POPUP_MOVED, true);
    widget->move(position);
}

void PDFWidgetStyleProperties::set(QWidget* widget, const char* name, const QVariant& value)
{
    if (!widget || !name)
    {
        return;
    }

    widget->setProperty(name, value);

    // The property changes the geometry and the look, the widget is prepared again
    QStyle* style = widget->style();
    if (style && widget->testAttribute(Qt::WA_WState_Polished))
    {
        style->unpolish(widget);
        style->polish(widget);

        // The focus frame has left the widget, when it was unpolished
        PDFWidgetStyleImpl::requestFocusFrameUpdate();
    }

    // Widgets cache their size hints, the change of the style makes them forget
    QEvent styleChange(QEvent::StyleChange);
    QCoreApplication::sendEvent(widget, &styleChange);

    widget->updateGeometry();
    widget->update();
}

bool PDFWidgetStyleProperties::isSet(const QObject* object, const char* name)
{
    if (!object || !name)
    {
        return false;
    }

    const QVariant value = object->property(name);
    if (!value.isValid())
    {
        return false;
    }

    if (value.typeId() == QMetaType::Bool)
    {
        return value.toBool();
    }

    return true;
}

}   // namespace pdf
