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

#include "pdfaccenthue.h"

namespace pdf
{

PDFThemeColors::Color PDFAccentHueColors::getColor(PDFAccentHue hue, Role role)
{
    // The variables of each hue follow each other in the order of the roles,
    // the hues follow each other in the order of the enumeration
    constexpr int ROLE_COUNT = 6;
    static_assert(PDFThemeColors::AccentVioletBg - PDFThemeColors::AccentBlueBg == ROLE_COUNT);
    static_assert(PDFThemeColors::AccentNeutralIcon - PDFThemeColors::AccentBlueBg == 7 * ROLE_COUNT - 1);
    static_assert(static_cast<int>(Role::Icon) == ROLE_COUNT - 1);

    // An invalid hue is Neutral, an invalid role the background: their index
    // would be another token of the theme (a color of another meaning)
    int hueIndex = static_cast<int>(hue);
    if (hueIndex < static_cast<int>(PDFAccentHue::Blue) || hueIndex > static_cast<int>(PDFAccentHue::Neutral))
    {
        hueIndex = static_cast<int>(PDFAccentHue::Neutral);
    }

    int roleIndex = static_cast<int>(role);
    if (roleIndex < 0 || roleIndex >= ROLE_COUNT)
    {
        roleIndex = static_cast<int>(Role::Background);
    }

    const int index = PDFThemeColors::AccentBlueBg + hueIndex * ROLE_COUNT + roleIndex;
    return static_cast<PDFThemeColors::Color>(index);
}

}   // namespace pdf
