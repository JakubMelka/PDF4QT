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

#include "pdfpathboolean.h"

#include <QTransform>

#include "clipper2/clipper.h"

#include <cmath>

namespace pdf
{

namespace
{

/// Size of the working coordinate space. Both operands are translated and scaled
/// so that their common bounding box fits into a square of this size. Two things
/// depend on the resolution of that space - the flattening of Bézier curves and
/// the integer grid of Clipper2 - and normalizing it makes both of them relative
/// to the size of the geometry instead of absolute. It also bounds the magnitude
/// of the integer coordinates, so the Clipper2 range cannot be exceeded.
/// \note The size also decides how finely Bézier curves are flattened, because
///       QPainterPath::toSubpathPolygons flattens them with a fixed tolerance of
///       half a unit of the space it maps them into. Half a unit of 4096 is an
///       error of 1/8192 of the size of the geometry, which is several times
///       finer than what QPathClipper achieves for a page sized path, and still
///       keeps the number of the produced segments low.
constexpr PDFReal NORMALIZED_EXTENT = 4096.0;

/// Number of decimal places of the Clipper2 integer grid, in the working space.
/// Together with NORMALIZED_EXTENT this snaps the coordinates to a grid of
/// 1e-4 / 65536 of the size of the geometry, which is far below the resolution
/// of any rasterization of it. The integer coordinates stay below 66e7, so
/// neither they, nor the products, which Clipper2 computes from them, can
/// overflow the supported range.
constexpr int CLIPPER_PRECISION = 4;

using namespace Clipper2Lib;

/// Maps the working space coordinates back to the original ones
struct PDFWorkSpace
{
    QPointF origin;
    PDFReal scale = 1.0;

    QTransform getTranslationMatrix() const
    {
        return QTransform::fromTranslate(-origin.x(), -origin.y());
    }

    QTransform getScaleMatrix() const
    {
        return QTransform::fromScale(scale, scale);
    }

