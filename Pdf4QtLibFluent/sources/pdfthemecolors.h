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

#ifndef PDFTHEMECOLORS_H
#define PDFTHEMECOLORS_H

#include "pdffluentglobal.h"

#include <QColor>
#include <QString>
#include <QPalette>
#include <QObject>
#include <QByteArray>

#include <array>
#include <vector>

namespace pdf
{

/// Colors of a single theme of the graphical user interface. The colors are the
/// variables of the collection "Color" of the design (one enumeration value
/// for each variable, in the order of the design), a theme is one mode of the
/// collection. Themes built into the library are returned by \p getBuiltinThemes,
/// a new theme is a new table of colors.
class PDF4QTLIBFLUENTSHARED_EXPORT PDFThemeColors
{
    Q_GADGET

public:
    enum Color
    {
        BgCanvas,
        BgSidebar,
        BgTitlebar,
        BgSurface,
        BgSubtle,
        BgMuted,
        BgTrack,
        BgAccent,
        BgAccentSubtle,
        BgAccentSubtleHover,
        BgAccentSubtlePressed,
        BgOverlay,
        BgHover,
        BgPressed,
        BorderDefault,
        BorderStrong,
        BorderDivider,
        BorderSeparator,
        BorderFocus,
        TextPrimary,
        TextSecondary,
        TextTertiary,
        TextMuted,
        TextAccent,
        TextAccentPressed,
        TextDanger,
        IconDefault,
        IconMuted,
        IconStrong,
        IconAccent,
        IconOnAccent,
        IconPdf,
        IconDanger,
        IconSun,
        EffectShadow,
        AccentBlueBg,
        AccentBlueBgHover,
        AccentBlueBgPressed,
        AccentBlueBorder,
        AccentBlueBorderStrong,
        AccentBlueIcon,
        AccentVioletBg,
        AccentVioletBgHover,
        AccentVioletBgPressed,
        AccentVioletBorder,
        AccentVioletBorderStrong,
        AccentVioletIcon,
        AccentAmberBg,
        AccentAmberBgHover,
        AccentAmberBgPressed,
        AccentAmberBorder,
        AccentAmberBorderStrong,
        AccentAmberIcon,
        AccentRedBg,
        AccentRedBgHover,
        AccentRedBgPressed,
        AccentRedBorder,
        AccentRedBorderStrong,
        AccentRedIcon,
        AccentTealBg,
        AccentTealBgHover,
        AccentTealBgPressed,
        AccentTealBorder,
        AccentTealBorderStrong,
        AccentTealIcon,
        AccentGreenBg,
        AccentGreenBgHover,
        AccentGreenBgPressed,
        AccentGreenBorder,
        AccentGreenBorderStrong,
        AccentGreenIcon,
        AccentNeutralBg,
        AccentNeutralBgHover,
        AccentNeutralBgPressed,
        AccentNeutralBorder,
        AccentNeutralBorderStrong,
        AccentNeutralIcon,
        BrandLogoRed,
        BrandLogoGreen,
        BgScrim,
        SampleBlack,
        SampleWhite,
        ReadingPaperPage,
        ReadingPaperText,
        ReadingSepiaPage,
        ReadingSepiaText,
        ReadingSoftGrayPage,
        ReadingSoftGrayText,
        ReadingMintPage,
        ReadingMintText,
        ReadingNightPage,
        ReadingNightText,
        ReadingBlackPage,
        ReadingBlackText,
        ReadingTerminalPage,
        ReadingTerminalText,
        ReadingAmberPage,
        ReadingAmberText,
        ReadingHighContrastPage,
        ReadingHighContrastText,
        ReadingCustomPage,
        ReadingCustomText,
        SampleGrayDark,
        SampleGrayMid,
        SampleGrayLight,
        SampleBlue,
        SampleOrange,
        SampleBlueSoft,
        SampleOrangeSoft,
        AnnotationYellow,
        AnnotationOrange,
        AnnotationRed,
        AnnotationPink,
        AnnotationPurple,
        AnnotationBlue,
        AnnotationGreen,
        AnnotationBlack,
        AnnotationWhite,
        InkCyan,
        InkMagenta,
        InkYellow,
        InkBlack,
        InkSpotOrange,
        InkSpotGreen,
        InkAlarm,
        HeatLow,
        HeatMid,
        HeatHigh,
        HeatMax,
        DiffRemoved,
        DiffAdded,
        DiffReplaced,
        DiffMoved,
        BgAccentHover,
        BgAccentPressed,
        TextDisabled,
        IconDisabled,
        SearchMatch,
        SearchMatchCurrent,
        SearchMatchBorder,
        ColorCount
    };
    Q_ENUM(Color)

    enum class Scheme
    {
        Light,
        Dark
    };
    Q_ENUM(Scheme)

    /// Creates the built-in light theme
    PDFThemeColors();

    /// Creates a theme, which has the colors of the built-in theme
    /// of the given scheme. Colors are then changed by \p setColor.
    /// \param id Identifier of the theme (stored in the settings)
    /// \param name Name of the theme displayed to the user
    /// \param scheme Scheme, to which the theme belongs
    explicit PDFThemeColors(QString id, QString name, Scheme scheme);

    const QString& getId() const { return m_id; }
    QString getName() const;
    Scheme getScheme() const { return m_scheme; }
    bool isDark() const { return m_scheme == Scheme::Dark; }

    /// Returns the color of the theme. Some colors are translucent,
    /// they are drawn over the colors below.
    QColor getColor(Color color) const;

    /// Sets the color of the theme
    void setColor(Color color, QColor value);

    /// Returns the name of the variable in the design, for example "bg/accent"
    static QByteArray getTokenName(Color color);

    /// Returns the color with the given name of the variable in the design,
    /// \p ColorCount is returned, if there is no such variable.
    static Color getColorFromTokenName(const QByteArray& tokenName);

    /// Returns the value of the variable in the design as it is written there,
    /// "#rrggbb" or "#rrggbbaa", in the built-in theme of the given scheme.
    static QByteArray getBuiltinTokenValue(Color color, Scheme scheme);

    /// Returns the themes built into the library, the first one is the default
    /// light theme, the default dark theme follows.
    static std::vector<PDFThemeColors> getBuiltinThemes();

    /// Identifier of the built-in theme of the scheme
    static QString getBuiltinThemeId(Scheme scheme);

    /// Returns the color, by which the whole palette of the application is filled.
    /// Neither the style nor the widgets of the library read the palette, so this
    /// color is seen only where a color of the system leaks into the user interface.
    static QColor getLeakColor();

    /// Returns the palette, in which each role of each group is the leak color
    static QPalette createLeakPalette();

    bool operator==(const PDFThemeColors& other) const = default;

private:
    static QColor parseTokenValue(const char* value);

    QString m_id;
    QString m_name;
    bool m_isNameTranslated = false;    ///< Name of a built-in theme, which is translated
    Scheme m_scheme = Scheme::Light;
    std::array<QRgb, ColorCount> m_colors = { };
};

}   // namespace pdf

#endif // PDFTHEMECOLORS_H
