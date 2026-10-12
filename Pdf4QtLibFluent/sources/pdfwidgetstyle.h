#ifndef PDFWIDGETSTYLE_H
#define PDFWIDGETSTYLE_H

#include "pdffluentglobal.h"

#include <QCommonStyle>

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

    /// Sets the time of the animations of the style (busy progress bars). It is
    /// used by the tests, a negative time returns the real clock.
    void setAnimationTime(qint64 milliseconds);

private:
    friend class PDFWidgetStyleImpl;

    PDFWidgetStyleImpl* m_impl;
};

}   // namespace pdf

#endif // PDFWIDGETSTYLE_H
