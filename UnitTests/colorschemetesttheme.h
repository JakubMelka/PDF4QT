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

#ifndef COLORSCHEMETESTTHEME_H
#define COLORSCHEMETESTTHEME_H

// Needs Qt6::GuiPrivate (the test defines PDF4QT_TEST_PLATFORM_THEME, when it is linked)

#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/qpa/qplatformtheme.h>
#include <QtGui/qpa/qwindowsysteminterface.h>

#include <QFileInfo>
#include <QIcon>
#include <QKeySequence>
#include <QPixmap>

/// Platform theme of a test with a color scheme of the "system", which the test
/// changes, and with the scheme requested by the application (QStyleHints::setColorScheme).
/// A requested scheme overrides the scheme of the system and hides its changes, as
/// QWindowsTheme does. The change of the theme is delivered synchronously (as by
/// QWindowsTheme) or through the event loop (as by QGnomeTheme / QGtk3Theme).
/// The offscreen platform ignores the requests, so it cannot reproduce errors
/// of a requested scheme. The theme replaces the platform theme of the application
/// for its lifetime; everything else is answered by the replaced theme.
class ColorSchemeTestTheme : public QPlatformTheme
{
public:
    explicit ColorSchemeTestTheme(Qt::ColorScheme systemScheme, bool isSynchronous = true) :
        m_original(QGuiApplicationPrivate::platform_theme),
        m_systemScheme(systemScheme),
        m_isSynchronous(isSynchronous)
    {
        QGuiApplicationPrivate::platform_theme = this;
        QWindowSystemInterface::handleThemeChange<QWindowSystemInterface::SynchronousDelivery>();
    }

    ~ColorSchemeTestTheme() override
    {
        // Changes of the theme still waiting in the queue are delivered to this theme
        QWindowSystemInterface::flushWindowSystemEvents();
        if (QGuiApplicationPrivate::platform_theme == this)
        {
            QGuiApplicationPrivate::platform_theme = m_original;

            // The replaced theme gets the last request of the application. A theme keeping
            // the request (QWindowsTheme) would otherwise report the scheme requested before
            // this theme was installed, and an application following the system (it does
            // not request anything again) would not see the scheme of the system.
            if (m_requestCount > 0)
            {
                m_original->requestColorScheme(m_requestedScheme);
            }
        }
        QWindowSystemInterface::handleThemeChange<QWindowSystemInterface::SynchronousDelivery>();
    }

    ColorSchemeTestTheme(const ColorSchemeTestTheme&) = delete;
    ColorSchemeTestTheme& operator=(const ColorSchemeTestTheme&) = delete;

    /// Scheme requested by the application, Unknown = the application follows the system
    Qt::ColorScheme getRequestedScheme() const { return m_requestedScheme; }

    /// Number of the requests (also of the same scheme)
    int getRequestCount() const { return m_requestCount; }

    /// The system changes its color scheme
    void setSystemScheme(Qt::ColorScheme scheme)
    {
        m_systemScheme = scheme;
        notify();
    }

    virtual Qt::ColorScheme colorScheme() const override
    {
        return m_requestedScheme != Qt::ColorScheme::Unknown ? m_requestedScheme : m_systemScheme;
    }

    virtual void requestColorScheme(Qt::ColorScheme scheme) override
    {
        m_requestedScheme = scheme;
        ++m_requestCount;
        notify();
    }

    virtual QPlatformMenuItem* createPlatformMenuItem() const override { return m_original->createPlatformMenuItem(); }
    virtual QPlatformMenu* createPlatformMenu() const override { return m_original->createPlatformMenu(); }
    virtual QPlatformMenuBar* createPlatformMenuBar() const override { return m_original->createPlatformMenuBar(); }
    virtual void showPlatformMenuBar() override { m_original->showPlatformMenuBar(); }
#ifndef QT_NO_SYSTEMTRAYICON
    virtual QPlatformSystemTrayIcon* createPlatformSystemTrayIcon() const override { return m_original->createPlatformSystemTrayIcon(); }
#endif
    virtual bool usePlatformNativeDialog(DialogType type) const override { return m_original->usePlatformNativeDialog(type); }
    virtual QPlatformDialogHelper* createPlatformDialogHelper(DialogType type) const override { return m_original->createPlatformDialogHelper(type); }
    virtual const QPalette* palette(Palette type) const override { return m_original->palette(type); }
    virtual const QFont* font(Font type) const override { return m_original->font(type); }
    virtual QVariant themeHint(ThemeHint hint) const override { return m_original->themeHint(hint); }
    virtual QPixmap standardPixmap(StandardPixmap sp, const QSizeF& size) const override { return m_original->standardPixmap(sp, size); }
    virtual QIcon fileIcon(const QFileInfo& fileInfo, QPlatformTheme::IconOptions iconOptions) const override { return m_original->fileIcon(fileInfo, iconOptions); }
    virtual QIconEngine* createIconEngine(const QString& iconName) const override { return m_original->createIconEngine(iconName); }
    virtual QList<QKeySequence> keyBindings(QKeySequence::StandardKey key) const override { return m_original->keyBindings(key); }
    virtual QString standardButtonText(int button) const override { return m_original->standardButtonText(button); }
    virtual QKeySequence standardButtonShortcut(int button) const override { return m_original->standardButtonShortcut(button); }
    virtual Qt::ContrastPreference contrastPreference() const override { return m_original->contrastPreference(); }

private:
    void notify()
    {
        if (m_isSynchronous)
        {
            QWindowSystemInterface::handleThemeChange<QWindowSystemInterface::SynchronousDelivery>();
        }
        else
        {
            QWindowSystemInterface::handleThemeChange<QWindowSystemInterface::AsynchronousDelivery>();
        }
    }

    QPlatformTheme* m_original;
    Qt::ColorScheme m_systemScheme;
    Qt::ColorScheme m_requestedScheme = Qt::ColorScheme::Unknown;
    int m_requestCount = 0;
    bool m_isSynchronous;
};

#endif // COLORSCHEMETESTTHEME_H
