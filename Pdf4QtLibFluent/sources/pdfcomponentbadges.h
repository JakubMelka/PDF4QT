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

#ifndef PDFCOMPONENTBADGES_H
#define PDFCOMPONENTBADGES_H

#include "pdffluentglobal.h"
#include "pdfcomponentpainter.h"

#include <QFrame>
#include <QWidget>

#include <optional>

namespace pdf
{

/// Badge of an icon (icon-circle of the cards of the pages, logo-box of About,
/// the circle of an empty state): a box of a fixed extent with the fill, an
/// optional border and the radius (a circle by default), the icon of the design
/// or the logo of the application in its center. Not interactive (the mouse
/// passes through it); a disabled badge has the icon icon/disabled. The badge
/// keeps its size also in a larger rectangle given by a layout.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFIconBadge : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    using Color = PDFThemeColors::Color;

    explicit PDFIconBadge(QWidget* parent = nullptr);

    /// Creates a circle of the extent with the icon (fill bg/subtle, icon icon/default)
    explicit PDFIconBadge(const QString& iconName, int extent, int iconExtent, QWidget* parent = nullptr);

    const QString& getIconName() const { return m_badge.iconName; }
    void setIconName(const QString& iconName);

    /// Extent of the box (a negative value is 0)
    int getExtent() const { return m_badge.extent; }
    void setExtent(int extent);

    /// Extent of the icon (bounded by the extent of the box when drawn)
    int getIconExtent() const { return m_badge.iconExtent; }
    void setIconExtent(int extent);

    /// Radius of the box, a negative value = a circle
    qreal getRadius() const { return m_badge.radius; }
    void setRadius(qreal radius);

    /// Colors of the badge: the fill (std::nullopt = no fill, only the icon), the icon and the border
    std::optional<Color> getFillColor() const { return m_badge.fill; }
    Color getIconColor() const { return m_badge.iconColor; }
    std::optional<Color> getBorderColor() const { return m_badge.border; }
    void setColors(std::optional<Color> fill, Color iconColor, std::optional<Color> border = std::nullopt);

    /// Sets the colors of the hue: the fill accent/{hue}/bg and the icon accent/{hue}/icon, no border
    void setHue(PDFAccentHue hue);

    /// Draws the logo of the application instead of the icon
    bool isLogo() const { return m_badge.logo; }
    void setLogo(bool isLogo);

    const PDFComponentPainter::IconBadge& getBadge() const { return m_badge; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void init();

    PDFComponentPainter::IconBadge m_badge;
};

/// Status of an item as a pill of a hue (badge-up-to-date of the release card
/// of What's New): the icon 12 and the text Semi Bold 11 in the icon color of
/// the hue, height 21. Not interactive, announced as a text (its accessible
/// name is the text).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFStatusBadge : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFStatusBadge(QWidget* parent = nullptr);
    explicit PDFStatusBadge(const QString& text, const QString& iconName, PDFAccentHue hue, QWidget* parent = nullptr);

    /// Sets the whole badge at once (one new geometry and one repaint); the dot
    /// and the color of the text are kept
    void setBadge(const QString& text, const QString& iconName, PDFAccentHue hue);

    const QString& getText() const { return m_badge.text; }
    const QString& getIconName() const { return m_badge.iconName; }
    PDFAccentHue getHue() const { return m_badge.hue; }

    /// The dot of 6 px instead of the icon (the status badges of the page AI)
    bool isDotVisible() const { return m_badge.dot; }
    void setDotVisible(bool isVisible);

    /// Color of the label (std::nullopt: the icon color of the hue; an invalid value resets it)
    std::optional<PDFThemeColors::Color> getTextColor() const { return m_badge.textColor; }
    void setTextColor(std::optional<PDFThemeColors::Color> color);

    const PDFComponentPainter::StatusBadge& getBadge() const { return m_badge; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    PDFComponentPainter::StatusBadge m_badge;
};

/// Fact of a hero card (pill of About and Sponsor): a pill bg/surface with the
/// border, the icon 13 icon/muted and the label Medium 12 text/secondary,
/// height 25. Not interactive, announced as a text.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFFactPill : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFFactPill(QWidget* parent = nullptr);
    explicit PDFFactPill(const QString& iconName, const QString& text, QWidget* parent = nullptr);

