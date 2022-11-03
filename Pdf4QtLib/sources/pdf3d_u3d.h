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

enum Context
{
    cPosDiffSign = 1,
    cPosDiffX,
    cPosDiffY,
    cPosDiffZ,
    cNormlCnt,
    cDiffNormalSign,
    cDiffNormalX,
    cDiffNormalY,
    cDiffNormalZ,
    cShading,
    cLineCnt = cShading, // Undocumented, that cLineCnt equals to cShading
    cPointCnt = cShading, // Undocumented, that cPointCnt equals to cShading
    cNormlIdx,
    cDiffDup,
    cDiffuseColorSign,
    cColorDiffR,
    cColorDiffG,
    cColorDiffB,
    cColorDiffA,
    cSpecDup,
    cSpecularColorSign,
    cTexCDup,
    cTexCoordSign,
    cTexCDiffU,
    cTexCDiffV,
    cTexCDiffS,
    cTexCDiffT,
    cZero,
    cDiffuseCount,
    cSpecularCount,
    cTexCoordCount,
    cFaceCnt,
    cFaceOrnt,
    cLocal3rdPos,
    cThrdPosType,
    cBoneWeightCnt,
    cBoneIdx,
    cQntBoneWeight
};

class PDF3D_U3D;
class PDF3D_U3D_Parser;
class PDF3D_U3D_DataReader;
class PDF3D_U3D_ContextManager;

struct PDF3D_U3D_Block_Data;

class PDF3D_U3D_AbstractBlock;
using PDF3D_U3D_AbstractBlockPtr = QSharedPointer<PDF3D_U3D_AbstractBlock>;

using PDF3D_U3D_Vec3 = std::array<float, 3>;
using PDF3D_U3D_Vec4 = std::array<float, 4>;

struct PDF3D_U3D_QuantizedVec4
{
    uint8_t signBits;
    uint32_t diff1;
    uint32_t diff2;
    uint32_t diff3;
    uint32_t diff4;
};

struct PDF3D_U3D_QuantizedVec3
{
    uint8_t signBits;
    uint32_t diff1;
    uint32_t diff2;
    uint32_t diff3;
};

struct PDF3D_U3D_Block_Info
{
    enum EBlockType : uint32_t
    {
        BT_Unknown                  = 0x00000000,
        BT_FileHeader               = 0x00443355,
        BT_FileReference            = 0xFFFFFF12,
        BT_FileModifierChain        = 0xFFFFFF14,
        BT_FilePriorityUpdate       = 0xFFFFFF15,
        BT_FileNewObject            = 0xFFFFFF16,

        BT_NodeGroup                = 0xFFFFFF21,
        BT_NodeModel                = 0xFFFFFF22,
        BT_NodeLight                = 0xFFFFFF23,
        BT_NodeView                 = 0xFFFFFF24,

        BT_GeneratorCLODMeshDecl    = 0xFFFFFF31,
        BT_GeneratorCLODBaseMesh    = 0xFFFFFF3B,
        BT_GeneratorCLODProgMesh    = 0xFFFFFF3C,
        BT_GeneratorPointSet        = 0xFFFFFF36,
        BT_GeneratorPointSetCont    = 0xFFFFFF3E,
        BT_GeneratorLineSet         = 0xFFFFFF37,
        BT_GeneratorLineSetCont     = 0xFFFFFF3F,

        BT_Modifier2DGlyph          = 0xFFFFFF41,
        BT_ModifierSubdivision      = 0xFFFFFF42,
        BT_ModifierAnimation        = 0xFFFFFF43,
        BT_ModifierBoneWeights      = 0xFFFFFF44,
        BT_ModifierShading          = 0xFFFFFF45,
        BT_ModifierCLOD             = 0xFFFFFF46,

        BT_ResourceLight            = 0xFFFFFF51,
        BT_ResourceView             = 0xFFFFFF52,
        BT_ResourceLitShader        = 0xFFFFFF53,
        BT_ResourceMaterial         = 0xFFFFFF54,
        BT_ResourceTexture          = 0xFFFFFF55,
        BT_ResourceTextureCont      = 0xFFFFFF5C,
        BT_ResourceMotion           = 0xFFFFFF56
    };

    enum EPalette
    {
        PL_Material,
        PL_Generator,
        PL_Shader,
        PL_Texture,
        PL_Motion,
        PL_Node,
        PL_View,
        PL_Light,
        PL_LastPalette
    };

    static constexpr bool isChain(EBlockType blockType);
    static constexpr bool isContinuation(EBlockType blockType);
    static EPalette getPalette(EBlockType blockType);
};

struct PDF3D_U3D_Block_Data
{
    PDF3D_U3D_Block_Info::EBlockType blockType = PDF3D_U3D_Block_Info::EBlockType();

    QByteArray blockData;
    QByteArray metaData;
};

struct PDF3D_U3D_BoneJoint
{
    float jointCenterU = 0.0;
    float jointCenterV = 0.0;
    float jointScaleU = 0.0;
    float jointScaleV = 0.0;
};

struct PDF3D_U3D_BoneDescription
{
    QString boneName;
    QString parentBoneName;
    uint32_t boneAttributes = 0;
    float boneLength = 0.0;
    PDF3D_U3D_Vec3 boneDisplacement = PDF3D_U3D_Vec3();
    PDF3D_U3D_Vec4 boneOrientation = PDF3D_U3D_Vec4();
    uint32_t boneLinkCount = 0;
    float boneLinkLength = 0.0;
    PDF3D_U3D_BoneJoint startJoint = PDF3D_U3D_BoneJoint();
    PDF3D_U3D_BoneJoint endJoint = PDF3D_U3D_BoneJoint();
    PDF3D_U3D_Vec3 rotationConstraintMin = PDF3D_U3D_Vec3();
    PDF3D_U3D_Vec3 rotationConstraintMax = PDF3D_U3D_Vec3();

    bool isLinkPresent() const { return boneAttributes & 0x00000001; }
    bool isJointPresent() const { return boneAttributes & 0x00000002; }
};

