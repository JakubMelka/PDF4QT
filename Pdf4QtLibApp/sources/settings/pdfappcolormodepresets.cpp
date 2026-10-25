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

#include "pdfappcolormodepresets.h"
#include "pdfapplocale.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace pdf
{

QString PDFAppColorModePreset::getTitle() const
{
    return QCoreApplication::translate(PDFAppColorModePresets::TRANSLATION_CONTEXT, title);
}

QString PDFAppColorModePreset::getPageColorName() const
{
    return QCoreApplication::translate(PDFAppColorModePresets::TRANSLATION_CONTEXT, pageColorName);
}

QString PDFAppColorModePreset::getTextColorName() const
{
    return QCoreApplication::translate(PDFAppColorModePresets::TRANSLATION_CONTEXT, textColorName);
}

const std::vector<PDFAppColorModePreset>& PDFAppColorModePresets::getPresets()
{
    static const std::vector<PDFAppColorModePreset> presets = createPresets();
    return presets;
}

std::vector<PDFAppColorModePreset> PDFAppColorModePresets::createPresets()
{
    using C = PDFThemeColors;

    struct Row
    {
        const char* id;
        const char* title;
        QRgb page;
        QRgb text;
        C::Color pageToken;
        C::Color textToken;
        const char* pageName;
        const char* textName;
    };

    // Reading theme cards of 160:4412 (the values of the tokens reading/*, which
    // the cards draw); the names of the colors for the color buttons (160:4441:
    // the text color of Amber is "Amber", its page "Near black")
    static const Row rows[] =
    {
        { "paper", QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Paper"), 0xFFFFFF, 0x111111, C::ReadingPaperPage, C::ReadingPaperText,
          QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "White"), QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Ink black") },
        { "sepia", QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Sepia"), 0xF4ECD8, 0x5B4636, C::ReadingSepiaPage, C::ReadingSepiaText,
          QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Sepia"), QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Sepia brown") },
        { "softGray", QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Soft gray"), 0xE8E8E8, 0x2B2B2B, C::ReadingSoftGrayPage, C::ReadingSoftGrayText,
          QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Soft gray"), QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Charcoal") },
        { "mint", QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Mint"), 0xCCE8CF, 0x1B2A20, C::ReadingMintPage, C::ReadingMintText,
          QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Mint"), QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Forest green") },
        { "night", QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Night"), 0x1E2228, 0xD6D9DE, C::ReadingNightPage, C::ReadingNightText,
          QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Night blue"), QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Silver") },
        { "black", QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Black"), 0x000000, 0xF2F2F2, C::ReadingBlackPage, C::ReadingBlackText,
          QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Black"), QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Off-white") },
        { "terminal", QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Terminal"), 0x0B0B10, 0x00FF7F, C::ReadingTerminalPage, C::ReadingTerminalText,
          QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Near black"), QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Terminal green") },
        { "amber", QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Amber"), 0x0B0B10, 0xFFB000, C::ReadingAmberPage, C::ReadingAmberText,
          QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Near black"), QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Amber") },
        { "highContrast", QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "High contrast"), 0x000000, 0xFFFF00, C::ReadingHighContrastPage, C::ReadingHighContrastText,
          QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Black"), QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Yellow") }
    };

    std::vector<PDFAppColorModePreset> result;
    for (const Row& row : rows)
    {
        PDFAppColorModePreset preset;
        preset.id = QString::fromLatin1(row.id);
        preset.title = row.title;
        preset.pageColor = QColor::fromRgb(row.page);
        preset.textColor = QColor::fromRgb(row.text);
        preset.pageToken = row.pageToken;
        preset.textToken = row.textToken;
        preset.pageColorName = row.pageName;
        preset.textColorName = row.textName;
        result.push_back(preset);
    }

    // Custom… (160:4429): the sample of the card are the stored colors
    PDFAppColorModePreset custom;
    custom.id = getCustomId();
    custom.title = QT_TRANSLATE_NOOP("pdf::PDFAppColorModePresets", "Custom…");
    result.push_back(custom);
    return result;
}

const PDFAppColorModePreset* PDFAppColorModePresets::findPreset(const QString& id)
{
    for (const PDFAppColorModePreset& preset : getPresets())
    {
        if (preset.id == id)
        {
            return &preset;
        }
    }
    return nullptr;
}

QString PDFAppColorModePresets::findPresetId(const QColor& pageColor, const QColor& textColor)
{
    if (!pageColor.isValid() || !textColor.isValid())
    {
        return QString();
    }

    for (const PDFAppColorModePreset& preset : getPresets())
    {
        if (!preset.isCustom() && preset.pageColor.rgb() == pageColor.rgb() && preset.textColor.rgb() == textColor.rgb())
        {
            return preset.id;
        }
    }
    return QString();
}

double PDFAppColorModePresets::getRelativeLuminance(const QColor& color)
{
    const QColor rgb = color.toRgb();
    const double channels[3] = { rgb.redF(), rgb.greenF(), rgb.blueF() };
    double linear[3] = { };
    for (int i = 0; i < 3; ++i)
    {
        const double value = std::clamp(channels[i], 0.0, 1.0);
        linear[i] = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2];
}

double PDFAppColorModePresets::getContrastRatio(const QColor& first, const QColor& second)
{
    if (!first.isValid() || !second.isValid())
    {
        return 1.0;
    }

    const double a = getRelativeLuminance(first);
    const double b = getRelativeLuminance(second);
    const double ratio = (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
    return std::clamp(ratio, 1.0, 21.0);
}

PDFAppColorModePresets::Verdict PDFAppColorModePresets::getVerdict(double ratio)
{
    // The ratio is compared as it is shown (one decimal): "4.5 : 1" is Good
    const double shown = std::round(ratio * 10.0) / 10.0;
    if (shown >= 7.0)
    {
        return Verdict::Excellent;
    }
    if (shown >= 4.5)
    {
        return Verdict::Good;
    }
    if (shown >= 3.0)
    {
        return Verdict::Low;
    }
    return Verdict::Poor;
}

QString PDFAppColorModePresets::getVerdictText(Verdict verdict)
{
    switch (verdict)
    {
        case Verdict::Excellent:
            return QCoreApplication::translate(TRANSLATION_CONTEXT, "Excellent");
        case Verdict::Good:
            return QCoreApplication::translate(TRANSLATION_CONTEXT, "Good");
        case Verdict::Low:
            return QCoreApplication::translate(TRANSLATION_CONTEXT, "Low");
        case Verdict::Poor:
            return QCoreApplication::translate(TRANSLATION_CONTEXT, "Poor");
    }
    return QString();
}

QString PDFAppColorModePresets::formatRatio(double ratio)
{
    return QCoreApplication::translate(TRANSLATION_CONTEXT, "%1 : 1").arg(PDFAppLocale::getUiLocale().toString(ratio, 'f', 1));
}

QColor PDFAppColorModePresets::parseColor(const QString& text)
{
    static const QRegularExpression expression(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    if (!expression.match(text).hasMatch())
    {
        return QColor();
    }
    return QColor::fromString(text);
}

QString PDFAppColorModePresets::formatColor(const QColor& color)
{
    return color.isValid() ? color.name(QColor::HexRgb).toUpper() : QString();
}

}   // namespace pdf
