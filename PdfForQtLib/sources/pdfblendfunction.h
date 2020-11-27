//    Copyright (C) 2019-2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFBLENDFUNCTION_H
#define PDFBLENDFUNCTION_H

#include "pdfglobal.h"

#include <QPainter>

namespace pdf
{

enum class BlendMode
{
    // Separable blending modes
    Normal,     ///< Select source color, backdrop is ignored: B(source, dest) = source
    Multiply,   ///< Backdrop and source colors are multiplied: B(source, dest) = source * dest
    Screen,     ///< B(source, dest) = 1.0 - (1.0 - source) * (1.0 - dest) + source + dest - source * dest
    Overlay,    ///< B(source, dest) = HardLight(dest, source)
    Darken,     ///< Selects the darker of the colors. B(source, dest) = qMin(source, dest)
    Lighten,    ///< Selects the lighter of the colors. B(source, dest) = qMax(source, dest)
    ColorDodge, ///< Brightens the backdrop color reflecting the source color. B(source, dest) = qMin(1.0, dest / (1.0 - source)), or 1.0, if source is 1.0
    ColorBurn,  ///< Darkens the backdrop color reflecting the source color. B(source, dest) = 1.0 - qMin(1.0, (1.0 - dest) / source), or 0.0, if source is 0.0
    HardLight,  ///< Multiply or screen the color, depending on the source value.
    SoftLight,  ///< Darkens or lightens the color, depending on the source value.
    Difference, ///< Subtract the darker of colors from the lighter color. B(source, dest) = qAbs(source - dest)
    Exclusion,  ///< B(source, dest) = source + dest - 2 * source * dest

    // Non-separable blending modes
    Hue,
    Saturation,
    Color,
    Luminosity,

    // For compatibility - older PDF specification specifies Compatible mode, which is equal
    // to normal. It should be recognized for sake of compatibility.
    Compatible, ///< Equals to normal

    // Invalid blending mode - for internal purposes only
    Invalid
};

class PDFBlendModeInfo
{
public:
    PDFBlendModeInfo() = delete;

    /// Returns blend mode from the specified string. If string is invalid,
    /// then \p Invalid blend mode is returned.
    /// \param name Name of the blend mode
    static BlendMode getBlendMode(const QByteArray& name);

    /// Returns true, if blend mode is supported by Qt drawing subsystem
    /// \param mode Blend mode
    static bool isSupportedByQt(BlendMode mode);

    /// Returns composition mode for Qt drawing subsystem from blend mode defined
    /// in PDF standard. If blend mode is not supported by Qt drawing subsystem, then default
    /// composition mode is returned.
    /// \param mode Blend mode
    static QPainter::CompositionMode getCompositionModeFromBlendMode(BlendMode mode);

    /// Returns blend mode name
    /// \param mode Blend mode
    static QString getBlendModeName(BlendMode mode);

    /// Returns blend mode translated name
    /// \param mode Blend mode
    static QString getBlendModeTranslatedName(BlendMode mode);

    /// Returns vector of all blend modes, excluding duplicate ones (for example,
    /// Compatible mode is equal to Normal blend mode)
    static std::vector<BlendMode> getBlendModes();
};

}   // namespace pdf

#endif // PDFBLENDFUNCTION_H
