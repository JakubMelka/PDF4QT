//    Copyright (C) 2019 Jakub Melka
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

#ifndef PDFPATTERN_H
#define PDFPATTERN_H

#include "pdfobject.h"
#include "pdffunction.h"
#include "pdfcolorspaces.h"

#include <QMatrix>

#include <memory>

namespace pdf
{
class PDFPattern;
class PDFShadingPattern;

using PDFPatternPtr = std::shared_ptr<PDFPattern>;

enum class PatternType
{
    Invalid = 0,
    Tiling = 1,
    Shading = 2,
};

enum class ShadingType
{
    Invalid = 0,
    Function = 1,
    Axial = 2,
    Radial = 3,
    FreeFormGouradTriangle = 4,
    LatticeFormGouradTriangle = 5,
    CoonsPatchMesh = 6,
    TensorProductPatchMesh = 7
};

struct PDFMeshQualitySettings
{
    /// Initializes default resolution
    void initDefaultResolution();

    /// Matrix, which transforms user space points (user space is target space of the shading)
    /// to the device space of the paint device.
    QMatrix userSpaceToDeviceSpaceMatrix;

    /// Rectangle in device space coordinate system, onto which is area meshed.
    QRectF deviceSpaceMeshingArea;

    /// Preferred mesh resolution in device space pixels. Mesh will be created in this
    /// resolution, if it is smooth enough (no jumps in colors occurs).
    PDFReal preferredMeshResolution = 1.0;

    /// Minimal mesh resolution in device space pixels. If jumps in colors occurs (jump
    /// is two colors, that differ more than \p color tolerance), then mesh is meshed to
    /// minimal mesh resolution.
    PDFReal minimalMeshResolution = 1.0;

    /// Color tolerance - 1% by default
    PDFReal tolerance = 0.01;

    /// Test points to determine maximal curvature of the tensor product patch meshes
    PDFInteger patchTestPoints = 256;

    /// Lower value of the surface curvature meshing resolution mapping. When ratio between
    /// current curvature at the center of meshed triangle and maximal curvature is below
    /// this value, then prefered mesh resolution is used. If ratio is higher than this value
    /// and lower than \p patchResolutionMappingRatioHigh, then target length is linearly mapped.
    /// If value is higher, than \p patchResolutionMappingRatioHigh, then minimal mesh resolution
    /// is used when generating triangles on the patch.
    PDFReal patchResolutionMappingRatioLow = 0.3;

    /// Highter value of the surface curvature meshing resolution mapping. \sa patchResolutionMappingRatioLow
    PDFReal patchResolutionMappingRatioHigh = 0.9;
};

/// Mesh consisting of triangles
class PDFMesh
{
public:
    explicit PDFMesh() = default;

    struct Triangle
    {
        uint32_t v1 = 0;
        uint32_t v2 = 0;
        uint32_t v3 = 0;

        QRgb color;
    };

    /// Adds vertex. Returns index of added vertex.
    /// \param vertex Vertex to be added
    /// \returns Index of the added vertex
    inline uint32_t addVertex(const QPointF& vertex) { const size_t index = m_vertices.size(); m_vertices.emplace_back(vertex); return static_cast<uint32_t>(index); }

    /// Adds triangle. Returns index of added triangle.
    /// \param triangle Triangle to be added
    /// \returns Index of the added vertex
    inline uint32_t addTriangle(const Triangle& triangle) { const size_t index = m_triangles.size(); m_triangles.emplace_back(triangle); return static_cast<uint32_t>(index); }

    /// Adds quad. Vertices are in clockwise order (so, we have edges v1-v2, v2-v3, v3-v4, v4-v1).
    /// \param v1 First vertex (for example, topleft)
    /// \param v2 Second vertex (for example, topright)
    /// \param v3 Third vertex (for example, bottomright)
    /// \param v4 Fourth vertex (for example, bottomleft)
    /// \param color Color of the quad.
    inline void addQuad(uint32_t v1, uint32_t v2, uint32_t v3, uint32_t v4, QRgb color) { addTriangle({v1, v2, v3, color}); addTriangle({ v1, v3, v4, color}); }

