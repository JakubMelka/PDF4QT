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

#ifndef PDF3DSCENEPROCESSOR_H
#define PDF3DSCENEPROCESSOR_H

#include "pdfglobal.h"

#include <QColor>
#include <QVector3D>

#include <set>

class QComboBox;

namespace Qt3DCore
{
class QNode;
class QEntity;
class QAttribute;
}

namespace Qt3DRender
{
class QMaterial;
}

namespace pdf
{
namespace u3d
{
class PDF3D_U3D;
class PDF3D_U3D_Node;
class PDF3D_U3D_MeshGeometry;
class PDF3D_U3D_LineSetGeometry;
class PDF3D_U3D_PointSetGeometry;
}
}

namespace pdfviewer
{

class PDF3DBoundingBox
{
public:
    constexpr inline PDF3DBoundingBox() = default;

    bool isEmpty() const { return m_min == m_max; }

    const QVector3D& getMin() const { return m_min; }
    const QVector3D& getMax() const { return m_max; }

    static PDF3DBoundingBox getBoundingBox(const std::vector<QVector3D>& points);

private:
    PDF3DBoundingBox(QVector3D min, QVector3D max);

    QVector3D m_min;
    QVector3D m_max;
};

class PDF3DSceneProcessor
{
public:

    enum SceneMode
    {
        Solid,
        SolidWireframe,
        Transparent,
        TransparentWireframe,
        BoundingBox,
        TransparentBoundingBox,
        TransparentBoundingBoxOutline,
        Wireframe,
        ShadedWireframe,
        HiddenWireframe,
        Vertices,
        ShadedVertices,
        Illustration,
        SolidOutline,
        ShadedIllustration
    };

    struct Scene
    {
        Qt3DCore::QNode* sceneRoot = nullptr;
    };

    Scene createScene(const pdf::u3d::PDF3D_U3D* sceneData);

    SceneMode getMode() const;
    void setMode(SceneMode newMode);

    const QColor& getAuxiliaryColor() const;
    void setAuxiliaryColor(const QColor& newAuxiliaryColor);

    const QColor& getFaceColor() const;
    void setFaceColor(const QColor& newFaceColor);

    const pdf::PDFReal& getOpacity() const;
    void setOpacity(const pdf::PDFReal& newOpacity);

    const pdf::PDFReal& getCreaseAngle() const;
    void setCreaseAngle(const pdf::PDFReal& newCreaseAngle);

    const QString& getSceneRoot() const;
    void setSceneRoot(const QString& newSceneRoot);

    pdf::PDFReal getPointSize() const;
    void setPointSize(pdf::PDFReal newPointSize);

private:
    Qt3DCore::QNode* createNode(const pdf::u3d::PDF3D_U3D_Node& node);
    Qt3DCore::QNode* createModelNode(const pdf::u3d::PDF3D_U3D_Node& node);
    Qt3DCore::QNode* createLightNode(const pdf::u3d::PDF3D_U3D_Node& node);
    Qt3DCore::QNode* createMeshGeometry(const pdf::u3d::PDF3D_U3D_MeshGeometry* meshGeometry);
    Qt3DCore::QNode* createPointSetGeometry(const pdf::u3d::PDF3D_U3D_PointSetGeometry* pointSetGeometry);
    Qt3DCore::QNode* createLineSetGeometry(const pdf::u3d::PDF3D_U3D_LineSetGeometry* lineSetGeometry);

    Qt3DCore::QNode* createVertexGeometry(const std::vector<QVector3D>& positions);
    Qt3DCore::QNode* createBoundingBoxWireGeometry(const PDF3DBoundingBox& boundingBox);
    Qt3DCore::QNode* createBoundingBoxTransparentGeometry(const PDF3DBoundingBox& boundingBox);
    Qt3DCore::QNode* createWireframeMeshGeometry(const pdf::u3d::PDF3D_U3D_MeshGeometry* meshGeometry);
    Qt3DCore::QNode* createSolidMeshGeometry(const pdf::u3d::PDF3D_U3D_MeshGeometry* meshGeometry, qreal opacity);
    Qt3DCore::QNode* createSolidSingleColoredFaceGeometry(const pdf::u3d::PDF3D_U3D_MeshGeometry* meshGeometry);
    Qt3DCore::QNode* createWireframeWithoutObscuredEdgesMeshGeometry(const pdf::u3d::PDF3D_U3D_MeshGeometry* meshGeometry);

    Qt3DCore::QAttribute* createGenericAttribute(const std::vector<QVector3D>& values) const;
    Qt3DCore::QAttribute* createPositionAttribute(const std::vector<QVector3D>& positions) const;
    Qt3DCore::QAttribute* createNormalAttribute(const std::vector<QVector3D>& normals) const;
    Qt3DCore::QAttribute* createColorAttribute(const std::vector<QVector3D>& colors) const;

    Qt3DRender::QMaterial* createMaterialFromShader(const QString& shaderName, bool forceUseVertexColors, qreal opacity) const;

    bool isLineObscured(const pdf::u3d::PDF3D_U3D_MeshGeometry* meshGeometry, const std::pair<uint32_t, uint32_t>& line) const;

    static void addDepthTestToMaterial(Qt3DRender::QMaterial* material);

    SceneMode m_mode = Solid;
    QColor m_auxiliaryColor = Qt::black;
    QColor m_faceColor = Qt::white;
    pdf::PDFReal m_opacity = 0.5;
    pdf::PDFReal m_creaseAngle = 45.0;
    pdf::PDFReal m_pointSize = 3.0f;
    QString m_sceneRoot;

    std::set<QString> m_processedNodes;
    const pdf::u3d::PDF3D_U3D* m_sceneData = nullptr;
    QColor m_globalAmbientColor;
};

} // namespace pdfviewer

#endif // PDF3DSCENEPROCESSOR_H
