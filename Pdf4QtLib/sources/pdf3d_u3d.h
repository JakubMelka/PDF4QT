//    Copyright (C) 2022 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDF3D_U3D_H
#define PDF3D_U3D_H

#include "pdfglobal.h"

#include <QUuid>
#include <QByteArray>
#include <QSharedPointer>
#include <QMatrix4x4>
#include <QPainterPath>

#include <array>

#include <QImage>

class QTextCodec;

namespace pdf
{

namespace u3d
{

// -------------------------------------------------------------------------------
//                                  PDF3D_U3D
// -------------------------------------------------------------------------------

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_Node
{
public:
    PDF3D_U3D_Node() = default;

    enum NodeType
    {
        Unknown,
        Group,
        Model,
        Light,
        View
    };

    NodeType getType() const;
    void setType(NodeType newType);

    const QString& getNodeName() const;
    void setNodeName(const QString& newNodeName);

    const QString& getResourceName() const;
    void setResourceName(const QString& newResourceName);

    const QStringList& getChildren() const;
    void setChildren(const QStringList& newChildren);

    void addChild(const QString& child, QMatrix4x4 childTransform);

    /// Returns transform of the child node
    QMatrix4x4 getChildTransform(const QString& nodeName) const;

    /// Returns constant child transform (if all children have the same transform),
    /// otherwise identity matrix is returned.
    QMatrix4x4 getConstantChildTransform() const;

    bool hasChildTransform() const;
    bool hasConstantChildTransform() const;

    bool isEnabled() const;
    void setIsEnabled(bool newIsEnabled);

    bool isFrontVisible() const;
    void setIsFrontVisible(bool newIsFrontVisible);

    bool isBackVisible() const;
    void setIsBackVisible(bool newIsBackVisible);

private:
    NodeType m_type = Unknown;
    QString m_nodeName;
    QString m_resourceName;
    QStringList m_children;
    std::map<QString, QMatrix4x4> m_childTransforms;
    bool m_isEnabled = true;
    bool m_isFrontVisible = true;
    bool m_isBackVisible = true;
};

class PDF3D_U3D_MeshGeometry;
class PDF3D_U3D_LineSetGeometry;
class PDF3D_U3D_PointSetGeometry;

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_Geometry
{
public:
    PDF3D_U3D_Geometry() = default;
    virtual ~PDF3D_U3D_Geometry() = default;

    virtual PDF3D_U3D_MeshGeometry* asMeshGeometry() { return nullptr; }
    virtual const PDF3D_U3D_MeshGeometry* asMeshGeometry() const { return nullptr; }

    virtual PDF3D_U3D_PointSetGeometry* asPointSetGeometry() { return nullptr; }
    virtual const PDF3D_U3D_PointSetGeometry* asPointSetGeometry() const { return nullptr; }

    virtual PDF3D_U3D_LineSetGeometry* asLineSetGeometry() { return nullptr; }
    virtual const PDF3D_U3D_LineSetGeometry* asLineSetGeometry() const { return nullptr; }
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_MeshGeometry : public PDF3D_U3D_Geometry
{
public:
    virtual PDF3D_U3D_MeshGeometry* asMeshGeometry() override { return this; }
    virtual const PDF3D_U3D_MeshGeometry* asMeshGeometry() const override { return this; }

    struct Vertex
    {
        uint32_t positionIndex = 0;
        uint32_t normalIndex = 0;
        uint32_t diffuseColorIndex = 0;
        uint32_t specularColorIndex = 0;
        uint32_t textureCoordIndex = 0;
    };

    struct Triangle
    {
        bool hasDiffuse = false;
        bool hasSpecular = false;
        bool hasTexture = false;

        std::array<Vertex, 3> vertices;
    };

    bool isEmpty() const { return m_triangles.empty(); }

    QVector3D getPosition(size_t index) const { return index < m_positions.size() ? m_positions[index] : QVector3D(0, 0, 0); }
    QVector3D getNormal(size_t index) const { return index < m_normals.size() ? m_normals[index] : QVector3D(0, 0, 0); }
    QVector4D getDiffuseColor(size_t index) const { return index < m_diffuseColors.size() ? m_diffuseColors[index] : QVector4D(0, 0, 0, 0); }
    QVector4D getSpecularColor(size_t index) const { return index < m_specularColors.size() ? m_specularColors[index] : QVector4D(0, 0, 0, 0); }
    QVector4D getTextureCoordinate(size_t index) const { return index < m_textureCoordinates.size() ? m_textureCoordinates[index] : QVector4D(0, 0, 0, 0); }

    void addPosition(const QVector3D& position) { m_positions.emplace_back(position); }
    void addNormal(const QVector3D& normal) { m_normals.emplace_back(normal); }
    void addDiffuseColor(const QVector4D& color) { m_diffuseColors.emplace_back(color); }
    void addSpecularColor(const QVector4D& color) { m_specularColors.emplace_back(color); }
    void addTextureCoordinate(const QVector4D& coordinate) { m_textureCoordinates.emplace_back(coordinate); }
    void addTriangle(Triangle triangle);

    size_t getPositionCount() const { return m_positions.size(); }
    size_t getNormalCount() const { return m_normals.size(); }
    size_t getDiffuseColorCount() const { return m_diffuseColors.size(); }
    size_t getSpecularColorCount() const { return m_specularColors.size(); }
    size_t getTextureCoordinateCount() const { return m_textureCoordinates.size(); }

private:
    std::vector<QVector3D> m_positions;
    std::vector<QVector3D> m_normals;
    std::vector<QVector4D> m_diffuseColors;
    std::vector<QVector4D> m_specularColors;
    std::vector<QVector4D> m_textureCoordinates;
    std::vector<Triangle> m_triangles;
    std::unordered_multimap<size_t, size_t> m_mapPosIndexToTriangle;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_PointSetGeometry : public PDF3D_U3D_Geometry
{
public:
    virtual PDF3D_U3D_PointSetGeometry* asPointSetGeometry() override { return this; }
    virtual const PDF3D_U3D_PointSetGeometry* asPointSetGeometry() const override { return this; }

    struct Point
    {
        uint32_t shadingId = 0;
        uint32_t position = 0;
        uint32_t normal = 0;
        uint32_t diffuseColor = 0;
        uint32_t specularColor = 0;
    };

    bool isEmpty() const { return m_positions.empty(); }

    QVector3D getPosition(size_t index) const { return index < m_positions.size() ? m_positions[index] : QVector3D(0, 0, 0); }
    QVector3D getNormal(size_t index) const { return index < m_normals.size() ? m_normals[index] : QVector3D(0, 0, 0); }
    QVector4D getDiffuseColor(size_t index) const { return index < m_diffuseColors.size() ? m_diffuseColors[index] : QVector4D(0, 0, 0, 0); }
    QVector4D getSpecularColor(size_t index) const { return index < m_specularColors.size() ? m_specularColors[index] : QVector4D(0, 0, 0, 0); }

    void addPosition(const QVector3D& position) { m_positions.emplace_back(position); }
    void addNormal(const QVector3D& normal) { m_normals.emplace_back(normal); }
    void addDiffuseColor(const QVector4D& color) { m_diffuseColors.emplace_back(color); }
    void addSpecularColor(const QVector4D& color) { m_specularColors.emplace_back(color); }
    void addPoint(Point point);

    /// Returns all points containing given vertex index
    std::vector<Point> queryPointsByVertexIndex(size_t vertexIndex) const;

    size_t getPositionCount() const { return m_positions.size(); }
    size_t getNormalCount() const { return m_normals.size(); }
    size_t getDiffuseColorCount() const { return m_diffuseColors.size(); }
    size_t getSpecularColorCount() const { return m_specularColors.size(); }

private:
    std::vector<QVector3D> m_positions;
    std::vector<QVector3D> m_normals;
    std::vector<QVector4D> m_diffuseColors;
    std::vector<QVector4D> m_specularColors;
    std::vector<Point> m_points;
    std::unordered_multimap<size_t, size_t> m_mapPosIndexToPoint;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_LineSetGeometry : public PDF3D_U3D_Geometry
{
public:
    virtual PDF3D_U3D_LineSetGeometry* asLineSetGeometry() override { return this; }
    virtual const PDF3D_U3D_LineSetGeometry* asLineSetGeometry() const override { return this; }

    struct Line
    {
        uint32_t shadingId = 0;
        uint32_t position1 = 0;
        uint32_t position2 = 0;
        uint32_t normal1 = 0;
        uint32_t normal2 = 0;
        uint32_t diffuseColor1 = 0;
        uint32_t diffuseColor2 = 0;
        uint32_t specularColor1 = 0;
        uint32_t specularColor2 = 0;
    };

    bool isEmpty() const { return m_positions.empty(); }

    QVector3D getPosition(size_t index) const { return index < m_positions.size() ? m_positions[index] : QVector3D(0, 0, 0); }
    QVector3D getNormal(size_t index) const { return index < m_normals.size() ? m_normals[index] : QVector3D(0, 0, 0); }
    QVector4D getDiffuseColor(size_t index) const { return index < m_diffuseColors.size() ? m_diffuseColors[index] : QVector4D(0, 0, 0, 0); }
    QVector4D getSpecularColor(size_t index) const { return index < m_specularColors.size() ? m_specularColors[index] : QVector4D(0, 0, 0, 0); }
    const Line& getLine(size_t index) const { return m_lines[index]; }

    void addPosition(const QVector3D& position) { m_positions.emplace_back(position); }
    void addNormal(const QVector3D& normal) { m_normals.emplace_back(normal); }
    void addDiffuseColor(const QVector4D& color) { m_diffuseColors.emplace_back(color); }
    void addSpecularColor(const QVector4D& color) { m_specularColors.emplace_back(color); }
    void addLine(Line line);

    /// Returns all lines containing given vertex index
    std::vector<Line> queryLinesByVertexIndex(size_t vertexIndex) const;

    size_t getPositionCount() const { return m_positions.size(); }
    size_t getNormalCount() const { return m_normals.size(); }
    size_t getDiffuseColorCount() const { return m_diffuseColors.size(); }
    size_t getSpecularColorCount() const { return m_specularColors.size(); }
    size_t getLineCount() const { return m_lines.size(); }

private:
    std::vector<QVector3D> m_positions;
    std::vector<QVector3D> m_normals;
    std::vector<QVector4D> m_diffuseColors;
    std::vector<QVector4D> m_specularColors;
    std::vector<Line> m_lines;
    std::unordered_multimap<size_t, size_t> m_mapPosIndexToLine;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_Light
{
public:
    constexpr PDF3D_U3D_Light() = default;

    enum Type
    {
        Ambient,
        Directional,
        Point,
        Spot
    };

    bool isEnabled() const;
    void setIsEnabled(bool newIsEnabled);

    bool isSpecular() const;
    void setIsSpecular(bool newIsSpecular);

    bool isSpotDecay() const;
    void setIsSpotDecay(bool newIsSpotDecay);

    Type getType() const;
    void setType(Type newType);

    const QColor& getColor() const;
    void setColor(const QColor& newColor);

    const QVector3D& getAttenuation() const;
    void setAttenuation(const QVector3D& newAttenuation);

    float getSpotAngle() const;
    void setSpotAngle(float newSpotAngle);

    float getIntensity() const;
    void setIntensity(float newIntensity);

private:
    bool m_isEnabled = false;
    bool m_isSpecular = false;
    bool m_isSpotDecay = false;
    Type m_type = Ambient;
    QColor m_color = QColor();
    QVector3D m_attenuation = QVector3D();
    float m_spotAngle = 0.0;
    float m_intensity = 0.0;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_Shader
{
public:

    enum AlphaTestFunction : uint32_t
    {
        NEVER       = 0x00000610,
        LESS        = 0x00000611,
        GREATER     = 0x00000612,
        EQUAL       = 0x00000613,
        NOT_EQUAL   = 0x00000614,
        LEQUAL      = 0x00000615,
        GEQUAL      = 0x00000616,
        ALWAYS      = 0x00000617
    };

    enum ColorBlendFunction : uint32_t
    {
        FB_ADD              = 0x00000604,
        FB_MULTIPLY         = 0x00000605,
        FB_ALPHA_BLEND      = 0x00000606,
        FB_INV_ALPHA_BLEND  = 0x00000607
    };

    enum TextureBlendFunction : uint8_t
    {
        Multiply    = 0,
        Add         = 1,
        Replace     = 2,
        Blend       = 3
    };

    enum TextureBlendSource : uint8_t
    {
        AlphaValueOfEachPixel = 0,
        BlendingConstant = 1
    };

    enum TextureMode : uint8_t
    {
        TM_NONE         = 0,
        TM_PLANAR       = 1,
        TM_CYLINDRICAL  = 2,
        TM_SPHERICAL    = 3,
        TM_REFLECTION   = 4
    };

    struct PDF_U3D_TextureInfo
    {
        QString textureName;
        float textureIntensity = 0.0;
        TextureBlendFunction blendFunction = Blend;
        TextureBlendSource blendSource = AlphaValueOfEachPixel;
        float blendConstant = 0.0;
        TextureMode textureMode = TM_NONE;
        QMatrix4x4 textureTransform;
        QMatrix4x4 textureMap;
        bool repeatU = false;
        bool repeatV = false;
    };

    using PDF_U3D_TextureInfos = std::vector<PDF_U3D_TextureInfo>;

    bool isLightingEnabled() const;
    void setIsLightingEnabled(bool newIsLightingEnabled);

    bool isAlphaTestEnabled() const;
    void setIsAlphaTestEnabled(bool newIsAlphaTestEnabled);

    bool isUseVertexColor() const;
    void setIsUseVertexColor(bool newIsUseVertexColor);

    float getAlphaTestReference() const;
    void setAlphaTestReference(float newAlphaTestReference);

    AlphaTestFunction getAlphaTestFunction() const;
    void setAlphaTestFunction(AlphaTestFunction newAlphaTestFunction);

    ColorBlendFunction getColorBlendFunction() const;
    void setColorBlendFunction(ColorBlendFunction newColorBlendFunction);

    uint32_t getRenderPassEnabledFlags() const;
    void setRenderPassEnabledFlags(uint32_t newRenderPassEnabledFlags);

    uint32_t getShaderChannels() const;
    void setShaderChannels(uint32_t newShaderChannels);

    uint32_t getAlphaTextureChannels() const;
    void setAlphaTextureChannels(uint32_t newAlphaTextureChannels);

    const QString& getMaterialName() const;
    void setMaterialName(const QString& newMaterialName);

    const PDF_U3D_TextureInfos& getTextureInfos() const;
    void setTextureInfos(const PDF_U3D_TextureInfos& newTextureInfos);

private:
    bool m_isLightingEnabled = false;
    bool m_isAlphaTestEnabled = false;
    bool m_isUseVertexColor = false;
    float m_alphaTestReference = 0.0;
    AlphaTestFunction m_alphaTestFunction = ALWAYS;
    ColorBlendFunction m_colorBlendFunction = FB_ALPHA_BLEND;
    uint32_t m_renderPassEnabledFlags = 0;
    uint32_t m_shaderChannels = 0;
    uint32_t m_alphaTextureChannels = 0;
    QString m_materialName;
    PDF_U3D_TextureInfos m_textureInfos;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_Fog
{
public:
    constexpr PDF3D_U3D_Fog() = default;

    enum FogMode : uint32_t
    {
        Linear = 0,
        Exponential = 1,
        Exponential2 = 2
    };

    bool isEnabled() const;
    void setEnabled(bool newEnabled);

    const QColor& getColor() const;
    void setColor(const QColor& newColor);

    float getNear() const;
    void setNear(float newNear);

    float getFar() const;
    void setFar(float newFar);

    FogMode getMode() const;
    void setMode(FogMode newMode);

private:
    bool m_enabled = false;
    FogMode m_mode = Linear;
    QColor m_color;
    float m_near = 0.0;
    float m_far = 0.0;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_RenderPass
{
public:
    constexpr PDF3D_U3D_RenderPass() = default;

    const QString& getRootNodeName() const;
    const PDF3D_U3D_Fog& getFog() const;

    void setRootNodeName(const QString& newRootNodeName);
    void setFog(const PDF3D_U3D_Fog& newFog);

private:
    QString m_rootNodeName;
    PDF3D_U3D_Fog m_fog;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_View
{
public:
    constexpr PDF3D_U3D_View() = default;

    const std::vector<PDF3D_U3D_RenderPass>& renderPasses() const;
    void setRenderPasses(const std::vector<PDF3D_U3D_RenderPass>& newRenderPasses);

private:
    std::vector<PDF3D_U3D_RenderPass> m_renderPasses;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_Material
{
public:
    constexpr PDF3D_U3D_Material() = default;

    const QColor& getAmbientColor() const;
    void setAmbientColor(const QColor& newAmbientColor);

    const QColor& getDiffuseColor() const;
    void setDiffuseColor(const QColor& newDiffuseColor);

    const QColor& getSpecularColor() const;
    void setSpecularColor(const QColor& newSpecularColor);

    const QColor& getEmmisiveColor() const;
    void setEmmisiveColor(const QColor& newEmmisiveColor);

    float getReflectivity() const;
    void setReflectivity(float newReflectivity);

private:
    QColor m_ambientColor;
    QColor m_diffuseColor;
    QColor m_specularColor;
    QColor m_emmisiveColor;
    float m_reflectivity = 0.0;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D
{
public:
    PDF3D_U3D();

    const PDF3D_U3D_Node& getNode(const QString& nodeName) const;
    PDF3D_U3D_Node& getOrCreateNode(const QString& nodeName);

    const PDF3D_U3D_View* getView(const QString& viewName) const;
    const PDF3D_U3D_Light* getLight(const QString& lightName) const;
    QImage getTexture(const QString& textureName) const;
    const PDF3D_U3D_Shader* getShader(const QString& shaderName) const;
    const PDF3D_U3D_Material* getMaterial(const QString& materialName) const;
    const PDF3D_U3D_Geometry* getGeometry(const QString& geometryName) const;

    void setViewResource(const QString& viewName, PDF3D_U3D_View view);
    void setLightResource(const QString& lightName, PDF3D_U3D_Light light);
    void setTextureResource(const QString& textureName, QImage texture);
    void setShaderResource(const QString& shaderName, PDF3D_U3D_Shader shader);
    void setMaterialResource(const QString& materialName, PDF3D_U3D_Material material);
    void setGeometry(const QString& geometryName, QSharedPointer<PDF3D_U3D_Geometry> geometry);

    static PDF3D_U3D parse(const QByteArray& data, QStringList* errors);

private:
    std::map<QString, PDF3D_U3D_Node> m_sceneGraph;
    std::map<QString, PDF3D_U3D_View> m_viewResources;
    std::map<QString, PDF3D_U3D_Light> m_lightResources;
    std::map<QString, QImage> m_textureResources;
    std::map<QString, PDF3D_U3D_Shader> m_shaderResources;
    std::map<QString, PDF3D_U3D_Material> m_materialResources;
    std::map<QString, QSharedPointer<PDF3D_U3D_Geometry>> m_geometries;
};

}   // namespace u3d

}   // namespace pdf

#endif // PDF3D_U3D_H
