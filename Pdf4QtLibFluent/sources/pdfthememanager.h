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

#ifndef PDFTHEMEMANAGER_H
#define PDFTHEMEMANAGER_H

#include "pdfthemecolors.h"

#include <QObject>

namespace pdf
{

/// Holds the themes of the application and the active one. The style and the
/// widgets of the library take all colors from here - never from a palette
/// and never by a cast of QWidget::style(), which can be wrapped by another style.
/// Functions are to be called from the thread of the user interface.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFThemeManager : public QObject
{
    Q_OBJECT

public:
    /// Returns the manager of the application
    static PDFThemeManager* getInstance();

    /// Returns the active theme
    const PDFThemeColors& getTheme() const { return m_theme; }

    /// Returns the color of the active theme
    QColor getColor(PDFThemeColors::Color color) const { return m_theme.getColor(color); }

    /// Returns all registered themes (built-in themes are the first ones)
    const std::vector<PDFThemeColors>& getThemes() const { return m_themes; }

    /// Returns the registered theme with the given identifier, or nullptr
    const PDFThemeColors* findTheme(const QString& id) const;

    /// Registers a theme, a theme with the same identifier is replaced.
    /// If the replaced theme is the active one, the new colors are applied.
    void registerTheme(const PDFThemeColors& theme);

    /// Activates the registered theme with the given identifier
    /// \returns False, if there is no such theme
    bool setTheme(const QString& id);

    /// Activates the built-in theme of the scheme
    void setScheme(PDFThemeColors::Scheme scheme);

    /// Number, which is changed with each change of the colors. It is a part
    /// of the keys of the caches of pixmaps created from the colors.
    quint64 getSerial() const { return m_serial; }

    /// The motion of the user interface (busy indicators, delayed hover effects)
    /// is reduced. It is set by the application from its settings.
    bool isReducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool reducedMotion);

Q_SIGNALS:
    /// Colors of the user interface have been changed
    void themeChanged();

private:
    explicit PDFThemeManager();

    void activate(const PDFThemeColors& theme);

    std::vector<PDFThemeColors> m_themes;
    PDFThemeColors m_theme;
    quint64 m_serial = 1;
    bool m_reducedMotion = false;
};

}   // namespace pdf

#endif // PDFTHEMEMANAGER_H