    /// Paints the mesh on the painter
    /// \param painter Painter, onto which is mesh drawn
    void paint(QPainter* painter) const;

    /// Transforms the mesh according to the matrix transform
    /// \param matrix Matrix transform to be performed
    void transform(const QMatrix& matrix);

    /// Reserves memory for meshing - both number of vertices and triangles.
    /// Use this function, if number of vertices and triangles is known.
    /// \param vertexCount Vertex count
    /// \param triangleCount Triangle count
    void reserve(size_t vertexCount, size_t triangleCount) { m_vertices.reserve(vertexCount); m_triangles.reserve(triangleCount); }

    const QPainterPath& getBoundingPath() const { return m_boundingPath; }
    void setBoundingPath(const QPainterPath& path) { m_boundingPath = path; }

    /// Sets the vertex array to the mesh
    /// \param vertices New vertex array
    void setVertices(std::vector<QPointF>&& vertices) { m_vertices = qMove(vertices); }

    /// Sets the triangle array to the mesh
    /// \param triangles New triangle array
    void setTriangles(std::vector<Triangle>&& triangles) { m_triangles = qMove(triangles); }

    /// Returns vertex at given index
    /// \param index Index of the vertex
    const QPointF& getVertex(size_t index) const { return m_vertices[index]; }

    /// Returns triangle center. Triangles vertice indices must be valid.
    /// \param triangle Triangle
    QPointF getTriangleCenter(const Triangle& triangle) const;

    /// Sets the background path. In order to draw background properly, the background
    /// color must be set to a valid color.
    /// \param path Background path
    void setBackgroundPath(QPainterPath path) { m_backgroundPath = qMove(path); }

    /// Sets the background color (background path is then painted with this color, if it is not
    /// empty), if color is invalid, it turns off background painting.
    /// \param backgroundColor Background color
    void setBackgroundColor(QColor backgroundColor) { m_backgroundColor = backgroundColor; }

private:
    std::vector<QPointF> m_vertices;
    std::vector<Triangle> m_triangles;
    QPainterPath m_boundingPath;
    QPainterPath m_backgroundPath;
    QColor m_backgroundColor;
};

/// Represents tiling/shading pattern
class PDFPattern
{
public:
    explicit PDFPattern() = default;
    virtual ~PDFPattern() = default;

    virtual PatternType getType() const = 0;
    virtual const PDFShadingPattern* getShadingPattern() const = 0;

    /// Returns bounding box in the shadings target coordinate system (not in
    /// pattern coordinate system).
    const QRectF& getBoundingBox() const { return m_boundingBox; }

    /// Returns transformation matrix from pattern space to the default
    /// target space.
    const QMatrix& getMatrix() const { return m_matrix; }

    /// Create pattern from the object. If error occurs, exception is thrown
    /// \param colorSpaceDictionary Color space dictionary
    /// \param document Document, owning the pdf object
    /// \param object Object defining the pattern
    static PDFPatternPtr createPattern(const PDFDictionary* colorSpaceDictionary, const PDFDocument* document, const PDFObject& object);

    /// Create shading pattern from the object. If error occurs, exception is thrown
    /// \param colorSpaceDictionary Color space dictionary
    /// \param document Document, owning the pdf object
    /// \param object Object defining the shading
    /// \param matrix Matrix converting reference coordinate system to the device coordinate system
    /// \param patternGraphicState Pattern graphic state
    /// \param ignoreBackgroundColor If set, then ignores background color, even if it is present
    static PDFPatternPtr createShadingPattern(const PDFDictionary* colorSpaceDictionary,
                                              const PDFDocument* document,
                                              const PDFObject& shadingObject,
                                              const QMatrix& matrix,
                                              const PDFObject& patternGraphicState,
                                              bool ignoreBackgroundColor);

protected:
    QRectF m_boundingBox;
    QMatrix m_matrix;
};

class PDFInvalidPattern : public PDFPattern
{
public:
    explicit PDFInvalidPattern() = default;

