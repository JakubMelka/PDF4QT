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

#ifndef PDFCOLORPICKERDIALOG_H
#define PDFCOLORPICKERDIALOG_H

#include "pdffluentglobal.h"
#include "pdfmessagebox.h"

#include <QAbstractSlider>
#include <QColor>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <optional>

class QPushButton;

namespace pdf
{

class PDFIconButton;
class PDFLabel;
class PDFLineEdit;
class PDFSpinBox;

/// Saturation / value area of the Color picker 1294:1695: the hue of the
/// color, white to the left, black to the bottom and the marker of the color.
/// The widget is larger than the area by a half of the marker on each side
/// (the ring of the marker may reach out of the area). The mouse and the
/// arrows (1 %, Shift 10 %, Home / End the saturation 0 / 100 %, Page Up /
/// Page Down the value 100 / 0 %) move the marker; the picture is not mirrored
/// for right-to-left.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFColorArea : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFColorArea(QWidget* parent = nullptr);
    virtual ~PDFColorArea() override;

    qreal getHue() const { return m_hue; }
    qreal getSaturation() const { return m_saturation; }
    qreal getValue() const { return m_value; }

    /// Sets the hue (0 - 360) of the shown colors, never emits a signal
    void setHue(qreal hue);

    /// Sets the marker (both 0 - 1, bounded), never emits a signal
    void setSaturationValue(qreal saturation, qreal value);

    /// Returns the rectangle of the area (the widget without the margins of the marker)
    QRect getAreaRect() const;

    /// Returns the margin of the widget around the area (a half of the marker)
    static int getMarkerMargin();

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

Q_SIGNALS:
    /// The user has moved the marker (the mouse or the keys)
    void saturationValueChanged(qreal saturation, qreal value);

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    /// Moves the marker by the user and emits the signal
    void setUserSaturationValue(qreal saturation, qreal value);
    void updateDescription();

    qreal m_hue = 0.0;
    qreal m_saturation = 0.0;
    qreal m_value = 1.0;
    bool m_isDragging = false;
};

/// Hue slider of the Color picker 1294:1695 (0 - 359): the track with the
/// spectrum and the thumb of Slider. Arrows 1°, Shift + arrows and Page Up /
/// Page Down 10°, Home / End 0° / 359°; not mirrored for right-to-left.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFHueSlider : public QAbstractSlider
{
    Q_OBJECT

private:
    using BaseClass = QAbstractSlider;

public:
    explicit PDFHueSlider(QWidget* parent = nullptr);
    virtual ~PDFHueSlider() override;

    /// Returns the rectangle of the thumb
    QRect getThumbRect() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    /// Moves the thumb to the position of the pointer
    void moveToPosition(qreal x);
    void setThumbHovered(bool isHovered);

    bool m_isThumbHovered = false;
};

/// Comparison chip of the Color picker 1294:1695: the current color at the
/// leading side and the new color at the trailing side, their hexadecimal
/// values in the tooltip and in the accessible description
class PDF4QTLIBFLUENTSHARED_EXPORT PDFColorComparison : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFColorComparison(QWidget* parent = nullptr);
    virtual ~PDFColorComparison() override;

    const QColor& getCurrentColor() const { return m_current; }
    const QColor& getColor() const { return m_color; }

    /// Sets the current color (an invalid color is no color) and the new color
    void setColors(const QColor& current, const QColor& color);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateTexts();

    QColor m_current;
    QColor m_color;
};

