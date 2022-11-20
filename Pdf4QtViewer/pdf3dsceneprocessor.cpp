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

#include "pdf3dsceneprocessor.h"
#include "pdf3d_u3d.h"
#include "pdfdbgheap.h"

#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>
#include <Qt3DRender/QSpotLight>
#include <Qt3DRender/QPointLight>
#include <Qt3DRender/QDirectionalLight>
#include <Qt3DRender/QAttribute>
#include <Qt3DRender/QGeometry>
#include <Qt3DRender/QGeometryRenderer>
#include <Qt3DRender/QBuffer>
#include <Qt3DExtras/QDiffuseSpecularMaterial>

namespace pdfviewer
{

/*
        English name                    Acrobat czech translation

        Solid                           Plny
        SolidWireframe                  Plny dratovy model
        Transparent                     Pruhledne
        TransparentWireframe            Pruhledny dratovy model
        BoundingBox                     Ohranicovaci ramecek
        TransparentBoundingBox          Pruhledny ohranicovaci ramecek
        TransparentBoundingBoxOutline   Obrys pruhledneho ohranicovaciho ramecku
        Wireframe                       Dratovy model
        ShadedWireframe                 Stinovany dratovy model
        HiddenWireframe                 Skryty dratovy model
        Vertices                        Vrcholy
        ShadedVertices                  Stinovane vrcholy
        Illustration                    Ilustrace
        SolidOutline                    Plny obrys
        ShadedIllustration              Stinovana ilustrace
*/

PDF3DSceneProcessor::Scene PDF3DSceneProcessor::createScene(const pdf::u3d::PDF3D_U3D* sceneData)
{
    Scene scene;

    // Clear processed nodes
    m_processedNodes.clear();
    m_sceneData = sceneData;

    scene.sceneRoot = createNode(sceneData->getNode(m_sceneRoot));

    return scene;
}

PDF3DSceneProcessor::SceneMode PDF3DSceneProcessor::getMode() const
{
    return m_mode;
}

void PDF3DSceneProcessor::setMode(SceneMode newMode)
{
    m_mode = newMode;
}

const QColor& PDF3DSceneProcessor::getAuxiliaryColor() const
{
    return m_auxiliaryColor;
}

void PDF3DSceneProcessor::setAuxiliaryColor(const QColor& newAuxiliaryColor)
{
    m_auxiliaryColor = newAuxiliaryColor;
}

const QColor& PDF3DSceneProcessor::getFaceColor() const
{
    return m_faceColor;
}

void PDF3DSceneProcessor::setFaceColor(const QColor& newFaceColor)
{
    m_faceColor = newFaceColor;
}

const pdf::PDFReal& PDF3DSceneProcessor::getOpacity() const
{
    return m_opacity;
}

void PDF3DSceneProcessor::setOpacity(const pdf::PDFReal& newOpacity)
{
    m_opacity = newOpacity;
}

const pdf::PDFReal& PDF3DSceneProcessor::getCreaseAngle() const
{
    return m_creaseAngle;
}

void PDF3DSceneProcessor::setCreaseAngle(const pdf::PDFReal& newCreaseAngle)
{
    m_creaseAngle = newCreaseAngle;
}

const QString& PDF3DSceneProcessor::getSceneRoot() const
{
    return m_sceneRoot;
}

void PDF3DSceneProcessor::setSceneRoot(const QString& newSceneRoot)
{
    m_sceneRoot = newSceneRoot;
}

Qt3DCore::QNode* PDF3DSceneProcessor::createNode(const pdf::u3d::PDF3D_U3D_Node& node)
{
    Qt3DCore::QNode* processedNode = nullptr;

    switch (node.getType())
    {
        case pdf::u3d::PDF3D_U3D_Node::Unknown:
            processedNode = new Qt3DCore::QNode(nullptr);
            break;

        case pdf::u3d::PDF3D_U3D_Node::Group:
        {
            if (node.hasChildTransform() && node.hasConstantChildTransform())
            {
                Qt3DCore::QEntity* entity = new Qt3DCore::QEntity(nullptr);
                Qt3DCore::QTransform* transform = new Qt3DCore::QTransform(nullptr);
                transform->setMatrix(node.getConstantChildTransform());
                entity->addComponent(transform);
                processedNode = entity;
            }
            else
            {
                processedNode = new Qt3DCore::QNode(nullptr);
            }
            break;
        }

        case pdf::u3d::PDF3D_U3D_Node::Model:
        {
            processedNode = createModelNode(node);
            break;
        }

        case pdf::u3d::PDF3D_U3D_Node::Light:
        {
            processedNode = createLightNode(node);
            break;
        }

        case pdf::u3d::PDF3D_U3D_Node::View:
            return nullptr;

        default:
            Q_ASSERT(false);
            break;
    }

    if (!processedNode)
    {
        return processedNode;
    }

    processedNode->setObjectName(node.getNodeName());

    for (const QString& childNodeName : node.getChildren())
    {
        Qt3DCore::QNode* childNode = createNode(m_sceneData->getNode(childNodeName));

        if (!childNode)
        {
            continue;
        }

        if (node.hasChildTransform() && !node.hasConstantChildTransform())
        {
            Qt3DCore::QEntity* entity = new Qt3DCore::QEntity(processedNode);
            Qt3DCore::QTransform* transform = new Qt3DCore::QTransform(nullptr);
            transform->setMatrix(node.getChildTransform(childNodeName));
            entity->addComponent(transform);
            childNode->setParent(entity);
        }
        else
        {
            childNode->setParent(processedNode);
        }
    }

    return processedNode;
}

Qt3DCore::QNode* PDF3DSceneProcessor::createModelNode(const pdf::u3d::PDF3D_U3D_Node& node)
{
    Qt3DCore::QNode* processedNode = nullptr;

    if (const pdf::u3d::PDF3D_U3D_Geometry* geometry = m_sceneData->getGeometry(node.getResourceName()))
    {
        if (auto meshGeometry = geometry->asMeshGeometry())
        {
            processedNode = createMeshGeometry(meshGeometry);
        }
        else if (auto pointSetGeometry = geometry->asPointSetGeometry())
        {
            processedNode = createPointSetGeometry(pointSetGeometry);
        }
        else if (auto lineSetGeometry = geometry->asLineSetGeometry())
        {
            processedNode = createLineSetGeometry(lineSetGeometry);
        }
    }

    if (processedNode)
    {
        processedNode->setObjectName(node.getNodeName());
    }

    return processedNode;
}

Qt3DCore::QNode* PDF3DSceneProcessor::createLightNode(const pdf::u3d::PDF3D_U3D_Node& node)
{
    Q_ASSERT(node.getType() == pdf::u3d::PDF3D_U3D_Node::NodeType::Light);

    if (const pdf::u3d::PDF3D_U3D_Light* light = m_sceneData->getLight(node.getResourceName()))
    {
        switch (light->getType())
        {
            case pdf::u3d::PDF3D_U3D_Light::Ambient:
            {
                m_globalAmbientColor.setRedF(m_globalAmbientColor.redF() + light->getColor().redF() * light->getIntensity());
                m_globalAmbientColor.setGreenF(m_globalAmbientColor.greenF() + light->getColor().greenF() * light->getIntensity());
                m_globalAmbientColor.setBlueF(m_globalAmbientColor.blueF() + light->getColor().blueF() * light->getIntensity());
                return nullptr;
            }

            case pdf::u3d::PDF3D_U3D_Light::Directional:
            {
                Qt3DCore::QEntity* entity = new Qt3DCore::QEntity();
                Qt3DRender::QDirectionalLight* processedLight = new Qt3DRender::QDirectionalLight();
                processedLight->setColor(light->getColor());
                processedLight->setIntensity(light->getIntensity());
                entity->addComponent(processedLight);
                return entity;
            }

            case pdf::u3d::PDF3D_U3D_Light::Point:
            {
                Qt3DCore::QEntity* entity = new Qt3DCore::QEntity();
                Qt3DRender::QPointLight* processedLight = new Qt3DRender::QPointLight();
                processedLight->setColor(light->getColor());
                processedLight->setIntensity(light->getIntensity());
                processedLight->setConstantAttenuation(light->getAttenuation()[0]);
                processedLight->setLinearAttenuation(light->getAttenuation()[1]);
                processedLight->setQuadraticAttenuation(light->getAttenuation()[2]);
                entity->addComponent(processedLight);
                return entity;
            }

            case pdf::u3d::PDF3D_U3D_Light::Spot:
            {
                Qt3DCore::QEntity* entity = new Qt3DCore::QEntity();
                Qt3DRender::QSpotLight* processedLight = new Qt3DRender::QSpotLight();
                processedLight->setColor(light->getColor());
                processedLight->setIntensity(light->getIntensity());
                processedLight->setConstantAttenuation(light->getAttenuation()[0]);
                processedLight->setLinearAttenuation(light->getAttenuation()[1]);
                processedLight->setQuadraticAttenuation(light->getAttenuation()[2]);
                processedLight->setCutOffAngle(light->getSpotAngle());
                entity->addComponent(processedLight);
                return entity;
            }

            default:
                Q_ASSERT(false);
                break;
        }
    }

    return nullptr;
}

Qt3DCore::QNode* PDF3DSceneProcessor::createMeshGeometry(const pdf::u3d::PDF3D_U3D_MeshGeometry* meshGeometry)
{
    // TODO: Implement mesh geometry
    return nullptr;
}

Qt3DCore::QNode* PDF3DSceneProcessor::createPointSetGeometry(const pdf::u3d::PDF3D_U3D_PointSetGeometry* pointSetGeometry)
{
    // TODO: Implement mesh geometry
    return nullptr;
}

Qt3DCore::QNode* PDF3DSceneProcessor::createLineSetGeometry(const pdf::u3d::PDF3D_U3D_LineSetGeometry* lineSetGeometry)
{
    if (lineSetGeometry->isEmpty())
    {
        return nullptr;
    }

    switch (m_mode)
    {
        case BoundingBox:
        {
            // We will display bounding box only, bounding
            // box has only edges colored with auxiliary color and faces are missing.
            // TODO: implement this
            break;
        }

        case TransparentBoundingBox:
        {
            // We will display bounding box only, bounding
            // box has no edges missing and faces are transparent.
            // TODO: implement this
            break;
        }

        case TransparentBoundingBoxOutline:
        {
            // We will display bounding box only, bounding
            // box has edges colored with auxiliary color and faces are
            // transparent.
            // TODO: implement this
            break;
        }

        case Illustration:
        case ShadedIllustration:
        case Wireframe:
        {
            // We will display lines colored by auxiliary color

            // Vertex buffer
            Qt3DRender::QBuffer* vertexBuffer = new Qt3DRender::QBuffer();
            vertexBuffer->setType(Qt3DRender::QBuffer::VertexBuffer);

            uint positionCount = lineSetGeometry->getPositionCount();
            QByteArray vertexBufferData;
            vertexBufferData.resize(positionCount * 3 * sizeof(float));
            float* vertexBufferDataPtr = reinterpret_cast<float*>(vertexBufferData.data());

            for (size_t i = 0; i < positionCount; ++i)
            {
                QVector3D position = lineSetGeometry->getPosition(i);
                *vertexBufferDataPtr++ = position.x();
                *vertexBufferDataPtr++ = position.y();
                *vertexBufferDataPtr++ = position.z();
            }
            vertexBuffer->setData(vertexBufferData);

            constexpr uint elementSize = 3;
            constexpr uint byteStride = elementSize * sizeof(float);

            Qt3DRender::QAttribute* positionAttribute = new Qt3DRender::QAttribute();
            positionAttribute->setName(Qt3DRender::QAttribute::defaultPositionAttributeName());
            positionAttribute->setVertexBaseType(Qt3DRender::QAttribute::Float);
            positionAttribute->setVertexSize(3);
            positionAttribute->setAttributeType(Qt3DRender::QAttribute::VertexAttribute);
            positionAttribute->setBuffer(vertexBuffer);
            positionAttribute->setByteOffset(0);
            positionAttribute->setByteStride(byteStride);
            positionAttribute->setCount(positionCount);

            // Index buffer
            uint lineCount = lineSetGeometry->getLineCount();
            QByteArray indexBufferData;
            indexBufferData.resize(lineCount * 2 * sizeof(unsigned int));
            unsigned int* indexBufferDataPtr = reinterpret_cast<unsigned int*>(indexBufferData.data());

            Qt3DRender::QBuffer* indexBuffer = new Qt3DRender::QBuffer();
            indexBuffer->setType(Qt3DRender::QBuffer::IndexBuffer);

            for (size_t i = 0; i < lineCount; ++i)
            {
                const auto& line = lineSetGeometry->getLine(i);
                *indexBufferDataPtr++ = line.position1;
                *indexBufferDataPtr++ = line.position2;
            }
            indexBuffer->setData(indexBufferData);

            Qt3DRender::QAttribute* indexAttribute = new Qt3DRender::QAttribute();
            indexAttribute->setAttributeType(Qt3DRender::QAttribute::IndexAttribute);
            indexAttribute->setVertexBaseType(Qt3DRender::QAttribute::UnsignedInt);
            indexAttribute->setBuffer(indexBuffer);
            indexAttribute->setCount(2 * lineCount);

            // Geometry
            Qt3DRender::QGeometry* geometry = new Qt3DRender::QGeometry();
            geometry->addAttribute(positionAttribute);
            geometry->addAttribute(indexAttribute);

            Qt3DRender::QGeometryRenderer* geometryRenderer = new Qt3DRender::QGeometryRenderer();
            geometryRenderer->setGeometry(geometry);
            geometryRenderer->setPrimitiveRestartEnabled(false);
            geometryRenderer->setPrimitiveType(Qt3DRender::QGeometryRenderer::Lines);

            Qt3DExtras::QDiffuseSpecularMaterial* material = new Qt3DExtras::QDiffuseSpecularMaterial();
            material->setAmbient(getAuxiliaryColor());
            material->setDiffuse(QColor(Qt::transparent));
            material->setSpecular(QColor(Qt::transparent));

            Qt3DCore::QEntity* entity = new Qt3DCore::QEntity();
            entity->addComponent(geometryRenderer);
            entity->addComponent(material);
            return entity;
        }

        case ShadedWireframe:
        case HiddenWireframe:
        case SolidOutline:
        case Transparent:
        case TransparentWireframe:
        case Solid:
        case SolidWireframe:
        {
            // We will display classic colored lines
            // TODO: implement this
            break;
        }

        case Vertices:
        {
            // We will display only vertices, with auxiliary color
            // TODO: implement this
            break;
        }

        case ShadedVertices:
        {
            // We will display vertices with line color
            // TODO: implement this
            break;
        }

        default:
            Q_ASSERT(false);
            break;
    }

    return nullptr;
}

} // namespace pdfviewer
