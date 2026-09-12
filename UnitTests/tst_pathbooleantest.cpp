// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include <QtTest>

#include "pdfpathboolean.h"

#include <cmath>

class PathBooleanTest : public QObject
{
    Q_OBJECT

private slots:
    void testEmptyOperands();
    void testDisjointPaths();
    void testDisjointPathsKeepCurves();
    void testRectangles();
    void testFillRuleOfOperands();
    void testMixedFillRules();
    void testSubtractionMakesHole();
    void testAlmostCoincidentEdges();
    void testCoincidentEdges_data();
    void testCoincidentEdges();
    void testDegenerateInput();
    void testHugeAndTinyCoordinates();

private:
    /// Returns the area of the filled region of the path
    static pdf::PDFReal getArea(const QPainterPath& path);

    /// Verifies the area of the path. The coordinates of the result are snapped
    /// to a grid, which is relative to the size of the geometry, so the areas
    /// can never be compared exactly.
    static void verifyArea(const QPainterPath& path, pdf::PDFReal expectedArea);

    static QPainterPath createPolygon(const std::vector<QPointF>& points, Qt::FillRule fillRule);
    static QPainterPath createRectangle(QRectF rect, Qt::FillRule fillRule);
};

pdf::PDFReal PathBooleanTest::getArea(const QPainterPath& path)
{
    // Jakub Melka: we deliberately sum up the signed areas of the subpaths
    // instead of using QPainterPath::toFillPolygons. toFillPolygons resolves the
    // overlaps using QPathClipper, i.e. exactly the code, which these functions
    // replace, so the test would inherit its precision problems. The results of
    // the tested functions have their outer boundaries and holes oriented
    // consistently, so the signed sum is the filled area. The paths used as the
    // expected values below are simple polygons, for which it holds as well.
    // The coordinates are taken relative to the center of the path. Without it
    // the terms of the shoelace formula would be huge compared to the area for a
    // small geometry, which lies far from the origin, and would cancel out.
    const QPointF center = path.controlPointRect().center();

    pdf::PDFReal area = 0.0;

    for (const QPolygonF& polygon : path.toSubpathPolygons())
    {
        for (int i = 0, count = polygon.size(); i < count; ++i)
        {
            const QPointF current = polygon[i] - center;
            const QPointF next = polygon[(i + 1) % count] - center;
            area += (current.x() * next.y() - next.x() * current.y()) * 0.5;
        }
    }

    return std::abs(area);
}

void PathBooleanTest::verifyArea(const QPainterPath& path, pdf::PDFReal expectedArea)
{
    const pdf::PDFReal area = getArea(path);
    const pdf::PDFReal tolerance = qMax(std::abs(expectedArea) * 1e-6, 1e-9);

    QVERIFY2(std::abs(area - expectedArea) <= tolerance,
             qPrintable(QString("area = %1, expected = %2").arg(area, 0, 'g', 12).arg(expectedArea, 0, 'g', 12)));
}

QPainterPath PathBooleanTest::createPolygon(const std::vector<QPointF>& points, Qt::FillRule fillRule)
{
    QPainterPath path;
    path.setFillRule(fillRule);

    for (size_t i = 0; i < points.size(); ++i)
    {
        if (i == 0)
        {
            path.moveTo(points[i]);
        }
        else
        {
            path.lineTo(points[i]);
        }
    }

    path.closeSubpath();
    return path;
}

QPainterPath PathBooleanTest::createRectangle(QRectF rect, Qt::FillRule fillRule)
{
    QPainterPath path;
    path.setFillRule(fillRule);
    path.addRect(rect);
    return path;
}