struct PDF3D_U3D_ShadingDescription
{
    uint32_t shadingAttributes = 0;
    uint32_t textureLayerCount = 0;
    std::vector<uint32_t> textureCoordDimensions;
    uint32_t originalShading = 0;

    bool hasDiffuseColors() const { return shadingAttributes & 0x00000001; }
    bool hasSpecularColors() const { return shadingAttributes & 0x00000002; }
};

class PDF3D_U3D_AbstractBlock
{
public:
    inline constexpr PDF3D_U3D_AbstractBlock() = default;
    virtual ~PDF3D_U3D_AbstractBlock() = default;

    void parseMetadata(QByteArray metaData, PDF3D_U3D_Parser* object);

    struct ParentNodeData
    {
        QString parentNodeName;
        QMatrix4x4 transformMatrix;
    };

    using ParentNodesData = std::vector<ParentNodeData>;

    struct MetaDataItem
    {
        uint32_t attributes = 0;
        QString key;
        QString value;
        QByteArray binaryData;
    };

    static ParentNodesData parseParentNodeData(PDF3D_U3D_DataReader& reader, PDF3D_U3D_Parser* object);

private:
    std::vector<MetaDataItem> m_metaData;
};

// Bounding sphere - x, y, z position and radius
using PDF3D_U3D_BoundingSphere = std::array<float, 4>;

// Bounding box - min (x,y,z), max (x,y,z) - six values
using PDF3D_U3D_AxisAlignedBoundingBox = std::array<float, 4>;

// -------------------------------------------------------------------------------
//                            FILE STRUCTURE BLOCKS
// -------------------------------------------------------------------------------

class PDF3D_U3D_FileBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_FileHeader;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    bool isExtensibleProfile() const { return m_profileIdentifier & 0x00000002; }
    bool isNoCompressionMode() const { return m_profileIdentifier & 0x00000004; }
    bool isDefinedUnits() const { return m_profileIdentifier & 0x00000008; }

    int16_t getMajorVersion() const;
    int16_t getMinorVersion() const;
    uint32_t getProfileIdentifier() const;
    uint32_t getDeclarationSize() const;
    uint64_t getFileSize() const;
    uint32_t getCharacterEncoding() const;
    PDFReal getUnitScalingFactor() const;

private:
    int16_t m_majorVersion = 0;
    int16_t m_minorVersion = 0;
    uint32_t m_profileIdentifier = 0;
    uint32_t m_declarationSize = 0;
    uint64_t m_fileSize = 0;
    uint32_t m_characterEncoding = 0;
    PDFReal m_unitScalingFactor = 1.0;
};

class PDF3D_U3D_FileReferenceBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_FileReference;

    bool isBoundingSpherePresent() const { return m_fileReferenceAttributes & 0x00000001; }
    bool isAxisAlignedBoundingBoxPresent() const { return m_fileReferenceAttributes & 0x00000002; }

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct Filter
    {
        uint8_t filterType = 0;
        QString objectNameFilter;
        uint32_t objectTypeFilter = 0;
    };

    const QString& getScopeName() const;
    uint32_t getFileReferenceAttributes() const;
    const PDF3D_U3D_BoundingSphere& getBoundingSphere() const;
    const PDF3D_U3D_AxisAlignedBoundingBox& getBoundingBox() const;
    uint32_t getUrlCount() const;
    const QStringList& getUrls() const;
    uint32_t getFilterCount() const;
    const std::vector<Filter>& getFilters() const;
    uint8_t getNameCollisionPolicy() const;
    const QString& getWorldAliasName() const;

private:
    QString m_scopeName;
    uint32_t m_fileReferenceAttributes = 0;
    PDF3D_U3D_BoundingSphere m_boundingSphere = PDF3D_U3D_BoundingSphere();
    PDF3D_U3D_AxisAlignedBoundingBox m_boundingBox = PDF3D_U3D_AxisAlignedBoundingBox();
    uint32_t m_urlCount = 0;
    QStringList m_urls;
    uint32_t m_filterCount = 0;
    std::vector<Filter> m_filters;
    uint8_t m_nameCollisionPolicy = 0;
    QString m_worldAliasName;
};

class PDF3D_U3D_ModifierChainBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_FileModifierChain;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    bool isBoundingSpherePresent() const { return m_modifierChainAttributes & 0x00000001; }
    bool isAxisAlignedBoundingBoxPresent() const { return m_modifierChainAttributes & 0x00000002; }

    bool isNodeModifierChain() const { return m_modifierChainType == 0; }
    bool isModelResourceModifierChain() const { return m_modifierChainType == 1; }
    bool isTextureModifierChain() const { return m_modifierChainType == 2; }

    const QString& getModifierChainName() const;
    uint32_t getModifierChainType() const;
    uint32_t getModifierChainAttributes() const;
    const PDF3D_U3D_BoundingSphere& getBoundingSphere() const;
    const PDF3D_U3D_AxisAlignedBoundingBox& getBoundingBox() const;
    uint32_t getModifierCount() const;
    const std::vector<PDF3D_U3D_Block_Data>& getModifierDeclarationBlocks() const;

private:
    QString m_modifierChainName;
    uint32_t m_modifierChainType = 0;
    uint32_t m_modifierChainAttributes = 0;
    PDF3D_U3D_BoundingSphere m_boundingSphere = PDF3D_U3D_BoundingSphere();
    PDF3D_U3D_AxisAlignedBoundingBox m_boundingBox = PDF3D_U3D_AxisAlignedBoundingBox();
    // Padding 0-3 bytes
    uint32_t m_modifierCount = 0;
    std::vector<PDF3D_U3D_Block_Data> m_modifierDeclarationBlocks;
};

class PDF3D_U3D_FilePriorityUpdateBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_FilePriorityUpdate;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    uint32_t getNewPriority() const;

private:
    uint32_t m_newPriority = 0;
};

class PDF3D_U3D_NewObjectTypeBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_FileNewObject;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getNewObjectTypeName() const;
    uint32_t getModifierType() const;
    const QUuid& getExtensionId() const;
    uint32_t getNewDeclarationBlockType() const;
    uint32_t getContinuationBlockTypeCount() const;
    const std::vector<uint32_t>& getContinuationBlockTypes() const;
    const QString& getExtensionVendorName() const;
    uint32_t getUrlCount() const;
    const QStringList& getUrls() const;
    const QString& getExtensionInformationString() const;

