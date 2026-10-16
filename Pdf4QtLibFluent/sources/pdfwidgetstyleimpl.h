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

#ifndef PDFWIDGETSTYLEIMPL_H
#define PDFWIDGETSTYLEIMPL_H

#include "pdfwidgetstyle.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyleproperties.h"

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QFont>
#include <QIcon>
#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QStyle>
#include <QStyleOption>

#include <memory>
#include <vector>

class QFocusFrame;
class QApplication;
class QMouseEvent;
class QToolBar;

namespace pdf
{

class PDFWidgetStyleImpl;

/// Visual state of a control decoded from the state of the style option
struct PDFStyleState
{
    bool enabled = true;        ///< State_Enabled
    bool hover = false;         ///< State_MouseOver of an enabled control
    bool pressed = false;       ///< State_Sunken of an enabled control
    bool hasFocus = false;      ///< State_HasFocus
    bool focusVisible = false;  ///< Focus obtained by the keyboard, the focus ring is to be shown
    bool on = false;            ///< State_On
    bool partial = false;       ///< State_NoChange
    bool selected = false;      ///< State_Selected
    bool active = true;         ///< State_Active
    bool rightToLeft = false;   ///< Layout direction of the option
};

/// Visual state of a field (line edit, spin box, combo box, text editor)
struct PDFFieldState
{
    bool enabled = true;
    bool hover = false;
    bool focus = false;     ///< The field has the focus (also after a click of the mouse)
    bool readOnly = false;
    bool invalid = false;   ///< Property "error" of the widget
    bool open = false;      ///< The popup of the field is shown
};

/// Part of the style, which draws a family of controls. Each function returns
/// false, if the element does not belong to the family.
class PDFWidgetStyleArea
{
public:
    explicit PDFWidgetStyleArea(PDFWidgetStyleImpl* impl) : m_impl(impl) { }
    virtual ~PDFWidgetStyleArea() = default;

    virtual bool drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    virtual bool drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    virtual bool drawComplexControl(QStyle::ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const;
    virtual bool subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const;
    virtual bool subControlRect(QStyle::ComplexControl control, const QStyleOptionComplex* option, QStyle::SubControl subControl, const QWidget* widget, QRect& rect) const;
    virtual bool sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const;

    /// Prepares the widget for the style. The widget is polished by all areas.
    virtual void polish(QWidget* widget);

    /// Returns the widget to the state before \p polish
    virtual void unpolish(QWidget* widget);

protected:
    using Color = PDFThemeColors::Color;
    using Radii = PDFStylePainter::Radii;
    using Font = PDFThemeFonts::Role;

    /// Style, by which the other elements are drawn and measured
    const QStyle* style() const;

    PDFWidgetStyleImpl* m_impl;
};

/// Implementation of PDFWidgetStyle: state of the style (keyboard focus, shown
/// mnemonics, animations), preparation of widgets and helper functions shared
/// by the areas of the style.
class PDFWidgetStyleImpl : public QObject
{
    Q_OBJECT

public:
    using Color = PDFThemeColors::Color;
    using Radii = PDFStylePainter::Radii;
    using Font = PDFThemeFonts::Role;

    explicit PDFWidgetStyleImpl(PDFWidgetStyle* style);
    virtual ~PDFWidgetStyleImpl() override;

    const PDFWidgetStyle* getStyle() const { return m_style; }
    const std::vector<std::unique_ptr<PDFWidgetStyleArea>>& getAreas() const { return m_areas; }

    // ------------------------------------------------------------------
    //  State
    // ------------------------------------------------------------------

    /// Decodes the state of the option
    PDFStyleState getState(const QStyleOption* option, const QWidget* widget) const;

    /// Returns true, if the focus ring of the control is to be shown
    /// (the focus was obtained by the keyboard).
    bool isFocusVisible(const QStyleOption* option, const QWidget* widget) const;

    /// Returns true, if the last input of the user was the keyboard
    bool isKeyboardModality() const { return m_keyboardModality; }

    /// Returns true, if a style of this class is the style of the application
    /// (also below a style sheet or a proxy style)
    static bool isApplicationStyle();

    /// Returns true, if the focus ring of the control is to be shown by the
    /// style of the application (the flag of the option without such style)
    static bool isApplicationFocusVisible(const QStyleOption* option, const QWidget* widget);

    /// Asks the style of the application to place its focus frame again
    static void requestFocusFrameUpdate();

    /// Returns true, if the mnemonics of the texts of the widget are underlined
    bool isMnemonicVisible(const QWidget* widget) const;

    /// Returns the text flag of the mnemonics for the widget
    int getMnemonicFlag(const QWidget* widget) const;

    /// Time of the animations in milliseconds
    qint64 getAnimationTime() const;
    void setAnimationTime(qint64 milliseconds);