    virtual PatternType getType() const { return PatternType::Invalid; }
    virtual const PDFShadingPattern* getShadingPattern() const { return nullptr; }
};

/// Shading pattern - smooth color distribution along the pattern's space
class PDFShadingPattern : public PDFPattern
{
public:
    explicit PDFShadingPattern() = default;

    virtual PatternType getType() const override;
    virtual ShadingType getShadingType() const = 0;
    virtual const PDFShadingPattern* getShadingPattern() const override { return this; }

    /// Creates a colored mesh using settings. Mesh is generated in device space
    /// coordinate system. You must transform the mesh, if you want to
    /// use it in another coordinate system.
    /// \param settings Meshing settings
    virtual PDFMesh createMesh(const PDFMeshQualitySettings& settings) const = 0;

    /// Returns patterns graphic state. This state must be applied before
    /// the shading pattern is painted to the target device.
    const PDFObject& getPatternGraphicState() const { return m_patternGraphicState; }

    /// Returns color space of the pattern.
    const PDFAbstractColorSpace* getColorSpace() const;

    /// Returns patterns background color (if pattern has background color).
    /// If pattern has not background color, then invalid color is returned.
    const QColor& getBackgroundColor() const { return m_backgroundColor; }

    /// Returns true, if shading pattern should be anti-aliased
    bool isAntialiasing() const { return m_antiAlias; }

    /// Returns matrix transforming pattern space to device space
    QMatrix getPatternSpaceToDeviceSpaceMatrix(const PDFMeshQualitySettings& settings) const;

protected:
    friend class PDFPattern;

    PDFObject m_patternGraphicState;
    PDFColorSpacePointer m_colorSpace;
    QColor m_backgroundColor;
    bool m_antiAlias = false;
};

class PDFSingleDimensionShading : public PDFShadingPattern
{
public:
    explicit PDFSingleDimensionShading() = default;

protected:
    friend class PDFPattern;

    std::vector<PDFFunctionPtr> m_functions;
    QPointF m_startPoint;
    QPointF m_endPoint;
    PDFReal m_domainStart = 0.0;
    PDFReal m_domainEnd = 1.0;
    bool m_extendStart = false;
    bool m_extendEnd = false;
};

class PDFFunctionShading : public PDFShadingPattern
{
public:
    explicit PDFFunctionShading() = default;

    virtual ShadingType getShadingType() const override;
    virtual PDFMesh createMesh(const PDFMeshQualitySettings& settings) const override;

private:
    friend class PDFPattern;

    QRectF m_domain; ///< Domain of the color function
    QMatrix m_domainToTargetTransform; ///< Transformation mapping from domain to shading coordinate space
    std::vector<PDFFunctionPtr> m_functions; ///< Color functions
};

class PDFAxialShading : public PDFSingleDimensionShading
{
public:
    explicit PDFAxialShading() = default;

    virtual ShadingType getShadingType() const override;
    virtual PDFMesh createMesh(const PDFMeshQualitySettings& settings) const override;

private:
    friend class PDFPattern;
};

class PDFRadialShading : public PDFSingleDimensionShading
{
public:
    explicit PDFRadialShading() = default;

    virtual ShadingType getShadingType() const override;
    virtual PDFMesh createMesh(const PDFMeshQualitySettings& settings) const override;

private:
    friend class PDFPattern;

    PDFReal m_r0 = 0.0;
    PDFReal m_r1 = 0.0;
};

class PDFType4567Shading : public PDFShadingPattern
{
public:
    explicit PDFType4567Shading() = default;

protected:
    friend class PDFPattern;

    /// Returns color for given color or function parameter
    PDFColor getColor(PDFColor colorOrFunctionParameter) const;

    void addSubdividedTriangles(const PDFMeshQualitySettings& settings, PDFMesh& mesh, uint32_t v1, uint32_t v2, uint32_t v3, PDFColor c1, PDFColor c2, PDFColor c3) const;

