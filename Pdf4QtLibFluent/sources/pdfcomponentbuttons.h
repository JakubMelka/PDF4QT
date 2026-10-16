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

#ifndef PDFCOMPONENTBUTTONS_H
#define PDFCOMPONENTBUTTONS_H

#include "pdffluentglobal.h"
#include "pdfaccenthue.h"
#include "pdfthemecolors.h"

#include <QAbstractButton>

#include <optional>

namespace pdf
{

/// Small button with an icon only (components Icon button and Tab close button).
/// It serves the secondary actions inside of rows, cards and fields: copy, edit,
/// confirm, cancel, expand, show password, save, close. The button has no
/// background and a muted icon at rest, the background of the hover or of the
/// press and a darker icon under the pointer, the focus ring of 2 px outside
/// (drawn by the style) and the icon icon/disabled when it is disabled.
/// The name of the button for assistive technologies and its tooltip must be set.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFIconButton : public QAbstractButton
{
    Q_OBJECT

private:
    using BaseClass = QAbstractButton;

public:
    /// Size of the button
    enum class Size
    {
        Small,      ///< 24 x 24 with the icon of 14 px, radius 6 (Icon button, Size=24)
        Medium,     ///< 28 x 28 with the icon of 16 px, radius 6 (Icon button, Size=28)
        TabClose    ///< 20 x 20 with the icon of 12 px, radius 4 (Tab close button)
    };

    explicit PDFIconButton(QWidget* parent = nullptr);
    explicit PDFIconButton(const QString& iconName, Size size = Size::Small, QWidget* parent = nullptr);

    const QString& getIconName() const { return m_iconName; }
    void setIconName(const QString& iconName);

    Size getButtonSize() const { return m_size; }
    void setButtonSize(Size size);

    /// Sets the color of the icon in all enabled states, only the background
    /// changes with the state (an icon of the confirming action is icon/accent,
    /// the toggle of a filled password field icon/default). A disabled button
    /// has the icon icon/disabled.
    void setIconColor(PDFThemeColors::Color color);
    void resetIconColor();
    std::optional<PDFThemeColors::Color> getIconColor() const { return m_iconColor; }

    /// Returns the color of the icon in the current state
    PDFThemeColors::Color getCurrentIconColor() const;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Returns the extent of the button of the size
    static int getExtent(Size size);

    /// Returns the extent of the icon of the button of the size
    static int getIconExtent(Size size);

    /// Returns the radius of the button of the size
    static qreal getRadius(Size size);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    void init();

    QString m_iconName;
    Size m_size = Size::Small;
    std::optional<PDFThemeColors::Color> m_iconColor;
};

/// Stamp of the Insert panel (component Stamp chip): a label in capitals in the
/// colors of a hue, which says the meaning of the stamp (Approved is green,
/// Rejected is red). Height 26, the label is 11 px from the edges, the focus
/// ring is outside (drawn by the style) and it replaces the border of the hue.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFStampChip : public QAbstractButton
{
    Q_OBJECT

private:
    using BaseClass = QAbstractButton;

public:
    explicit PDFStampChip(QWidget* parent = nullptr);
    explicit PDFStampChip(const QString& text, PDFAccentHue hue = PDFAccentHue::Green, QWidget* parent = nullptr);

    /// Hue of the stamp: Green, Red, Blue or Amber
    PDFAccentHue getHue() const { return m_hue; }
    void setHue(PDFAccentHue hue);

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    /// Returns the font of the label (Semi Bold 11, capitals, letter spacing 0.4 px)
    static QFont getLabelFont();

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void init();

    PDFAccentHue m_hue = PDFAccentHue::Green;
};

}   // namespace pdf

#endif // PDFCOMPONENTBUTTONS_H