void PathBooleanTest::testEmptyOperands()
{
    QPainterPath empty;
    QPainterPath rectangle = createRectangle(QRectF(0, 0, 10, 10), Qt::OddEvenFill);

    QVERIFY(pdf::PDFPathBoolean::intersect(rectangle, empty).isEmpty());
    QVERIFY(pdf::PDFPathBoolean::intersect(empty, rectangle).isEmpty());
    QVERIFY(pdf::PDFPathBoolean::intersect(empty, empty).isEmpty());

    verifyArea(pdf::PDFPathBoolean::subtract(rectangle, empty), 100.0);
    QVERIFY(pdf::PDFPathBoolean::subtract(empty, rectangle).isEmpty());

    verifyArea(pdf::PDFPathBoolean::unite(rectangle, empty), 100.0);
    verifyArea(pdf::PDFPathBoolean::unite(empty, rectangle), 100.0);
    QVERIFY(pdf::PDFPathBoolean::unite(empty, empty).isEmpty());
}

void PathBooleanTest::testDisjointPaths()
{
    QPainterPath first = createRectangle(QRectF(0, 0, 10, 10), Qt::OddEvenFill);
    QPainterPath second = createRectangle(QRectF(100, 100, 10, 10), Qt::OddEvenFill);

    QVERIFY(pdf::PDFPathBoolean::intersect(first, second).isEmpty());
    verifyArea(pdf::PDFPathBoolean::subtract(first, second), 100.0);
    verifyArea(pdf::PDFPathBoolean::unite(first, second), 200.0);

    // The same, but with operands, which have different fill rules, so the
    // shortcut for disjoint paths cannot be used for the union
    QPainterPath third = createRectangle(QRectF(100, 100, 10, 10), Qt::WindingFill);
    verifyArea(pdf::PDFPathBoolean::unite(first, third), 200.0);
}

void PathBooleanTest::testDisjointPathsKeepCurves()
{
    // A path, which cannot be touched by the operation, must be returned
    // unchanged - including its curves. Precompiled pages store paths in the
    // page space and paint them at an arbitrary zoom, so flattening them
    // during redaction would be visible.
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, 10, 10));

    QPainterPath farAway = createRectangle(QRectF(100, 100, 10, 10), Qt::OddEvenFill);
    QPainterPath result = pdf::PDFPathBoolean::subtract(circle, farAway);

    QCOMPARE(result.elementCount(), circle.elementCount());

    bool hasCurve = false;
    for (int i = 0; i < result.elementCount(); ++i)
    {
        hasCurve = hasCurve || result.elementAt(i).isCurveTo();
    }
    QVERIFY(hasCurve);
}

void PathBooleanTest::testRectangles()
{
    QPainterPath first = createRectangle(QRectF(0, 0, 10, 10), Qt::OddEvenFill);
    QPainterPath second = createRectangle(QRectF(5, 5, 10, 10), Qt::OddEvenFill);

    verifyArea(pdf::PDFPathBoolean::intersect(first, second), 25.0);
    verifyArea(pdf::PDFPathBoolean::subtract(first, second), 75.0);
    verifyArea(pdf::PDFPathBoolean::unite(first, second), 175.0);

    // Intersection is commutative, difference is not
    verifyArea(pdf::PDFPathBoolean::intersect(second, first), 25.0);
    verifyArea(pdf::PDFPathBoolean::subtract(second, first), 75.0);
}

void PathBooleanTest::testFillRuleOfOperands()
{
    // Two concentric squares in the same direction. Using the even-odd fill rule
    // the inner square is a hole, using the non-zero fill rule it is filled.
    auto createConcentricSquares = [](Qt::FillRule fillRule)
    {
        QPainterPath path;
        path.setFillRule(fillRule);
        path.addRect(QRectF(0, 0, 10, 10));
        path.addRect(QRectF(2.5, 2.5, 5, 5));
        return path;
    };

    QPainterPath clipPath = createRectangle(QRectF(0, 0, 10, 10), Qt::OddEvenFill);

    const pdf::PDFReal oddEvenArea = getArea(pdf::PDFPathBoolean::intersect(createConcentricSquares(Qt::OddEvenFill), clipPath));
    const pdf::PDFReal windingArea = getArea(pdf::PDFPathBoolean::intersect(createConcentricSquares(Qt::WindingFill), clipPath));

    // Even-odd: outer square without the inner one; non-zero: the whole square
    QVERIFY(qAbs(oddEvenArea - 75.0) < 1e-4);
    QVERIFY(qAbs(windingArea - 100.0) < 1e-4);
}

