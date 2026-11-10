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

#include "pdfpagesizepresets.h"

#include <cmath>

namespace pdf
{

const std::vector<PDFPageSizePresets::Preset>& PDFPageSizePresets::getPresets()
{
    // ISO 216 in millimeters, the US sizes and 16:9 in points (5.4 of the plan of step 2)
    static const std::vector<Preset> presets =
    {
        { A3, getPoints(297.0), getPoints(420.0) },
        { A4, getPoints(210.0), getPoints(297.0) },
        { A5, getPoints(148.0), getPoints(210.0) },
        { LETTER, 612.0, 792.0 },
        { LEGAL, 612.0, 1008.0 },
        { SIZE_16X9, 540.0, 960.0 },
    };
    return presets;
}

std::optional<QSizeF> PDFPageSizePresets::getSize(QStringView id)
{
    for (const Preset& preset : getPresets())
    {
        if (id == QLatin1StringView(preset.id))
        {
            return QSizeF(preset.width, preset.height);
        }
    }
    return std::nullopt;
}

QString PDFPageSizePresets::findPreset(const QSizeF& sizeInPoints, double tolerance)
{
    for (const Preset& preset : getPresets())
    {
        const QSizeF size(preset.width, preset.height);
        for (const QSizeF& oriented : { size, size.transposed() })
        {
            if (std::abs(oriented.width() - sizeInPoints.width()) <= tolerance && std::abs(oriented.height() - sizeInPoints.height()) <= tolerance)
            {
                return QString::fromLatin1(preset.id);
            }
        }
    }
    return QString();
}

double PDFPageSizePresets::getPoints(double millimeters)
{
    return millimeters * 72.0 / 25.4;
}

double PDFPageSizePresets::getMillimeters(double points)
{
    return points * 25.4 / 72.0;
}

bool PDFPageSizePresets::isValidSide(double points)
{
    // A limit given in millimeters (the definitions of width and height) is the limit in points
    // up to the rounding of the conversion
    static constexpr double TOLERANCE = 1e-9;
    return std::isfinite(points) && points >= MINIMUM_SIDE_POINTS - TOLERANCE && points <= MAXIMUM_SIDE_POINTS + TOLERANCE;
}

}   // namespace pdf
