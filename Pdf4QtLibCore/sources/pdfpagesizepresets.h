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

#ifndef PDFPAGESIZEPRESETS_H
#define PDFPAGESIZEPRESETS_H

#include "pdfglobal.h"

#include <QSizeF>
#include <QString>
#include <QStringView>

#include <optional>
#include <vector>

namespace pdf
{

/// The named sizes of pages (plan of step 2 of phase 2, 5.4; plan of step 3, 4.3, K16): one
/// table of the values for Blank PDF, Resize Pages, Insert Pages, Add Image as Page and Images
/// to PDF. Only the values live here - each tool has the list and the texts of its own frame
/// (K16). The sizes are portrait, in points, exact: ISO 216 in millimeters (mm x 72 / 25.4),
/// the US sizes and 16:9 in points.
///
/// Every function is reentrant and thread safe (constant data only).
class PDF4QTLIBCORESHARED_EXPORT PDFPageSizePresets
{
public:
    PDFPageSizePresets() = delete;

    /// Identifiers of the sizes (the values of the parameters of the tools and of MCP)
    static constexpr const char* A3 = "a3";
    static constexpr const char* A4 = "a4";
    static constexpr const char* A5 = "a5";
    static constexpr const char* LETTER = "letter";
    static constexpr const char* LEGAL = "legal";
    static constexpr const char* SIZE_16X9 = "16x9";

    /// Limits of a side of a page in points (K26 of step 2; PDF Reference 1.7, Table C.1)
    static constexpr double MINIMUM_SIDE_POINTS = 3.0;
    static constexpr double MAXIMUM_SIDE_POINTS = 14400.0;

    /// The same limits in millimeters
    static constexpr double MINIMUM_SIDE_MILLIMETERS = MINIMUM_SIDE_POINTS * 25.4 / 72.0;
    static constexpr double MAXIMUM_SIDE_MILLIMETERS = MAXIMUM_SIDE_POINTS * 25.4 / 72.0;

    /// A named size
    struct Preset
    {
        const char* id;         ///< Identifier ("a4")
        double width;           ///< Width of the portrait page in points
        double height;          ///< Height of the portrait page in points
    };

    /// All named sizes in the order A3, A4, A5, Letter, Legal, 16:9
    static const std::vector<Preset>& getPresets();

    /// Portrait size of a named size in points, std::nullopt for an unknown identifier
    static std::optional<QSizeF> getSize(QStringView id);

    /// Identifier of the named size, which \p sizeInPoints is in either orientation (each side
    /// within \p tolerance points); empty = none
    static QString findPreset(const QSizeF& sizeInPoints, double tolerance);

    /// Points of millimeters (mm x 72 / 25.4)
    static double getPoints(double millimeters);

    /// Millimeters of points (pt x 25.4 / 72)
    static double getMillimeters(double points);

    /// The side in points is a valid side of a page (MINIMUM_SIDE_POINTS to MAXIMUM_SIDE_POINTS,
    /// finite; a limit given in millimeters counts up to the rounding of its conversion)
    static bool isValidSide(double points);
};

}   // namespace pdf

#endif // PDFPAGESIZEPRESETS_H