    /// Returns true, if animations are to be shown
    bool isAnimated() const;

    /// Registers the widget, which is repainted while an animation runs.
    /// The widget is removed, when it is hidden or not registered again
    /// during its repaint.
    void animate(const QWidget* widget) const;

    // ------------------------------------------------------------------
    //  Shared helpers
    // ------------------------------------------------------------------

    static QColor color(Color color) { return PDFStylePainter::color(color); }

    /// Returns true, if the widget has the dynamic property and it is not false
    static bool hasProperty(const QWidget* widget, const char* name);

    /// Returns true, if the widget (or its parent of that class) inherits the class
    static bool inherits(const QWidget* widget, const char* className);

    /// Returns the font of the control: the font of the design, or the font
    /// set explicitly to the widget by the application.
    static QFont getFont(const QWidget* widget, Font role);

    /// Draws the hover / pressed overlay of the state over the shape
    static void drawStateOverlay(QPainter* painter, const QRectF& rect, const Radii& radii, const PDFStyleState& state);

    /// Draws the icon. An icon of the design is drawn by the given color,
    /// another icon is drawn as it is in the mode of the state.
    static void drawIcon(QPainter* painter, const QRectF& rect, const QIcon& icon, const QColor& color, const PDFStyleState& state);

    /// Draws a text of a single line with a mnemonic
    void drawText(QPainter* painter, const QRectF& rect, int alignment, const QString& text, const QFont& font, const QColor& color, const QWidget* widget) const;

    /// Returns the width of the text (mnemonic characters are not counted)
    static int getTextWidth(const QString& text, const QFont& font);

    /// Returns the text elided to the width
    static QString getElidedText(const QString& text, const QFont& font, qreal width, Qt::TextElideMode mode = Qt::ElideRight);

    /// Decodes the state of a field
    static PDFFieldState getFieldState(const QStyleOption* option, const QWidget* widget);

    /// Draws the fill and the border of a field. The border of a focused field
    /// is the focus ring of 2 px, which lies inside of the field.
    static void drawFieldFrame(QPainter* painter, const QRectF& rect, const Radii& radii, const PDFFieldState& state);

    /// Returns the shape of the widget, around which the focus ring is drawn
    /// \param style Style
    /// \param target Widget with the focus
    /// \param targetRect Rectangle of the widget in the coordinates of the painter
    /// \param shape Shape of the widget
    /// \param radii Radii of the shape
    /// \returns False, if the widget shows no focus ring outside of its shape
    static bool getFocusRingShape(const QStyle* style, const QWidget* target, const QRectF& targetRect, QRectF& shape, Radii& radii);

    /// Returns the distance of the focus frame from the widget
    static int getFocusFrameMargin(const QWidget* focusFrame);

    /// Returns true, if the shape of the widget, around which the focus ring is
    /// drawn, is filled with the accent color (a primary button, a checked indicator)
    static bool isFocusRingShapeAccent(const QWidget* target);

    /// Returns the rectangle mirrored for the right-to-left direction of the option
    static QRect visualRect(const QStyleOption* option, const QRect& outer, const QRect& rect);
    static QRectF visualRect(bool rightToLeft, const QRectF& outer, const QRectF& rect);

    // ------------------------------------------------------------------
    //  Popups
    // ------------------------------------------------------------------

    enum class PopupKind
    {
        None,
        Menu,       ///< QMenu: radius 10, shadow 0 / 4 / 16
        Popover,    ///< Combo box popup, calendar, popover menu: shadow 0 / 8 / 24
        ToolTip     ///< Tooltip: radius 8, shadow 0 / 8 / 24
    };

    /// Returns true, if windows of popups can be translucent on the platform
    static bool isTranslucencyAvailable();

    /// Returns the kind of the popup window, PopupKind::None for other widgets
    static PopupKind getPopupKind(const QWidget* widget);

    /// Returns the transparent margins around the panel of the popup, where
    /// its shadow is drawn. Margins are empty, if the popup is not translucent.
    static QMargins getPopupMargins(const QWidget* widget);

    /// Returns the margins of the shadow of the popup kind
    static QMargins getShadowMargins(PopupKind kind);

    /// Returns the margins of the shadow of the popup kind, empty margins,
    /// if popups are not translucent on the platform.
    static QMargins getShadowMarginsIfTranslucent(PopupKind kind);

    /// Returns the paddings of the panel of the toolbar
    static QMargins getToolBarPaddings(const QToolBar* toolBar);

    /// Sets the paddings of the panel of the toolbar. Qt gives the same margin
    /// to all sides of the layout of a toolbar, and it does so again, whenever
    /// the style of the toolbar is changed.
    static void applyToolBarMargins(QToolBar* toolBar);