    uint8_t m_bitsPerCoordinate = 0;
    uint8_t m_bitsPerComponent = 0;
    uint8_t m_bitsPerFlag = 0;
    PDFReal m_xmin = 0.0;
    PDFReal m_xmax = 0.0;
    PDFReal m_ymin = 0.0;
    PDFReal m_ymax = 0.0;
    std::vector<PDFReal> m_limits;
    size_t m_colorComponentCount = 0;

    /// Color functions. This array can be empty. If it is empty,
    /// then colors should be determined directly from color space.
    std::vector<PDFFunctionPtr> m_functions;

    /// Data of the shading, containing triangles and colors
    QByteArray m_data;
};

class PDFFreeFormGouradTriangleShading : public PDFType4567Shading
{
public:
    explicit PDFFreeFormGouradTriangleShading() = default;

    virtual ShadingType getShadingType() const override;
    virtual PDFMesh createMesh(const PDFMeshQualitySettings& settings) const override;

private:
    friend class PDFPattern;
};

class PDFLatticeFormGouradTriangleShading : public PDFType4567Shading
{
public:
    explicit PDFLatticeFormGouradTriangleShading() = default;

    virtual ShadingType getShadingType() const override;
    virtual PDFMesh createMesh(const PDFMeshQualitySettings& settings) const override;

private:
    friend class PDFPattern;

    PDFInteger m_verticesPerRow = 0;
};

class PDFTensorPatch
{
public:
    enum ColorIndex
    {
        C_00 = 0,
        C_03 = 1,
        C_33 = 2,
        C_30 = 3
    };

    using PointMatrix = std::array<std::array<QPointF, 4>, 4>;
    using Colors = std::array<PDFColor, 4>;

    explicit inline PDFTensorPatch(PointMatrix P, Colors colors) : m_P(P), m_colors(colors) { }

    /// Calculates value at point in the patch.
    /// \param u Horizontal coordinate of the patch, must be in range [0, 1]
    /// \param v Vertical coordinate of the patch, must be in range [0, 1]
    QPointF getValue(PDFReal u, PDFReal v) const;

    /// Calculates value at point in the patch, possibly derivation, if derivation
    /// variables are positive.
    /// \param u Horizontal coordinate of the patch, must be in range [0, 1]
    /// \param v Vertical coordinate of the patch, must be in range [0, 1]
    /// \param derivativeOrderU Derivation order in direction u (0 means no derivation)
    /// \param derivativeOrderV Derivation order in direction v (0 means no derivation)
    QPointF getValue(PDFReal u, PDFReal v, int derivativeOrderU, int derivativeOrderV) const;

    /// Calculates first derivate in the surface, in the direction of variable u.
    /// \param u Horizontal coordinate of the patch, must be in range [0, 1]
    /// \param v Vertical coordinate of the patch, must be in range [0, 1]
    QPointF getDerivative_u(PDFReal u, PDFReal v) const { return getValue(u, v, 1, 0); }

    /// Calculates second derivate in the surface, in the direction of variable u.
    /// \param u Horizontal coordinate of the patch, must be in range [0, 1]
    /// \param v Vertical coordinate of the patch, must be in range [0, 1]
    QPointF getDerivative_uu(PDFReal u, PDFReal v) const { return getValue(u, v, 2, 0); }

    /// Calculates first derivate in the surface, in the direction of variable v.
    /// \param u Horizontal coordinate of the patch, must be in range [0, 1]
    /// \param v Vertical coordinate of the patch, must be in range [0, 1]
    QPointF getDerivative_v(PDFReal u, PDFReal v) const { return getValue(u, v, 0, 1); }

    /// Calculates second derivate in the surface, in the direction of variable v.
    /// \param u Horizontal coordinate of the patch, must be in range [0, 1]
    /// \param v Vertical coordinate of the patch, must be in range [0, 1]
    QPointF getDerivative_vv(PDFReal u, PDFReal v) const { return getValue(u, v, 0, 2); }

    /// Calculates curvature of the given point in the surface, in the direction of u.
    /// \param u Horizontal coordinate of the patch, must be in range [0, 1]
    /// \param v Vertical coordinate of the patch, must be in range [0, 1]
    PDFReal getCurvature_u(PDFReal u, PDFReal v) const;