private:
    QString m_newObjectTypeName;
    uint32_t m_modifierType = 0;
    QUuid m_extensionId;
    uint32_t m_newDeclarationBlockType = 0;
    uint32_t m_continuationBlockTypeCount = 0;
    std::vector<uint32_t> m_continuationBlockTypes;
    QString m_extensionVendorName;
    uint32_t m_urlCount = 0;
    QStringList m_urls;
    QString m_extensionInformationString;
};

class PDF3D_U3D_NewObjectBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID_MIN = 0x00000100;
    static constexpr uint32_t ID_MAX = 0x00FFFFFF;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getObjectName() const;
    uint32_t getChainIndex() const;
    const QByteArray& getData() const;

private:
    QString m_objectName;
    uint32_t m_chainIndex = 0;
    QByteArray m_data;
};

// -------------------------------------------------------------------------------
//                                 NODE BLOCKS
// -------------------------------------------------------------------------------

class PDF3D_U3D_GroupNodeBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_NodeGroup;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getGroupNodeName() const;
    const ParentNodesData& getParentNodesData() const;

private:
    QString m_groupNodeName;
    ParentNodesData m_parentNodesData;
};

class PDF3D_U3D_ModelNodeBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_NodeModel;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    bool isHidden() const { return m_modelVisibility == 0; }
    bool isFrontVisible() const { return m_modelVisibility == 1; }
    bool isBackVisible() const { return m_modelVisibility == 2; }
    bool isFrontAndBackVisible() const { return m_modelVisibility == 3; }

    const QString& getModelNodeName() const;
    const ParentNodesData& getParentNodesData() const;
    const QString& getModelResourceName() const;
    uint32_t getModelVisibility() const;

private:
    QString m_modelNodeName;
    ParentNodesData m_parentNodesData;
    QString m_modelResourceName;
    uint32_t m_modelVisibility;
};

class PDF3D_U3D_LightNodeBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_NodeLight;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getLightNodeName() const;
    const ParentNodesData& getParentNodesData() const;
    const QString& getLightResourceName() const;

private:
    QString m_lightNodeName;
    ParentNodesData m_parentNodesData;
    QString m_lightResourceName;
};

class PDF3D_U3D_ViewNodeBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_NodeView;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct BackdropOrOverlay
    {
        QString m_textureName;
        float m_textureBlend = 0.0;
        float m_rotation = 0.0;
        float m_locationX = 0.0;
        float m_locationY = 0.0;
        int32_t m_registrationPointX = 0;
        int32_t m_registrationPointY = 0;
        float m_scaleX = 0.0;
        float m_scaleY = 0.0;
    };

    using BackdropItem = BackdropOrOverlay;
    using OverlayItem = BackdropOrOverlay;

    bool isScreenPositionRelative() const { return m_viewNodeAttributes & 0x00000001; }
    bool isProjectionOrthographic() const { return m_viewNodeAttributes & 0x00000002; }
    bool isProjectionTwoPointPerspective() const { return m_viewNodeAttributes & 0x00000004; }
    bool isProjectionOnePointPerspective() const { return (m_viewNodeAttributes & 0x00000002) && (m_viewNodeAttributes & 0x00000004); }
    bool isProjectionThreePointPerspective() const { return !isProjectionOrthographic() && !isProjectionTwoPointPerspective() && !isProjectionOnePointPerspective(); }

    const QString& getViewNodeName() const;
    const ParentNodesData& getParentNodesData() const;
    const QString& getViewResourceName() const;
    uint32_t getViewNodeAttributes() const;
    float getViewNearFlipping() const;
    float getViewFarFlipping() const;
    float getViewProjection() const;
    float getViewOrthographicHeight() const;
    const PDF3D_U3D_Vec3& getViewProjectionVector() const;
    float getViewPortWidth() const;
    float getViewPortHeight() const;
    float getViewPortHorizontalPosition() const;
    float getViewPortVerticalPosition() const;
    const std::vector<BackdropItem>& getBackdrop() const;
    const std::vector<OverlayItem>& getOverlay() const;

private:
    QString m_viewNodeName;
    ParentNodesData m_parentNodesData;
    QString m_viewResourceName;
    uint32_t m_viewNodeAttributes = 0;
    float m_viewNearFlipping = 0.0;
    float m_viewFarFlipping = 0.0;
    float m_viewProjection = 0.0;
    float m_viewOrthographicHeight = 0.0;
    PDF3D_U3D_Vec3 m_viewProjectionVector = PDF3D_U3D_Vec3();

    // Viewport
    float m_viewPortWidth = 0.0;
    float m_viewPortHeight = 0.0;
    float m_viewPortHorizontalPosition = 0.0;
    float m_viewPortVerticalPosition = 0.0;

    // Backdrop
    std::vector<BackdropItem> m_backdrop;

    // Overlay
    std::vector<OverlayItem> m_overlay;
};

// -------------------------------------------------------------------------------
//                                 GEOMETRY BLOCKS
// -------------------------------------------------------------------------------

class PDF3D_U3D_CLODMeshDeclarationBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_GeneratorCLODMeshDecl;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getName() const { return getMeshName(); }
    const QString& getMeshName() const;
    uint32_t getChainIndex() const;

    bool isNormalsExcluded() const { return getMeshAttributes() & 0x00000001; }

    /* max mesh description */
    uint32_t getMeshAttributes() const;
    uint32_t getFaceCount() const;
    uint32_t getPositionCount() const;
    uint32_t getNormalCount() const;
    uint32_t getDiffuseColorCount() const;
    uint32_t getSpecularColorCount() const;
    uint32_t getTextureColorCount() const;
    uint32_t getShadingCount() const;
    const std::vector<PDF3D_U3D_ShadingDescription>& getShadingDescription() const;
    const PDF3D_U3D_ShadingDescription* getShadingDescriptionItem(uint32_t index) const { return index < m_shadingDescription.size() ? &m_shadingDescription[index] : nullptr; }

    /* clod description */
    uint32_t getMinimumResolution() const;
    uint32_t getFinalResolution() const;

    /* resource description */
    uint32_t getPositionQualityFactor() const;
    uint32_t getNormalQualityFactor() const;
    uint32_t getTextureCoordQualityFactor() const;
    float getPositionInverseQuant() const;
    float getNormalInverseQuant() const;
    float getTextureCoordInverseQuant() const;
    float getDiffuseColorInverseQuant() const;
    float getSpecularColorInverseQuant() const;
    float getNormalCreaseParameter() const;
    float getNormalUpdateParameter() const;
    float getNormalToleranceParameter() const;

    /* bone description */
    uint32_t getBoneCount() const;
    const std::vector<PDF3D_U3D_BoneDescription>& getBoneDescription() const;

