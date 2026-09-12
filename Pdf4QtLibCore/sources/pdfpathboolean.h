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

#ifndef PDFPATHBOOLEAN_H
#define PDFPATHBOOLEAN_H

#include "pdfglobal.h"

#include <QPainterPath>

namespace pdf
{

/// Boolean set operations on painter paths. These functions are a replacement
/// for QPainterPath::intersected/subtracted/united, which are computed by
/// QPathClipper in floating point arithmetic and are not numerically robust -
/// when an edge of one operand is almost coincident with an edge of the other
/// one, QPathClipper can silently return one of the operands instead of the
/// result of the operation. Such input is not exotic at all, it is produced for
/// example by tiling patterns, where the content of a tile paints exactly the
/// tile bounding box, which is at the same time the clipping path of the tile.
///
/// These functions use Clipper2 instead, which decides the topology using exact
/// integer arithmetic, so this class of failures cannot occur. Both operands are
/// first normalized using their own fill rule, so paths with different fill
/// rules are combined correctly.
///
/// \note Because Clipper2 is a polygon clipper, Bézier curves are flattened.
///       QPathClipper flattens them as well (see the note in the documentation
///       of QPainterPath::united), so this is not a change in behaviour, but the
///       operations should not be used where the curves must be preserved.
///       Operations, whose result is one of the operands unchanged (an operation
///       with an empty path, or with paths, which cannot overlap at all), return
///       that operand including its curves.
class PDF4QTLIBCORESHARED_EXPORT PDFPathBoolean
{
public:
    PDFPathBoolean() = delete;

    /// Returns intersection of the two paths, i.e. the area, which is filled
    /// in both of them. Empty path is returned, if the paths do not overlap.
    /// \param path First path
    /// \param clipPath Second path
    static QPainterPath intersect(const QPainterPath& path, const QPainterPath& clipPath);

    /// Returns difference of the two paths, i.e. the area, which is filled in
    /// \p path and is not filled in \p subtractedPath.
    /// \param path Path, from which is subtracted
    /// \param subtractedPath Subtracted path
    static QPainterPath subtract(const QPainterPath& path, const QPainterPath& subtractedPath);

    /// Returns union of the two paths, i.e. the area, which is filled in at
    /// least one of them.
    /// \param path First path
    /// \param unitedPath Second path
    static QPainterPath unite(const QPainterPath& path, const QPainterPath& unitedPath);

private:
    enum class Operation
    {
        Intersection,
        Difference,
        Union
    };

    static QPainterPath perform(Operation operation, const QPainterPath& path, const QPainterPath& otherPath);
};

}   // namespace pdf

#endif // PDFPATHBOOLEAN_H