    const QString& getText() const { return m_pill.text; }
    void setText(const QString& text);

    const QString& getIconName() const { return m_pill.iconName; }
    void setIconName(const QString& iconName);

    const PDFComponentPainter::FactPill& getPill() const { return m_pill; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    PDFComponentPainter::FactPill m_pill;
};

/// Avatar of a person (About, Sponsor): the initials Bold in a circle of the
/// hue (Blue by default). Not interactive; the application gives it the
/// accessible name of the person.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFAvatar : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFAvatar(QWidget* parent = nullptr);
    explicit PDFAvatar(const QString& initials, int extent, int fontPixelSize, QWidget* parent = nullptr);

    /// Initials (at most three characters are drawn)
    const QString& getInitials() const { return m_avatar.initials; }
    void setInitials(const QString& initials);

    int getExtent() const { return m_avatar.extent; }
    void setExtent(int extent);

    int getFontPixelSize() const { return m_avatar.fontPixelSize; }
    void setFontPixelSize(int pixelSize);

    PDFAccentHue getHue() const { return m_avatar.hue; }
    void setHue(PDFAccentHue hue);

    const PDFComponentPainter::Avatar& getAvatar() const { return m_avatar; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    PDFComponentPainter::Avatar m_avatar;
};

/// Row of a list of states (Sponsor, "What the time goes into now"): the dot
/// (bg/accent for an active item), the title and the subtitle (data, elided)
/// and the status tag at the trailing side, height 46. Without interaction;
/// announced as a text (the title, the subtitle and the status as its description).
class PDF4QTLIBFLUENTSHARED_EXPORT PDFStatusRow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    explicit PDFStatusRow(QWidget* parent = nullptr);
    explicit PDFStatusRow(const QString& title, const QString& subtitle, const QString& status, bool isActive, QWidget* parent = nullptr);

    const QString& getTitle() const { return m_row.title; }
    void setTitle(const QString& title);

    const QString& getSubtitle() const { return m_row.subtitle; }
    void setSubtitle(const QString& subtitle);

    /// Text of the status tag (empty: no tag)
    const QString& getStatus() const { return m_row.status; }
    void setStatus(const QString& status);

    /// An active item has the dot bg/accent
    bool isActive() const { return m_row.active; }
    void setActive(bool isActive);

    const PDFComponentPainter::StatusRow& getRow() const { return m_row; }

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

protected:
    virtual void paintEvent(QPaintEvent* event) override;
    virtual void changeEvent(QEvent* event) override;

private:
    void updateAccessibility();

    PDFComponentPainter::StatusRow m_row;
    QString m_name;
    QString m_description;
};

/// Card of a hue (the release card of the running version, the hero cards,
/// Thank you, the promise of Sponsor): the background and the border of the
/// hue, radius 12; without a hue the plain card bg/surface with border/default.
/// The application gives it a layout with its paddings.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFTintedCard : public QFrame
{
    Q_OBJECT

private:
    using BaseClass = QFrame;

public:
    explicit PDFTintedCard(QWidget* parent = nullptr);
    explicit PDFTintedCard(std::optional<PDFAccentHue> hue, QWidget* parent = nullptr);

    /// Hue of the card, std::nullopt = bg/surface with border/default
    std::optional<PDFAccentHue> getHue() const { return m_hue; }
    void setHue(std::optional<PDFAccentHue> hue);

    /// Radius of the corners (PDFComponentPainter::TINTED_CARD_RADIUS by default;
    /// a callout of the page AI has 8); a negative or NaN radius is 0
    qreal getRadius() const { return m_radius; }
    void setRadius(qreal radius);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    std::optional<PDFAccentHue> m_hue;
    qreal m_radius = PDFComponentPainter::TINTED_CARD_RADIUS;
};

}   // namespace pdf

#endif // PDFCOMPONENTBADGES_H