    QPointF mapBack(const PointD& point) const
    {
        return QPointF(point.x / scale + origin.x(), point.y / scale + origin.y());
    }
};

bool isFinite(const QRectF& rect)
{
    return std::isfinite(rect.left()) && std::isfinite(rect.top()) &&
           std::isfinite(rect.right()) && std::isfinite(rect.bottom());
}

FillRule getClipperFillRule(Qt::FillRule fillRule)
{
    return (fillRule == Qt::OddEvenFill) ? FillRule::EvenOdd : FillRule::NonZero;
}

/// Converts the path to Clipper2 polygons in the working space. The fill rule of
/// the path is not applied here - the caller decides, under which fill rule the
/// polygons are interpreted.
PathsD getPaths(const QPainterPath& path, const PDFWorkSpace& workSpace)
{
    // Jakub Melka: the translation and the scaling must be applied as two
    // separate steps. Folding them into a single matrix would compute the
    // coordinate as x * scale - origin * scale, and for a small geometry, which
    // lies far from the origin, the two large terms cancel each other out and
    // the precision of the result is lost. Translating first keeps the
    // subtraction exact, and the scaling of the small numbers is exact as well.
    // The curves are flattened by the second step, so the flattening happens in
    // the normalized space and its resolution scales with the geometry.
    const QPainterPath translatedPath = workSpace.getTranslationMatrix().map(path);
    const QList<QPolygonF> polygons = translatedPath.toSubpathPolygons(workSpace.getScaleMatrix());

    PathsD paths;
    paths.reserve(polygons.size());

    for (const QPolygonF& polygon : polygons)
    {
        if (polygon.size() < 3)
        {
            // Degenerate subpath, it has no area
            continue;
        }

        PathD convertedPolygon;
        convertedPolygon.reserve(polygon.size());

        for (const QPointF& point : polygon)
        {
            convertedPolygon.push_back(PointD(point.x(), point.y()));
        }

        paths.push_back(std::move(convertedPolygon));
    }

    return paths;
}

/// Rewrites the polygons so that they do not overlap and the outer boundaries
/// and the holes are oriented consistently. Such polygons describe the same area
/// under any fill rule, so a normalized operand can take part in an operation,
/// which is performed under the fill rule of the other operand.
PathsD normalizePaths(const PathsD& paths, Qt::FillRule fillRule)
{
    if (paths.empty())
    {
        return paths;
    }

    return Union(paths, getClipperFillRule(fillRule), CLIPPER_PRECISION);
}

QPainterPath getPainterPath(const PathsD& paths, const PDFWorkSpace& workSpace)
{
    QPainterPath path;

    // Jakub Melka: Clipper2 orients the outer boundaries and the holes
    // consistently, so the non-zero fill rule is the correct one here.
    path.setFillRule(Qt::WindingFill);

    for (const PathD& convertedPolygon : paths)
    {
        if (convertedPolygon.size() < 3)
        {
            continue;
        }

        QPolygonF polygon;
        polygon.reserve(int(convertedPolygon.size()) + 1);

        for (const PointD& point : convertedPolygon)
        {
            polygon << workSpace.mapBack(point);
        }

        // Clipper2 returns implicitly closed polygons
        polygon << polygon.front();
        path.addPolygon(polygon);
        path.closeSubpath();
    }

    return path;
}

}   // namespace

QPainterPath PDFPathBoolean::intersect(const QPainterPath& path, const QPainterPath& clipPath)
{
    return perform(Operation::Intersection, path, clipPath);
}

QPainterPath PDFPathBoolean::subtract(const QPainterPath& path, const QPainterPath& subtractedPath)
{
    return perform(Operation::Difference, path, subtractedPath);
}

QPainterPath PDFPathBoolean::unite(const QPainterPath& path, const QPainterPath& unitedPath)
{
    return perform(Operation::Union, path, unitedPath);
}

QPainterPath PDFPathBoolean::perform(Operation operation, const QPainterPath& path, const QPainterPath& otherPath)
{
    // Trivial cases - an empty operand makes the result one of the operands,
    // which we then return unchanged, including its Bézier curves
    if (path.isEmpty() || otherPath.isEmpty())
    {
        switch (operation)
        {
            case Operation::Intersection:
                return QPainterPath();

            case Operation::Difference:
                return path.isEmpty() ? QPainterPath() : path;

            case Operation::Union:
                return path.isEmpty() ? otherPath : path;
        }
    }

    const QRectF boundingBox = path.controlPointRect();
    const QRectF otherBoundingBox = otherPath.controlPointRect();

    if (!isFinite(boundingBox) || !isFinite(otherBoundingBox))
    {
        // We cannot normalize the working space for a path with non-finite
        // coordinates. Such path cannot be painted anyway, so just fall back
        // to the Qt implementation instead of failing.
        switch (operation)
        {
            case Operation::Intersection:
                return path.intersected(otherPath);

            case Operation::Difference:
                return path.subtracted(otherPath);

            case Operation::Union:
                return path.united(otherPath);
        }
    }

    // When the paths cannot overlap at all, the result is known without
    // computing anything. This is not just an optimization - it also keeps the
    // curves of the returned operand intact, which matters for the paths of a
    // precompiled page, which are stored in the page space and are painted at
    // an arbitrary zoom.
    if (!boundingBox.intersects(otherBoundingBox))
    {
        switch (operation)
        {
            case Operation::Intersection:
                return QPainterPath();

            case Operation::Difference:
                return path;

            case Operation::Union:
            {
                if (path.fillRule() == otherPath.fillRule())
                {
                    QPainterPath result = path;
                    result.addPath(otherPath);
                    return result;
                }

                // Fill rules differ, so the paths cannot be simply concatenated
                // and the operation has to be computed
                break;
            }
        }
    }

    const QRectF workSpaceBoundingBox = boundingBox.united(otherBoundingBox);
    const PDFReal extent = qMax(workSpaceBoundingBox.width(), workSpaceBoundingBox.height());

    if (!(extent > 0.0))
    {
        // Both paths degenerated to a single point, so they have no area
        return (operation == Operation::Union) ? path : QPainterPath();
    }

    PDFWorkSpace workSpace;
    workSpace.origin = workSpaceBoundingBox.topLeft();
    workSpace.scale = NORMALIZED_EXTENT / extent;

    try
    {
        PathsD paths = getPaths(path, workSpace);
        PathsD otherPaths = getPaths(otherPath, workSpace);

        // Clipper2 applies a single fill rule to both operands. When the fill
        // rules differ, we normalize one of them, because normalized polygons
        // are interpreted identically under both rules, and then perform the
        // operation under the fill rule of the other one. We normalize the
        // smaller operand - normalization is a boolean operation itself, so it
        // is the expensive part for a path with many segments.
        Qt::FillRule fillRule = path.fillRule();

        if (path.fillRule() != otherPath.fillRule())
        {
            if (path.elementCount() <= otherPath.elementCount())
            {
                paths = normalizePaths(paths, path.fillRule());
                fillRule = otherPath.fillRule();
            }
            else
            {
                otherPaths = normalizePaths(otherPaths, otherPath.fillRule());
            }
        }

        const FillRule clipperFillRule = getClipperFillRule(fillRule);

        PathsD result;

        switch (operation)
        {
            case Operation::Intersection:
                result = Intersect(paths, otherPaths, clipperFillRule, CLIPPER_PRECISION);
                break;

            case Operation::Difference:
                result = Difference(paths, otherPaths, clipperFillRule, CLIPPER_PRECISION);
                break;

            case Operation::Union:
                result = Union(paths, otherPaths, clipperFillRule, CLIPPER_PRECISION);
                break;
        }

        return getPainterPath(result, workSpace);
    }
    catch (const Clipper2Lib::Clipper2Exception&)
    {
        // Clipper2 reports range and precision errors using exceptions. The
        // working space is normalized, so this should not happen, but we must
        // not let the exception escape into the painting code.
        switch (operation)
        {
            case Operation::Intersection:
                return path.intersected(otherPath);

            case Operation::Difference:
                return path.subtracted(otherPath);

            case Operation::Union:
                return path.united(otherPath);
        }
    }

    return QPainterPath();
}

}   // namespace pdf