    /// Calculates curvature of the given point in the surface, in the direction of v.
    /// \param u Horizontal coordinate of the patch, must be in range [0, 1]
    /// \param v Vertical coordinate of the patch, must be in range [0, 1]
    PDFReal getCurvature_v(PDFReal u, PDFReal v) const;

    /// Returns matrix of control points
    const PointMatrix& getP() const { return m_P; }

    /// Returns colors of corner points
    const Colors& getColors() const { return m_colors; }

private:
    /// Computes Bernstein polynomial B0, B1, B2, B3, for parameter t.
    /// If \p derivative is zero, then it evaluates polynomial's value,
    /// otherwise it computes value of the derivation of Bx, up to degree 3
    /// derivation.
    /// \param index Index of polynomial, from 0 to 3 (B0, B1, B2, B3)
    /// \param t Parameter of polynomial function
    /// \param derivativeOrder Derivative order (0 - value, 1 - first derivation, 2 - second derivation, 3 - third derivation)
    static constexpr PDFReal B(int index, PDFReal t, int derivativeOrder);

    /// Computes Bernstein polynomial B0 for parameter t.
    /// If \p derivative is zero, then it evaluates polynomial's value,
    /// otherwise it computes value of the derivation of B0, up to degree 3
    /// derivation.
    /// \param t Parameter of polynomial function
    /// \param derivativeOrder Derivative order (0 - value, 1 - first derivation, 2 - second derivation, 3 - third derivation)
    static constexpr PDFReal B0(PDFReal t, int derivative);

    /// Computes Bernstein polynomial B1 for parameter t.
    /// If \p derivative is zero, then it evaluates polynomial's value,
    /// otherwise it computes value of the derivation of B1, up to degree 3
    /// derivation.
    /// \param t Parameter of polynomial function
    /// \param derivativeOrder Derivative order (0 - value, 1 - first derivation, 2 - second derivation, 3 - third derivation)
    static constexpr PDFReal B1(PDFReal t, int derivative);

    /// Computes Bernstein polynomial B2 for parameter t.
    /// If \p derivative is zero, then it evaluates polynomial's value,
    /// otherwise it computes value of the derivation of B2, up to degree 3
    /// derivation.
    /// \param t Parameter of polynomial function
    /// \param derivativeOrder Derivative order (0 - value, 1 - first derivation, 2 - second derivation, 3 - third derivation)
    static constexpr PDFReal B2(PDFReal t, int derivative);

    /// Computes Bernstein polynomial B3 for parameter t.
    /// If \p derivative is zero, then it evaluates polynomial's value,
    /// otherwise it computes value of the derivation of B3, up to degree 3
    /// derivation.
    /// \param t Parameter of polynomial function
    /// \param derivativeOrder Derivative order (0 - value, 1 - first derivation, 2 - second derivation, 3 - third derivation)
    static constexpr PDFReal B3(PDFReal t, int derivative);

    static constexpr PDFReal pow2(PDFReal x) { return x * x; }
    static constexpr PDFReal pow3(PDFReal x) { return x * x * x; }

    PointMatrix m_P = { };
    Colors m_colors = { };
};

using PDFTensorPatches = std::vector<PDFTensorPatch>;

class PDFTensorProductPatchShadingBase : public PDFType4567Shading
{
public:
    explicit inline PDFTensorProductPatchShadingBase() = default;

protected:
    void fillMesh(PDFMesh& mesh, const PDFMeshQualitySettings& settings, const PDFTensorPatch& patch) const;
    void fillMesh(PDFMesh& mesh, const PDFMeshQualitySettings& settings, const PDFTensorPatches& patches) const;

private:
    friend class PDFPattern;
};

class PDFTensorProductPatchShading : public PDFTensorProductPatchShadingBase
{
public:
    explicit PDFTensorProductPatchShading() = default;

    virtual ShadingType getShadingType() const override;
    virtual PDFMesh createMesh(const PDFMeshQualitySettings& settings) const override;

private:
    friend class PDFPattern;
};

}   // namespace pdf

#endif // PDFPATTERN_H