    /// Places the extension button of a toolbar by the paddings of the panel
    static void placeToolBarExtension(QWidget* extension);

    /// Property of a handle of a splitter, over which the pointer rests
    static constexpr const char* SPLITTER_HOVER = "_pdf4qt_splitterHover";

    /// Draws the panel of a popup with its shadow into the rectangle of the widget
    static void drawPopupPanel(QPainter* painter, const QRect& rect, const QWidget* widget, PopupKind kind, qreal radius, Color border);

    // ------------------------------------------------------------------
    //  Polish
    // ------------------------------------------------------------------

    void polish(QWidget* widget);
    void unpolish(QWidget* widget);
    void polish(QApplication* application);
    void unpolish(QApplication* application);

    virtual bool eventFilter(QObject* watched, QEvent* event) override;

protected:
    virtual void timerEvent(QTimerEvent* event) override;

private:
    void onThemeChanged();
    void applyLeakPalette();
    void setKeyboardModality(bool keyboardModality);
    void updateFocusFrame();
    void updateMnemonics(QWidget* window);
    bool isOutsideFocusRingWidget(const QWidget* widget) const;
    void preparePopup(QWidget* widget);
    void restorePopup(QWidget* widget);
    void movePopup(QWidget* widget);
    bool navigateVerticalTabBar(QObject* watched, int key);
    bool paintLineEditButton(QObject* watched);
    void setSplitterHover(QObject* watched, bool isEntered);
    static bool needsStyledBackground(const QWidget* widget);
    static bool needsHover(const QWidget* widget);
    void ensureStyledBackground(QWidget* widget);
    bool handlePopupMarginEvent(QObject* watched, QMouseEvent* event);
    void resetAlt(QObject* watched);
    static QPalette createToolTipPalette();

    PDFWidgetStyle* m_style;
    std::vector<std::unique_ptr<PDFWidgetStyleArea>> m_areas;
    QPointer<QFocusFrame> m_focusFrame;
    QList<QPointer<QWidget>> m_seenAlt;
    mutable QList<QPointer<QWidget>> m_animatedWidgets;
    mutable QBasicTimer m_animationTimer;
    QElapsedTimer m_clock;
    qint64 m_animationTime = -1;
    bool m_keyboardModality = false;
    bool m_altDown = false;
    bool m_applicationPolished = false;
    bool m_paletteGuard = false;
    const QEvent* m_forwardedMouseEvent = nullptr;

    /// Style, which has prepared the application
    static QPointer<PDFWidgetStyleImpl> s_applicationImpl;

    // State of the application before the style was set
    QPalette m_savedPalette;
    QPalette m_savedToolTipPalette;
    QFont m_savedFont;
    bool m_savedEffects[6] = { };

    /// Default item editor factory installed by the style (owned by Qt, only compared)
    const void* m_installedItemEditorFactory = nullptr;
};

/// Factory functions of the areas of the style (each area is implemented in its file)
class PDFWidgetStyleAreas
{
public:
    PDFWidgetStyleAreas() = delete;

    static std::unique_ptr<PDFWidgetStyleArea> createButtons(PDFWidgetStyleImpl* impl);
    static std::unique_ptr<PDFWidgetStyleArea> createInputs(PDFWidgetStyleImpl* impl);
    static std::unique_ptr<PDFWidgetStyleArea> createRanges(PDFWidgetStyleImpl* impl);
    static std::unique_ptr<PDFWidgetStyleArea> createMenus(PDFWidgetStyleImpl* impl);
    static std::unique_ptr<PDFWidgetStyleArea> createTabs(PDFWidgetStyleImpl* impl);
    static std::unique_ptr<PDFWidgetStyleArea> createItemViews(PDFWidgetStyleImpl* impl);
    static std::unique_ptr<PDFWidgetStyleArea> createContainers(PDFWidgetStyleImpl* impl);
};

/// Metrics, hints and standard icons of the style (tables of the design)
class PDFWidgetStyleMetrics
{
public:
    PDFWidgetStyleMetrics() = delete;

    /// Returns false, if the metric is not defined by the design
    static bool pixelMetric(const PDFWidgetStyleImpl* impl, QStyle::PixelMetric metric, const QStyleOption* option, const QWidget* widget, int& value);

    /// Returns false, if the hint is not defined by the design
    static bool styleHint(const PDFWidgetStyleImpl* impl, QStyle::StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData, int& value);

    /// Returns the name of the icon of the design for the standard pixmap
    static QString getStandardIconName(QStyle::StandardPixmap standardPixmap);

    /// Returns the standard icon
    static QIcon standardIcon(QStyle::StandardPixmap standardPixmap);
};

}   // namespace pdf

#endif // PDFWIDGETSTYLEIMPL_H