private:
    QString m_meshName;
    uint32_t m_chainIndex = 0;

    /* max mesh description */
    uint32_t m_meshAttributes = 0;
    uint32_t m_faceCount = 0;
    uint32_t m_positionCount = 0;
    uint32_t m_normalCount = 0;
    uint32_t m_diffuseColorCount = 0;
    uint32_t m_specularColorCount = 0;
    uint32_t m_textureColorCount = 0;
    std::vector<PDF3D_U3D_ShadingDescription> m_shadingDescription;

    /* clod description */
    uint32_t m_minimumResolution = 0;
    uint32_t m_finalResolution = 0;

    /* resource description */
    uint32_t m_positionQualityFactor = 0;
    uint32_t m_normalQualityFactor = 0;
    uint32_t m_textureCoordQualityFactor = 0;
    float m_positionInverseQuant = 0.0;
    float m_normalInverseQuant = 0.0;
    float m_textureCoordInverseQuant = 0.0;
    float m_diffuseColorInverseQuant = 0.0;
    float m_specularColorInverseQuant = 0.0;
    float m_normalCreaseParameter = 0.0;
    float m_normalUpdateParameter = 0.0;
    float m_normalToleranceParameter = 0.0;

    /* bone description */
    uint32_t m_boneCount = 0;
    std::vector<PDF3D_U3D_BoneDescription> m_boneDescription;
};

class PDF3D_U3D_CLODBaseMeshContinuationBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_GeneratorCLODBaseMesh;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object, const PDF3D_U3D_CLODMeshDeclarationBlock* declarationBlock);

    struct BaseCornerInfo
    {
        uint32_t basePositionIndex = 0; // rBasePositionCount
        uint32_t baseNormalIndex = 0; // rBaseNormalCount
        uint32_t baseDiffuseColorIndex = 0; // rBaseDiffColorCnt
        uint32_t baseSpecularColorIndex = 0; // rBaseSpecColorCnt
        std::vector<uint32_t> baseTextureCoordIndex; // rBaseTexCoordCnt
    };

    struct BaseFace
    {
        uint32_t m_shadingId = 0; // cShading
        std::array<BaseCornerInfo, 3> m_corners = { };
    };

    const QString& getMeshName() const;
    uint32_t getChainIndex() const;

    uint32_t getFaceCount() const;
    uint32_t getPositionCount() const;
    uint32_t getNormalCount() const;
    uint32_t getDiffuseColorCount() const;
    uint32_t getSpecularColorCount() const;
    uint32_t getTextureColorCount() const;

    const std::vector<PDF3D_U3D_Vec3>& getBasePositions() const;
    const std::vector<PDF3D_U3D_Vec3>& getBaseNormals() const;
    const std::vector<PDF3D_U3D_Vec4>& getBaseDiffuseColors() const;
    const std::vector<PDF3D_U3D_Vec4>& getBaseSpecularColors() const;
    const std::vector<PDF3D_U3D_Vec4>& getBaseTextureCoords() const;
    const std::vector<BaseFace>& getBaseFaces() const;

private:
    QString m_meshName;
    uint32_t m_chainIndex = 0;

    /* base mesh description */
    uint32_t m_faceCount = 0;
    uint32_t m_positionCount = 0;
    uint32_t m_normalCount = 0;
    uint32_t m_diffuseColorCount = 0;
    uint32_t m_specularColorCount = 0;
    uint32_t m_textureColorCount = 0;

    std::vector<PDF3D_U3D_Vec3> m_basePositions;
    std::vector<PDF3D_U3D_Vec3> m_baseNormals;
    std::vector<PDF3D_U3D_Vec4> m_baseDiffuseColors;
    std::vector<PDF3D_U3D_Vec4> m_baseSpecularColors;
    std::vector<PDF3D_U3D_Vec4> m_baseTextureCoords;
    std::vector<BaseFace> m_baseFaces;
};

class PDF3D_U3D_CLODProgressiveMeshContinuationBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_GeneratorCLODProgMesh;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct NewFacePositionInfo
    {
        uint32_t shadingId = 0;
        uint8_t faceOrientation = 0;
        uint8_t thirdPositionType = 0;
        uint32_t localThirdPositionIndex = 0;
        uint32_t globalThirdPositionIndex = 0;
    };

    struct ResolutionUpdate
    {
        uint32_t splitPositionIndex = 0;

        std::vector<PDF3D_U3D_QuantizedVec4> newDiffuseColors;
        std::vector<PDF3D_U3D_QuantizedVec4> newSpecularColors;
        std::vector<PDF3D_U3D_QuantizedVec4> newTextureCoords;
        std::vector<NewFacePositionInfo> newFaces;
    };

private:
    QString m_meshName;
    uint32_t m_chainIndex = 0;

    uint32_t m_startResolution = 0;
    uint32_t m_endResolution = 0;
    uint32_t m_resolutionUpdateCount = 0;

    std::vector<ResolutionUpdate> m_resolutionUpdates;
};

