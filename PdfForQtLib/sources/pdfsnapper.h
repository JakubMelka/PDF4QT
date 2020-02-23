//    Copyright (C) 2020 Jakub Melka
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

#ifndef PDFSNAPPER_H
#define PDFSNAPPER_H

#include "pdfglobal.h"

#include <array>

class QPainter;

namespace pdf
{
class PDFPrecompiledPage;

enum class SnapType
{
    Invalid,
    PageCorner,        ///< Corner of the page media box
    ImageCorner,       ///< Corner of image
    PageCenter,        ///< Center of page media box
    ImageCenter,       ///< Center of image
    LineCenter,        ///< Center of line
    GeneratedLineProjection    ///< Generated point to line projections
};

/// Contain informations for snap points in the pdf page. Snap points
/// can be for example image centers, rectangle corners, line start/end
/// points, page boundary boxes etc. All coordinates are in page coordinates.
class PDFSnapInfo
{
public:
    explicit inline PDFSnapInfo() = default;

    struct SnapPoint
    {
        explicit inline constexpr SnapPoint() = default;
        explicit inline constexpr SnapPoint(SnapType type, QPointF point) :
            type(type),
            point(point)
        {

        }

        SnapType type = SnapType::Invalid;
        QPointF point;
    };

    /// Adds page media box. Media box must be in page coordinates.
    /// \param mediaBox Media box
    void addPageMediaBox(const QRectF& mediaBox);

    /// Adds image box. Because it is not guaranteed, that it will be rectangle, five
    /// points are defined - four corners and center.
    /// \param points Four corner points in clockwise order, fifth point is center,
    ///        all in page coordinates.
    void addImage(const std::array<QPointF, 5>& points);

    /// Adds line and line center points
    /// \param start Start point of line, in page coordinates
    /// \param end End point of line, in page coordinates
    void addLine(const QPointF& start, const QPointF& end);

    /// Returns snap points
    const std::vector<SnapPoint>& getSnapPoints() const { return m_snapPoints; }

private:
    std::vector<SnapPoint> m_snapPoints;
    std::vector<QLineF> m_snapLines;
};

/// Snap engine, which handles snapping of points on the page.
class PDFSnapper
{
public:
    PDFSnapper();

    /// Draws snapping points onto the page
    /// \param painter Painter
    /// \param pageIndex Page index
    /// \param compiledPage Compiled page
    /// \param pagePointToDevicePointMatrix Matrix mapping page space to device point space
    void drawSnapPoints(QPainter* painter,
                        pdf::PDFInteger pageIndex,
                        const PDFPrecompiledPage* compiledPage,
                        const QMatrix& pagePointToDevicePointMatrix) const;

private:
    PDFSnapInfo m_currentPageSnapInfo;
    PDFInteger m_currentPage = -1;
};

}   // namespace pdf

#endif // PDFSNAPPER_H
