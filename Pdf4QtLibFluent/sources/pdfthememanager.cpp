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

#include "pdfthememanager.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace pdf
{

PDFThemeManager::PDFThemeManager() :
    QObject(nullptr),
    m_themes(PDFThemeColors::getBuiltinThemes()),
    m_theme(m_themes.front())
{
#ifdef Q_OS_WIN
    // Setting "Animation effects" of the system
    BOOL isAnimated = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &isAnimated, 0))
    {
        m_reducedMotion = !isAnimated;
    }
#endif
}

PDFThemeManager* PDFThemeManager::getInstance()
{
    static PDFThemeManager instance;
    return &instance;
}

const PDFThemeColors* PDFThemeManager::findTheme(const QString& id) const
{
    for (const PDFThemeColors& theme : m_themes)
    {
        if (theme.getId() == id)
        {
            return &theme;
        }
    }

    return nullptr;
}

void PDFThemeManager::registerTheme(const PDFThemeColors& theme)
{
    bool isReplaced = false;
    for (PDFThemeColors& current : m_themes)
    {
        if (current.getId() == theme.getId())
        {
            current = theme;
            isReplaced = true;
            break;
        }
    }

    if (!isReplaced)
    {
        m_themes.push_back(theme);
    }

    if (m_theme.getId() == theme.getId())
    {
        activate(theme);
    }
}

bool PDFThemeManager::setTheme(const QString& id)
{
    const PDFThemeColors* theme = findTheme(id);

    if (!theme)
    {
        return false;
    }

    activate(*theme);
    return true;
}

void PDFThemeManager::setScheme(PDFThemeColors::Scheme scheme)
{
    setTheme(PDFThemeColors::getBuiltinThemeId(scheme));
}

void PDFThemeManager::setReducedMotion(bool reducedMotion)
{
    m_reducedMotion = reducedMotion;
}

void PDFThemeManager::activate(const PDFThemeColors& theme)
{
    if (m_theme == theme)
    {
        return;
    }

    // The theme can be an item of m_themes, which is replaced by the assignment
    const PDFThemeColors copy = theme;
    m_theme = copy;
    ++m_serial;
    Q_EMIT themeChanged();
}

}   // namespace pdf
