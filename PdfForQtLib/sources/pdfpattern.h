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

private:
    std::vector<QPointF> m_vertices;
    std::vector<Triangle> m_triangles;
    QPainterPath m_boundingPath;
};

/// Represents tiling/shading pattern
class PDFPattern
{
public:
    explicit PDFPattern() = default;
    virtual ~PDFPattern() = default;

    virtual PatternType getType() const = 0;

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

/// Shading pattern - smooth color distribution along the pattern's space
class PDFShadingPattern : public PDFPattern
{
public:
    explicit PDFShadingPattern() = default;

    virtual PatternType getType() const override;
    virtual ShadingType getShadingType() const = 0;

    /// Creates a colored mesh using settings
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

class PDFAxialShading : public PDFSingleDimensionShading
{
public:
    explicit PDFAxialShading() = default;

    virtual ShadingType getShadingType() const override;
    virtual PDFMesh createMesh(const PDFMeshQualitySettings& settings) const override;

private:
    friend class PDFPattern;
};

}   // namespace pdf

#endif // PDFPATTERN_H
