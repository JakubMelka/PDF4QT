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

#include "pdfthemefonts.h"

#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>

namespace pdf
{

class PDFThemeFontsHelper
{
public:
    PDFThemeFontsHelper() = delete;

    enum class State
    {
        NotLoaded,
        Loaded,
        Failed
    };

    static State s_state;
    static QString s_family;
    static QString s_monospaceFamily;   ///< Empty = the fixed font of the system

    /// Loads one file of a font of the resources, returns its family (empty on a failure)
    static QString loadFile(const QString& fileName);
};

PDFThemeFontsHelper::State PDFThemeFontsHelper::s_state = PDFThemeFontsHelper::State::NotLoaded;
QString PDFThemeFontsHelper::s_family = QStringLiteral("Inter");
QString PDFThemeFontsHelper::s_monospaceFamily;

QString PDFThemeFontsHelper::loadFile(const QString& fileName)
{
    const int id = QFontDatabase::addApplicationFont(fileName);
    if (id < 0)
    {
        return QString();
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    return families.isEmpty() ? QString() : families.front();
}

bool PDFThemeFonts::loadFonts()
{
    if (PDFThemeFontsHelper::s_state != PDFThemeFontsHelper::State::NotLoaded)
    {
        return PDFThemeFontsHelper::s_state == PDFThemeFontsHelper::State::Loaded;
    }

    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
    {
        // Font database needs the application, try it again later
        return false;
    }

    bool ok = true;
    QString family;

    for (const char* fileName : { ":/pdf4qtfluent/fonts/Inter-Regular.ttf",
                                  ":/pdf4qtfluent/fonts/Inter-Medium.ttf",
                                  ":/pdf4qtfluent/fonts/Inter-SemiBold.ttf",
                                  ":/pdf4qtfluent/fonts/Inter-Bold.ttf" })
    {
        const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(fileName));

        if (id < 0)
        {
            ok = false;
            continue;
        }

        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (family.isEmpty() && !families.isEmpty())
        {
            family = families.front();
        }
    }

    if (!family.isEmpty())
    {
        PDFThemeFontsHelper::s_family = family;
    }

    // JetBrains Mono (owner decision Q5 of step 7, the static file of the
    // release, SIL OFL 1.1): its absence does not fail the fonts of the user
    // interface, the monospaced texts take the fixed font of the system then
    const QString monospaceFile = QStringLiteral(":/pdf4qtfluent/fonts/JetBrainsMono-Regular.ttf");
    if (QFile::exists(monospaceFile))
    {
        PDFThemeFontsHelper::s_monospaceFamily = PDFThemeFontsHelper::loadFile(monospaceFile);
    }

    PDFThemeFontsHelper::s_state = ok ? PDFThemeFontsHelper::State::Loaded : PDFThemeFontsHelper::State::Failed;
    return ok;
}

QString PDFThemeFonts::getFamily()
{
    loadFonts();
    return PDFThemeFontsHelper::s_family;
}

QFont PDFThemeFonts::getFont(Role role)
{
    switch (role)
    {
        case Role::Body:
            return getFont(QFont::Normal, 13);
        case Role::BodyMedium:
            return getFont(QFont::Medium, 13);
        case Role::BodySemiBold:
            return getFont(QFont::DemiBold, 13);
        case Role::Small:
            return getFont(QFont::Normal, 12);
        case Role::SmallMedium:
            return getFont(QFont::Medium, 12);
        case Role::SmallSemiBold:
            return getFont(QFont::DemiBold, 12);
        case Role::Caption:
            return getFont(QFont::Medium, 11);
        case Role::CaptionRegular:
            return getFont(QFont::Normal, 11);
        case Role::CaptionSemiBold:
            return getFont(QFont::DemiBold, 11);
        case Role::Tiny:
            return getFont(QFont::Normal, 10);
        case Role::Title:
            return getFont(QFont::Bold, 15);
        case Role::DialogTitle:
            return getFont(QFont::DemiBold, 16);
        case Role::TinySemiBold:
            return getFont(QFont::DemiBold, 10);
        case Role::BodyBold:
            return getFont(QFont::Bold, 13);
        case Role::TileTitle:
            return getFont(QFont::Bold, 14);
        case Role::Display:
            return getFont(QFont::Bold, 22);
    }

    return getFont(QFont::Normal, 13);
}

QString PDFThemeFonts::getMonospaceFamily()
{
    loadFonts();
    if (!PDFThemeFontsHelper::s_monospaceFamily.isEmpty())
    {
        return PDFThemeFontsHelper::s_monospaceFamily;
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

QFont PDFThemeFonts::getMonospaceFont(QFont::Weight weight, int pixelSize)
{
    QFont font(getMonospaceFamily());
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);

    // As the fonts of the user interface: without hinting
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QFont PDFThemeFonts::getFont(QFont::Weight weight, int pixelSize)
{
    QFont font(getFamily());
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);

    // The design is drawn without hinting, glyphs keep their designed widths
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

}   // namespace pdf
