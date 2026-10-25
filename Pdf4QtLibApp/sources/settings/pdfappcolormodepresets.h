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

#ifndef PDFAPPCOLORMODEPRESETS_H
#define PDFAPPCOLORMODEPRESETS_H

#include "pdfappglobal.h"

#include "pdfthemecolors.h"

#include <QColor>
#include <QString>

#include <vector>

namespace pdf
{

/// Preset of the Custom colors mode (Reading theme card of 160:4412): the page
/// and the text color stored into colorModes.pageColor / colorModes.textColor.
/// The colors are data (the values of the tokens reading/<preset>/*, which the
/// cards draw), the names of the colors are shown by the color buttons.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorModePreset
{
public:
    QString id;                                     ///< Value of colorModes.customPreset
    const char* title = "";                         ///< Source text of the name of the card
    QColor pageColor;                               ///< Page color (invalid for Custom)
    QColor textColor;                               ///< Text color (invalid for Custom)
    PDFThemeColors::Color pageToken = PDFThemeColors::ReadingCustomPage;
    PDFThemeColors::Color textToken = PDFThemeColors::ReadingCustomText;
    const char* pageColorName = "";                 ///< Source text of the name of the page color
    const char* textColorName = "";                 ///< Source text of the name of the text color

    /// The preset Custom… (the colors of the user)
    bool isCustom() const { return !pageColor.isValid(); }

    QString getTitle() const;
    QString getPageColorName() const;
    QString getTextColorName() const;
};

/// Presets of the Custom colors mode and the contrast of a pair of colors
/// (Settings › Color modes, package B of step 5)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppColorModePresets
{
public:
    PDFAppColorModePresets() = delete;

    static constexpr const char* TRANSLATION_CONTEXT = "pdf::PDFAppColorModePresets";

    /// Verdict of a contrast ratio (WCAG 2: 7, 4.5 and 3 are the limits)
    enum class Verdict
    {
        Excellent,  ///< At least 7 : 1
        Good,       ///< At least 4.5 : 1
        Low,        ///< At least 3 : 1
        Poor        ///< Below 3 : 1
    };

    /// Presets in the order of the cards (Paper … High contrast, Custom… last)
    static const std::vector<PDFAppColorModePreset>& getPresets();

    /// Preset of the id (nullptr for an unknown id)
    static const PDFAppColorModePreset* findPreset(const QString& id);

    /// Id of the preset Custom…
    static QString getCustomId() { return QStringLiteral("custom"); }

    /// Id of the preset, whose colors are the pair (the first one), empty = none
    static QString findPresetId(const QColor& pageColor, const QColor& textColor);

    /// Relative luminance of the color (WCAG 2, sRGB)
    static double getRelativeLuminance(const QColor& color);

    /// Contrast ratio of the two colors (1 … 21), 1 for an invalid color
    static double getContrastRatio(const QColor& first, const QColor& second);

    static Verdict getVerdict(double ratio);
    static QString getVerdictText(Verdict verdict);

    /// Text of the ratio ("10.7 : 1", one decimal in the language of the interface)
    static QString formatRatio(double ratio);

    /// Color of a stored text "#RRGGBB" (an invalid color for anything else)
    static QColor parseColor(const QString& text);

    /// Stored text of the color ("#RRGGBB", upper case)
    static QString formatColor(const QColor& color);

private:
    static std::vector<PDFAppColorModePreset> createPresets();
};

}   // namespace pdf

#endif // PDFAPPCOLORMODEPRESETS_H