void PathBooleanTest::testMixedFillRules()
{
    // The clipping path uses the even-odd fill rule and has a hole, the clipped
    // path uses the non-zero fill rule. The fill rule of each operand must be
    // resolved using that operand's own rule.
    QPainterPath clipPath;
    clipPath.setFillRule(Qt::OddEvenFill);
    clipPath.addRect(QRectF(0, 0, 10, 10));
    clipPath.addRect(QRectF(2.5, 2.5, 5, 5));

    QPainterPath path = createRectangle(QRectF(0, 0, 10, 10), Qt::WindingFill);

    // 100 (path) intersected with 75 (square with a hole) = 75
    verifyArea(pdf::PDFPathBoolean::intersect(path, clipPath), 75.0);
    verifyArea(pdf::PDFPathBoolean::intersect(clipPath, path), 75.0);

    // The whole square minus the square with a hole leaves the hole
    verifyArea(pdf::PDFPathBoolean::subtract(path, clipPath), 25.0);

    // Union of both is the full square
    verifyArea(pdf::PDFPathBoolean::unite(path, clipPath), 100.0);
}

void PathBooleanTest::testSubtractionMakesHole()
{
    QPainterPath outer = createRectangle(QRectF(0, 0, 10, 10), Qt::OddEvenFill);
    QPainterPath inner = createRectangle(QRectF(2.5, 2.5, 5, 5), Qt::OddEvenFill);

    QPainterPath result = pdf::PDFPathBoolean::subtract(outer, inner);
    verifyArea(result, 75.0);

    // The hole must really be a hole, not just a second boundary
    QVERIFY(result.contains(QPointF(1.0, 1.0)));
    QVERIFY(!result.contains(QPointF(5.0, 5.0)));
}

void PathBooleanTest::testAlmostCoincidentEdges()
{
    // Issue #328. The tile of a rotated tiling pattern paints a rectangle, which
    // is a hair larger than the pattern bounding box used to clip the tile
    // (the content stream scales by 208.90261, the /BBox is 208.9026), so two
    // edges of the clipping path are almost - but not exactly - coincident with
    // two edges of the clipped path. QPainterPath::intersected returns the whole
    // unclipped diamond for this input.
    //
    // The coordinates are the ones captured from the renderer.
    QPainterPath diamond = createPolygon({ { 458.24737091045876, 403.24463489026584 },
                                           { 525.44893208691985, 336.07626552267232 },
                                           { 458.24737091045876, 268.90884605494404 },
                                           { 391.04580973399766, 336.07721542253751 } }, Qt::WindingFill);

    QPainterPath clipPath = createPolygon({ { 413.61325218221629, 313.52092578633687 },
                                            { 391.04581295088235, 336.07721863778789 },
                                            { 413.61325218221629, 358.6331924965383 } }, Qt::OddEvenFill);

    // The clipping triangle lies inside the diamond, so the intersection is the
    // triangle itself
    const pdf::PDFReal expectedArea = getArea(clipPath);
    QVERIFY(expectedArea > 500.0 && expectedArea < 520.0);

    const QPainterPath result = pdf::PDFPathBoolean::intersect(diamond, clipPath);
    QVERIFY(qAbs(getArea(result) - expectedArea) < 1.0);

    // The result must not escape the clipping path
    QVERIFY(clipPath.controlPointRect().adjusted(-0.1, -0.1, 0.1, 0.1).contains(result.controlPointRect()));
}

