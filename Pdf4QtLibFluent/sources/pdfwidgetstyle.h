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

#ifndef PDFWIDGETSTYLE_H
#define PDFWIDGETSTYLE_H

#include "pdffluentglobal.h"

#include <QCommonStyle>

#include <memory>

namespace pdf
{

class PDFWidgetStyleImpl;

/// Style of the widgets of the application. It draws every element by itself
/// from the colors of the active theme (\p PDFThemeManager), so the user
/// interface looks the same on all platforms.
///
/// The style never reads a palette: the palette of the application is filled
/// with the leak color (\p PDFThemeColors::getLeakColor), which is seen only
/// where something draws itself without the style. Widgets, which do so, have
/// their replacement in the library (PDFLineEdit, PDFLabel, ...).
///
/// The style is set by QApplication::setStyle(new PDFWidgetStyle()). It loads
/// the fonts of the library, sets the font of the application and follows the
/// changes of the theme.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFWidgetStyle : public QCommonStyle
{
    Q_OBJECT

private:
    using BaseClass = QCommonStyle;

public:
    explicit PDFWidgetStyle();
    virtual ~PDFWidgetStyle() override;

    virtual void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override;
    virtual void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override;
    virtual void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget = nullptr) const override;
    virtual QRect subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget = nullptr) const override;
    virtual QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl subControl, const QWidget* widget = nullptr) const override;
    virtual SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex* option, const QPoint& point, const QWidget* widget = nullptr) const override;
    virtual QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget = nullptr) const override;
    virtual int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
    virtual int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override;
    virtual QIcon standardIcon(StandardPixmap standardIcon, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
    virtual QPixmap standardPixmap(StandardPixmap standardPixmap, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
    virtual QPixmap generatedIconPixmap(QIcon::Mode iconMode, const QPixmap& pixmap, const QStyleOption* option) const override;
    virtual QPalette standardPalette() const override;
    virtual int layoutSpacing(QSizePolicy::ControlType control1, QSizePolicy::ControlType control2, Qt::Orientation orientation, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
    virtual void drawItemText(QPainter* painter, const QRect& rect, int flags, const QPalette& palette, bool enabled, const QString& text, QPalette::ColorRole textRole = QPalette::NoRole) const override;
    virtual void drawItemPixmap(QPainter* painter, const QRect& rect, int alignment, const QPixmap& pixmap) const override;

    virtual void polish(QWidget* widget) override;
    virtual void unpolish(QWidget* widget) override;
    virtual void polish(QApplication* application) override;
    virtual void unpolish(QApplication* application) override;
    virtual void polish(QPalette& palette) override;

    /// Returns true, if the style of the application is this style
    /// (directly, or wrapped by a style of a style sheet).
    static bool isActive();

    /// Returns true, if the widget, which draws its focus ring by itself (rows
    /// of lists, fields), shows the ring: it has the focus and the last input of
    /// the user was the keyboard. The widget is repainted, when it changes.
    static bool isFocusRingVisible(const QWidget* widget);

    /// Sets the time of the animations of the style (busy progress bars). It is
    /// used by the tests, a negative time returns the real clock.
    void setAnimationTime(qint64 milliseconds);

    /// Returns true, while the delay of the hover line of a splitter handle
    /// runs (the tests wait for the decision instead of a fixed time)
    bool isSplitterHoverPending() const;

private:
    friend class PDFWidgetStyleImpl;

    std::unique_ptr<PDFWidgetStyleImpl> m_impl;
};

}   // namespace pdf

#endif // PDFWIDGETSTYLE_H