class PDF3D_U3D_PointSetDeclarationBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_GeneratorPointSet;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getName() const { return m_pointSetName; }
    uint32_t getPointCount() const;
    uint32_t getPositionCount() const;
    uint32_t getNormalCount() const;
    uint32_t getDiffuseColorCount() const;
    uint32_t getSpecularColorCount() const;
    uint32_t getTextureCoordCount() const;
    const std::vector<PDF3D_U3D_ShadingDescription>& getShadingDescriptions() const;
    const PDF3D_U3D_ShadingDescription* getShadingDescriptionItem(uint32_t index) const { return index < m_shadingDescriptions.size() ? &m_shadingDescriptions[index] : nullptr; }

    uint32_t getPositionQualityFactor() const;
    uint32_t getNormalQualityFactor() const;
    uint32_t getTextureCoordQualityFactor() const;
    float getPositionInverseQuant() const;
    float getNormalInverseQuant() const;
    float getTextureCoordInverseQuant() const;
    float getDiffuseColorInverseQuant() const;
    float getSpecularColorInverseQuant() const;

    uint32_t getBoneCount() const;

    const std::vector<PDF3D_U3D_BoneDescription>& getBoneDescription() const;

private:
    QString m_pointSetName;
    uint32_t m_chainIndex = 0;

    /* point set description */
    uint32_t m_pointSetReserved = 0;
    uint32_t m_pointCount = 0;
    uint32_t m_positionCount = 0;
    uint32_t m_normalCount = 0;
    uint32_t m_diffuseColorCount = 0;
    uint32_t m_specularColorCount = 0;
    uint32_t m_textureCoordCount = 0;
    std::vector<PDF3D_U3D_ShadingDescription> m_shadingDescriptions;

    /* resource description */
    uint32_t m_positionQualityFactor = 0;
    uint32_t m_normalQualityFactor = 0;
    uint32_t m_textureCoordQualityFactor = 0;
    float m_positionInverseQuant = 0.0;
    float m_normalInverseQuant = 0.0;
    float m_textureCoordInverseQuant = 0.0;
    float m_diffuseColorInverseQuant = 0.0;
    float m_specularColorInverseQuant = 0.0;
    float m_reserved1 = 0.0;
    float m_reserved2 = 0.0;
    float m_reserved3 = 0.0;

    /* bone description */
    uint32_t m_boneCount = 0;
    std::vector<PDF3D_U3D_BoneDescription> m_boneDescription;
};

class PDF3D_U3D_PointSetContinuationBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_GeneratorPointSetCont;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object, const PDF3D_U3D_PointSetDeclarationBlock* declarationBlock);

    struct NewPointInfo
    {
        uint32_t shadingId = 0;
        uint32_t normalLocalIndex = 0;

        bool duplicateDiffuse = false;
        bool duplicateSpecular = false;

        PDF3D_U3D_QuantizedVec4 diffuseColor = PDF3D_U3D_QuantizedVec4();
        PDF3D_U3D_QuantizedVec4 specularColor = PDF3D_U3D_QuantizedVec4();
        std::vector<std::pair<bool, PDF3D_U3D_QuantizedVec4>> textureCoords;
    };

    struct UpdateItem
    {
        uint32_t splitPositionIndex = 0;

        PDF3D_U3D_QuantizedVec3 newPositionInfo;
        std::vector<PDF3D_U3D_QuantizedVec3> newNormals;
        std::vector<NewPointInfo> newPoints;
    };

    const QString& getName() const { return m_pointSetName; }
    const std::vector<UpdateItem>& getUpdateItems() const;

private:
    QString m_pointSetName;
    uint32_t m_chainIndex = 0;

    uint32_t m_startResolution = 0;
    uint32_t m_endResolution = 0;

    std::vector<UpdateItem> m_updateItems;
};

class PDF3D_U3D_LineSetDeclarationBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_GeneratorLineSet;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getName() const { return m_lineSetName; }
    uint32_t getLineCount() const;
    uint32_t getPositionCount() const;
    uint32_t getNormalCount() const;
    uint32_t getDiffuseColorCount() const;
    uint32_t getSpecularColorCount() const;
    uint32_t getTextureCoordCount() const;
    const std::vector<PDF3D_U3D_ShadingDescription>& getShadingDescriptions() const;
    const PDF3D_U3D_ShadingDescription* getShadingDescriptionItem(uint32_t index) const { return index < m_shadingDescriptions.size() ? &m_shadingDescriptions[index] : nullptr; }

    uint32_t getPositionQualityFactor() const;
    uint32_t getNormalQualityFactor() const;
    uint32_t getTextureCoordQualityFactor() const;
    float getPositionInverseQuant() const;
    float getNormalInverseQuant() const;
    float getTextureCoordInverseQuant() const;
    float getDiffuseColorInverseQuant() const;
    float getSpecularColorInverseQuant() const;

private:
    QString m_lineSetName;
    uint32_t m_chainIndex = 0;

    /* line set description */
    uint32_t m_lineSetReserved = 0;
    uint32_t m_lineCount = 0;
    uint32_t m_positionCount = 0;
    uint32_t m_normalCount = 0;
    uint32_t m_diffuseColorCount = 0;
    uint32_t m_specularColorCount = 0;
    uint32_t m_textureCoordCount = 0;
    std::vector<PDF3D_U3D_ShadingDescription> m_shadingDescriptions;

    /* resource description */
    uint32_t m_positionQualityFactor = 0;
    uint32_t m_normalQualityFactor = 0;
    uint32_t m_textureCoordQualityFactor = 0;
    float m_positionInverseQuant = 0.0;
    float m_normalInverseQuant = 0.0;
    float m_textureCoordInverseQuant = 0.0;
    float m_diffuseColorInverseQuant = 0.0;
    float m_specularColorInverseQuant = 0.0;
    float m_reserved1 = 0.0;
    float m_reserved2 = 0.0;
    float m_reserved3 = 0.0;
};

