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

#ifndef PDFTHEMEICONENGINE_H
#define PDFTHEMEICONENGINE_H

#include "pdfthemecolors.h"

#include <QIcon>
#include <QIconEngine>
#include <QStringList>

class QPainter;

namespace pdf
{

/// Icons of the design (24 px grid, stroke of 2 px, single color). An icon is
/// drawn from its SVG in the requested size and it is filled by a color of the
/// active theme, so the same icon serves all states and all themes.
///
/// The functions (and the icon engine) are to be called only from the thread of
/// the user interface: they read the active theme of PDFThemeManager, keep
/// their pixmaps in QPixmapCache and share the parsed SVG renderers (QObjects)
/// in caches without a lock.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFThemeIcons
{
public:
    PDFThemeIcons() = delete;

    /// Round badge of 36 px with an icon, as message boxes show it
    enum class Badge
    {
        Information,
        Question,
        Warning,
        Critical,
        Success
    };

    /// Returns the icon of the design with the given name ("check", "chevron-down", ...).
    /// The icon follows the mode (normal, disabled, active, selected) and the active
    /// theme. A null icon is returned, if there is no icon of that name.
    static QIcon getIcon(const QString& name);

    /// Returns the icon of the badge. The badge follows the active theme.
    static QIcon getBadgeIcon(Badge badge);

    /// Draws the badge into the rectangle (in logical pixels)
    static void paintBadge(QPainter* painter, const QRectF& rect, Badge badge);

    /// Returns the names of all icons
    static QStringList getIconNames();

    /// Returns true, if the icon with the given name exists
    static bool hasIcon(const QString& name);

    /// Returns the name of the icon, if the icon has been created by \p getIcon,
    /// otherwise an empty string.
    static QString getIconName(const QIcon& icon);

    /// Returns the name of the icon of the design, which replaces an icon of the
    /// icon theme of the system (QIcon::fromTheme, for example the icons of the
    /// standard context menus of the text widgets). The system draws such icons
    /// by the colors of the palette. An empty string is returned, if the icon
    /// is not an icon of the icon theme or if the design has no replacement.
    static QString getReplacementName(const QIcon& icon);

    /// Returns the color, by which an icon is drawn in the given mode
    static PDFThemeColors::Color getModeColor(QIcon::Mode mode, QIcon::State state = QIcon::Off);

    /// Draws the icon into the rectangle (in logical pixels) by the given color
    static void paint(QPainter* painter, const QRectF& rect, const QString& name, const QColor& color);

    /// Draws a filled icon: the inside of the shape of the icon by the fill color
    /// and the strokes over it by the stroke color (the filled bookmark of
    /// Bookmark row, the amber lock of a document tab). The icons of the design
    /// have a single color, the fill is the area enclosed by their strokes.
    static void paintFilled(QPainter* painter, const QRectF& rect, const QString& name, const QColor& strokeColor, const QColor& fillColor);

    /// Returns the pixmap of the icon
    /// \param name Name of the icon
    /// \param size Size in logical pixels
    /// \param devicePixelRatio Device pixel ratio of the pixmap
    /// \param color Color of the icon
    static QPixmap getPixmap(const QString& name, const QSizeF& size, qreal devicePixelRatio, const QColor& color);

    /// Returns the pixmap of the area enclosed by the strokes of the icon
    /// filled by the color (without the strokes)
    static QPixmap getFillPixmap(const QString& name, const QSizeF& size, qreal devicePixelRatio, const QColor& color);

    /// Returns the number of the parsed SVG renderers kept by the icons (for
    /// the tests: the caches are bounded - one renderer of the strokes of the
    /// design and one of the fill for each existing icon and at most 256 of
    /// other widths of the strokes; missing icons keep no renderer)
    static qsizetype getCachedRendererCount();
};

/// Engine of the icons returned by \p PDFThemeIcons::getIcon (in the thread of
/// the user interface only, see PDFThemeIcons)
class PDF4QTLIBFLUENTSHARED_EXPORT PDFThemeIconEngine : public QIconEngine
{
public:
    explicit PDFThemeIconEngine(QString name);

    virtual void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override;
    virtual QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override;
    virtual QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override;
    virtual QSize actualSize(const QSize& size, QIcon::Mode mode, QIcon::State state) override;
    virtual QIconEngine* clone() const override;
    virtual QString key() const override;
    virtual QString iconName() override;
    virtual bool isNull() override;

    /// Prefix of the names returned by \p iconName
    static QString getIconNamePrefix();

private:
    QString m_name;
};

}   // namespace pdf

#endif // PDFTHEMEICONENGINE_H