/// Dialog Color picker 1294:1695 opened by "More colors…" of the palettes
/// (PDFColorPalette, PDFColorButton, PDFColorSwatchRow): picks any opaque color
/// (no alpha). A message box of the design without the badge, 400 wide: the
/// saturation / value area, the comparison chip, the hue slider, the eyedropper
/// (hidden, where the screen cannot be read: Wayland, a failed grab), the Hex
/// field (#RRGGBB) and the R / G / B spin boxes (0 - 255), Cancel and OK. All
/// controls follow each other without feedback; an invalid Hex shows the error
/// below the fields and disables OK, the other controls keep the last valid
/// color. Enter = OK, Esc = Cancel (while the eyedropper picks, Esc gives the
/// color before the picking back and Enter / a click takes the color).
///
/// OK emits \p colorSelected and accepts the dialog; Cancel rejects it.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFColorPickerDialog : public PDFMessageBox
{
    Q_OBJECT

private:
    using BaseClass = PDFMessageBox;

public:
    /// Reads the color of the screen at the global position (nullopt, if the
    /// screen cannot be read). The test sets its own reader.
    using ScreenColorReader = std::function<std::optional<QColor>(const QPoint& globalPosition)>;

    /// Index of a channel field
    enum class Channel
    {
        Red,
        Green,
        Blue
    };

    /// \param currentColor Color, which is changed (an invalid color is no color;
    ///        the new color starts as white then)
    /// \param parent Parent widget
    explicit PDFColorPickerDialog(const QColor& currentColor, QWidget* parent = nullptr);
    virtual ~PDFColorPickerDialog() override;

    /// Returns the color, which is changed (as given to the constructor)
    const QColor& getCurrentColor() const { return m_currentColor; }

    /// Returns the new color (opaque)
    QColor getColor() const { return m_color; }

    /// Sets the new color (the alpha is dropped), all controls follow; it emits nothing
    void setColor(const QColor& color);

    /// The Hex field holds a valid color
    bool isHexValid() const { return m_isHexValid; }

    /// The eyedropper can read the screen (its button is shown)
    bool isEyedropperAvailable() const { return m_isEyedropperAvailable; }

    /// The eyedropper is picking a color of the screen
    bool isPicking() const { return m_isPicking; }

    /// Starts the eyedropper (nothing, when it is not available): the pointer
    /// reads the screen, a click takes the color, Esc gives the color back. The
    /// screen is read first here (never at the opening of the dialog); a screen,
    /// which gives no picture, hides the eyedropper. A modal window opened
    /// meanwhile (WindowBlocked), the deactivation and the hide end the picking.
    void startPicking();

    /// Stops the eyedropper, \p keepColor takes the picked color
    void stopPicking(bool keepColor);

    /// Parts of the dialog (nullptr, if the application has deleted them)
    PDFColorArea* getArea() const;
    PDFHueSlider* getHueSlider() const;
    PDFColorComparison* getComparison() const;
    PDFIconButton* getEyedropperButton() const;
    PDFLineEdit* getHexField() const;
    PDFSpinBox* getChannelField(Channel channel) const;
    PDFLabel* getErrorLabel() const;
    QPushButton* getOkButton() const;
    QPushButton* getCancelButton() const;

    /// Parses a hexadecimal color: "#RRGGBB" or "RRGGBB" (the case and the
    /// white space around it do not matter); nullopt for anything else
    static std::optional<QColor> parseHex(const QString& text);

    /// Returns the hexadecimal name of the color ("#0A84FF")
    static QString getHexName(const QColor& color);

    /// Replaces the reader of the screen (an empty function = the screens of
    /// the application); for the tests
    static void setScreenColorReader(ScreenColorReader reader);

    /// Reads the color of the screen at the global position by the reader
    static std::optional<QColor> readScreenColor(const QPoint& globalPosition);

Q_SIGNALS:
    /// OK has been activated with the new color (the dialog is accepted after it)
    void colorSelected(const QColor& color);

protected:
    virtual void keyPressEvent(QKeyEvent* event) override;
    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void hideEvent(QHideEvent* event) override;
    virtual void changeEvent(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;
    virtual bool event(QEvent* event) override;

private:
    /// Source of a change of the color (the control, which is not updated)
    enum class Source
    {
        Program,
        Area,
        Hue,
        Hex,
        Channels,
        Eyedropper
    };

    struct ColorState
    {
        QColor color;
        qreal hue = 0.0;
        qreal saturation = 0.0;
        qreal value = 1.0;
    };

    void retranslate();
    void placeArea();
    /// The new color as RGB: the hue (and the saturation of black) are kept for achromatic colors
    void setRgbColor(const QColor& color, Source source);
    /// The new color from the HSV values of the members
    void setHsvColor(Source source);
    void updateControls(Source source);
    void setHexValid(bool isValid);
    /// The window has the height of its content (the error row shown or hidden)
    void fitCardHeight();
    ColorState getState() const;
    void setState(const ColorState& state);
    void pickAt(const QPoint& globalPosition);

    void onAreaChanged(qreal saturation, qreal value);
    void onHueChanged(int hue);
    void onHexEdited(const QString& text);
    void onChannelChanged();
    void onEyedropperClicked();
    void onPickingTimer();
    void onOkClicked();

    QColor m_currentColor;
    QColor m_color;
    qreal m_hue = 0.0;
    qreal m_saturation = 0.0;
    qreal m_value = 1.0;
    ColorState m_stateBeforePicking;
    bool m_isHexValid = true;
    bool m_isEyedropperAvailable = false;
    bool m_isPicking = false;
    bool m_wasMouseTracking = false;
    bool m_isGrabbing = false;
    QPoint m_lastCursorPosition;
    /// A member, not a child: the application cannot reach it by children() and delete it
    QTimer m_pickingTimer;

    // The parts are children of the dialog, which the application may delete
    // (Qt deletes them with the dialog), so they are held by QPointer
    QPointer<QWidget> m_areaPlaceholder;
    QPointer<PDFColorArea> m_area;
    QPointer<PDFColorComparison> m_comparison;
    QPointer<PDFHueSlider> m_hueSlider;
    QPointer<PDFIconButton> m_eyedropper;
    QPointer<PDFLabel> m_hexCaption;
    QPointer<PDFLineEdit> m_hexField;
    QPointer<PDFLabel> m_channelCaptions[3];
    QPointer<PDFSpinBox> m_channelFields[3];
    QPointer<PDFLabel> m_errorLabel;
    QPointer<QPushButton> m_cancelButton;
    QPointer<QPushButton> m_okButton;
};

}   // namespace pdf

#endif // PDFCOLORPICKERDIALOG_H