class PDF3D_U3D_LineSetContinuationBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_GeneratorLineSetCont;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object, const PDF3D_U3D_LineSetDeclarationBlock* declarationBlock);

    struct NewLineEndingInfo
    {
        uint32_t normalLocalIndex = 0;

        bool duplicateDiffuse = false;
        bool duplicateSpecular = false;

        PDF3D_U3D_QuantizedVec4 diffuseColor = PDF3D_U3D_QuantizedVec4();
        PDF3D_U3D_QuantizedVec4 specularColor = PDF3D_U3D_QuantizedVec4();
        std::vector<std::pair<bool, PDF3D_U3D_QuantizedVec4>> textureCoords;
    };

    struct NewLineInfo
    {
        uint32_t shadingId = 0;
        uint32_t firstPositionIndex = 0;

        NewLineEndingInfo p1;
        NewLineEndingInfo p2;
    };

    struct UpdateItem
    {
        uint32_t splitPositionIndex = 0;

        PDF3D_U3D_QuantizedVec3 newPositionInfo;
        std::vector<PDF3D_U3D_QuantizedVec3> newNormals;
        std::vector<NewLineInfo> newLines;
    };

    const QString& getName() const { return m_lineSetName; }

    const std::vector<UpdateItem>& getUpdateItems() const;

private:
    QString m_lineSetName;
    uint32_t m_chainIndex = 0;

    uint32_t m_startResolution = 0;
    uint32_t m_endResolution = 0;

    std::vector<UpdateItem> m_updateItems;
};

class PDF3D_U3D_2DGlyphModifierBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_Modifier2DGlyph;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getModifierName() const;
    uint32_t getChainIndex() const;
    uint32_t getGlyphAttributes() const;
    QPainterPath getPath() const;
    const QMatrix4x4& getMatrix() const;

private:
    QString m_modifierName;
    uint32_t m_chainIndex = 0;
    uint32_t m_glyphAttributes = 0;

    QPainterPath m_path;
    QMatrix4x4 m_matrix;
};

class PDF3D_U3D_SubdivisionModifierBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ModifierSubdivision;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getModifierName() const;
    uint32_t getChainIndex() const;
    uint32_t getAttributes() const;
    uint32_t getDepth() const;
    float getTension() const;
    float getError() const;

private:
    QString m_modifierName;
    uint32_t m_chainIndex = 0;
    uint32_t m_attributes = 0;
    uint32_t m_depth = 0;

    float m_tension = 0.0;
    float m_error = 0.0;
};

class PDF3D_U3D_AnimationModifierBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ModifierAnimation;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct MotionInformation
    {
        QString motionName;
        uint32_t motionAttributes = 0;
        float timeOffset = 0.0;
        float timeScale = 0.0;
    };

    const QString& getModifierName() const;
    uint32_t getChainIndex() const;

    uint32_t getAttributes() const;
    float getTimescale() const;
    uint32_t getMotionCount() const;
    const std::vector<MotionInformation>& getMotionInformations() const;
    float getBlendTime() const;

private:
    QString m_modifierName;
    uint32_t m_chainIndex = 0;

    uint32_t m_attributes = 0;
    float m_timescale = 0.0;
    uint32_t m_motionCount = 0;
    std::vector<MotionInformation> m_motionInformations;

    float m_blendTime = 0.0;
};

class PDF3D_U3D_BoneWeightModifierBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ModifierBoneWeights;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct BoneWeightList
    {
        std::vector<uint32_t> boneIndex;
        std::vector<uint32_t> boneWeight;
    };

    const QString& getModifierName() const;
    uint32_t getChainIndex() const;
    uint32_t getAttributes() const;
    float getInverseQuant() const;
    const std::vector<BoneWeightList>& getBoneWeightLists() const;

private:
    QString m_modifierName;
    uint32_t m_chainIndex = 0;
    uint32_t m_attributes = 0;
    float m_inverseQuant = 0.0;
    std::vector<BoneWeightList> m_boneWeightLists;
};

class PDF3D_U3D_ShadingModifierBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ModifierShading;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    using ShaderLists = std::vector<QStringList>;

    const QString& getModifierName() const;
    uint32_t getChainIndex() const;
    uint32_t getAttributes() const;
    const ShaderLists& getShaderLists() const;

private:
    QString m_modifierName;
    uint32_t m_chainIndex = 0;
    uint32_t m_attributes = 0;
    ShaderLists m_shaderLists;
};

class PDF3D_U3D_CLODModifierBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ModifierCLOD;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getModifierName() const;
    uint32_t getChainIndex() const;
    uint32_t getAttributes() const;
    float getCLODAutomaticLevelOfDetails() const;
    float getCLODModifierLevel() const;

private:
    QString m_modifierName;
    uint32_t m_chainIndex = 0;
    uint32_t m_attributes = 0;
    float m_CLODAutomaticLevelOfDetails = 0.0;
    float m_CLODModifierLevel = 0.0;
};

class PDF3D_U3D_LightResourceBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ResourceLight;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getResourceName() const;
    uint32_t getAttributes() const;
    uint8_t getType() const;
    const PDF3D_U3D_Vec4& getColor() const;
    const PDF3D_U3D_Vec3& getAttenuation() const;
    float getSpotAngle() const;
    float getIntensity() const;

private:
    QString m_resourceName;
    uint32_t m_attributes = 0;
    uint8_t m_type = 0;
    PDF3D_U3D_Vec4 m_color = { };
    PDF3D_U3D_Vec3 m_attenuation = { };
    float m_spotAngle = 0.0;
    float m_intensity = 0.0;
};

class PDF3D_U3D_ViewResourceBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ResourceView;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct Pass
    {
        QString rootNodeName;
        uint32_t renderAttributes = 0;
        uint32_t fogMode = 0;
        PDF3D_U3D_Vec4 color = { };
        float fogNear = 0.0;
        float fogFar = 0.0;
    };

    const QString& getResourceName() const;
    const std::vector<Pass>& getRenderPasses() const;

private:
    QString m_resourceName;
    std::vector<Pass> m_renderPasses;
};

class PDF3D_U3D_LitTextureShaderResourceBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ResourceLitShader;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct TextureInfo
    {
        QString textureName;
        float textureIntensity = 0.0;
        uint8_t blendFunction = 0;
        uint8_t blendSource = 0;
        float blendConstant = 0.0;
        uint8_t textureMode = 0;
        QMatrix4x4 textureTransform;
        QMatrix4x4 textureMap;
        uint8_t repeat = 0;
    };

    const QString& getResourceName() const;
    uint32_t getAttributes() const;
    float getAlphaTestReference() const;
    uint32_t getColorBlendFunction() const;
    uint32_t getAlphaTestFunction() const;
    uint32_t getRenderPassEnabled() const;
    uint32_t getShaderChannels() const;
    uint32_t getAlphaTextureChannels() const;
    const QString& getMaterialName() const;
    const std::vector<TextureInfo>& getTextureInfos() const;