void PathBooleanTest::testCoincidentEdges_data()
{
    QTest::addColumn<double>("offset");

    // The clipping triangle shares its apex with the left vertex of the diamond.
    // The offset moves the apex along the diagonal, from well inside the diamond,
    // through the exactly coincident position, to well outside of it.
    for (int i = -8; i <= 8; ++i)
    {
        const double offset = (i == 0) ? 0.0 : std::pow(10.0, double(i) * 0.5 - 4.0) * ((i < 0) ? -1.0 : 1.0);
        QTest::addRow("offset=%g", offset) << offset;
    }
}

void PathBooleanTest::testCoincidentEdges()
{
    QFETCH(double, offset);

    // A diamond with its left vertex at the origin and a triangle cut off it by
    // a vertical line. The apex of the triangle is moved by the offset, so it is
    // exactly on, slightly inside or slightly outside the diamond vertex.
    const double size = 100.0;
    QPainterPath diamond = createPolygon({ { 0.0, 0.0 },
                                           { size, -size },
                                           { 2.0 * size, 0.0 },
                                           { size, size } }, Qt::WindingFill);

    const double cut = 20.0;
    QPainterPath clipPath = createPolygon({ { cut, -cut },
                                            { offset, offset },
                                            { cut, cut } }, Qt::OddEvenFill);

    const QPainterPath result = pdf::PDFPathBoolean::intersect(diamond, clipPath);

    // Whatever the offset is, the result must never be bigger than the clipping
    // path, and it must never escape its bounding box - that is the failure mode
    // of issue #328. The tolerance covers the grid, to which the coordinates of
    // the result are snapped; the failure mode itself is off by a factor of 50,
    // so it is detected regardless.
    QVERIFY(getArea(result) <= getArea(clipPath) * (1.0 + 1e-5) + 1e-6);
    QVERIFY(clipPath.controlPointRect().adjusted(-1e-3, -1e-3, 1e-3, 1e-3).contains(result.controlPointRect()));

    // For a non-negative offset the whole triangle lies inside the diamond
    if (offset >= 0.0)
    {
        verifyArea(result, getArea(clipPath));
    }
}

void PathBooleanTest::testDegenerateInput()
{
    QPainterPath rectangle = createRectangle(QRectF(0, 0, 10, 10), Qt::OddEvenFill);

    // A path degenerated to a line has no area
    QPainterPath line;
    line.moveTo(0, 5);
    line.lineTo(10, 5);

    QVERIFY(pdf::PDFPathBoolean::intersect(rectangle, line).isEmpty());
    verifyArea(pdf::PDFPathBoolean::subtract(rectangle, line), 100.0);

    // A path degenerated to a single point
    QPainterPath point;
    point.moveTo(5, 5);
    point.lineTo(5, 5);

    QVERIFY(pdf::PDFPathBoolean::intersect(rectangle, point).isEmpty());

    // Both operands degenerated - this must not divide by a zero extent
    QVERIFY(pdf::PDFPathBoolean::intersect(point, point).isEmpty());
}

void PathBooleanTest::testHugeAndTinyCoordinates()
{
    // The working space is normalized, so neither the magnitude of the
    // coordinates nor the size of the geometry may matter.
    for (double scale : { 1e-6, 1e-3, 1.0, 1e3, 1e6 })
    {
        for (double origin : { 0.0, 1e6, -1e6 })
        {
            QPainterPath first = createRectangle(QRectF(origin, origin, 10.0 * scale, 10.0 * scale), Qt::OddEvenFill);
            QPainterPath second = createRectangle(QRectF(origin + 5.0 * scale, origin + 5.0 * scale, 10.0 * scale, 10.0 * scale), Qt::OddEvenFill);

            const pdf::PDFReal expectedArea = 25.0 * scale * scale;
            const pdf::PDFReal area = getArea(pdf::PDFPathBoolean::intersect(first, second));

            QVERIFY2(qAbs(area - expectedArea) < expectedArea * 1e-4,
                     qPrintable(QString("scale = %1, origin = %2, area = %3, expected = %4").arg(scale).arg(origin).arg(area).arg(expectedArea)));
        }
    }
}

QTEST_MAIN(PathBooleanTest)

#include "tst_pathbooleantest.moc"
