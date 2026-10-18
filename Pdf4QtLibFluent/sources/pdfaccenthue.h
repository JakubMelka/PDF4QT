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

#ifndef PDFACCENTHUE_H
#define PDFACCENTHUE_H

#include "pdfthemecolors.h"

namespace pdf
{

/// Hue of the accent colors of the design (variables accent/<hue>/...). The hue
/// is the category of a tool (Organize Pages Amber, Create & Convert Blue, Edit
/// Green, Protect & Sign Red, Optimize Violet, Review & Inspect Teal), or the
/// meaning of an element (stamps, resources). Neutral is for everything else.
enum class PDFAccentHue
{
    Blue,
    Violet,
    Amber,
    Red,
    Teal,
    Green,
    Neutral
};

/// Colors of an accent hue
class PDF4QTLIBFLUENTSHARED_EXPORT PDFAccentHueColors
{
public:
    PDFAccentHueColors() = delete;

    /// Role of a color of a hue, in the order of the variables of the design
    enum class Role
    {
        Background,
        BackgroundHover,
        BackgroundPressed,
        Border,
        BorderStrong,
        Icon
    };

    /// Returns the color of the hue (for example accent/green/bg-hover); an
    /// invalid hue is Neutral, an invalid role is Background
    static PDFThemeColors::Color getColor(PDFAccentHue hue, Role role);
};

}   // namespace pdf

#endif // PDFACCENTHUE_H