private:
    QString m_resourceName;
    uint32_t m_attributes = 0;
    float m_alphaTestReference = 0.0;
    uint32_t m_alphaTestFunction = 0;
    uint32_t m_colorBlendFunction = 0;
    uint32_t m_renderPassEnabled = 0;
    uint32_t m_shaderChannels = 0;
    uint32_t m_alphaTextureChannels = 0;
    QString m_materialName;
    std::vector<TextureInfo> m_textureInfos;
};

class PDF3D_U3D_MaterialResourceBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ResourceMaterial;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getResourceName() const;
    uint32_t getMaterialAttributes() const;
    const PDF3D_U3D_Vec3& getAmbientColor() const;
    const PDF3D_U3D_Vec3& getDiffuseColor() const;
    const PDF3D_U3D_Vec3& getSpecularColor() const;
    const PDF3D_U3D_Vec3& getEmissiveColor() const;
    float getReflectivity() const;
    float getOpacity() const;

private:
    QString m_resourceName;
    uint32_t m_materialAttributes = 0;
    PDF3D_U3D_Vec3 m_ambientColor = { };
    PDF3D_U3D_Vec3 m_diffuseColor = { };
    PDF3D_U3D_Vec3 m_specularColor = { };
    PDF3D_U3D_Vec3 m_emissiveColor = { };
    float m_reflectivity = 0.0;
    float m_opacity = 0.0;
};

class PDF3D_U3D_TextureResourceBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ResourceTexture;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct ContinuationImageFormat
    {
        bool isExternal() const { return attributes & 0x0001; }

        uint8_t compressionType = 0;
        uint8_t channels = 0;
        uint16_t attributes = 0;
        uint32_t imageDataByteCount = 0;
        uint32_t imageURLCount = 0;
        QStringList imageURLs;
    };

    const QString& getResourceName() const;
    uint32_t getTextureHeight() const;
    uint32_t getTextureWidth() const;
    uint8_t getType() const;
    const std::vector<ContinuationImageFormat>& getFormats() const;

private:
    QString m_resourceName;
    uint32_t m_textureHeight = 0;
    uint32_t m_textureWidth = 0;
    uint8_t m_type = 0;
    std::vector<ContinuationImageFormat> m_formats;
};

class PDF3D_U3D_TextureContinuationResourceBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ResourceTextureCont;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getResourceName() const;
    uint32_t getImageIndex() const;
    const QByteArray& getImageData() const;

private:
    QString m_resourceName;
    uint32_t m_imageIndex = 0;
    QByteArray m_imageData;
};

// -------------------------------------------------------------------------------
//                                  PDF3D_U3D
// -------------------------------------------------------------------------------

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D
{
public:
    PDF3D_U3D();

    void setTexture(const QString& textureName, QImage texture);

private:
    std::map<QString, QImage> m_textures;
};

// -------------------------------------------------------------------------------
//                                  READER OBJECTS
// -------------------------------------------------------------------------------

class PDF3D_U3D_ContextManager
{
public:
    bool isContextCompressed(uint32_t context) const;
    bool isContextStaticAndCompressed(uint32_t context) const;

    uint32_t getTotalSymbolFrequency(uint32_t context) const;
    uint32_t getSymbolFrequency(uint32_t context, uint32_t symbol) const;
    uint32_t getCumulativeSymbolFrequency(uint32_t context, uint32_t symbol) const;
    uint32_t getSymbolFromFrequency(uint32_t context, uint32_t symbolFrequency) const;

    void addSymbol(uint32_t context, uint32_t symbol);

private:
    struct ContextData
    {
        ContextData()
        {
            symbols= { { 0, Data{ 1, 1} } };
        }

        struct Data
        {
            uint32_t symbolCount = 0;
            uint32_t cumulativeSymbolCount = 0;
        };

        std::map<uint32_t, Data> symbols;
    };

    const ContextData* getContextData(uint32_t context) const;

    std::map<uint32_t, ContextData> m_contexts;
};

class PDF3D_U3D_Constants
{
public:
    static constexpr uint32_t S_NOT_THREE_QUARTER_MASK = 0x00003FFF;
    static constexpr uint32_t S_QUARTER_MASK = 0x00004000;
    static constexpr uint32_t S_NOT_HALF_MASK = 0x00007FFF;
    static constexpr uint32_t S_HALF_MASK = 0x00008000;
    static constexpr uint32_t S_STATIC_FULL = 0x00000400;
    static constexpr uint32_t S_MAX_RANGE = S_STATIC_FULL + 0x00003FFF;
    static constexpr uint32_t S_CONTEXT_8 = 0;

    static constexpr uint32_t S_MAX_CUMULATIVE_SYMBOL_COUNT = 0x00001fff;
    static constexpr uint32_t S_MAXIMUM_SYMBOL_IN_HISTOGRAM = 0x0000FFFF;
};

class PDF3D_U3D_Decoder
{
public:

    void addBlock(const PDF3D_U3D_Block_Data& block) { m_blocks.push_back(block); }

    bool isEmpty() const { return m_blocks.empty(); }

    auto begin() const { return m_blocks.begin(); }
    auto end() const { return m_blocks.end(); }

    auto front() const { return m_blocks.front(); }
    auto back() const { return m_blocks.back(); }

    auto size() const { return m_blocks.size(); }

private:
    std::vector<PDF3D_U3D_Block_Data> m_blocks;
};

class PDF3D_U3D_DecoderChain
{
public:

    const QString& getName() const;
    void setName(const QString& newName);

    PDF3D_U3D_Decoder* getDecoder(uint32_t chainPosition);
    void addDecoder(PDF3D_U3D_Decoder&& decoder) { m_decoders.emplace_back(std::move(decoder)); }

    auto begin() const { return m_decoders.begin(); }
    auto end() const { return m_decoders.end(); }

private:
    QString m_name;
    std::vector<PDF3D_U3D_Decoder> m_decoders;
};

class PDF3D_U3D_DecoderPalette
{
public:

    PDF3D_U3D_DecoderChain* getDecoderChain(const QString& name);
    PDF3D_U3D_DecoderChain* createDecoderChain(const QString& name);

    auto begin() const { return m_decoderChains.begin(); }
    auto end() const { return m_decoderChains.end(); }

private:
    std::vector<PDF3D_U3D_DecoderChain> m_decoderChains;
};

class PDF3D_U3D_DecoderPalettes
{
public:
    using EPalette = PDF3D_U3D_Block_Info::EPalette;

    PDF3D_U3D_DecoderPalette* getDecoderPalette(EPalette palette);

    auto begin() const { return m_decoderLists.begin(); }
    auto end() const { return m_decoderLists.end(); }

private:
    std::array<PDF3D_U3D_DecoderPalette, PDF3D_U3D_Block_Info::PL_LastPalette> m_decoderLists;
};

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D_Parser
{
public:
    PDF3D_U3D_Parser();

    PDF3D_U3D parse(QByteArray data);

    void processBlock(const PDF3D_U3D_Block_Data& blockData, PDF3D_U3D_Block_Info::EPalette palette);
    void processModifierBlock(const PDF3D_U3D_Block_Data& blockData);
    void processGenericBlock(const PDF3D_U3D_Block_Data& blockData, PDF3D_U3D_Block_Info::EPalette palette);

    PDF3D_U3D_ContextManager* getContextManager() { return &m_contextManager; }
    bool isCompressed() const { return m_isCompressed; }
    QTextCodec* getTextCodec() const { return m_textCodec; }

    PDF3D_U3D_AbstractBlockPtr parseBlock(uint32_t blockType, const QByteArray& data, const QByteArray& metaData);
    PDF3D_U3D_AbstractBlockPtr parseBlock(const PDF3D_U3D_Block_Data& data);

    static uint32_t getBlockPadding(uint32_t blockSize);

    static PDF3D_U3D_Block_Data readBlockData(PDF3D_U3D_DataReader& reader);

    void processCLODMesh(const PDF3D_U3D_Decoder& decoder);
    void processPointSet(const PDF3D_U3D_Decoder& decoder);
    void processLineSet(const PDF3D_U3D_Decoder& decoder);
    void processTexture(const PDF3D_U3D_Decoder& decoder);

    void addBlockToU3D(PDF3D_U3D_AbstractBlockPtr block);

private:
    struct LoadBlockInfo
    {
        QString typeName;
        bool success = false;
        PDF3D_U3D_Block_Info::EBlockType blockType = PDF3D_U3D_Block_Info::EBlockType();
        uint32_t dataSize = 0;
        uint32_t metaDataSize = 0;
        PDF3D_U3D_AbstractBlockPtr block;
    };

    std::vector<PDF3D_U3D_AbstractBlockPtr> m_blocks;
    std::vector<LoadBlockInfo> m_allBlocks;

    QStringList m_errors;
    PDF3D_U3D_DecoderPalettes m_decoderPalettes;
    PDF3D_U3D_ContextManager m_contextManager;
    PDF3D_U3D m_object;
    PDF3D_U3D_AbstractBlockPtr m_fileBlock;
    QTextCodec* m_textCodec = nullptr;
    bool m_isCompressed = true;
    uint32_t m_priority = 0;
};

class PDF3D_U3D_DataReader
{
public:
    PDF3D_U3D_DataReader(QByteArray data, bool isCompressed);
    ~PDF3D_U3D_DataReader();

    void setData(QByteArray data);

    static constexpr uint32_t getRangeContext(uint32_t value) { return value + PDF3D_U3D_Constants::S_STATIC_FULL; }
    static constexpr uint32_t getStaticContext(uint32_t value) { return value; }

    uint8_t readU8();
    uint16_t readU16();
    uint32_t readU32();
    uint64_t readU64();
    int16_t readI16();
    int32_t readI32();
    float readF32();
    double readF64();

    uint8_t readCompressedU8(uint32_t context);
    uint16_t readCompressedU16(uint32_t context);
    uint32_t readCompressedU32(uint32_t context);

    QByteArray readByteArray(uint32_t size);
    QByteArray readRemainingData();
    void skipBytes(uint32_t size);
    void padTo32Bits();

    QUuid readUuid();
    QString readString(QTextCodec* textCodec);
    QStringList readStringList(uint32_t count, QTextCodec* textCodec);

    QMatrix4x4 readMatrix4x4();

    PDF3D_U3D_QuantizedVec4 readQuantizedVec4(uint32_t contextSign,
                                              uint32_t context1,
                                              uint32_t context2,
                                              uint32_t context3,
                                              uint32_t context4);

    PDF3D_U3D_QuantizedVec3 readQuantizedVec3(uint32_t contextSign,
                                              uint32_t context1,
                                              uint32_t context2,
                                              uint32_t context3);

    std::vector<PDF3D_U3D_ShadingDescription> readShadingDescriptions(uint32_t count);
    std::vector<PDF3D_U3D_BoneDescription> readBoneDescriptions(QTextCodec* textCodec, uint32_t count);

    bool isAtEnd() const;

    template<typename T>
    void readFloats32(T& container)
    {
        for (auto& value : container)
        {
            value = readF32();
        }
    }

    template<typename T>
    std::vector<T> readVectorFloats32(uint32_t count)
    {
        std::vector<T> result;
        result.reserve(count);

        for (uint32_t i = 0; i < count; ++i)
        {
            T value = T();
            readFloats32(value);
            result.emplace_back(std::move(value));
        }

        return result;
    }

private:
    uint32_t readSymbol(uint32_t context);
    uint8_t swapBits8(uint8_t source);
    uint32_t readBit();
    uint32_t read15Bits();

    QByteArray m_data;
    PDF3D_U3D_ContextManager m_contextManager;

    uint32_t m_high;
    uint32_t m_low;
    uint32_t m_underflow;
    uint32_t m_code;
    uint32_t m_position;
    bool m_isCompressed;
};

}   // namespace u3d

}   // namespace pdf

#endif // PDF3D_U3D_H
