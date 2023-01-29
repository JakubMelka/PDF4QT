//    Copyright (C) 2022-2023 Jakub Melka
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

#include "pdf3d_u3d.h"
#include "pdfstreamfilters.h"

#include <QImageReader>
#include <QStringConverter>

#include <array>
#include <algorithm>

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
    cQntBoneWeight,
    cTimeSign,
    cTimeDiff,
    cDispSign,
    cDispDiff,
    cRotSign,
    cRotDiff,
    cScalSign,
    cScalDiff
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

struct PDF3D_U3D_QuantizedVec1
{
    uint8_t signBits;
    uint32_t diff1;
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
    bool hasTextures() const { return textureLayerCount > 0; }
};

class PDF3D_U3D_AbstractBlock
{
public:
    inline constexpr PDF3D_U3D_AbstractBlock() = default;
    virtual ~PDF3D_U3D_AbstractBlock() = default;

    using EBlockType = PDF3D_U3D_Block_Info::EBlockType;

    virtual EBlockType getBlockType() const = 0;

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    uint32_t getNewPriority() const;

private:
    uint32_t m_newPriority = 0;
};

class PDF3D_U3D_NewObjectTypeBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_FileNewObject;
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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

    virtual EBlockType getBlockType() const override { return PDF3D_U3D_Block_Info::BT_FileNewObject; }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    const std::vector<UpdateItem>& getUpdateItems() const { return m_updateItems; }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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

class PDF3D_U3D_RHAdobeMeshResourceBlock : public PDF3D_U3D_AbstractBlock
{
public:
    virtual EBlockType getBlockType() const override { return PDF3D_U3D_Block_Info::BT_Unknown; }

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

private:
};

class PDF3D_U3D_LineSetContinuationBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_GeneratorLineSetCont;
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct Pass
    {
        QString rootNodeName;
        uint32_t renderAttributes = 0;
        uint32_t fogMode = 0;
        PDF3D_U3D_Vec4 fogColor = { };
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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

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
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    const QString& getResourceName() const;
    uint32_t getImageIndex() const;
    const QByteArray& getImageData() const;

private:
    QString m_resourceName;
    uint32_t m_imageIndex = 0;
    QByteArray m_imageData;
};

class PDF3D_U3D_MotionResourceBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = PDF3D_U3D_Block_Info::BT_ResourceMotion;
    virtual EBlockType getBlockType() const override { return static_cast<EBlockType>(ID); }

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object);

    struct KeyFrame
    {
        float time = 0.0;
        PDF3D_U3D_Vec3 displacement = { };
        PDF3D_U3D_Vec4 rotation = { };
        PDF3D_U3D_Vec3 scale = { };

        PDF3D_U3D_QuantizedVec1 timeDiff;
        PDF3D_U3D_QuantizedVec3 displacementDiff;
        PDF3D_U3D_QuantizedVec3 rotationDiff;
        PDF3D_U3D_QuantizedVec3 scaleDiff;
    };

    struct Motion
    {
        QString trackName;
        uint32_t timeCount = 0;
        float displacementInverseQuant = 0.0;
        float rotationInverseQuant = 0.0;
        std::vector<KeyFrame> keyFrames;
    };

    const QString& getResourceName() const;
    uint32_t getTrackCount() const;
    float getTimeInverseQuant() const;
    float getRotationInverseQuant() const;
    const std::vector<Motion>& getMotions() const;

private:
    QString m_resourceName;
    uint32_t m_trackCount = 0;
    float m_timeInverseQuant = 0.0f;
    float m_rotationInverseQuant = 0.0f;
    std::vector<Motion> m_motions;
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
    auto get(size_t index) const { return m_blocks[index]; }

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

    bool isCompressed() const { return m_isCompressed; }
    QStringDecoder* getStringDecoder() { return &m_stringDecoder; }

    PDF3D_U3D_AbstractBlockPtr parseBlock(uint32_t blockType, const QByteArray& data, const QByteArray& metaData);
    PDF3D_U3D_AbstractBlockPtr parseBlock(const PDF3D_U3D_Block_Data& data);

    static uint32_t getBlockPadding(uint32_t blockSize);

    static PDF3D_U3D_Block_Data readBlockData(PDF3D_U3D_DataReader& reader);

    void processCLODMesh(const PDF3D_U3D_Decoder& decoder);
    void processPointSet(const PDF3D_U3D_Decoder& decoder);
    void processLineSet(const PDF3D_U3D_Decoder& decoder);
    void processTexture(const PDF3D_U3D_Decoder& decoder);

    void addBaseMeshGeometry(PDF3D_U3D_MeshGeometry* geometry,
                             const PDF3D_U3D_CLODMeshDeclarationBlock* declarationBlock,
                             const PDF3D_U3D_CLODBaseMeshContinuationBlock* geometryBlock);

    void addPointSetGeometry(PDF3D_U3D_PointSetGeometry* geometry,
                             const PDF3D_U3D_PointSetDeclarationBlock* declarationBlock,
                             const PDF3D_U3D_PointSetContinuationBlock* geometryBlock);

    void addLineSetGeometry(PDF3D_U3D_LineSetGeometry* geometry,
                            const PDF3D_U3D_LineSetDeclarationBlock* declarationBlock,
                            const PDF3D_U3D_LineSetContinuationBlock* geometryBlock);

    QVector3D getDequantizedVec3(PDF3D_U3D_QuantizedVec3 quantizedV3, QVector3D prediction, float inverseQuant);
    QVector4D getDequantizedVec4(PDF3D_U3D_QuantizedVec4 quantizedV4, QVector4D prediction, float inverseQuant);

    void addBlockToU3D(PDF3D_U3D_AbstractBlockPtr block);

    PDF3D_U3D_Block_Info::EPalette getPaletteByBlockType(uint32_t blockType) const;

    QStringList getErrors() const { return m_errors; }

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
    PDF3D_U3D m_object;
    PDF3D_U3D_AbstractBlockPtr m_fileBlock;
    bool m_isCompressed = true;
    uint32_t m_priority = 0;
    QStringDecoder m_stringDecoder;
    uint32_t m_RHAdobeMeshResourceId = 0;
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
    uint32_t readU24();
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
    QString readString(QStringDecoder* textCodec);
    QStringList readStringList(uint32_t count, QStringDecoder* stringDecoder);

    QMatrix4x4 readMatrix4x4();

    PDF3D_U3D_QuantizedVec1 readQuantizedVec1(uint32_t contextSign,
                                              uint32_t context1);

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
    std::vector<PDF3D_U3D_BoneDescription> readBoneDescriptions(QStringDecoder* stringDecoder, uint32_t count);

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

PDF3D_U3D_DataReader::PDF3D_U3D_DataReader(QByteArray data, bool isCompressed) :
    m_data(std::move(data)),
    m_contextManager(),
    m_high(0x0000FFFF),
    m_low(0),
    m_underflow(0),
    m_code(0),
    m_position(0),
    m_isCompressed(isCompressed)
{

}

PDF3D_U3D_DataReader::~PDF3D_U3D_DataReader()
{

}

void PDF3D_U3D_DataReader::setData(QByteArray data)
{
    m_data = std::move(data);
    m_position = 0;
    m_low = 0;
    m_high = 0x0000FFFF;
    m_underflow = 0;
    m_code = 0;
}

uint8_t PDF3D_U3D_DataReader::readU8()
{
    uint32_t value = readSymbol(PDF3D_U3D_Constants::S_CONTEXT_8);
    --value;
    return swapBits8(value);
}

uint16_t PDF3D_U3D_DataReader::readU16()
{
    const uint16_t low = readU8();
    const uint16_t high = readU8();
    return low + (high << 8);
}

uint32_t PDF3D_U3D_DataReader::readU24()
{
    const uint32_t low = readU8();
    const uint32_t high = readU8();
    const uint32_t highest = readU8();
    return low + (high << 8) + (highest << 16);
}

uint32_t PDF3D_U3D_DataReader::readU32()
{
    const uint32_t low = readU16();
    const uint32_t high = readU16();
    return low + (high << 16);
}

uint64_t PDF3D_U3D_DataReader::readU64()
{
    const uint64_t low = readU32();
    const uint64_t high = readU32();
    return low + (high << 32);
}

int16_t PDF3D_U3D_DataReader::readI16()
{
    return static_cast<int16_t>(readU16());
}

int32_t PDF3D_U3D_DataReader::readI32()
{
    return static_cast<int32_t>(readU32());
}

float PDF3D_U3D_DataReader::readF32()
{
    const uint32_t value = readU32();
    return std::bit_cast<float>(value);
}

double PDF3D_U3D_DataReader::readF64()
{
    const uint64_t value = readU64();
    return std::bit_cast<double>(value);
}

uint8_t PDF3D_U3D_DataReader::readCompressedU8(uint32_t context)
{
    if (m_isCompressed && m_contextManager.isContextCompressed(context))
    {
        const uint32_t symbol = readSymbol(context);
        if (symbol != 0)
        {
            // Compressed symbol
            return symbol - 1;
        }
        else
        {
            // New symbol
            const uint32_t value = readU8();
            m_contextManager.addSymbol(context, value + 1);
            return value;
        }
    }

    return readU8();
}

uint16_t PDF3D_U3D_DataReader::readCompressedU16(uint32_t context)
{
    if (m_isCompressed && m_contextManager.isContextCompressed(context))
    {
        const uint32_t symbol = readSymbol(context);
        if (symbol != 0)
        {
            // Compressed symbol
            return symbol - 1;
        }
        else
        {
            // New symbol
            const uint32_t value = readU16();
            m_contextManager.addSymbol(context, value + 1);
            return value;
        }
    }

    return readU16();
}

uint32_t PDF3D_U3D_DataReader::readCompressedU32(uint32_t context)
{
    if (m_isCompressed && m_contextManager.isContextCompressed(context))
    {
        const uint32_t symbol = readSymbol(context);
        if (symbol != 0)
        {
            // Compressed symbol
            return symbol - 1;
        }
        else
        {
            // New symbol
            const uint32_t value = readU32();
            m_contextManager.addSymbol(context, value + 1);
            return value;
        }
    }

    return readU32();
}

QByteArray PDF3D_U3D_DataReader::readByteArray(uint32_t size)
{
    Q_ASSERT(m_position % 8 == 0);
    uint32_t bytePosition = m_position / 8;
    QByteArray data = m_data.mid(bytePosition, int(size));
    m_position += size * 8;
    return data;
}

QByteArray PDF3D_U3D_DataReader::readRemainingData()
{
    Q_ASSERT(m_position % 8 == 0);
    uint32_t bytePosition = m_position / 8;
    QByteArray data = m_data.mid(bytePosition, -1);
    m_position = m_data.size() * 8;
    return data;
}

void PDF3D_U3D_DataReader::skipBytes(uint32_t size)
{
    m_position += size * 8;
}

void PDF3D_U3D_DataReader::padTo32Bits()
{
    uint32_t padBits = m_position % 32;

    if (padBits > 0)
    {
        m_position += 32 - padBits;
    }
}

QUuid PDF3D_U3D_DataReader::readUuid()
{
    uint32_t A = readU32();
    uint16_t B = readU16();
    uint16_t C = readU16();

    std::array<uint8_t, 8> D = { };
    for (uint8_t& value : D)
    {
        value = readU8();
    }

    return QUuid(A, B, C, D[0], D[1], D[2], D[3], D[4], D[5], D[6], D[7]);
}

QString PDF3D_U3D_DataReader::readString(QStringDecoder* stringDecoder)
{
    int size = readU16();

    QByteArray encodedString(size, '0');

    for (int i = 0; i < size; ++i)
    {
        encodedString[i] = readU8();
    }

    Q_ASSERT(stringDecoder);
    return stringDecoder->decode(encodedString);
}

QStringList PDF3D_U3D_DataReader::readStringList(uint32_t count, QStringDecoder* stringDecoder)
{
    QStringList stringList;

    for (uint32_t i = 0; i < count; ++i)
    {
        stringList << readString(stringDecoder);
    }

    return stringList;
}

QMatrix4x4 PDF3D_U3D_DataReader::readMatrix4x4()
{
    std::array<float, 16> transformMatrix = { };
    readFloats32(transformMatrix);

    QMatrix4x4 matrix(transformMatrix.data());
    return matrix.transposed();
}

PDF3D_U3D_QuantizedVec1 PDF3D_U3D_DataReader::readQuantizedVec1(uint32_t contextSign,
                                                                uint32_t context1)
{
    PDF3D_U3D_QuantizedVec1 result;

    result.signBits = readCompressedU8(contextSign);
    result.diff1 = readCompressedU32(context1);

    return result;
}

PDF3D_U3D_QuantizedVec4 PDF3D_U3D_DataReader::readQuantizedVec4(uint32_t contextSign,
                                                                uint32_t context1,
                                                                uint32_t context2,
                                                                uint32_t context3,
                                                                uint32_t context4)
{
    PDF3D_U3D_QuantizedVec4 result;

    result.signBits = readCompressedU8(contextSign);
    result.diff1 = readCompressedU32(context1);
    result.diff2 = readCompressedU32(context2);
    result.diff3 = readCompressedU32(context3);
    result.diff4 = readCompressedU32(context4);

    return result;
}

PDF3D_U3D_QuantizedVec3 PDF3D_U3D_DataReader::readQuantizedVec3(uint32_t contextSign,
                                                                uint32_t context1,
                                                                uint32_t context2,
                                                                uint32_t context3)
{
    PDF3D_U3D_QuantizedVec3 result;

    result.signBits = readCompressedU8(contextSign);
    result.diff1 = readCompressedU32(context1);
    result.diff2 = readCompressedU32(context2);
    result.diff3 = readCompressedU32(context3);

    return result;
}

std::vector<PDF3D_U3D_ShadingDescription> PDF3D_U3D_DataReader::readShadingDescriptions(uint32_t count)
{
    std::vector<PDF3D_U3D_ShadingDescription> result;

    for (uint32_t i = 0; i < count; ++i)
    {
        PDF3D_U3D_ShadingDescription item;

        item.shadingAttributes = readU32();
        item.textureLayerCount = readU32();
        for (uint32_t j = 0; j < item.textureLayerCount; ++j)
        {
            item.textureCoordDimensions.push_back(readU32());
        }
        item.originalShading = readU32();

        result.emplace_back(std::move(item));
    }

    return result;
}

std::vector<PDF3D_U3D_BoneDescription> PDF3D_U3D_DataReader::readBoneDescriptions(QStringDecoder* stringDecoder, uint32_t count)
{
    std::vector<PDF3D_U3D_BoneDescription> result;

    for (uint32_t i = 0; i < count; ++i)
    {
        PDF3D_U3D_BoneDescription item;

        item.boneName = readString(stringDecoder);
        item.parentBoneName = readString(stringDecoder);
        item.boneAttributes = readU32();
        item.boneLength = readF32();
        readFloats32(item.boneDisplacement);
        readFloats32(item.boneOrientation);

        if (item.isLinkPresent())
        {
            item.boneLinkCount = readU32();
            item.boneLinkLength = readF32();
        }

        if (item.isJointPresent())
        {
            item.startJoint.jointCenterU = readF32();
            item.startJoint.jointCenterV = readF32();
            item.startJoint.jointScaleU = readF32();
            item.startJoint.jointScaleV = readF32();

            item.endJoint.jointCenterU = readF32();
            item.endJoint.jointCenterV = readF32();
            item.endJoint.jointScaleU = readF32();
            item.endJoint.jointScaleV = readF32();
        }

        item.rotationConstraintMax[0] = readF32();
        item.rotationConstraintMin[0] = readF32();
        item.rotationConstraintMax[1] = readF32();
        item.rotationConstraintMin[1] = readF32();
        item.rotationConstraintMax[2] = readF32();
        item.rotationConstraintMin[2] = readF32();

        result.emplace_back(std::move(item));
    }

    return result;
}

bool PDF3D_U3D_DataReader::isAtEnd() const
{
    return m_position >= uint32_t(m_data.size()) * 8;
}

uint32_t PDF3D_U3D_DataReader::readSymbol(uint32_t context)
{
    uint32_t position = m_position;
    m_code = readBit();
    m_position += m_underflow;
    m_code = (m_code << 15) | read15Bits();
    m_position = position;

    const uint32_t totalCumFreq = m_contextManager.getTotalSymbolFrequency(context);
    const uint32_t range = (m_high + 1) - m_low;
    const uint32_t codeCumFreq = ((totalCumFreq) * (1 + m_code - m_low) - 1) / range;
    const uint32_t value = m_contextManager.getSymbolFromFrequency(context, codeCumFreq);
    const uint32_t valueCumFreq = m_contextManager.getCumulativeSymbolFrequency(context, value);
    const uint32_t valueFreq = m_contextManager.getSymbolFrequency(context, value);

    uint32_t low = m_low;
    uint32_t high = m_high;

    high = low - 1 + range * (valueCumFreq + valueFreq) / totalCumFreq;
    low = low + range * valueCumFreq / totalCumFreq;
    m_contextManager.addSymbol(context, value);

    constexpr std::array S_BIT_COUNTS = { 4, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint32_t bitCount = S_BIT_COUNTS[((low >> 12) ^ (high >> 12)) & 0x0000000F];

    constexpr std::array S_FAST_NOT_MASK = { 0x0000FFFF, 0x00007FFF, 0x00003FFF, 0x00001FFF, 0x00000FFF };
    low = low & S_FAST_NOT_MASK[bitCount];
    high = high & S_FAST_NOT_MASK[bitCount];

    if (bitCount > 0)
    {
        high <<= bitCount;
        low <<= bitCount;
        high = high | ((1 << bitCount) - 1);
    }

    uint32_t maskedLow = low & PDF3D_U3D_Constants::S_HALF_MASK;
    uint32_t maskedHigh = high & PDF3D_U3D_Constants::S_HALF_MASK;

    while (((maskedLow | maskedHigh) == 0) || ((maskedLow == PDF3D_U3D_Constants::S_HALF_MASK) && maskedHigh == PDF3D_U3D_Constants::S_HALF_MASK))
    {
        low = (low & PDF3D_U3D_Constants::S_NOT_HALF_MASK) << 1;
        high = ((high & PDF3D_U3D_Constants::S_NOT_HALF_MASK) << 1) | 1;
        maskedLow = low & PDF3D_U3D_Constants::S_HALF_MASK;
        maskedHigh = high & PDF3D_U3D_Constants::S_HALF_MASK;
        ++bitCount;
    }

    const uint32_t savedBitsLow = maskedLow;
    const uint32_t savedBitsHigh = maskedHigh;

    if (bitCount > 0)
    {
        bitCount += m_underflow;
        m_underflow = 0;
    }

    maskedLow = low & PDF3D_U3D_Constants::S_QUARTER_MASK;
    maskedHigh = high & PDF3D_U3D_Constants::S_QUARTER_MASK;

    uint32_t underflow = 0;
    while ((maskedLow == 0x4000) && (maskedHigh == 0))
    {
        low = low & PDF3D_U3D_Constants::S_NOT_THREE_QUARTER_MASK;
        high = high & PDF3D_U3D_Constants::S_NOT_THREE_QUARTER_MASK;
        low += low;
        high += high;
        high = high | 1;
        maskedLow = low & PDF3D_U3D_Constants::S_QUARTER_MASK;
        maskedHigh = high & PDF3D_U3D_Constants::S_QUARTER_MASK;
        ++underflow;
    }

    m_underflow += underflow;
    low = low | savedBitsLow;
    high = high | savedBitsHigh;
    m_low = low;
    m_high = high;
    m_position += bitCount;

    return value;
}

uint8_t PDF3D_U3D_DataReader::swapBits8(uint8_t source)
{
    // Jakub Melka: Code is public domain from here:http:
    // graphics.stanford.edu/~seander/bithacks.html#ReverseByteWith32Bits
    return ((source * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
}

uint32_t PDF3D_U3D_DataReader::readBit()
{
    uint32_t bytePosition = m_position / 8;
    if (bytePosition < uint32_t(m_data.size()))
    {
        uint32_t data = m_data[bytePosition];
        uint32_t bitPosition = m_position % 8;
        ++m_position;
        return (data >> bitPosition) & 1;
    }

    return 0;
}

uint32_t PDF3D_U3D_DataReader::read15Bits()
{
    uint32_t result = 0;
    for (int i = 0; i < 15; ++i)
    {
        result = (result << 1) | readBit();
    }

    return result;
}

bool PDF3D_U3D_ContextManager::isContextCompressed(uint32_t context) const
{
    return (context != PDF3D_U3D_Constants::S_CONTEXT_8) && context < PDF3D_U3D_Constants::S_MAX_RANGE;
}

bool PDF3D_U3D_ContextManager::isContextStaticAndCompressed(uint32_t context) const
{
    return (context != PDF3D_U3D_Constants::S_CONTEXT_8) && context < PDF3D_U3D_Constants::S_STATIC_FULL;
}

uint32_t PDF3D_U3D_ContextManager::getTotalSymbolFrequency(uint32_t context) const
{
    if (isContextStaticAndCompressed(context))
    {
        if (const ContextData* contextData = getContextData(context))
        {
            return contextData->symbols.at(0).cumulativeSymbolCount;
        }

        return 1;
    }
    else if (context == PDF3D_U3D_Constants::S_CONTEXT_8)
    {
        return 256;
    }
    else
    {
        return context - PDF3D_U3D_Constants::S_STATIC_FULL;
    }
}

uint32_t PDF3D_U3D_ContextManager::getSymbolFrequency(uint32_t context, uint32_t symbol) const
{
    if (isContextStaticAndCompressed(context))
    {
        if (const ContextData* contextData = getContextData(context))
        {
            auto it = contextData->symbols.find(symbol);
            if (it != contextData->symbols.cend())
            {
                return it->second.symbolCount;
            }
        }
        else if (symbol == 0)
        {
            return 1;
        }

        return 0;
    }

    return 1;
}

uint32_t PDF3D_U3D_ContextManager::getCumulativeSymbolFrequency(uint32_t context, uint32_t symbol) const
{
    if (isContextStaticAndCompressed(context))
    {
        uint32_t value = 0;

        if (const ContextData* contextData = getContextData(context))
        {
            auto it = contextData->symbols.find(symbol);
            if (it != contextData->symbols.cend())
            {
                return contextData->symbols.at(0).cumulativeSymbolCount - it->second.cumulativeSymbolCount;
            }
            else
            {
                return contextData->symbols.at(0).cumulativeSymbolCount;
            }
        }

        return value;
    }

    return symbol - 1;
}

uint32_t PDF3D_U3D_ContextManager::getSymbolFromFrequency(uint32_t context, uint32_t symbolFrequency) const
{
    if (isContextStaticAndCompressed(context))
    {
        if (m_contexts.count(context) &&
            symbolFrequency > 0 &&
            m_contexts.at(context).symbols.at(0).cumulativeSymbolCount >= symbolFrequency)
        {
            uint32_t value = 0;

            for (const auto& item : m_contexts.at(context).symbols)
            {
                if (getCumulativeSymbolFrequency(context, item.first) <= symbolFrequency)
                {
                    value = item.first;
                }
                else
                {
                    break;
                }
            }

            return value;
        }

        return 0;
    }

    return symbolFrequency + 1;
}

void PDF3D_U3D_ContextManager::addSymbol(uint32_t context, uint32_t symbol)
{
    if (isContextStaticAndCompressed(context) && symbol < PDF3D_U3D_Constants::S_MAXIMUM_SYMBOL_IN_HISTOGRAM)
    {
        ContextData& contextData = m_contexts[context];

        if (contextData.symbols[0].cumulativeSymbolCount >= PDF3D_U3D_Constants::S_MAX_CUMULATIVE_SYMBOL_COUNT)
        {
            // Scale down
            uint32_t tempAccum = 0;

            for (auto it = contextData.symbols.rbegin(); it != contextData.symbols.rend(); ++it)
            {
                ContextData::Data& data = it->second;
                data.symbolCount >>= 1;
                tempAccum += data.symbolCount;
                data.cumulativeSymbolCount = tempAccum;
            }

            // Preserve initial count of 1
            ContextData::Data& initialData = contextData.symbols[0];
            ++initialData.symbolCount;
            ++initialData.cumulativeSymbolCount;
        }

        auto it = contextData.symbols.find(symbol);
        if (it == contextData.symbols.cend())
        {
            it = contextData.symbols.insert(std::make_pair(symbol, ContextData::Data())).first;

            if (std::next(it) != contextData.symbols.cend())
            {
                it->second.cumulativeSymbolCount = std::next(it)->second.cumulativeSymbolCount;
            }
        }

        ++it->second.symbolCount;
        ++it->second.cumulativeSymbolCount;
        for(auto cit = contextData.symbols.begin(); cit != it; ++cit)
        {
            ++cit->second.cumulativeSymbolCount;
        }
    }
}

const PDF3D_U3D_ContextManager::ContextData* PDF3D_U3D_ContextManager::getContextData(uint32_t context) const
{
    auto it = m_contexts.find(context);
    if (it != m_contexts.cend())
    {
        return &it->second;
    }

    return nullptr;
}

PDF3D_U3D::PDF3D_U3D()
{
    // Default light
    m_defaultLight.setType(PDF3D_U3D_Light::Ambient);
    m_defaultLight.setIsSpecular(false);
    m_defaultLight.setIsSpotDecay(false);
    m_defaultLight.setIsEnabled(true);
    m_defaultLight.setIntensity(1.0);
    m_defaultLight.setColor(QColor::fromRgbF(0.75, 0.75, 0.75, 1.0));

    // Default texture
    m_defaultTexture = QImage(8, 8, QImage::Format_RGBA8888);
    m_defaultTexture.fill(Qt::black);

    for (int i = 0; i < 4; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            m_defaultTexture.setPixelColor(4 + i, j, QColor::fromRgbF(1.0f, 0.4f, 0.2f, 1.0f));
            m_defaultTexture.setPixelColor(i, 4 + j, QColor::fromRgbF(1.0f, 0.4f, 0.2f, 1.0f));
        }
    }

    // Default material
    m_defaultMaterial.setAmbientColor(QColor::fromRgbF(0.75, 0.75, 0.75, 1.0));

    // Default shader - initialized in the constructor
    m_defaultShader = PDF3D_U3D_Shader();
}

const PDF3D_U3D_Node& PDF3D_U3D::getNode(const QString& nodeName) const
{
    auto it = m_sceneGraph.find(nodeName);
    if (it != m_sceneGraph.end())
    {
        return it->second;
    }

    static const PDF3D_U3D_Node dummyNode;
    return dummyNode;
}

PDF3D_U3D_Node& PDF3D_U3D::getOrCreateNode(const QString& nodeName)
{
    return m_sceneGraph[nodeName];
}

const PDF3D_U3D_View* PDF3D_U3D::getView(const QString& viewName) const
{
    auto it = m_viewResources.find(viewName);
    if (it != m_viewResources.cend())
    {
        return &it->second;
    }

    return nullptr;
}

const PDF3D_U3D_Light* PDF3D_U3D::getLight(const QString& lightName) const
{
    auto it = m_lightResources.find(lightName);
    if (it != m_lightResources.cend())
    {
        return &it->second;
    }

    return &m_defaultLight;
}

QImage PDF3D_U3D::getTexture(const QString& textureName) const
{
    auto it = m_textureResources.find(textureName);
    if (it != m_textureResources.cend())
    {
        return it->second;
    }

    return m_defaultTexture;
}

const PDF3D_U3D_Shader* PDF3D_U3D::getShader(const QString& shaderName) const
{
    auto it = m_shaderResources.find(shaderName);
    if (it != m_shaderResources.cend())
    {
        return &it->second;
    }

    return &m_defaultShader;
}

const PDF3D_U3D_Material* PDF3D_U3D::getMaterial(const QString& materialName) const
{
    auto it = m_materialResources.find(materialName);
    if (it != m_materialResources.cend())
    {
        return &it->second;
    }

    return &m_defaultMaterial;
}

const PDF3D_U3D_Geometry* PDF3D_U3D::getGeometry(const QString& geometryName) const
{
    auto it = m_geometries.find(geometryName);
    if (it != m_geometries.cend())
    {
        return it->second.data();
    }

    return nullptr;
}

void PDF3D_U3D::setViewResource(const QString& viewName, PDF3D_U3D_View view)
{
    m_viewResources[viewName] = std::move(view);
}

void PDF3D_U3D::setLightResource(const QString& lightName, PDF3D_U3D_Light light)
{
    m_lightResources[lightName] = std::move(light);
}

void PDF3D_U3D::setTextureResource(const QString& textureName, QImage texture)
{
    m_textureResources[textureName] = std::move(texture);
}

void PDF3D_U3D::setShaderResource(const QString& shaderName, PDF3D_U3D_Shader shader)
{
    m_shaderResources[shaderName] = std::move(shader);
}

void PDF3D_U3D::setMaterialResource(const QString& materialName, PDF3D_U3D_Material material)
{
    m_materialResources[materialName] = std::move(material);
}

void PDF3D_U3D::setGeometry(const QString& geometryName, QSharedPointer<PDF3D_U3D_Geometry> geometry)
{
    m_geometries[geometryName] = std::move(geometry);
}

void PDF3D_U3D::setShadersToGeometry(const QString& geometryName, const std::vector<QStringList>& shaderLists)
{
    auto it = m_geometries.find(geometryName);
    if (it != m_geometries.cend())
    {
        it->second->setShaders(shaderLists);
    }
}

PDF3D_U3D PDF3D_U3D::parse(const QByteArray& data, QStringList* errors)
{
    PDF3D_U3D_Parser parser;
    PDF3D_U3D result = parser.parse(data);

    if (errors)
    {
        *errors = parser.getErrors();
    }

    return result;
}

PDF3D_U3D_Block_Data PDF3D_U3D_Parser::readBlockData(PDF3D_U3D_DataReader& reader)
{
    PDF3D_U3D_Block_Data data;

    // Read block
    uint32_t blockType = reader.readU32();
    uint32_t dataSize = reader.readU32();
    uint32_t metaDataSize = reader.readU32();

    //qDebug() << QString("BT: %1, data size = %2").arg(QString::number(blockType, 16), QString::number(dataSize));

    // Read block data
    QByteArray blockData = reader.readByteArray(dataSize);
    reader.skipBytes(getBlockPadding(dataSize));

    // Read block metadata
    QByteArray metaData = reader.readByteArray(metaDataSize);
    reader.skipBytes(getBlockPadding(metaDataSize));

    data.blockType = static_cast<PDF3D_U3D_Block_Info::EBlockType>(blockType);
    data.blockData = std::move(blockData);
    data.metaData = std::move(metaData);

    return data;
}

void PDF3D_U3D_Parser::processCLODMesh(const PDF3D_U3D_Decoder& decoder)
{
    auto block = parseBlock(decoder.front());
    const PDF3D_U3D_CLODMeshDeclarationBlock* declarationBlock = dynamic_cast<const PDF3D_U3D_CLODMeshDeclarationBlock*>(block.data());

    QSharedPointer<PDF3D_U3D_MeshGeometry> geometry(new PDF3D_U3D_MeshGeometry());

    for (auto it = std::next(decoder.begin()); it != decoder.end(); ++it)
    {
        auto blockData = *it;
        switch (blockData.blockType)
        {
            case PDF3D_U3D_Block_Info::BT_GeneratorCLODBaseMesh:
            {
                auto currentBlock = PDF3D_U3D_CLODBaseMeshContinuationBlock::parse(blockData.blockData, blockData.metaData, this, declarationBlock);
                const PDF3D_U3D_CLODBaseMeshContinuationBlock* typedBlock = dynamic_cast<const PDF3D_U3D_CLODBaseMeshContinuationBlock*>(currentBlock.data());

                addBaseMeshGeometry(geometry.data(), declarationBlock, typedBlock);
                break;
            }

            case PDF3D_U3D_Block_Info::BT_GeneratorCLODProgMesh:
            {
                auto currentBlock = PDF3D_U3D_CLODProgressiveMeshContinuationBlock::parse(blockData.blockData, blockData.metaData, this);
                Q_UNUSED(currentBlock);
                break;
            }

            default:
                m_errors << PDFTranslationContext::tr("Invalid block type '%1' for CLOD mesh!").arg(blockData.blockType, 8, 16);
                break;
        }
    }

    if (!geometry->isEmpty())
    {
        m_object.setGeometry(declarationBlock->getName(), std::move(geometry));
    }
}

void PDF3D_U3D_Parser::processPointSet(const PDF3D_U3D_Decoder& decoder)
{
    auto block = parseBlock(decoder.front());
    const PDF3D_U3D_PointSetDeclarationBlock* declarationBlock = dynamic_cast<const PDF3D_U3D_PointSetDeclarationBlock*>(block.data());

    QSharedPointer<PDF3D_U3D_PointSetGeometry> geometry(new PDF3D_U3D_PointSetGeometry());

    for (auto it = std::next(decoder.begin()); it != decoder.end(); ++it)
    {
        auto blockData = *it;
        switch (blockData.blockType)
        {
            case PDF3D_U3D_Block_Info::BT_GeneratorPointSetCont:
            {
                auto currentBlock = PDF3D_U3D_PointSetContinuationBlock::parse(blockData.blockData, blockData.metaData, this, declarationBlock);
                const PDF3D_U3D_PointSetContinuationBlock* typedBlock = dynamic_cast<const PDF3D_U3D_PointSetContinuationBlock*>(currentBlock.data());

                addPointSetGeometry(geometry.data(), declarationBlock, typedBlock);
                break;
            }

            default:
                m_errors << PDFTranslationContext::tr("Invalid block type '%1' for Point set!").arg(blockData.blockType, 8, 16);
                break;
        }
    }

    if (!geometry->isEmpty())
    {
        m_object.setGeometry(declarationBlock->getName(), std::move(geometry));
    }
}

void PDF3D_U3D_Parser::processLineSet(const PDF3D_U3D_Decoder& decoder)
{
    auto block = parseBlock(decoder.front());
    const PDF3D_U3D_LineSetDeclarationBlock* declarationBlock = dynamic_cast<const PDF3D_U3D_LineSetDeclarationBlock*>(block.data());

    QSharedPointer<PDF3D_U3D_LineSetGeometry> geometry(new PDF3D_U3D_LineSetGeometry());

    for (auto it = std::next(decoder.begin()); it != decoder.end(); ++it)
    {
        auto blockData = *it;
        switch (blockData.blockType)
        {
            case PDF3D_U3D_Block_Info::BT_GeneratorLineSetCont:
            {
                auto currentBlock = PDF3D_U3D_LineSetContinuationBlock::parse(blockData.blockData, blockData.metaData, this, declarationBlock);
                const PDF3D_U3D_LineSetContinuationBlock* typedBlock = dynamic_cast<const PDF3D_U3D_LineSetContinuationBlock*>(currentBlock.data());

                addLineSetGeometry(geometry.data(), declarationBlock, typedBlock);
                break;
            }

            default:
                m_errors << PDFTranslationContext::tr("Invalid block type '%1' for line set!").arg(blockData.blockType, 8, 16);
                break;
        }
    }

    if (!geometry->isEmpty())
    {
        m_object.setGeometry(declarationBlock->getName(), std::move(geometry));
    }
}

void PDF3D_U3D_Parser::processTexture(const PDF3D_U3D_Decoder& decoder)
{
    auto block = parseBlock(decoder.front());
    const PDF3D_U3D_TextureResourceBlock* declarationBlock = dynamic_cast<const PDF3D_U3D_TextureResourceBlock*>(block.data());

    std::map<uint32_t, QByteArray> imageData;

    for (auto it = std::next(decoder.begin()); it != decoder.end(); ++it)
    {
        auto blockData = *it;
        switch (blockData.blockType)
        {
            case PDF3D_U3D_Block_Info::BT_ResourceTextureCont:
            {
                auto currentBlock = PDF3D_U3D_TextureContinuationResourceBlock::parse(blockData.blockData, blockData.metaData, this);
                const PDF3D_U3D_TextureContinuationResourceBlock* typedBlock = dynamic_cast<const PDF3D_U3D_TextureContinuationResourceBlock*>(currentBlock.data());

                imageData[typedBlock->getImageIndex()].append(typedBlock->getImageData());
                break;
            }

            default:
                m_errors << PDFTranslationContext::tr("Invalid block type '%1' for texture!").arg(blockData.blockType, 8, 16);
        }
    }

    QImage::Format format = QImage::Format_Invalid;
    switch (declarationBlock->getType())
    {
        case 0x01:
            format = QImage::Format_Alpha8;
            break;

        case 0x0E:
            format = QImage::Format_RGB888;
            break;

        case 0x0F:
            format = QImage::Format_RGBA8888;
            break;

        case 0x10:
            format = QImage::Format_Grayscale8;
            break;

        case 0x11:
            format = QImage::Format_RGBA8888;
            break;

        default:
            m_errors << PDFTranslationContext::tr("Invalid texture format '%1'.").arg(declarationBlock->getType(), 2, 16);
            break;
    }

    if (format == QImage::Format_Invalid)
    {
        m_errors << PDFTranslationContext::tr("Texture image bad format.");
        return;
    }

    QImage texture(declarationBlock->getTextureWidth(),
                   declarationBlock->getTextureHeight(),
                   format);
    texture.fill(Qt::transparent);

    const std::vector<PDF3D_U3D_TextureResourceBlock::ContinuationImageFormat>& formats = declarationBlock->getFormats();
    for (size_t i = 0; i < formats.size(); ++i)
    {
        const PDF3D_U3D_TextureResourceBlock::ContinuationImageFormat& continuationFormat = formats[i];

        if (continuationFormat.isExternal())
        {
            m_errors << PDFTranslationContext::tr("Textures with external images not supported.");
            continue;
        }

        QBuffer buffer(&imageData[static_cast<uint32_t>(i)]);
        buffer.open(QBuffer::ReadOnly);

        QImageReader reader;
        reader.setAutoDetectImageFormat(true);
        reader.setAutoTransform(false);
        reader.setDecideFormatFromContent(true);
        reader.setDevice(&buffer);

        QImage image = reader.read();
        buffer.close();

        if (image.width() != texture.width() || image.height() != texture.height())
        {
            m_errors << PDFTranslationContext::tr("Texture image bad size.");
            continue;
        }

        switch (texture.format())
        {
            case QImage::Format_Alpha8:
            {
                if (image.hasAlphaChannel())
                {
                    texture = image.createAlphaMask();
                }
                else
                {
                    image = image.convertToFormat(QImage::Format_Grayscale8);
                    std::memcpy(texture.bits(), image.bits(), image.sizeInBytes());
                }

                break;
            }
            case QImage::Format_RGB888:  
            case QImage::Format_RGBA8888:  
            case QImage::Format_Grayscale8:
            {
                // Jakub Melka: We will ignore channel bit flags - they are very often not used anyway
                texture = image.convertToFormat(texture.format());
                break;
            }

            default:
                Q_ASSERT(false);
                break;
        }
    }

    m_object.setTextureResource(declarationBlock->getResourceName(), std::move(texture));
}

void PDF3D_U3D_Parser::addBaseMeshGeometry(PDF3D_U3D_MeshGeometry* geometry,
                                           const PDF3D_U3D_CLODMeshDeclarationBlock* declarationBlock,
                                           const PDF3D_U3D_CLODBaseMeshContinuationBlock* geometryBlock)
{
    const std::vector<PDF3D_U3D_Vec3>& basePositions = geometryBlock->getBasePositions();
    const std::vector<PDF3D_U3D_Vec3>& baseNormals = geometryBlock->getBaseNormals();
    const std::vector<PDF3D_U3D_Vec4>& baseDiffuseColors = geometryBlock->getBaseDiffuseColors();
    const std::vector<PDF3D_U3D_Vec4>& baseSpecularColors = geometryBlock->getBaseSpecularColors();
    const std::vector<PDF3D_U3D_Vec4>& baseTextureCoords = geometryBlock->getBaseTextureCoords();
    const std::vector<PDF3D_U3D_CLODBaseMeshContinuationBlock::BaseFace>& baseFaces = geometryBlock->getBaseFaces();

    for (const PDF3D_U3D_Vec3& value : basePositions)
    {
        geometry->addPosition(QVector3D(value[0], value[1], value[2]));
    }

    for (const PDF3D_U3D_Vec3& value : baseNormals)
    {
        geometry->addNormal(QVector3D(value[0], value[1], value[2]));
    }

    for (const PDF3D_U3D_Vec4& value : baseDiffuseColors)
    {
        geometry->addDiffuseColor(QVector4D(value[0], value[1], value[2], value[3]));
    }

    for (const PDF3D_U3D_Vec4& value : baseSpecularColors)
    {
        geometry->addSpecularColor(QVector4D(value[0], value[1], value[2], value[3]));
    }

    for (const PDF3D_U3D_Vec4& value : baseTextureCoords)
    {
        geometry->addTextureCoordinate(QVector4D(value[0], value[1], value[2], value[3]));
    }

    size_t maxTextureLayers = 0;

    for (const PDF3D_U3D_CLODBaseMeshContinuationBlock::BaseFace& face : baseFaces)
    {
        PDF3D_U3D_MeshGeometry::Triangle triangle;

        const PDF3D_U3D_ShadingDescription* shading = declarationBlock->getShadingDescriptionItem(face.m_shadingId);

        triangle.shadingId = face.m_shadingId;

        if (shading)
        {
            triangle.hasDiffuse = shading->hasDiffuseColors();
            triangle.hasSpecular = shading->hasSpecularColors();
            triangle.hasTexture = shading->hasTextures();
        }

        Q_ASSERT(triangle.vertices.size() == face.m_corners.size());
        for (size_t i = 0; i < triangle.vertices.size(); ++i)
        {
            PDF3D_U3D_MeshGeometry::Vertex& vertex = triangle.vertices[i];
            const PDF3D_U3D_CLODBaseMeshContinuationBlock::BaseCornerInfo& vertexInfo = face.m_corners[i];

            vertex.positionIndex = vertexInfo.basePositionIndex;
            vertex.normalIndex = vertexInfo.baseNormalIndex;
            vertex.diffuseColorIndex = vertexInfo.baseDiffuseColorIndex;
            vertex.specularColorIndex = vertexInfo.baseSpecularColorIndex;
            vertex.textureCoordIndex = !vertexInfo.baseTextureCoordIndex.empty() ? vertexInfo.baseTextureCoordIndex.front() : 0;

            maxTextureLayers = qMax(maxTextureLayers, vertexInfo.baseTextureCoordIndex.size());
        }

        geometry->addTriangle(std::move(triangle));
    }

    if (maxTextureLayers > 1)
    {
        m_errors << PDFTranslationContext::tr("More than one texture not supported.");
    }
}

void PDF3D_U3D_Parser::addPointSetGeometry(PDF3D_U3D_PointSetGeometry* geometry,
                                           const PDF3D_U3D_PointSetDeclarationBlock* declarationBlock,
                                           const PDF3D_U3D_PointSetContinuationBlock* geometryBlock)
{
    const auto& updateItems = geometryBlock->getUpdateItems();

    if (geometry->isEmpty())
    {
        geometry->addNormal(QVector3D());
        geometry->addDiffuseColor(QVector4D());
        geometry->addSpecularColor(QVector4D());
    }

    bool hasTextures = false;

    for (const PDF3D_U3D_PointSetContinuationBlock::UpdateItem& item : updateItems)
    {
        // 1. Determine position
        const size_t splitPositionIndex = item.splitPositionIndex;

        QVector3D splitPosition = geometry->getPosition(splitPositionIndex);
        QVector3D currentPosition = getDequantizedVec3(item.newPositionInfo, splitPosition, declarationBlock->getPositionInverseQuant());

        geometry->addPosition(currentPosition);
        auto points = geometry->queryPointsByVertexIndex(splitPositionIndex);

        size_t predictedNormalCount = 0;
        size_t predictedDiffuseCount = 0;
        size_t predictedSpecularCount = 0;
        QVector3D predictedNormal(0, 0, 0);
        QVector4D predictedDiffuseColor(0, 0, 0, 0);
        QVector4D predictedSpecularColor(0, 0, 0, 0);

        // 2. Prepare predicted values
        for (const PDF3D_U3D_PointSetGeometry::Point & point : points)
        {
            auto description = declarationBlock->getShadingDescriptionItem(point.shadingId);
            const bool hasDiffuse = description && description->hasDiffuseColors();
            const bool hasSpecular = description && description->hasSpecularColors();

            if (point.position == splitPositionIndex)
            {
                predictedNormal += geometry->getNormal(point.normal);
                ++predictedNormalCount;

                if (hasDiffuse)
                {
                    predictedDiffuseColor += geometry->getDiffuseColor(point.diffuseColor);
                    ++predictedDiffuseCount;
                }

                if (hasSpecular)
                {
                    predictedSpecularColor += geometry->getSpecularColor(point.specularColor);
                    ++predictedSpecularCount;
                }

            }
        }

        if (predictedNormalCount > 0)
        {
            predictedNormal /= qreal(predictedNormalCount);
        }

        if (predictedDiffuseCount > 0)
        {
            predictedDiffuseColor /= qreal(predictedDiffuseCount);
        }

        if (predictedSpecularCount > 0)
        {
            predictedSpecularColor /= qreal(predictedSpecularCount);
        }

        const size_t normalCount = geometry->getNormalCount();
        for (const auto& normal : item.newNormals)
        {
            QVector3D normalVector = getDequantizedVec3(normal, predictedNormal, declarationBlock->getNormalInverseQuant());
            geometry->addNormal(normalVector);
        }

        for (const PDF3D_U3D_PointSetContinuationBlock::NewPointInfo& point : item.newPoints)
        {
            PDF3D_U3D_PointSetGeometry::Point processedPoint;

            processedPoint.shadingId = static_cast<uint32_t>(point.shadingId);
            processedPoint.position = static_cast<uint32_t>(geometry->getPositionCount() - 1);
            processedPoint.normal = static_cast<uint32_t>(point.normalLocalIndex + normalCount);

            if (auto description = declarationBlock->getShadingDescriptionItem(point.shadingId))
            {
                if (description->hasDiffuseColors())
                {
                    if (point.duplicateDiffuse)
                    {
                        processedPoint.diffuseColor = static_cast<uint32_t>(geometry->getDiffuseColorCount() - 1);
                    }
                    else
                    {
                        processedPoint.diffuseColor = static_cast<uint32_t>(geometry->getDiffuseColorCount());
                        QVector4D diffuseColor = getDequantizedVec4(point.diffuseColor, predictedDiffuseColor, declarationBlock->getDiffuseColorInverseQuant());
                        geometry->addDiffuseColor(diffuseColor);
                    }
                }

                if (description->hasSpecularColors())
                {
                    if (point.duplicateSpecular)
                    {
                        processedPoint.specularColor = static_cast<uint32_t>(geometry->getSpecularColorCount() - 1);
                    }
                    else
                    {
                        processedPoint.specularColor = static_cast<uint32_t>(geometry->getSpecularColorCount());
                        QVector4D specularColor = getDequantizedVec4(point.specularColor, predictedSpecularColor, declarationBlock->getSpecularColorInverseQuant());
                        geometry->addSpecularColor(specularColor);
                    }
                }

                if (description->hasTextures())
                {
                    hasTextures = true;
                }
            }

            geometry->addPoint(std::move(processedPoint));
        }
    }

    if (hasTextures)
    {
        m_errors << PDFTranslationContext::tr("Textures for points are not supported.");
    }
}

void PDF3D_U3D_Parser::addLineSetGeometry(PDF3D_U3D_LineSetGeometry* geometry,
                                          const PDF3D_U3D_LineSetDeclarationBlock* declarationBlock,
                                          const PDF3D_U3D_LineSetContinuationBlock* geometryBlock)
{
    const auto& updateItems = geometryBlock->getUpdateItems();

    if (geometry->isEmpty())
    {
        geometry->addNormal(QVector3D());
        geometry->addDiffuseColor(QVector4D());
        geometry->addSpecularColor(QVector4D());
    }

    bool hasTextures = false;

    for (const PDF3D_U3D_LineSetContinuationBlock::UpdateItem& item : updateItems)
    {
        // 1. Determine position
        const size_t splitPositionIndex = item.splitPositionIndex;

        QVector3D splitPosition = geometry->getPosition(splitPositionIndex);
        QVector3D currentPosition = getDequantizedVec3(item.newPositionInfo, splitPosition, declarationBlock->getPositionInverseQuant());

        geometry->addPosition(currentPosition);
        auto lines = geometry->queryLinesByVertexIndex(splitPositionIndex);

        size_t predictedNormalCount = 0;
        size_t predictedDiffuseCount = 0;
        size_t predictedSpecularCount = 0;
        QVector3D predictedNormal(0, 0, 0);
        QVector4D predictedDiffuseColor(0, 0, 0, 0);
        QVector4D predictedSpecularColor(0, 0, 0, 0);

        // 2. Prepare predicted values
        for (const PDF3D_U3D_LineSetGeometry::Line & line : lines)
        {
            auto description = declarationBlock->getShadingDescriptionItem(line.shadingId);
            const bool hasDiffuse = description && description->hasDiffuseColors();
            const bool hasSpecular = description && description->hasSpecularColors();

            if (line.position1 == splitPositionIndex)
            {
                predictedNormal += geometry->getNormal(line.normal1);
                ++predictedNormalCount;

                if (hasDiffuse)
                {
                    predictedDiffuseColor += geometry->getDiffuseColor(line.diffuseColor1);
                    ++predictedDiffuseCount;
                }

                if (hasSpecular)
                {
                    predictedSpecularColor += geometry->getSpecularColor(line.specularColor1);
                    ++predictedSpecularCount;
                }

            }
            if (line.position2 == splitPositionIndex)
            {
                predictedNormal += geometry->getNormal(line.normal2);
                ++predictedNormalCount;

                if (hasDiffuse)
                {
                    predictedDiffuseColor += geometry->getDiffuseColor(line.diffuseColor2);
                    ++predictedDiffuseCount;
                }

                if (hasSpecular)
                {
                    predictedSpecularColor += geometry->getSpecularColor(line.specularColor2);
                    ++predictedSpecularCount;
                }
            }
        }

        if (predictedNormalCount > 0)
        {
            predictedNormal /= qreal(predictedNormalCount);
        }

        if (predictedDiffuseCount > 0)
        {
            predictedDiffuseColor /= qreal(predictedDiffuseCount);
        }

        if (predictedSpecularCount > 0)
        {
            predictedSpecularColor /= qreal(predictedSpecularCount);
        }

        const size_t normalCount = geometry->getNormalCount();
        for (const auto& normal : item.newNormals)
        {
            QVector3D normalVector = getDequantizedVec3(normal, predictedNormal, declarationBlock->getNormalInverseQuant());
            geometry->addNormal(normalVector);
        }

        for (const PDF3D_U3D_LineSetContinuationBlock::NewLineInfo& line : item.newLines)
        {
            PDF3D_U3D_LineSetGeometry::Line processedLine;

            processedLine.shadingId = static_cast<uint32_t>(line.shadingId);
            processedLine.position1 = static_cast<uint32_t>(line.firstPositionIndex);
            processedLine.position2 = static_cast<uint32_t>(geometry->getPositionCount() - 1);
            processedLine.normal1 = static_cast<uint32_t>(line.p1.normalLocalIndex + normalCount);
            processedLine.normal2 = static_cast<uint32_t>(line.p2.normalLocalIndex + normalCount);

            if (auto description = declarationBlock->getShadingDescriptionItem(line.shadingId))
            {
                if (description->hasDiffuseColors())
                {
                    if (line.p1.duplicateDiffuse)
                    {
                        processedLine.diffuseColor1 = static_cast<uint32_t>(geometry->getDiffuseColorCount() - 1);
                    }
                    else
                    {
                        processedLine.diffuseColor1 = static_cast<uint32_t>(geometry->getDiffuseColorCount());
                        QVector4D diffuseColor = getDequantizedVec4(line.p1.diffuseColor, predictedDiffuseColor, declarationBlock->getDiffuseColorInverseQuant());
                        geometry->addDiffuseColor(diffuseColor);
                    }

                    if (line.p2.duplicateDiffuse)
                    {
                        processedLine.diffuseColor2 = static_cast<uint32_t>(geometry->getDiffuseColorCount() - 1);
                    }
                    else
                    {
                        processedLine.diffuseColor2 = static_cast<uint32_t>(geometry->getDiffuseColorCount());
                        QVector4D diffuseColor = getDequantizedVec4(line.p2.diffuseColor, predictedDiffuseColor, declarationBlock->getDiffuseColorInverseQuant());
                        geometry->addDiffuseColor(diffuseColor);
                    }
                }

                if (description->hasSpecularColors())
                {
                    if (line.p1.duplicateSpecular)
                    {
                        processedLine.specularColor1 = static_cast<uint32_t>(geometry->getSpecularColorCount() - 1);
                    }
                    else
                    {
                        processedLine.specularColor1 = static_cast<uint32_t>(geometry->getSpecularColorCount());
                        QVector4D specularColor = getDequantizedVec4(line.p1.specularColor, predictedSpecularColor, declarationBlock->getSpecularColorInverseQuant());
                        geometry->addSpecularColor(specularColor);
                    }

                    if (line.p2.duplicateSpecular)
                    {
                        processedLine.specularColor2 = static_cast<uint32_t>(geometry->getSpecularColorCount() - 1);
                    }
                    else
                    {
                        processedLine.specularColor2 = static_cast<uint32_t>(geometry->getSpecularColorCount());
                        QVector4D specularColor = getDequantizedVec4(line.p2.specularColor, predictedSpecularColor, declarationBlock->getSpecularColorInverseQuant());
                        geometry->addSpecularColor(specularColor);
                    }
                }

                if (description->hasTextures())
                {
                    hasTextures = true;
                }
            }

            geometry->addLine(std::move(processedLine));
        }
    }

    if (hasTextures)
    {
        m_errors << PDFTranslationContext::tr("Textures for lines are not supported.");
    }
}

QVector3D PDF3D_U3D_Parser::getDequantizedVec3(PDF3D_U3D_QuantizedVec3 quantizedV3,
                                               QVector3D prediction,
                                               float inverseQuant)
{
    QVector3D result = prediction;

    if (quantizedV3.signBits & 0x01)
    {
        result[0] -= quantizedV3.diff1 * inverseQuant;
    }
    else
    {
        result[0] += quantizedV3.diff1 * inverseQuant;
    }

    if (quantizedV3.signBits & 0x02)
    {
        result[1] -= quantizedV3.diff2 * inverseQuant;
    }
    else
    {
        result[1] += quantizedV3.diff2 * inverseQuant;
    }

    if (quantizedV3.signBits & 0x04)
    {
        result[2] -= quantizedV3.diff3 * inverseQuant;
    }
    else
    {
        result[2] += quantizedV3.diff3 * inverseQuant;
    }

    return result;
}

QVector4D PDF3D_U3D_Parser::getDequantizedVec4(PDF3D_U3D_QuantizedVec4 quantizedV4,
                                               QVector4D prediction,
                                               float inverseQuant)
{
    QVector4D result = prediction;

    if (quantizedV4.signBits & 0x01)
    {
        result[0] -= quantizedV4.diff1 * inverseQuant;
    }
    else
    {
        result[0] += quantizedV4.diff1 * inverseQuant;
    }

    if (quantizedV4.signBits & 0x02)
    {
        result[1] -= quantizedV4.diff2 * inverseQuant;
    }
    else
    {
        result[1] += quantizedV4.diff2 * inverseQuant;
    }

    if (quantizedV4.signBits & 0x04)
    {
        result[2] -= quantizedV4.diff3 * inverseQuant;
    }
    else
    {
        result[2] += quantizedV4.diff3 * inverseQuant;
    }

    if (quantizedV4.signBits & 0x08)
    {
        result[3] -= quantizedV4.diff4 * inverseQuant;
    }
    else
    {
        result[3] += quantizedV4.diff4 * inverseQuant;
    }

    return result;
}

void PDF3D_U3D_Parser::addBlockToU3D(PDF3D_U3D_AbstractBlockPtr block)
{
    switch (block->getBlockType())
    {
        case PDF3D_U3D_Block_Info::BT_ResourceLight:
        {
            const PDF3D_U3D_LightResourceBlock* lightResourceBlock = dynamic_cast<const PDF3D_U3D_LightResourceBlock*>(block.data());
            PDF3D_U3D_Light light;

            auto colorVector = lightResourceBlock->getColor();
            auto attenuation = lightResourceBlock->getAttenuation();

            light.setIsEnabled(lightResourceBlock->getAttributes() & 0x01);
            light.setIsSpecular(lightResourceBlock->getAttributes() & 0x02);
            light.setIsSpotDecay(lightResourceBlock->getAttributes() & 0x04);
            light.setColor(QColor::fromRgbF(colorVector[0], colorVector[1], colorVector[2], colorVector[3]));
            light.setSpotAngle(lightResourceBlock->getSpotAngle());
            light.setIntensity(lightResourceBlock->getIntensity());
            light.setAttenuation(QVector3D(attenuation[0], attenuation[1], attenuation[2]));

            PDF3D_U3D_Light::Type type = PDF3D_U3D_Light::Ambient;
            switch (lightResourceBlock->getType())
            {
                case 0x00:
                    type = PDF3D_U3D_Light::Ambient;
                    break;

                case 0x01:
                    type = PDF3D_U3D_Light::Directional;
                    break;

                case 0x02:
                    type = PDF3D_U3D_Light::Point;
                    break;

                case 0x03:
                    type = PDF3D_U3D_Light::Spot;
                    break;

                default:
                    m_errors << PDFTranslationContext::tr("Unknown light type '%1'.").arg(lightResourceBlock->getType(), 2, 16);
                    break;
            }

            m_object.setLightResource(lightResourceBlock->getResourceName(), std::move(light));
            break;
        }

        case PDF3D_U3D_Block_Info::BT_ResourceView:
        {
            const PDF3D_U3D_ViewResourceBlock* viewResourceBlock = dynamic_cast<const PDF3D_U3D_ViewResourceBlock*>(block.data());
            PDF3D_U3D_View view;

            std::vector<PDF3D_U3D_RenderPass> renderPasses;
            for (const PDF3D_U3D_ViewResourceBlock::Pass& sourceRenderPass : viewResourceBlock->getRenderPasses())
            {
                PDF3D_U3D_RenderPass renderPass;

                renderPass.setRootNodeName(sourceRenderPass.rootNodeName);

                if (sourceRenderPass.renderAttributes & 0x00000001)
                {
                    PDF3D_U3D_Fog fog;

                    fog.setEnabled(true);
                    fog.setMode(static_cast<PDF3D_U3D_Fog::FogMode>(sourceRenderPass.fogMode));
                    fog.setColor(QColor::fromRgbF(sourceRenderPass.fogColor[0],
                                                  sourceRenderPass.fogColor[1],
                                                  sourceRenderPass.fogColor[2],
                                                  sourceRenderPass.fogColor[3]));
                    fog.setNear(sourceRenderPass.fogNear);
                    fog.setFar(sourceRenderPass.fogFar);

                    renderPass.setFog(fog);
                }

                renderPasses.emplace_back(std::move(renderPass));
            }

            view.setRenderPasses(std::move(renderPasses));

            m_object.setViewResource(viewResourceBlock->getResourceName(), std::move(view));
            break;
        }

        case PDF3D_U3D_Block_Info::BT_ResourceLitShader:
        {
            const PDF3D_U3D_LitTextureShaderResourceBlock* shaderBlock = dynamic_cast<const PDF3D_U3D_LitTextureShaderResourceBlock*>(block.data());

            PDF3D_U3D_Shader shader;

            shader.setIsLightingEnabled(shaderBlock->getAttributes() & 0x01);
            shader.setIsAlphaTestEnabled(shaderBlock->getAttributes() & 0x02);
            shader.setIsUseVertexColor(shaderBlock->getAttributes() & 0x04);
            shader.setAlphaTestReference(shaderBlock->getAlphaTestReference());
            shader.setAlphaTestFunction(static_cast<PDF3D_U3D_Shader::AlphaTestFunction>(shaderBlock->getAlphaTestFunction()));
            shader.setColorBlendFunction(static_cast<PDF3D_U3D_Shader::ColorBlendFunction>(shaderBlock->getColorBlendFunction()));
            shader.setRenderPassEnabledFlags(shaderBlock->getRenderPassEnabled());
            shader.setShaderChannels(shaderBlock->getShaderChannels());
            shader.setAlphaTextureChannels(shaderBlock->getAlphaTextureChannels());
            shader.setMaterialName(shaderBlock->getMaterialName());

            PDF3D_U3D_Shader::PDF_U3D_TextureInfos textureInfos;
            for (const PDF3D_U3D_LitTextureShaderResourceBlock::TextureInfo& textureInfo : shaderBlock->getTextureInfos())
            {
                PDF3D_U3D_Shader::PDF_U3D_TextureInfo info;

                info.textureName = textureInfo.textureName;
                info.textureIntensity = textureInfo.textureIntensity;
                info.blendFunction = static_cast<PDF3D_U3D_Shader::TextureBlendFunction>(textureInfo.blendFunction);
                info.blendSource = static_cast<PDF3D_U3D_Shader::TextureBlendSource>(textureInfo.blendSource);
                info.blendConstant = textureInfo.blendConstant;
                info.textureMode = static_cast<PDF3D_U3D_Shader::TextureMode>(textureInfo.textureMode);
                info.textureTransform = textureInfo.textureTransform;
                info.textureMap = textureInfo.textureMap;
                info.repeatU = textureInfo.repeat & 0x01;
                info.repeatV = textureInfo.repeat & 0x02;

                textureInfos.emplace_back(std::move(info));
            }
            shader.setTextureInfos(std::move(textureInfos));

            m_object.setShaderResource(shaderBlock->getResourceName(), std::move(shader));
            break;
        }

        case PDF3D_U3D_Block_Info::BT_ResourceMaterial:
        {
            const PDF3D_U3D_MaterialResourceBlock* materialBlock = dynamic_cast<const PDF3D_U3D_MaterialResourceBlock*>(block.data());

            PDF3D_U3D_Material material;

            const bool hasAmbient = materialBlock->getMaterialAttributes() & 0x01;
            const bool hasDiffuse = materialBlock->getMaterialAttributes() & 0x02;
            const bool hasSpecular = materialBlock->getMaterialAttributes() & 0x04;
            const bool hasEmmissive = materialBlock->getMaterialAttributes() & 0x08;
            const bool hasReflectivity = materialBlock->getMaterialAttributes() & 0x10;
            const bool hasOpacity = materialBlock->getMaterialAttributes() & 0x20;

            const float opacity = hasOpacity ? materialBlock->getOpacity() : 1.0f;
            const float reflectivity = hasReflectivity ? materialBlock->getReflectivity() : 0.0f;

            auto a = materialBlock->getAmbientColor();
            auto d = materialBlock->getDiffuseColor();
            auto s = materialBlock->getSpecularColor();
            auto e = materialBlock->getEmissiveColor();

            QColor ambientColor = hasAmbient ? QColor::fromRgbF(a[0], a[1], a[2], opacity) : QColor();
            QColor diffuseColor = hasDiffuse ? QColor::fromRgbF(d[0], d[1], d[2], opacity) : QColor();
            QColor specularColor = hasSpecular ? QColor::fromRgbF(s[0], s[1], s[2], opacity) : QColor();
            QColor emmisiveColor = hasEmmissive ? QColor::fromRgbF(e[0], e[1], e[2], opacity) : QColor();

            material.setAmbientColor(ambientColor);
            material.setDiffuseColor(diffuseColor);
            material.setSpecularColor(specularColor);
            material.setEmmisiveColor(emmisiveColor);
            material.setReflectivity(reflectivity);

            m_object.setMaterialResource(materialBlock->getResourceName(), std::move(material));
            break;
        }

        case PDF3D_U3D_Block_Info::BT_NodeGroup:
        {
            const PDF3D_U3D_GroupNodeBlock* groupNodeBlock = dynamic_cast<const PDF3D_U3D_GroupNodeBlock*>(block.data());

            PDF3D_U3D_Node& node = m_object.getOrCreateNode(groupNodeBlock->getGroupNodeName());
            node.setNodeName(groupNodeBlock->getGroupNodeName());
            node.setType(PDF3D_U3D_Node::Group);

            for (const PDF3D_U3D_AbstractBlock::ParentNodeData& parentNodeData : groupNodeBlock->getParentNodesData())
            {
                m_object.getOrCreateNode(parentNodeData.parentNodeName).addChild(groupNodeBlock->getGroupNodeName(), parentNodeData.transformMatrix);
            }

            break;
        }

        case PDF3D_U3D_Block_Info::BT_NodeModel:
        {
            const PDF3D_U3D_ModelNodeBlock* modelNodeBlock = dynamic_cast<const PDF3D_U3D_ModelNodeBlock*>(block.data());

            PDF3D_U3D_Node& node = m_object.getOrCreateNode(modelNodeBlock->getModelNodeName());
            node.setNodeName(modelNodeBlock->getModelNodeName());
            node.setResourceName(modelNodeBlock->getModelResourceName());
            node.setType(PDF3D_U3D_Node::Model);
            node.setIsEnabled(!modelNodeBlock->isHidden());
            node.setIsFrontVisible(modelNodeBlock->isFrontVisible() || modelNodeBlock->isFrontAndBackVisible());
            node.setIsBackVisible(modelNodeBlock->isBackVisible() || modelNodeBlock->isFrontAndBackVisible());

            for (const PDF3D_U3D_AbstractBlock::ParentNodeData& parentNodeData : modelNodeBlock->getParentNodesData())
            {
                m_object.getOrCreateNode(parentNodeData.parentNodeName).addChild(modelNodeBlock->getModelNodeName(), parentNodeData.transformMatrix);
            }

            break;
        }

        case PDF3D_U3D_Block_Info::BT_NodeLight:
        {
            const PDF3D_U3D_LightNodeBlock* lightNodeBlock = dynamic_cast<const PDF3D_U3D_LightNodeBlock*>(block.data());

            PDF3D_U3D_Node& node = m_object.getOrCreateNode(lightNodeBlock->getLightNodeName());
            node.setNodeName(lightNodeBlock->getLightNodeName());
            node.setResourceName(lightNodeBlock->getLightResourceName());
            node.setType(PDF3D_U3D_Node::Light);

            for (const PDF3D_U3D_AbstractBlock::ParentNodeData& parentNodeData : lightNodeBlock->getParentNodesData())
            {
                m_object.getOrCreateNode(parentNodeData.parentNodeName).addChild(lightNodeBlock->getLightNodeName(), parentNodeData.transformMatrix);
            }

            break;
        }

        case PDF3D_U3D_Block_Info::BT_NodeView:
        {
            const PDF3D_U3D_ViewNodeBlock* viewNodeBlock = dynamic_cast<const PDF3D_U3D_ViewNodeBlock*>(block.data());

            PDF3D_U3D_Node& node = m_object.getOrCreateNode(viewNodeBlock->getViewNodeName());
            node.setNodeName(viewNodeBlock->getViewNodeName());
            node.setResourceName(viewNodeBlock->getViewResourceName());
            node.setType(PDF3D_U3D_Node::View);

            for (const PDF3D_U3D_AbstractBlock::ParentNodeData& parentNodeData : viewNodeBlock->getParentNodesData())
            {
                m_object.getOrCreateNode(parentNodeData.parentNodeName).addChild(viewNodeBlock->getViewNodeName(), parentNodeData.transformMatrix);
            }

            break;
        }

        case PDF3D_U3D_Block_Info::BT_ResourceMotion:
        {
            m_errors << PDFTranslationContext::tr("Motion block (%1) is not supported.").arg(block->getBlockType(), 8, 16);
            break;
        }

        case PDF3D_U3D_Block_Info::BT_Modifier2DGlyph:
        case PDF3D_U3D_Block_Info::BT_ModifierSubdivision:
        case PDF3D_U3D_Block_Info::BT_ModifierAnimation:
        case PDF3D_U3D_Block_Info::BT_ModifierBoneWeights:
        case PDF3D_U3D_Block_Info::BT_ModifierCLOD:
            m_errors << PDFTranslationContext::tr("Block type (%1) not supported in scene decoder.").arg(block->getBlockType(), 8, 16);
            break;

        case PDF3D_U3D_Block_Info::BT_ModifierShading:
        {
            const PDF3D_U3D_ShadingModifierBlock* modelNodeBlock = dynamic_cast<const PDF3D_U3D_ShadingModifierBlock*>(block.data());

            const PDF3D_U3D_ShadingModifierBlock::ShaderLists& shaderLists = modelNodeBlock->getShaderLists();
            const auto& node = m_object.getNode(modelNodeBlock->getModifierName());
            m_object.setShadersToGeometry(node.getResourceName(), shaderLists);
            break;
        }

        default:
        {
            m_errors << PDFTranslationContext::tr("Block type (%1) not supported in scene decoder.").arg(block->getBlockType(), 8, 16);
            break;
        }
    }
}

PDF3D_U3D_Block_Info::EPalette PDF3D_U3D_Parser::getPaletteByBlockType(uint32_t blockType) const
{
    if (blockType == m_RHAdobeMeshResourceId)
    {
        return PDF3D_U3D_Block_Info::PL_Generator;
    }

    return PDF3D_U3D_Block_Info::getPalette(static_cast<PDF3D_U3D_Block_Info::EBlockType>(blockType));
}

PDF3D_U3D_Parser::PDF3D_U3D_Parser()
{
    // Jakub Melka: Utf-8 (MIB 106) is default value for U3D strings
    m_stringDecoder = QStringDecoder(QStringDecoder::Encoding::Utf8, QStringDecoder::Flag::Stateless);
}

PDF3D_U3D PDF3D_U3D_Parser::parse(QByteArray data)
{
    PDF3D_U3D_DataReader reader(data, true);
    m_priority = 0;

    while (!reader.isAtEnd())
    {
        PDF3D_U3D_Block_Data blockData = readBlockData(reader);
        processBlock(blockData, PDF3D_U3D_Block_Info::PL_LastPalette);
    }

    for (const PDF3D_U3D_DecoderPalette& palette : m_decoderPalettes)
    {
        for (const PDF3D_U3D_DecoderChain& chain : palette)
        {
            for (const PDF3D_U3D_Decoder& decoder : chain)
            {
                if (decoder.isEmpty())
                {
                    continue;
                }

                const PDF3D_U3D_Block_Data& blockData = decoder.front();
                switch (blockData.blockType)
                {
                    case PDF3D_U3D_Block_Info::BT_GeneratorCLODMeshDecl:
                        processCLODMesh(decoder);
                        break;

                    case PDF3D_U3D_Block_Info::BT_GeneratorPointSet:
                        processPointSet(decoder);
                        break;

                    case PDF3D_U3D_Block_Info::BT_GeneratorLineSet:
                        processLineSet(decoder);
                        break;

                    case PDF3D_U3D_Block_Info::BT_ResourceTexture:
                        processTexture(decoder);
                        break;

                    default:
                    {
                        if (decoder.size() > 1)
                        {
                            m_errors << PDFTranslationContext::tr("Invalid block count in decoder.");
                        }

                        auto block = parseBlock(blockData);
                        addBlockToU3D(block);
                    }
                }
            }
        }
    }

    return m_object;
}

void PDF3D_U3D_Parser::processBlock(const PDF3D_U3D_Block_Data& blockData,
                                    PDF3D_U3D_Block_Info::EPalette palette)
{
    switch (blockData.blockType)
    {
        case PDF3D_U3D_FileBlock::ID:
        {
            // Parse file block
            m_fileBlock = parseBlock(blockData);
            break;
        }

        case PDF3D_U3D_FilePriorityUpdateBlock::ID:
        {
            // Parse priority update block
            auto block = parseBlock(blockData);
            const PDF3D_U3D_FilePriorityUpdateBlock* priorityUpdateBlock = dynamic_cast<const PDF3D_U3D_FilePriorityUpdateBlock*>(block.get());
            m_priority = priorityUpdateBlock->getNewPriority();
            break;
        }

        case PDF3D_U3D_NewObjectTypeBlock::ID:
        {
            auto block = parseBlock(blockData);
            const PDF3D_U3D_NewObjectTypeBlock* newObjectTypeBlock = dynamic_cast<const PDF3D_U3D_NewObjectTypeBlock*>(block.get());

            // Right Hemisphere Adobe Mesh (RHAdobeMeshResource)
            //                                                             {00000000-0000-0000-0000-000000000000}
            if (newObjectTypeBlock->getExtensionId() == QUuid::fromString("{96a804a6-3fb9-43c5-b2df-2a31b5569340}"))
            {
                m_RHAdobeMeshResourceId = newObjectTypeBlock->getNewDeclarationBlockType();
            }
            break;
        }

        case PDF3D_U3D_FileReferenceBlock::ID:
            // Skip this block, we do not handle these type of blocks,
            // just read it... to check errors.
            parseBlock(blockData);
            break;

        case PDF3D_U3D_ModifierChainBlock::ID:
            processModifierBlock(blockData);
            break;

        default:
            processGenericBlock(blockData, palette);
            break;
    }
}

void PDF3D_U3D_Parser::processModifierBlock(const PDF3D_U3D_Block_Data& blockData)
{
    // Add decoder list for modifier chain block
    auto block = parseBlock(blockData);
    const PDF3D_U3D_ModifierChainBlock* chainBlock = dynamic_cast<const PDF3D_U3D_ModifierChainBlock*>(block.get());

    PDF3D_U3D_Block_Info::EPalette palette = PDF3D_U3D_Block_Info::PL_LastPalette;
    switch (chainBlock->getModifierChainType())
    {
        case 0:
            palette = PDF3D_U3D_Block_Info::PL_Node;
            break;

        case 1:
            palette = PDF3D_U3D_Block_Info::PL_Generator;
            break;

        case 2:
            palette = PDF3D_U3D_Block_Info::PL_Texture;
            break;

        default:
            return;
    }

    for (const auto& modifierDeclarationBlock : chainBlock->getModifierDeclarationBlocks())
    {
        processBlock(modifierDeclarationBlock, palette);
    }
}

void PDF3D_U3D_Parser::processGenericBlock(const PDF3D_U3D_Block_Data& blockData,
                                           PDF3D_U3D_Block_Info::EPalette palette)
{
    PDF3D_U3D_DataReader blockReader(blockData.blockData, isCompressed());
    QString blockName = blockReader.readString(getStringDecoder());

    uint32_t chainIndex = 0;
    PDF3D_U3D_Block_Info::EPalette effectivePalette = PDF3D_U3D_Block_Info::isChain(blockData.blockType) ? palette
                                                                                                         : getPaletteByBlockType(blockData.blockType);

    if (effectivePalette == PDF3D_U3D_Block_Info::PL_LastPalette)
    {
        m_errors << PDFTranslationContext::tr("Failed to add block '%1' - decoder chain does not exist!").arg(blockName);
        return;
    }

    PDF3D_U3D_DecoderPalette* decoderPalette = m_decoderPalettes.getDecoderPalette(effectivePalette);

    if (PDF3D_U3D_Block_Info::isContinuation(blockData.blockType))
    {
        if (blockData.blockType != PDF3D_U3D_Block_Info::BT_ResourceTextureCont)
        {
            chainIndex = blockReader.readU32();
        }

        PDF3D_U3D_DecoderChain* decoderChain = decoderPalette->getDecoderChain(blockName);
        if (!decoderChain)
        {
            m_errors << PDFTranslationContext::tr("Failed to add continuation block '%1' - decoder chain does not exist!").arg(blockName);
            return;
        }

        PDF3D_U3D_Decoder* decoder = decoderChain->getDecoder(chainIndex);
        if (!decoder)
        {
            m_errors << PDFTranslationContext::tr("Failed to add continuation block '%1' - decoder does not exist at position %2!").arg(blockName).arg(chainIndex);
            return;
        }

        decoder->addBlock(blockData);
    }
    else
    {
        PDF3D_U3D_Decoder decoder;
        decoder.addBlock(blockData);

        PDF3D_U3D_DecoderChain* decoderChain = decoderPalette->getDecoderChain(blockName);
        if (!decoderChain)
        {
            decoderChain = decoderPalette->createDecoderChain(blockName);
        }

        decoderChain->addDecoder(std::move(decoder));
    }
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_Parser::parseBlock(uint32_t blockType,
                                                        const QByteArray& data,
                                                        const QByteArray& metaData)
{
    switch (blockType)
    {
        case PDF3D_U3D_FileBlock::ID:
        {
            // Only first block is file block, otherwise
            // it can be disambiguation with other block type
            if (m_blocks.empty())
            {
                PDF3D_U3D_AbstractBlockPtr fileBlockPtr = PDF3D_U3D_FileBlock::parse(data, metaData, this);

                if (const PDF3D_U3D_FileBlock* fileBlock = dynamic_cast<const PDF3D_U3D_FileBlock*>(fileBlockPtr.get()))
                {
                    m_fileBlock = fileBlockPtr;
                    QStringDecoder::Encoding encoding = fileBlock->getCharacterEncoding() == 106 ? QStringDecoder::Utf8 : QStringDecoder::System;
                    m_stringDecoder = QStringDecoder(encoding, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::UsesIcu);
                    m_isCompressed = !fileBlock->isNoCompressionMode();
                }

                return fileBlockPtr;
            }
            break;
        }

        case PDF3D_U3D_FileReferenceBlock::ID:
            return PDF3D_U3D_FileReferenceBlock::parse(data, metaData, this);

        case PDF3D_U3D_ModifierChainBlock::ID:
            return PDF3D_U3D_ModifierChainBlock::parse(data, metaData, this);

        case PDF3D_U3D_FilePriorityUpdateBlock::ID:
            return PDF3D_U3D_FilePriorityUpdateBlock::parse(data, metaData, this);

        case PDF3D_U3D_NewObjectTypeBlock::ID:
            return PDF3D_U3D_NewObjectTypeBlock::parse(data, metaData, this);

        case PDF3D_U3D_GroupNodeBlock::ID:
            return PDF3D_U3D_GroupNodeBlock::parse(data, metaData, this);

        case PDF3D_U3D_ModelNodeBlock::ID:
            return PDF3D_U3D_ModelNodeBlock::parse(data, metaData, this);

        case PDF3D_U3D_LightNodeBlock::ID:
            return PDF3D_U3D_LightNodeBlock::parse(data, metaData, this);

        case PDF3D_U3D_ViewNodeBlock::ID:
            return PDF3D_U3D_ViewNodeBlock::parse(data, metaData, this);

        case PDF3D_U3D_CLODMeshDeclarationBlock::ID:
            return PDF3D_U3D_CLODMeshDeclarationBlock::parse(data, metaData, this);

        case PDF3D_U3D_LineSetDeclarationBlock::ID:
            return PDF3D_U3D_LineSetDeclarationBlock::parse(data, metaData, this);

        case PDF3D_U3D_2DGlyphModifierBlock::ID:
            return PDF3D_U3D_2DGlyphModifierBlock::parse(data, metaData, this);

        case PDF3D_U3D_SubdivisionModifierBlock::ID:
            return PDF3D_U3D_SubdivisionModifierBlock::parse(data, metaData, this);

        case PDF3D_U3D_AnimationModifierBlock::ID:
            return PDF3D_U3D_AnimationModifierBlock::parse(data, metaData, this);

        case PDF3D_U3D_BoneWeightModifierBlock::ID:
            return PDF3D_U3D_BoneWeightModifierBlock::parse(data, metaData, this);

        case PDF3D_U3D_ShadingModifierBlock::ID:
            return PDF3D_U3D_ShadingModifierBlock::parse(data, metaData, this);

        case PDF3D_U3D_CLODModifierBlock::ID:
            return PDF3D_U3D_CLODModifierBlock::parse(data, metaData, this);

        case PDF3D_U3D_LightResourceBlock::ID:
            return PDF3D_U3D_LightResourceBlock::parse(data, metaData, this);

        case PDF3D_U3D_ViewResourceBlock::ID:
            return PDF3D_U3D_ViewResourceBlock::parse(data, metaData, this);

        case PDF3D_U3D_LitTextureShaderResourceBlock::ID:
            return PDF3D_U3D_LitTextureShaderResourceBlock::parse(data, metaData, this);

        case PDF3D_U3D_MaterialResourceBlock::ID:
            return PDF3D_U3D_MaterialResourceBlock::parse(data, metaData, this);

        case PDF3D_U3D_TextureResourceBlock::ID:
            return PDF3D_U3D_TextureResourceBlock::parse(data, metaData, this);

        case PDF3D_U3D_MotionResourceBlock::ID:
            return PDF3D_U3D_MotionResourceBlock::parse(data, metaData, this);

        default:
            if (blockType == m_RHAdobeMeshResourceId)
            {
                return PDF3D_U3D_RHAdobeMeshResourceBlock::parse(data, metaData, this);
            }

            m_errors << PDFTranslationContext::tr("Unable to parse block of type '%1'.").arg(blockType, 8, 16);
            break;
    }

    if (blockType >= PDF3D_U3D_NewObjectBlock::ID_MIN && blockType <= PDF3D_U3D_NewObjectBlock::ID_MAX)
    {
        return PDF3D_U3D_NewObjectBlock::parse(data, metaData, this);
    }

    return PDF3D_U3D_AbstractBlockPtr();
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_Parser::parseBlock(const PDF3D_U3D_Block_Data& data)
{
    return parseBlock(data.blockType, data.blockData, data.metaData);
}

uint32_t PDF3D_U3D_Parser::getBlockPadding(uint32_t blockSize)
{
    uint32_t extraBytes = blockSize % 4;

    if (extraBytes > 0)
    {
        return 4 - extraBytes;
    }

    return 0;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_FileBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_FileBlock* block = new PDF3D_U3D_FileBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_majorVersion = reader.readI16();
    block->m_minorVersion = reader.readI16();
    block->m_profileIdentifier = reader.readU32();
    block->m_declarationSize = reader.readU32();
    block->m_fileSize = reader.readU64();
    block->m_characterEncoding = reader.readU32();

    if (block->m_profileIdentifier & 0x00000008)
    {
        block->m_unitScalingFactor = reader.readF64();
    }

    block->parseMetadata(metaData, object);

    return pointer;
}

int16_t PDF3D_U3D_FileBlock::getMajorVersion() const
{
    return m_majorVersion;
}

int16_t PDF3D_U3D_FileBlock::getMinorVersion() const
{
    return m_minorVersion;
}

uint32_t PDF3D_U3D_FileBlock::getProfileIdentifier() const
{
    return m_profileIdentifier;
}

uint32_t PDF3D_U3D_FileBlock::getDeclarationSize() const
{
    return m_declarationSize;
}

uint64_t PDF3D_U3D_FileBlock::getFileSize() const
{
    return m_fileSize;
}

uint32_t PDF3D_U3D_FileBlock::getCharacterEncoding() const
{
    return m_characterEncoding;
}

PDFReal PDF3D_U3D_FileBlock::getUnitScalingFactor() const
{
    return m_unitScalingFactor;
}

void PDF3D_U3D_AbstractBlock::parseMetadata(QByteArray metaData, PDF3D_U3D_Parser* object)
{
    if (metaData.isEmpty())
    {
        return;
    }

    PDF3D_U3D_DataReader reader(metaData, object->isCompressed());

    const uint32_t itemCount = reader.readU32();
    for (uint32_t i = 0; i < itemCount; ++i)
    {
        MetaDataItem item;
        item.attributes = reader.readU32();
        item.key = reader.readString(object->getStringDecoder());

        if (item.attributes & 0x00000001)
        {
            uint32_t binaryLength = reader.readU32();
            item.binaryData = reader.readByteArray(binaryLength);
        }
        else
        {
            item.value = reader.readString(object->getStringDecoder());
        }

        m_metaData.emplace_back(std::move(item));
    }
}

PDF3D_U3D_AbstractBlock::ParentNodesData PDF3D_U3D_AbstractBlock::parseParentNodeData(PDF3D_U3D_DataReader& reader, PDF3D_U3D_Parser* object)
{
    ParentNodesData result;

    uint32_t parentNodeCount = reader.readU32();
    result.reserve(parentNodeCount);

    for (uint32_t i = 0; i < parentNodeCount; ++i)
    {
        ParentNodeData data;
        data.parentNodeName = reader.readString(object->getStringDecoder());
        data.transformMatrix = reader.readMatrix4x4();
        result.emplace_back(std::move(data));
    }

    return result;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_FileReferenceBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_FileReferenceBlock* block = new PDF3D_U3D_FileReferenceBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_scopeName = reader.readString(object->getStringDecoder());
    block->m_fileReferenceAttributes = reader.readU32();
    if (block->isBoundingSpherePresent())
    {
        reader.readFloats32(block->m_boundingSphere);
    }
    if (block->isAxisAlignedBoundingBoxPresent())
    {
        reader.readFloats32(block->m_boundingBox);
    }
    block->m_urlCount = reader.readU32();
    block->m_urls = reader.readStringList(block->m_urlCount, object->getStringDecoder());
    block->m_filterCount = reader.readU32();

    for (uint32_t i = 0; i < block->m_filterCount; ++i)
    {
        Filter filter;
        filter.filterType = reader.readU8();

        switch (filter.filterType)
        {
            case 0x00:
                filter.objectNameFilter = reader.readString(object->getStringDecoder());
                break;

            case 0x01:
                filter.objectTypeFilter = reader.readU32();
                break;

            default:
                break;
        }

        block->m_filters.emplace_back(std::move(filter));
    }

    block->m_nameCollisionPolicy = reader.readU8();
    block->m_worldAliasName = reader.readString(object->getStringDecoder());

    block->parseMetadata(metaData, object);

    return pointer;
}

const QString& PDF3D_U3D_FileReferenceBlock::getScopeName() const
{
    return m_scopeName;
}

uint32_t PDF3D_U3D_FileReferenceBlock::getFileReferenceAttributes() const
{
    return m_fileReferenceAttributes;
}

const PDF3D_U3D_BoundingSphere& PDF3D_U3D_FileReferenceBlock::getBoundingSphere() const
{
    return m_boundingSphere;
}

const PDF3D_U3D_AxisAlignedBoundingBox& PDF3D_U3D_FileReferenceBlock::getBoundingBox() const
{
    return m_boundingBox;
}

uint32_t PDF3D_U3D_FileReferenceBlock::getUrlCount() const
{
    return m_urlCount;
}

const QStringList& PDF3D_U3D_FileReferenceBlock::getUrls() const
{
    return m_urls;
}

uint32_t PDF3D_U3D_FileReferenceBlock::getFilterCount() const
{
    return m_filterCount;
}

const std::vector<PDF3D_U3D_FileReferenceBlock::Filter>& PDF3D_U3D_FileReferenceBlock::getFilters() const
{
    return m_filters;
}

uint8_t PDF3D_U3D_FileReferenceBlock::getNameCollisionPolicy() const
{
    return m_nameCollisionPolicy;
}

const QString& PDF3D_U3D_FileReferenceBlock::getWorldAliasName() const
{
    return m_worldAliasName;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ModifierChainBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_ModifierChainBlock* block = new PDF3D_U3D_ModifierChainBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_modifierChainName = reader.readString(object->getStringDecoder());
    block->m_modifierChainType = reader.readU32();
    block->m_modifierChainAttributes = reader.readU32();
    if (block->isBoundingSpherePresent())
    {
        reader.readFloats32(block->m_boundingSphere);
    }
    if (block->isAxisAlignedBoundingBoxPresent())
    {
        reader.readFloats32(block->m_boundingBox);
    }

    reader.padTo32Bits();
    const uint32_t modifierCount = reader.readU32();
    for (uint32_t i = 0; i < modifierCount; ++i)
    {
        PDF3D_U3D_Block_Data blockData = object->readBlockData(reader);
        block->m_modifierDeclarationBlocks.emplace_back(std::move(blockData));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_ModifierChainBlock::getModifierChainName() const
{
    return m_modifierChainName;
}

uint32_t PDF3D_U3D_ModifierChainBlock::getModifierChainType() const
{
    return m_modifierChainType;
}

uint32_t PDF3D_U3D_ModifierChainBlock::getModifierChainAttributes() const
{
    return m_modifierChainAttributes;
}

const PDF3D_U3D_BoundingSphere& PDF3D_U3D_ModifierChainBlock::getBoundingSphere() const
{
    return m_boundingSphere;
}

const PDF3D_U3D_AxisAlignedBoundingBox& PDF3D_U3D_ModifierChainBlock::getBoundingBox() const
{
    return m_boundingBox;
}

uint32_t PDF3D_U3D_ModifierChainBlock::getModifierCount() const
{
    return m_modifierCount;
}

const std::vector<PDF3D_U3D_Block_Data>& PDF3D_U3D_ModifierChainBlock::getModifierDeclarationBlocks() const
{
    return m_modifierDeclarationBlocks;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_FilePriorityUpdateBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_FilePriorityUpdateBlock* block = new PDF3D_U3D_FilePriorityUpdateBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_newPriority = reader.readU32();

    block->parseMetadata(metaData, object);
    return pointer;
}

uint32_t PDF3D_U3D_FilePriorityUpdateBlock::getNewPriority() const
{
    return m_newPriority;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_NewObjectTypeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_NewObjectTypeBlock* block = new PDF3D_U3D_NewObjectTypeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_newObjectTypeName = reader.readString(object->getStringDecoder());
    block->m_modifierType = reader.readU32();
    block->m_extensionId = reader.readUuid();
    block->m_newDeclarationBlockType = reader.readU32();
    block->m_continuationBlockTypeCount = reader.readU32();

    for (uint32_t i = 0; i < block->m_continuationBlockTypeCount; ++i)
    {
        block->m_continuationBlockTypes.push_back(reader.readU32());
    }

    block->m_extensionVendorName = reader.readString(object->getStringDecoder());
    block->m_urlCount = reader.readU32();
    block->m_urls = reader.readStringList(block->m_urlCount, object->getStringDecoder());
    block->m_extensionInformationString = reader.readString(object->getStringDecoder());

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_NewObjectTypeBlock::getNewObjectTypeName() const
{
    return m_newObjectTypeName;
}

uint32_t PDF3D_U3D_NewObjectTypeBlock::getModifierType() const
{
    return m_modifierType;
}

const QUuid& PDF3D_U3D_NewObjectTypeBlock::getExtensionId() const
{
    return m_extensionId;
}

uint32_t PDF3D_U3D_NewObjectTypeBlock::getNewDeclarationBlockType() const
{
    return m_newDeclarationBlockType;
}

uint32_t PDF3D_U3D_NewObjectTypeBlock::getContinuationBlockTypeCount() const
{
    return m_continuationBlockTypeCount;
}

const std::vector<uint32_t>& PDF3D_U3D_NewObjectTypeBlock::getContinuationBlockTypes() const
{
    return m_continuationBlockTypes;
}

const QString& PDF3D_U3D_NewObjectTypeBlock::getExtensionVendorName() const
{
    return m_extensionVendorName;
}

uint32_t PDF3D_U3D_NewObjectTypeBlock::getUrlCount() const
{
    return m_urlCount;
}

const QStringList& PDF3D_U3D_NewObjectTypeBlock::getUrls() const
{
    return m_urls;
}

const QString& PDF3D_U3D_NewObjectTypeBlock::getExtensionInformationString() const
{
    return m_extensionInformationString;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_NewObjectBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_NewObjectBlock* block = new PDF3D_U3D_NewObjectBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_objectName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();
    block->m_data = reader.readRemainingData();

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_NewObjectBlock::getObjectName() const
{
    return m_objectName;
}

uint32_t PDF3D_U3D_NewObjectBlock::getChainIndex() const
{
    return m_chainIndex;
}

const QByteArray& PDF3D_U3D_NewObjectBlock::getData() const
{
    return m_data;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_GroupNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_GroupNodeBlock* block = new PDF3D_U3D_GroupNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_groupNodeName = reader.readString(object->getStringDecoder());
    block->m_parentNodesData = parseParentNodeData(reader, object);

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_GroupNodeBlock::getGroupNodeName() const
{
    return m_groupNodeName;
}

const PDF3D_U3D_GroupNodeBlock::ParentNodesData& PDF3D_U3D_GroupNodeBlock::getParentNodesData() const
{
    return m_parentNodesData;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ModelNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_ModelNodeBlock* block = new PDF3D_U3D_ModelNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_modelNodeName = reader.readString(object->getStringDecoder());
    block->m_parentNodesData = parseParentNodeData(reader, object);
    block->m_modelResourceName = reader.readString(object->getStringDecoder());
    block->m_modelVisibility = reader.readU32();

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_ModelNodeBlock::getModelNodeName() const
{
    return m_modelNodeName;
}

const PDF3D_U3D_ModelNodeBlock::ParentNodesData& PDF3D_U3D_ModelNodeBlock::getParentNodesData() const
{
    return m_parentNodesData;
}

const QString& PDF3D_U3D_ModelNodeBlock::getModelResourceName() const
{
    return m_modelResourceName;
}

uint32_t PDF3D_U3D_ModelNodeBlock::getModelVisibility() const
{
    return m_modelVisibility;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LightNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_LightNodeBlock* block = new PDF3D_U3D_LightNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_lightNodeName = reader.readString(object->getStringDecoder());
    block->m_parentNodesData = parseParentNodeData(reader, object);
    block->m_lightResourceName = reader.readString(object->getStringDecoder());

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_LightNodeBlock::getLightNodeName() const
{
    return m_lightNodeName;
}

const PDF3D_U3D_LightNodeBlock::ParentNodesData& PDF3D_U3D_LightNodeBlock::getParentNodesData() const
{
    return m_parentNodesData;
}

const QString& PDF3D_U3D_LightNodeBlock::getLightResourceName() const
{
    return m_lightResourceName;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ViewNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_ViewNodeBlock* block = new PDF3D_U3D_ViewNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_viewNodeName = reader.readString(object->getStringDecoder());
    block->m_parentNodesData = parseParentNodeData(reader, object);
    block->m_viewResourceName = reader.readString(object->getStringDecoder());
    block->m_viewNodeAttributes = reader.readU32();
    block->m_viewNearFlipping = reader.readF32();
    block->m_viewFarFlipping = reader.readF32();

    if (block->isProjectionThreePointPerspective())
    {
        block->m_viewProjection = reader.readF32();
    }
    if (block->isProjectionOrthographic())
    {
        block->m_viewOrthographicHeight = reader.readF32();
    }
    if (block->isProjectionOnePointPerspective() || block->isProjectionTwoPointPerspective())
    {
        reader.readFloats32(block->m_viewProjectionVector);
    }

    // Viewport
    block->m_viewPortWidth = reader.readF32();
    block->m_viewPortHeight = reader.readF32();
    block->m_viewPortHorizontalPosition = reader.readF32();
    block->m_viewPortVerticalPosition = reader.readF32();

    auto readBackdropOrOverlayItems = [&reader, object]()
    {
        std::vector<BackdropOrOverlay> items;

        uint32_t count = reader.readU32();
        items.reserve(count);

        for (uint32_t i = 0; i < count; ++i)
        {
            BackdropOrOverlay item;

            item.m_textureName = reader.readString(object->getStringDecoder());
            item.m_textureBlend = reader.readF32();
            item.m_rotation = reader.readF32();
            item.m_locationX = reader.readF32();
            item.m_locationY = reader.readF32();
            item.m_registrationPointX = reader.readI32();
            item.m_registrationPointY = reader.readI32();
            item.m_scaleX = reader.readF32();
            item.m_scaleY = reader.readF32();

            items.emplace_back(std::move(item));
        }

        return items;
    };

    // Backdrop
    block->m_backdrop = readBackdropOrOverlayItems();

    // Overlay
    block->m_overlay = readBackdropOrOverlayItems();

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_ViewNodeBlock::getViewNodeName() const
{
    return m_viewNodeName;
}

const PDF3D_U3D_ViewNodeBlock::ParentNodesData& PDF3D_U3D_ViewNodeBlock::getParentNodesData() const
{
    return m_parentNodesData;
}

const QString& PDF3D_U3D_ViewNodeBlock::getViewResourceName() const
{
    return m_viewResourceName;
}

uint32_t PDF3D_U3D_ViewNodeBlock::getViewNodeAttributes() const
{
    return m_viewNodeAttributes;
}

float PDF3D_U3D_ViewNodeBlock::getViewNearFlipping() const
{
    return m_viewNearFlipping;
}

float PDF3D_U3D_ViewNodeBlock::getViewFarFlipping() const
{
    return m_viewFarFlipping;
}

float PDF3D_U3D_ViewNodeBlock::getViewProjection() const
{
    return m_viewProjection;
}

float PDF3D_U3D_ViewNodeBlock::getViewOrthographicHeight() const
{
    return m_viewOrthographicHeight;
}

const PDF3D_U3D_Vec3& PDF3D_U3D_ViewNodeBlock::getViewProjectionVector() const
{
    return m_viewProjectionVector;
}

float PDF3D_U3D_ViewNodeBlock::getViewPortWidth() const
{
    return m_viewPortWidth;
}

float PDF3D_U3D_ViewNodeBlock::getViewPortHeight() const
{
    return m_viewPortHeight;
}

float PDF3D_U3D_ViewNodeBlock::getViewPortHorizontalPosition() const
{
    return m_viewPortHorizontalPosition;
}

float PDF3D_U3D_ViewNodeBlock::getViewPortVerticalPosition() const
{
    return m_viewPortVerticalPosition;
}

const std::vector<PDF3D_U3D_ViewNodeBlock::BackdropItem>& PDF3D_U3D_ViewNodeBlock::getBackdrop() const
{
    return m_backdrop;
}

const std::vector<PDF3D_U3D_ViewNodeBlock::OverlayItem>& PDF3D_U3D_ViewNodeBlock::getOverlay() const
{
    return m_overlay;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_CLODMeshDeclarationBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_CLODMeshDeclarationBlock* block = new PDF3D_U3D_CLODMeshDeclarationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_meshName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();

    /* max mesh  description */
    block->m_meshAttributes = reader.readU32();
    block->m_faceCount = reader.readU32();
    block->m_positionCount = reader.readU32();
    block->m_normalCount = reader.readU32();
    block->m_diffuseColorCount = reader.readU32();
    block->m_specularColorCount = reader.readU32();
    block->m_textureColorCount = reader.readU32();
    block->m_shadingDescription = reader.readShadingDescriptions(reader.readU32());

    /* clod description */
    block->m_minimumResolution = reader.readU32();
    block->m_finalResolution = reader.readU32();

    /* resource description */
    block->m_positionQualityFactor = reader.readU32();
    block->m_normalQualityFactor = reader.readU32();
    block->m_textureCoordQualityFactor = reader.readU32();
    block->m_positionInverseQuant = reader.readF32();
    block->m_normalInverseQuant = reader.readF32();
    block->m_textureCoordInverseQuant = reader.readF32();
    block->m_diffuseColorInverseQuant = reader.readF32();
    block->m_specularColorInverseQuant = reader.readF32();
    block->m_normalCreaseParameter = reader.readF32();
    block->m_normalUpdateParameter = reader.readF32();
    block->m_normalToleranceParameter = reader.readF32();

    /* bone description */
    block->m_boneCount = reader.readU32();
    block->m_boneDescription = reader.readBoneDescriptions(object->getStringDecoder(), block->m_boneCount);

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_CLODMeshDeclarationBlock::getMeshName() const
{
    return m_meshName;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getChainIndex() const
{
    return m_chainIndex;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getMeshAttributes() const
{
    return m_meshAttributes;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getFaceCount() const
{
    return m_faceCount;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getPositionCount() const
{
    return m_positionCount;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getNormalCount() const
{
    return m_normalCount;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getDiffuseColorCount() const
{
    return m_diffuseColorCount;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getSpecularColorCount() const
{
    return m_specularColorCount;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getTextureColorCount() const
{
    return m_textureColorCount;
}

const std::vector<PDF3D_U3D_ShadingDescription>& PDF3D_U3D_CLODMeshDeclarationBlock::getShadingDescription() const
{
    return m_shadingDescription;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getMinimumResolution() const
{
    return m_minimumResolution;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getFinalResolution() const
{
    return m_finalResolution;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getPositionQualityFactor() const
{
    return m_positionQualityFactor;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getNormalQualityFactor() const
{
    return m_normalQualityFactor;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getTextureCoordQualityFactor() const
{
    return m_textureCoordQualityFactor;
}

float PDF3D_U3D_CLODMeshDeclarationBlock::getPositionInverseQuant() const
{
    return m_positionInverseQuant;
}

float PDF3D_U3D_CLODMeshDeclarationBlock::getNormalInverseQuant() const
{
    return m_normalInverseQuant;
}

float PDF3D_U3D_CLODMeshDeclarationBlock::getTextureCoordInverseQuant() const
{
    return m_textureCoordInverseQuant;
}

float PDF3D_U3D_CLODMeshDeclarationBlock::getDiffuseColorInverseQuant() const
{
    return m_diffuseColorInverseQuant;
}

float PDF3D_U3D_CLODMeshDeclarationBlock::getSpecularColorInverseQuant() const
{
    return m_specularColorInverseQuant;
}

float PDF3D_U3D_CLODMeshDeclarationBlock::getNormalCreaseParameter() const
{
    return m_normalCreaseParameter;
}

float PDF3D_U3D_CLODMeshDeclarationBlock::getNormalUpdateParameter() const
{
    return m_normalUpdateParameter;
}

float PDF3D_U3D_CLODMeshDeclarationBlock::getNormalToleranceParameter() const
{
    return m_normalToleranceParameter;
}

uint32_t PDF3D_U3D_CLODMeshDeclarationBlock::getBoneCount() const
{
    return m_boneCount;
}

const std::vector<PDF3D_U3D_BoneDescription>& PDF3D_U3D_CLODMeshDeclarationBlock::getBoneDescription() const
{
    return m_boneDescription;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_CLODBaseMeshContinuationBlock::parse(QByteArray data,
                                                                          QByteArray metaData,
                                                                          PDF3D_U3D_Parser* object,
                                                                          const PDF3D_U3D_CLODMeshDeclarationBlock* declarationBlock)
{
    PDF3D_U3D_CLODBaseMeshContinuationBlock* block = new PDF3D_U3D_CLODBaseMeshContinuationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_meshName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();

    /* max mesh description */
    block->m_faceCount = reader.readU32();
    block->m_positionCount = reader.readU32();
    block->m_normalCount = reader.readU32();
    block->m_diffuseColorCount = reader.readU32();
    block->m_specularColorCount = reader.readU32();
    block->m_textureColorCount = reader.readU32();

    block->m_basePositions = reader.readVectorFloats32<PDF3D_U3D_Vec3>(block->m_positionCount);
    block->m_baseNormals = reader.readVectorFloats32<PDF3D_U3D_Vec3>(block->m_normalCount);
    block->m_baseDiffuseColors = reader.readVectorFloats32<PDF3D_U3D_Vec4>(block->m_diffuseColorCount);
    block->m_baseSpecularColors = reader.readVectorFloats32<PDF3D_U3D_Vec4>(block->m_specularColorCount);
    block->m_baseTextureCoords = reader.readVectorFloats32<PDF3D_U3D_Vec4>(block->m_textureColorCount);

    // We must read attributes of the mesh to read faces
    const bool hasNormals = !declarationBlock->isNormalsExcluded();

    for (size_t i = 0; i < block->m_faceCount; ++i)
    {
        BaseFace face;
        face.m_shadingId = reader.readCompressedU32(reader.getStaticContext(cShading));

        const PDF3D_U3D_ShadingDescription* shadingDescription = declarationBlock->getShadingDescriptionItem(face.m_shadingId);
        if (!shadingDescription)
        {
            return nullptr;
        }

        for (size_t ci = 0; ci < face.m_corners.size(); ++ci)
        {
            BaseCornerInfo cornerInfo;

            cornerInfo.basePositionIndex = reader.readCompressedU32(reader.getRangeContext(block->m_positionCount));

            if (hasNormals)
            {
                cornerInfo.baseNormalIndex = reader.readCompressedU32(reader.getRangeContext(block->m_normalCount));
            }

            if (shadingDescription->hasDiffuseColors())
            {
                cornerInfo.baseDiffuseColorIndex = reader.readCompressedU32(reader.getRangeContext(block->m_diffuseColorCount));
            }

            if (shadingDescription->hasSpecularColors())
            {
                cornerInfo.baseSpecularColorIndex = reader.readCompressedU32(reader.getRangeContext(block->m_specularColorCount));
            }

            for (uint32_t ti = 0; ti < shadingDescription->textureLayerCount; ++ti)
            {
                cornerInfo.baseTextureCoordIndex.push_back(reader.readCompressedU32(reader.getRangeContext(block->m_textureColorCount)));
            }

            face.m_corners[ci] = std::move(cornerInfo);
        }

        block->m_baseFaces.emplace_back(std::move(face));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

uint32_t PDF3D_U3D_CLODBaseMeshContinuationBlock::getFaceCount() const
{
    return m_faceCount;
}

uint32_t PDF3D_U3D_CLODBaseMeshContinuationBlock::getPositionCount() const
{
    return m_positionCount;
}

uint32_t PDF3D_U3D_CLODBaseMeshContinuationBlock::getNormalCount() const
{
    return m_normalCount;
}

uint32_t PDF3D_U3D_CLODBaseMeshContinuationBlock::getDiffuseColorCount() const
{
    return m_diffuseColorCount;
}

uint32_t PDF3D_U3D_CLODBaseMeshContinuationBlock::getSpecularColorCount() const
{
    return m_specularColorCount;
}

uint32_t PDF3D_U3D_CLODBaseMeshContinuationBlock::getTextureColorCount() const
{
    return m_textureColorCount;
}

const std::vector<PDF3D_U3D_Vec3>& PDF3D_U3D_CLODBaseMeshContinuationBlock::getBasePositions() const
{
    return m_basePositions;
}

const std::vector<PDF3D_U3D_Vec3>& PDF3D_U3D_CLODBaseMeshContinuationBlock::getBaseNormals() const
{
    return m_baseNormals;
}

const std::vector<PDF3D_U3D_Vec4>& PDF3D_U3D_CLODBaseMeshContinuationBlock::getBaseDiffuseColors() const
{
    return m_baseDiffuseColors;
}

const std::vector<PDF3D_U3D_Vec4>& PDF3D_U3D_CLODBaseMeshContinuationBlock::getBaseSpecularColors() const
{
    return m_baseSpecularColors;
}

const std::vector<PDF3D_U3D_Vec4>& PDF3D_U3D_CLODBaseMeshContinuationBlock::getBaseTextureCoords() const
{
    return m_baseTextureCoords;
}

const std::vector<PDF3D_U3D_CLODBaseMeshContinuationBlock::BaseFace>& PDF3D_U3D_CLODBaseMeshContinuationBlock::getBaseFaces() const
{
    return m_baseFaces;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_CLODProgressiveMeshContinuationBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_CLODProgressiveMeshContinuationBlock* block = new PDF3D_U3D_CLODProgressiveMeshContinuationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_meshName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();

    block->m_startResolution = reader.readU32();
    block->m_endResolution = reader.readU32();

    if (block->m_endResolution < block->m_startResolution)
    {
        // Error - bad resolution
        return nullptr;
    }

    block->m_resolutionUpdateCount = block->m_endResolution - block->m_startResolution;

    // We do not implement this block
#if 0
    for (uint32_t i = block->m_startResolution; i < block->m_endResolution; ++i)
    {
        ResolutionUpdate updateItem;

        if (i == 0)
        {
            updateItem.splitPositionIndex = reader.readCompressedU32(reader.getStaticContext(cZero));
        }
        else
        {
            updateItem.splitPositionIndex = reader.readCompressedU32(reader.getRangeContext(i));
        }

        // Read diffuse color info
        uint16_t newDiffuseColors = reader.readCompressedU16(reader.getStaticContext(cDiffuseCount));
        for (uint16_t i = 0; i < newDiffuseColors; ++i)
        {
            auto colorDiffInfo = reader.readQuantizedVec4(reader.getStaticContext(cDiffuseColorSign),
                                                          reader.getStaticContext(cColorDiffR),
                                                          reader.getStaticContext(cColorDiffG),
                                                          reader.getStaticContext(cColorDiffB),
                                                          reader.getStaticContext(cColorDiffA));
            updateItem.newDiffuseColors.emplace_back(colorDiffInfo);
        }

        // Read specular color info
        uint16_t newSpecularColors = reader.readCompressedU16(reader.getStaticContext(cSpecularCount));
        for (uint16_t i = 0; i < newSpecularColors; ++i)
        {
            auto colorSpecularInfo = reader.readQuantizedVec4(reader.getStaticContext(cSpecularColorSign),
                                                              reader.getStaticContext(cColorDiffR),
                                                              reader.getStaticContext(cColorDiffG),
                                                              reader.getStaticContext(cColorDiffB),
                                                              reader.getStaticContext(cColorDiffA));
            updateItem.newSpecularColors.emplace_back(colorSpecularInfo);
        }

        // Read texture coordinates
        uint16_t newTextureCoords = reader.readCompressedU16(reader.getStaticContext(cTexCoordCount));
        for (uint16_t i = 0; i < newTextureCoords; ++i)
        {
            auto textureCoordsInfo = reader.readQuantizedVec4(reader.getStaticContext(cTexCoordSign),
                                                              reader.getStaticContext(cTexCDiffU),
                                                              reader.getStaticContext(cTexCDiffV),
                                                              reader.getStaticContext(cTexCDiffS),
                                                              reader.getStaticContext(cTexCDiffT));
            updateItem.newTextureCoords.emplace_back(textureCoordsInfo);
        }

        const uint32_t newFaceCount = reader.readCompressedU32(reader.getStaticContext(cFaceCnt));
        for (uint32_t i = 0; i < newFaceCount; ++i)
        {
            NewFacePositionInfo facePositionInfo;

            facePositionInfo.shadingId = reader.readCompressedU32(reader.getStaticContext(cShading));
            facePositionInfo.faceOrientation = reader.readCompressedU8(reader.getStaticContext(cFaceOrnt));
            facePositionInfo.thirdPositionType = reader.readCompressedU8(reader.getStaticContext(cThrdPosType));

            if (facePositionInfo.thirdPositionType & 0x01)
            {
                facePositionInfo.localThirdPositionIndex = reader.readCompressedU32(reader.getStaticContext(cLocal3rdPos));
            }
            else
            {
                facePositionInfo.globalThirdPositionIndex = reader.readCompressedU32(reader.getRangeContext(i));
            }

            updateItem.newFaces.emplace_back(std::move(facePositionInfo));
        }

        block->m_resolutionUpdates.emplace_back(std::move(updateItem));
    }

#endif

    block->parseMetadata(metaData, object);
    return pointer;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LineSetDeclarationBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_LineSetDeclarationBlock* block = new PDF3D_U3D_LineSetDeclarationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_lineSetName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();

    // line set description
    block->m_lineSetReserved = reader.readU32();
    block->m_lineCount = reader.readU32();
    block->m_positionCount = reader.readU32();
    block->m_normalCount = reader.readU32();
    block->m_diffuseColorCount = reader.readU32();
    block->m_specularColorCount = reader.readU32();
    block->m_textureCoordCount = reader.readU32();
    block->m_shadingDescriptions = reader.readShadingDescriptions(reader.readU32());

    // resource description
    block->m_positionQualityFactor = reader.readU32();
    block->m_normalQualityFactor = reader.readU32();
    block->m_textureCoordQualityFactor = reader.readU32();
    block->m_positionInverseQuant = reader.readF32();
    block->m_normalInverseQuant = reader.readF32();
    block->m_textureCoordInverseQuant = reader.readF32();
    block->m_diffuseColorInverseQuant = reader.readF32();
    block->m_specularColorInverseQuant = reader.readF32();
    block->m_reserved1 = reader.readF32();
    block->m_reserved2 = reader.readF32();
    block->m_reserved3 = reader.readF32();

    block->parseMetadata(metaData, object);
    return pointer;
}

uint32_t PDF3D_U3D_LineSetDeclarationBlock::getLineCount() const
{
    return m_lineCount;
}

uint32_t PDF3D_U3D_LineSetDeclarationBlock::getPositionCount() const
{
    return m_positionCount;
}

uint32_t PDF3D_U3D_LineSetDeclarationBlock::getNormalCount() const
{
    return m_normalCount;
}

uint32_t PDF3D_U3D_LineSetDeclarationBlock::getDiffuseColorCount() const
{
    return m_diffuseColorCount;
}

uint32_t PDF3D_U3D_LineSetDeclarationBlock::getSpecularColorCount() const
{
    return m_specularColorCount;
}

uint32_t PDF3D_U3D_LineSetDeclarationBlock::getTextureCoordCount() const
{
    return m_textureCoordCount;
}

const std::vector<PDF3D_U3D_ShadingDescription>& PDF3D_U3D_LineSetDeclarationBlock::getShadingDescriptions() const
{
    return m_shadingDescriptions;
}

uint32_t PDF3D_U3D_LineSetDeclarationBlock::getPositionQualityFactor() const
{
    return m_positionQualityFactor;
}

uint32_t PDF3D_U3D_LineSetDeclarationBlock::getNormalQualityFactor() const
{
    return m_normalQualityFactor;
}

uint32_t PDF3D_U3D_LineSetDeclarationBlock::getTextureCoordQualityFactor() const
{
    return m_textureCoordQualityFactor;
}

float PDF3D_U3D_LineSetDeclarationBlock::getPositionInverseQuant() const
{
    return m_positionInverseQuant;
}

float PDF3D_U3D_LineSetDeclarationBlock::getNormalInverseQuant() const
{
    return m_normalInverseQuant;
}

float PDF3D_U3D_LineSetDeclarationBlock::getTextureCoordInverseQuant() const
{
    return m_textureCoordInverseQuant;
}

float PDF3D_U3D_LineSetDeclarationBlock::getDiffuseColorInverseQuant() const
{
    return m_diffuseColorInverseQuant;
}

float PDF3D_U3D_LineSetDeclarationBlock::getSpecularColorInverseQuant() const
{
    return m_specularColorInverseQuant;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LineSetContinuationBlock::parse(QByteArray data,
                                                                     QByteArray metaData,
                                                                     PDF3D_U3D_Parser* object,
                                                                     const PDF3D_U3D_LineSetDeclarationBlock* declarationBlock)
{
    PDF3D_U3D_LineSetContinuationBlock* block = new PDF3D_U3D_LineSetContinuationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_lineSetName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();

    block->m_startResolution = reader.readU32();
    block->m_endResolution = reader.readU32();

    for (uint32_t i = block->m_startResolution; i < block->m_endResolution; ++i)
    {
        UpdateItem item;

        if (i > 0)
        {
            item.splitPositionIndex = reader.readCompressedU32(reader.getRangeContext(i));
        }
        else
        {
            item.splitPositionIndex = reader.readCompressedU32(reader.getRangeContext(i + 1));
        }

        item.newPositionInfo = reader.readQuantizedVec3(reader.getStaticContext(cPosDiffSign),
                                                        reader.getStaticContext(cPosDiffX),
                                                        reader.getStaticContext(cPosDiffY),
                                                        reader.getStaticContext(cPosDiffZ));

        const uint32_t newNormalCount = reader.readCompressedU32(reader.getStaticContext(cNormlCnt));
        for (uint32_t ni = 0; ni < newNormalCount; ++ni)
        {
            item.newNormals.push_back(reader.readQuantizedVec3(reader.getStaticContext(cDiffNormalSign),
                                                               reader.getStaticContext(cDiffNormalX),
                                                               reader.getStaticContext(cDiffNormalY),
                                                               reader.getStaticContext(cDiffNormalZ)));
        }

        const uint32_t newLineCount = reader.readCompressedU32(reader.getStaticContext(cLineCnt));
        for (uint32_t li = 0; li < newLineCount; ++li)
        {
            NewLineInfo lineInfo;

            lineInfo.shadingId = reader.readCompressedU32(reader.getStaticContext(cShading));
            lineInfo.firstPositionIndex = reader.readCompressedU32(reader.getRangeContext(i));

            const PDF3D_U3D_ShadingDescription* shadingDescription = declarationBlock->getShadingDescriptionItem(lineInfo.shadingId);
            if (!shadingDescription)
            {
                return nullptr;
            }

            for (NewLineEndingInfo* endingInfo : { &lineInfo.p1, &lineInfo.p2 })
            {
                endingInfo->normalLocalIndex = reader.readCompressedU32(reader.getStaticContext(cNormlIdx));

                if (shadingDescription->hasDiffuseColors())
                {
                    endingInfo->duplicateDiffuse = reader.readCompressedU8(reader.getStaticContext(cDiffDup)) & 0x02;

                    if (!endingInfo->duplicateDiffuse)
                    {
                        endingInfo->diffuseColor = reader.readQuantizedVec4(reader.getStaticContext(cDiffuseColorSign),
                                                                            reader.getStaticContext(cColorDiffR),
                                                                            reader.getStaticContext(cColorDiffG),
                                                                            reader.getStaticContext(cColorDiffB),
                                                                            reader.getStaticContext(cColorDiffA));
                    }
                }

                if (shadingDescription->hasSpecularColors())
                {
                    endingInfo->duplicateSpecular = reader.readCompressedU8(reader.getStaticContext(cSpecDup)) & 0x02;

                    if (!endingInfo->duplicateSpecular)
                    {
                        endingInfo->specularColor = reader.readQuantizedVec4(reader.getStaticContext(cSpecularColorSign),
                                                                             reader.getStaticContext(cColorDiffR),
                                                                             reader.getStaticContext(cColorDiffG),
                                                                             reader.getStaticContext(cColorDiffB),
                                                                             reader.getStaticContext(cColorDiffA));
                    }
                }

                for (uint32_t ti = 0; ti < shadingDescription->textureLayerCount; ++ti)
                {
                    PDF3D_U3D_QuantizedVec4 textCoord = { };
                    const bool duplicateTextCoord = reader.readCompressedU8(reader.getStaticContext(cTexCDup)) & 0x02;

                    if (!duplicateTextCoord)
                    {
                        textCoord = reader.readQuantizedVec4(reader.getStaticContext(cTexCoordSign),
                                                             reader.getStaticContext(cTexCDiffU),
                                                             reader.getStaticContext(cTexCDiffV),
                                                             reader.getStaticContext(cTexCDiffS),
                                                             reader.getStaticContext(cTexCDiffT));
                    }

                    endingInfo->textureCoords.emplace_back(duplicateTextCoord, textCoord);
                }
            }

            item.newLines.emplace_back(std::move(lineInfo));
        }

        block->m_updateItems.emplace_back(std::move(item));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

const std::vector<PDF3D_U3D_LineSetContinuationBlock::UpdateItem>& PDF3D_U3D_LineSetContinuationBlock::getUpdateItems() const
{
    return m_updateItems;
}


PDF3D_U3D_DecoderPalette* PDF3D_U3D_DecoderPalettes::getDecoderPalette(EPalette palette)
{
    Q_ASSERT(palette < m_decoderLists.size());
    return &m_decoderLists[palette];
}

constexpr bool PDF3D_U3D_Block_Info::isChain(EBlockType blockType)
{
    switch (blockType)
    {
        case BT_Modifier2DGlyph:
        case BT_ModifierSubdivision:
        case BT_ModifierAnimation:
        case BT_ModifierBoneWeights:
        case BT_ModifierShading:
        case BT_ModifierCLOD:
            return true;

        default:
            return false;
    }
}

constexpr bool PDF3D_U3D_Block_Info::isContinuation(EBlockType blockType)
{
    switch (blockType)
    {
        case BT_GeneratorCLODBaseMesh:
        case BT_GeneratorCLODProgMesh:
        case BT_GeneratorPointSetCont:
        case BT_GeneratorLineSetCont:
        case BT_ResourceTextureCont:
            return true;

        default:
            break;
    }

    return false;
}

PDF3D_U3D_Block_Info::EPalette PDF3D_U3D_Block_Info::getPalette(EBlockType blockType)
{
    switch (blockType)
    {
        case BT_Unknown:
            Q_ASSERT(false);
            break;

        case BT_FileHeader:
        case BT_FileReference:
        case BT_FileModifierChain:
        case BT_FilePriorityUpdate:
        case BT_FileNewObject:
            Q_ASSERT(false); // Do not have a palette
            break;

        case BT_NodeGroup:
        case BT_NodeModel:
        case BT_NodeLight:
        case BT_NodeView:
            return PL_Node;

        case BT_GeneratorCLODMeshDecl:
        case BT_GeneratorCLODBaseMesh:
        case BT_GeneratorCLODProgMesh:
        case BT_GeneratorPointSet:
        case BT_GeneratorPointSetCont:
        case BT_GeneratorLineSet:
        case BT_GeneratorLineSetCont:
            return PL_Generator;

        case BT_Modifier2DGlyph:
        case BT_ModifierSubdivision:
        case BT_ModifierAnimation:
        case BT_ModifierBoneWeights:
        case BT_ModifierShading:
        case BT_ModifierCLOD:
            return PL_Node;

        case BT_ResourceLight:
            return PL_Light;

        case BT_ResourceView:
            return PL_View;

        case BT_ResourceLitShader:
            return PL_Shader;

        case BT_ResourceMaterial:
            return PL_Material;

        case BT_ResourceTexture:
        case BT_ResourceTextureCont:
            return PL_Texture;

        case BT_ResourceMotion:
            return PL_Motion;
    }

    return PL_LastPalette;
}

const QString& PDF3D_U3D_DecoderChain::getName() const
{
    return m_name;
}

void PDF3D_U3D_DecoderChain::setName(const QString& newName)
{
    m_name = newName;
}

PDF3D_U3D_Decoder* PDF3D_U3D_DecoderChain::getDecoder(uint32_t chainPosition)
{
    return (chainPosition < m_decoders.size()) ? &m_decoders[chainPosition] : nullptr;
}

PDF3D_U3D_DecoderChain* PDF3D_U3D_DecoderPalette::getDecoderChain(const QString& name)
{
    for (auto& decoderChain : m_decoderChains)
    {
        if (decoderChain.getName() == name)
        {
            return &decoderChain;
        }
    }

    return nullptr;
}

PDF3D_U3D_DecoderChain* PDF3D_U3D_DecoderPalette::createDecoderChain(const QString& name)
{
    Q_ASSERT(!getDecoderChain(name));

    m_decoderChains.emplace_back();
    m_decoderChains.back().setName(name);
    return &m_decoderChains.back();
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_PointSetDeclarationBlock::parse(QByteArray data,
                                                                     QByteArray metaData,
                                                                     PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_PointSetDeclarationBlock* block = new PDF3D_U3D_PointSetDeclarationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_pointSetName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();

    // point set description
    block->m_pointSetReserved = reader.readU32();
    block->m_pointCount = reader.readU32();
    block->m_positionCount = reader.readU32();
    block->m_normalCount = reader.readU32();
    block->m_diffuseColorCount = reader.readU32();
    block->m_specularColorCount = reader.readU32();
    block->m_textureCoordCount = reader.readU32();
    block->m_shadingDescriptions = reader.readShadingDescriptions(reader.readU32());

    // resource description
    block->m_positionQualityFactor = reader.readU32();
    block->m_normalQualityFactor = reader.readU32();
    block->m_textureCoordQualityFactor = reader.readU32();
    block->m_positionInverseQuant = reader.readF32();
    block->m_normalInverseQuant = reader.readF32();
    block->m_textureCoordInverseQuant = reader.readF32();
    block->m_diffuseColorInverseQuant = reader.readF32();
    block->m_specularColorInverseQuant = reader.readF32();
    block->m_reserved1 = reader.readF32();
    block->m_reserved2 = reader.readF32();
    block->m_reserved3 = reader.readF32();

    /* bone description */
    block->m_boneCount = reader.readU32();
    block->m_boneDescription = reader.readBoneDescriptions(object->getStringDecoder(), block->m_boneCount);

    block->parseMetadata(metaData, object);
    return pointer;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getPointCount() const
{
    return m_pointCount;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getPositionCount() const
{
    return m_positionCount;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getNormalCount() const
{
    return m_normalCount;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getDiffuseColorCount() const
{
    return m_diffuseColorCount;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getSpecularColorCount() const
{
    return m_specularColorCount;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getTextureCoordCount() const
{
    return m_textureCoordCount;
}

const std::vector<PDF3D_U3D_ShadingDescription>& PDF3D_U3D_PointSetDeclarationBlock::getShadingDescriptions() const
{
    return m_shadingDescriptions;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getPositionQualityFactor() const
{
    return m_positionQualityFactor;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getNormalQualityFactor() const
{
    return m_normalQualityFactor;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getTextureCoordQualityFactor() const
{
    return m_textureCoordQualityFactor;
}

float PDF3D_U3D_PointSetDeclarationBlock::getPositionInverseQuant() const
{
    return m_positionInverseQuant;
}

float PDF3D_U3D_PointSetDeclarationBlock::getNormalInverseQuant() const
{
    return m_normalInverseQuant;
}

float PDF3D_U3D_PointSetDeclarationBlock::getTextureCoordInverseQuant() const
{
    return m_textureCoordInverseQuant;
}

float PDF3D_U3D_PointSetDeclarationBlock::getDiffuseColorInverseQuant() const
{
    return m_diffuseColorInverseQuant;
}

float PDF3D_U3D_PointSetDeclarationBlock::getSpecularColorInverseQuant() const
{
    return m_specularColorInverseQuant;
}

uint32_t PDF3D_U3D_PointSetDeclarationBlock::getBoneCount() const
{
    return m_boneCount;
}

const std::vector<PDF3D_U3D_BoneDescription>& PDF3D_U3D_PointSetDeclarationBlock::getBoneDescription() const
{
    return m_boneDescription;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_PointSetContinuationBlock::parse(QByteArray data,
                                                                      QByteArray metaData,
                                                                      PDF3D_U3D_Parser* object,
                                                                      const PDF3D_U3D_PointSetDeclarationBlock* declarationBlock)
{
    PDF3D_U3D_PointSetContinuationBlock* block = new PDF3D_U3D_PointSetContinuationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_pointSetName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();

    block->m_startResolution = reader.readU32();
    block->m_endResolution = reader.readU32();

    for (uint32_t i = block->m_startResolution; i < block->m_endResolution; ++i)
    {
        UpdateItem item;

        if (i > 0)
        {
            item.splitPositionIndex = reader.readCompressedU32(reader.getRangeContext(i));
        }
        else
        {
            item.splitPositionIndex = reader.readCompressedU32(reader.getRangeContext(i + 1));
        }

        item.newPositionInfo = reader.readQuantizedVec3(reader.getStaticContext(cPosDiffSign),
                                                        reader.getStaticContext(cPosDiffX),
                                                        reader.getStaticContext(cPosDiffY),
                                                        reader.getStaticContext(cPosDiffZ));

        const uint32_t newNormalCount = reader.readCompressedU32(reader.getStaticContext(cNormlCnt));
        for (uint32_t ni = 0; ni < newNormalCount; ++ni)
        {
            item.newNormals.push_back(reader.readQuantizedVec3(reader.getStaticContext(cDiffNormalSign),
                                                               reader.getStaticContext(cDiffNormalX),
                                                               reader.getStaticContext(cDiffNormalY),
                                                               reader.getStaticContext(cDiffNormalZ)));
        }

        const uint32_t newPointCount = reader.readCompressedU32(reader.getStaticContext(cPointCnt));
        for (uint32_t li = 0; li < newPointCount; ++li)
        {
            NewPointInfo pointInfo;
            pointInfo.shadingId = reader.readCompressedU32(reader.getStaticContext(cShading));

            const PDF3D_U3D_ShadingDescription* shadingDescription = declarationBlock->getShadingDescriptionItem(pointInfo.shadingId);
            if (!shadingDescription)
            {
                return nullptr;
            }

            pointInfo.normalLocalIndex = reader.readCompressedU32(reader.getStaticContext(cNormlIdx));

            if (shadingDescription->hasDiffuseColors())
            {
                pointInfo.duplicateDiffuse = reader.readCompressedU8(reader.getStaticContext(cDiffDup)) & 0x02;

                if (!pointInfo.duplicateDiffuse)
                {
                    pointInfo.diffuseColor = reader.readQuantizedVec4(reader.getStaticContext(cDiffuseColorSign),
                                                                      reader.getStaticContext(cColorDiffR),
                                                                      reader.getStaticContext(cColorDiffG),
                                                                      reader.getStaticContext(cColorDiffB),
                                                                      reader.getStaticContext(cColorDiffA));
                }
            }

            if (shadingDescription->hasSpecularColors())
            {
                pointInfo.duplicateSpecular = reader.readCompressedU8(reader.getStaticContext(cSpecDup)) & 0x02;

                if (!pointInfo.duplicateSpecular)
                {
                    pointInfo.specularColor = reader.readQuantizedVec4(reader.getStaticContext(cSpecularColorSign),
                                                                       reader.getStaticContext(cColorDiffR),
                                                                       reader.getStaticContext(cColorDiffG),
                                                                       reader.getStaticContext(cColorDiffB),
                                                                       reader.getStaticContext(cColorDiffA));
                }
            }

            for (uint32_t ti = 0; ti < shadingDescription->textureLayerCount; ++ti)
            {
                PDF3D_U3D_QuantizedVec4 textCoord = { };
                const bool duplicateTextCoord = reader.readCompressedU8(reader.getStaticContext(cTexCDup)) & 0x02;

                if (!duplicateTextCoord)
                {
                    textCoord = reader.readQuantizedVec4(reader.getStaticContext(cTexCoordSign),
                                                         reader.getStaticContext(cTexCDiffU),
                                                         reader.getStaticContext(cTexCDiffV),
                                                         reader.getStaticContext(cTexCDiffS),
                                                         reader.getStaticContext(cTexCDiffT));
                }

                pointInfo.textureCoords.emplace_back(duplicateTextCoord, textCoord);
            }

            item.newPoints.emplace_back(std::move(pointInfo));
        }

        block->m_updateItems.emplace_back(std::move(item));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_2DGlyphModifierBlock::parse(QByteArray data,
                                                                 QByteArray metaData,
                                                                 PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_2DGlyphModifierBlock* block = new PDF3D_U3D_2DGlyphModifierBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_modifierName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();
    block->m_glyphAttributes = reader.readU32();

    uint32_t count = reader.readU32();
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t command = reader.readU32();
        switch (command)
        {
            case 0: // STARTGLYPHSTRING
            {
                break;
            }

            case 1: // STARTGLYPH
            {
                break;
            }

            case 2: // STARTPATH
            {
                break;
            }

            case 3: // MOVETO
            {
                float moveToX = reader.readF32();
                float moveToY = reader.readF32();
                block->m_path.moveTo(moveToX, moveToY);
                break;
            }

            case 4: // LINETO
            {
                float lineToX = reader.readF32();
                float lineToY = reader.readF32();
                block->m_path.lineTo(lineToX, lineToY);
                break;
            }

            case 5: // CURVETO
            {
                float control1X = reader.readF32();
                float control1Y = reader.readF32();
                float control2X = reader.readF32();
                float control2Y = reader.readF32();
                float endPointX = reader.readF32();
                float endPointY = reader.readF32();
                block->m_path.cubicTo(QPointF(control1X, control1Y),
                                      QPointF(control2X, control2Y),
                                      QPointF(endPointX, endPointY));
                break;
            }

            case 6: // ENDPATH
            {
                block->m_path.closeSubpath();
                break;
            }

            case 7: // ENDGLYPH
            {
                block->m_path.closeSubpath();
                float offsetX = reader.readF32();
                float offsetY = reader.readF32();
                block->m_path.moveTo(offsetX, offsetY);
                break;
            }

            case 8: // ENDGLYPHSTRING
                break;

            default:
                   return nullptr;
        }
    }


    std::array<float, 16> transformMatrix = { };
    reader.readFloats32(transformMatrix);

    QMatrix4x4 matrix(transformMatrix.data());
    block->m_matrix = matrix.transposed();

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_2DGlyphModifierBlock::getModifierName() const
{
    return m_modifierName;
}

uint32_t PDF3D_U3D_2DGlyphModifierBlock::getChainIndex() const
{
    return m_chainIndex;
}

uint32_t PDF3D_U3D_2DGlyphModifierBlock::getGlyphAttributes() const
{
    return m_glyphAttributes;
}

QPainterPath PDF3D_U3D_2DGlyphModifierBlock::getPath() const
{
    return m_path;
}

const QMatrix4x4& PDF3D_U3D_2DGlyphModifierBlock::getMatrix() const
{
    return m_matrix;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_SubdivisionModifierBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_SubdivisionModifierBlock* block = new PDF3D_U3D_SubdivisionModifierBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_modifierName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();
    block->m_attributes = reader.readU32();
    block->m_depth = reader.readU32();
    block->m_tension = reader.readF32();
    block->m_error = reader.readF32();

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_SubdivisionModifierBlock::getModifierName() const
{
    return m_modifierName;
}

uint32_t PDF3D_U3D_SubdivisionModifierBlock::getChainIndex() const
{
    return m_chainIndex;
}

uint32_t PDF3D_U3D_SubdivisionModifierBlock::getAttributes() const
{
    return m_attributes;
}

uint32_t PDF3D_U3D_SubdivisionModifierBlock::getDepth() const
{
    return m_depth;
}

float PDF3D_U3D_SubdivisionModifierBlock::getTension() const
{
    return m_tension;
}

float PDF3D_U3D_SubdivisionModifierBlock::getError() const
{
    return m_error;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_AnimationModifierBlock::parse(QByteArray data,
                                                                   QByteArray metaData,
                                                                   PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_AnimationModifierBlock* block = new PDF3D_U3D_AnimationModifierBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_modifierName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();
    block->m_attributes = reader.readU32();
    block->m_timescale = reader.readF32();

    uint32_t motionCount = reader.readU32();
    for (uint32_t i = 0; i < motionCount; ++i)
    {
        MotionInformation item;
        item.motionName = reader.readString(object->getStringDecoder());
        item.motionAttributes = reader.readU32();
        item.timeOffset = reader.readF32();
        item.timeScale = reader.readF32();
        block->m_motionInformations.emplace_back(item);
    }

    block->m_blendTime = reader.readF32();

    block->parseMetadata(metaData, object);
    return pointer;
}

uint32_t PDF3D_U3D_AnimationModifierBlock::getAttributes() const
{
    return m_attributes;
}

float PDF3D_U3D_AnimationModifierBlock::getTimescale() const
{
    return m_timescale;
}

uint32_t PDF3D_U3D_AnimationModifierBlock::getMotionCount() const
{
    return m_motionCount;
}

const std::vector<PDF3D_U3D_AnimationModifierBlock::MotionInformation>& PDF3D_U3D_AnimationModifierBlock::getMotionInformations() const
{
    return m_motionInformations;
}

float PDF3D_U3D_AnimationModifierBlock::getBlendTime() const
{
    return m_blendTime;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_BoneWeightModifierBlock::parse(QByteArray data,
                                                                    QByteArray metaData,
                                                                    PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_BoneWeightModifierBlock* block = new PDF3D_U3D_BoneWeightModifierBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_modifierName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();
    block->m_attributes = reader.readU32();
    block->m_inverseQuant = reader.readF32();

    uint32_t positionCount = reader.readU32();
    for (uint32_t i = 0; i < positionCount; ++i)
    {
        uint32_t count = reader.readCompressedU32(cBoneWeightCnt);

        BoneWeightList item;

        for (uint32_t j = 0; j < count; ++j)
        {
            item.boneIndex.push_back(reader.readCompressedU32(cBoneIdx));
        }
        for (uint32_t j = 0; j < count - 1; ++j)
        {
            item.boneWeight.push_back(reader.readCompressedU32(cQntBoneWeight));
        }

        block->m_boneWeightLists.emplace_back(std::move(item));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

float PDF3D_U3D_BoneWeightModifierBlock::getInverseQuant() const
{
    return m_inverseQuant;
}

const std::vector<PDF3D_U3D_BoneWeightModifierBlock::BoneWeightList>& PDF3D_U3D_BoneWeightModifierBlock::getBoneWeightLists() const
{
    return m_boneWeightLists;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ShadingModifierBlock::parse(QByteArray data,
                                                                 QByteArray metaData,
                                                                 PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_ShadingModifierBlock* block = new PDF3D_U3D_ShadingModifierBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_modifierName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();
    block->m_attributes = reader.readU32();

    uint32_t shaderListCount = reader.readU32();
    for (uint32_t i = 0; i < shaderListCount; ++i)
    {
        QStringList shaderNames;

        uint32_t shaderCount = reader.readU32();
        for (uint32_t j = 0; j < shaderCount; ++j)
        {
            shaderNames << reader.readString(object->getStringDecoder());
        }

        block->m_shaderLists.emplace_back(std::move(shaderNames));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_ShadingModifierBlock::getModifierName() const
{
    return m_modifierName;
}

uint32_t PDF3D_U3D_ShadingModifierBlock::getChainIndex() const
{
    return m_chainIndex;
}

uint32_t PDF3D_U3D_ShadingModifierBlock::getAttributes() const
{
    return m_attributes;
}

const PDF3D_U3D_ShadingModifierBlock::ShaderLists& PDF3D_U3D_ShadingModifierBlock::getShaderLists() const
{
    return m_shaderLists;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_CLODModifierBlock::parse(QByteArray data,
                                                              QByteArray metaData,
                                                              PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_CLODModifierBlock* block = new PDF3D_U3D_CLODModifierBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_modifierName = reader.readString(object->getStringDecoder());
    block->m_chainIndex = reader.readU32();
    block->m_attributes = reader.readU32();
    block->m_CLODAutomaticLevelOfDetails = reader.readF32();
    block->m_CLODModifierLevel = reader.readF32();

    block->parseMetadata(metaData, object);
    return pointer;
}

float PDF3D_U3D_CLODModifierBlock::getCLODAutomaticLevelOfDetails() const
{
    return m_CLODAutomaticLevelOfDetails;
}

float PDF3D_U3D_CLODModifierBlock::getCLODModifierLevel() const
{
    return m_CLODModifierLevel;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LightResourceBlock::parse(QByteArray data,
                                                               QByteArray metaData,
                                                               PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_LightResourceBlock* block = new PDF3D_U3D_LightResourceBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_resourceName = reader.readString(object->getStringDecoder());
    block->m_attributes = reader.readU32();
    block->m_type = reader.readU8();
    reader.readFloats32(block->m_color);
    reader.readFloats32(block->m_attenuation);
    block->m_spotAngle = reader.readF32();
    block->m_intensity = reader.readF32();

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_LightResourceBlock::getResourceName() const
{
    return m_resourceName;
}

uint32_t PDF3D_U3D_LightResourceBlock::getAttributes() const
{
    return m_attributes;
}

uint8_t PDF3D_U3D_LightResourceBlock::getType() const
{
    return m_type;
}

const PDF3D_U3D_Vec4& PDF3D_U3D_LightResourceBlock::getColor() const
{
    return m_color;
}

const PDF3D_U3D_Vec3& PDF3D_U3D_LightResourceBlock::getAttenuation() const
{
    return m_attenuation;
}

float PDF3D_U3D_LightResourceBlock::getSpotAngle() const
{
    return m_spotAngle;
}

float PDF3D_U3D_LightResourceBlock::getIntensity() const
{
    return m_intensity;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ViewResourceBlock::parse(QByteArray data,
                                                              QByteArray metaData,
                                                              PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_ViewResourceBlock* block = new PDF3D_U3D_ViewResourceBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_resourceName = reader.readString(object->getStringDecoder());

    const uint32_t passCount = reader.readU32();
    for (uint32_t i = 0; i < passCount; ++i)
    {
        Pass pass;

        pass.rootNodeName = reader.readString(object->getStringDecoder());
        pass.renderAttributes = reader.readU32();
        pass.fogMode = reader.readU32();
        reader.readFloats32(pass.fogColor);
        pass.fogNear = reader.readF32();
        pass.fogFar = reader.readF32();

        block->m_renderPasses.emplace_back(std::move(pass));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_ViewResourceBlock::getResourceName() const
{
    return m_resourceName;
}

const std::vector<PDF3D_U3D_ViewResourceBlock::Pass>& PDF3D_U3D_ViewResourceBlock::getRenderPasses() const
{
    return m_renderPasses;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LitTextureShaderResourceBlock::parse(QByteArray data,
                                                                          QByteArray metaData,
                                                                          PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_LitTextureShaderResourceBlock* block = new PDF3D_U3D_LitTextureShaderResourceBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_resourceName = reader.readString(object->getStringDecoder());
    block->m_attributes = reader.readU32();
    block->m_alphaTestReference = reader.readF32();
    block->m_alphaTestFunction = reader.readU32();
    block->m_colorBlendFunction = reader.readU32();
    block->m_renderPassEnabled = reader.readU32();
    block->m_shaderChannels = reader.readU32();
    block->m_alphaTextureChannels = reader.readU32();
    block->m_materialName = reader.readString(object->getStringDecoder());

    uint32_t value = block->m_shaderChannels & 0xFF;
    uint32_t activeChannelCount = 0;
    for (; value; ++activeChannelCount)
    {
        value = value & (value - 1);
    }

    for (uint32_t i = 0; i < activeChannelCount; ++i)
    {
        TextureInfo info;

        info.textureName = reader.readString(object->getStringDecoder());
        info.textureIntensity = reader.readF32();
        info.blendFunction = reader.readU8();
        info.blendSource = reader.readU8();
        info.blendConstant = reader.readF32();
        info.textureMode = reader.readU8();
        info.textureTransform = reader.readMatrix4x4();
        info.textureMap = reader.readMatrix4x4();
        info.repeat = reader.readU8();

        block->m_textureInfos.emplace_back(std::move(info));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_LitTextureShaderResourceBlock::getResourceName() const
{
    return m_resourceName;
}

uint32_t PDF3D_U3D_LitTextureShaderResourceBlock::getAttributes() const
{
    return m_attributes;
}

float PDF3D_U3D_LitTextureShaderResourceBlock::getAlphaTestReference() const
{
    return m_alphaTestReference;
}

uint32_t PDF3D_U3D_LitTextureShaderResourceBlock::getColorBlendFunction() const
{
    return m_colorBlendFunction;
}

uint32_t PDF3D_U3D_LitTextureShaderResourceBlock::getAlphaTestFunction() const
{
    return m_alphaTestFunction;
}

uint32_t PDF3D_U3D_LitTextureShaderResourceBlock::getRenderPassEnabled() const
{
    return m_renderPassEnabled;
}

uint32_t PDF3D_U3D_LitTextureShaderResourceBlock::getShaderChannels() const
{
    return m_shaderChannels;
}

uint32_t PDF3D_U3D_LitTextureShaderResourceBlock::getAlphaTextureChannels() const
{
    return m_alphaTextureChannels;
}

const QString& PDF3D_U3D_LitTextureShaderResourceBlock::getMaterialName() const
{
    return m_materialName;
}

const std::vector<PDF3D_U3D_LitTextureShaderResourceBlock::TextureInfo>& PDF3D_U3D_LitTextureShaderResourceBlock::getTextureInfos() const
{
    return m_textureInfos;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_MaterialResourceBlock::parse(QByteArray data,
                                                                  QByteArray metaData,
                                                                  PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_MaterialResourceBlock* block = new PDF3D_U3D_MaterialResourceBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_resourceName = reader.readString(object->getStringDecoder());
    block->m_materialAttributes = reader.readU32();
    reader.readFloats32(block->m_ambientColor);
    reader.readFloats32(block->m_diffuseColor);
    reader.readFloats32(block->m_specularColor);
    reader.readFloats32(block->m_emissiveColor);
    block->m_reflectivity = reader.readF32();
    block->m_opacity = reader.readF32();

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_MaterialResourceBlock::getResourceName() const
{
    return m_resourceName;
}

uint32_t PDF3D_U3D_MaterialResourceBlock::getMaterialAttributes() const
{
    return m_materialAttributes;
}

const PDF3D_U3D_Vec3& PDF3D_U3D_MaterialResourceBlock::getAmbientColor() const
{
    return m_ambientColor;
}

const PDF3D_U3D_Vec3& PDF3D_U3D_MaterialResourceBlock::getDiffuseColor() const
{
    return m_diffuseColor;
}

const PDF3D_U3D_Vec3& PDF3D_U3D_MaterialResourceBlock::getSpecularColor() const
{
    return m_specularColor;
}

const PDF3D_U3D_Vec3& PDF3D_U3D_MaterialResourceBlock::getEmissiveColor() const
{
    return m_emissiveColor;
}

float PDF3D_U3D_MaterialResourceBlock::getReflectivity() const
{
    return m_reflectivity;
}

float PDF3D_U3D_MaterialResourceBlock::getOpacity() const
{
    return m_opacity;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_TextureResourceBlock::parse(QByteArray data,
                                                                 QByteArray metaData,
                                                                 PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_TextureResourceBlock* block = new PDF3D_U3D_TextureResourceBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_resourceName = reader.readString(object->getStringDecoder());
    block->m_textureHeight = reader.readU32();
    block->m_textureWidth = reader.readU32();
    block->m_type = reader.readU8();

    const uint32_t imageCount = reader.readU32();
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        ContinuationImageFormat format;

        format.compressionType = reader.readU8();
        format.channels = reader.readU8();
        format.attributes = reader.readU16();

        if (!format.isExternal())
        {
            format.imageDataByteCount = reader.readU32();
        }
        else
        {
            format.imageURLCount = reader.readU32();
            format.imageURLs = reader.readStringList(format.imageURLCount, object->getStringDecoder());
        }

        block->m_formats.emplace_back(std::move(format));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

const QString& PDF3D_U3D_TextureResourceBlock::getResourceName() const
{
    return m_resourceName;
}

uint32_t PDF3D_U3D_TextureResourceBlock::getTextureHeight() const
{
    return m_textureHeight;
}

uint32_t PDF3D_U3D_TextureResourceBlock::getTextureWidth() const
{
    return m_textureWidth;
}

uint8_t PDF3D_U3D_TextureResourceBlock::getType() const
{
    return m_type;
}

const std::vector<PDF3D_U3D_TextureResourceBlock::ContinuationImageFormat>& PDF3D_U3D_TextureResourceBlock::getFormats() const
{
    return m_formats;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_TextureContinuationResourceBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_TextureContinuationResourceBlock* block = new PDF3D_U3D_TextureContinuationResourceBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_resourceName = reader.readString(object->getStringDecoder());
    block->m_imageIndex = reader.readU32();
    block->m_imageData = reader.readRemainingData();

    block->parseMetadata(metaData, object);
    return pointer;
}

uint32_t PDF3D_U3D_TextureContinuationResourceBlock::getImageIndex() const
{
    return m_imageIndex;
}

const QByteArray& PDF3D_U3D_TextureContinuationResourceBlock::getImageData() const
{
    return m_imageData;
}

bool PDF3D_U3D_Light::isEnabled() const
{
    return m_isEnabled;
}

void PDF3D_U3D_Light::setIsEnabled(bool newIsEnabled)
{
    m_isEnabled = newIsEnabled;
}

bool PDF3D_U3D_Light::isSpecular() const
{
    return m_isSpecular;
}

void PDF3D_U3D_Light::setIsSpecular(bool newIsSpecular)
{
    m_isSpecular = newIsSpecular;
}

bool PDF3D_U3D_Light::isSpotDecay() const
{
    return m_isSpotDecay;
}

void PDF3D_U3D_Light::setIsSpotDecay(bool newIsSpotDecay)
{
    m_isSpotDecay = newIsSpotDecay;
}

PDF3D_U3D_Light::Type PDF3D_U3D_Light::getType() const
{
    return m_type;
}

void PDF3D_U3D_Light::setType(Type newType)
{
    m_type = newType;
}

const QColor& PDF3D_U3D_Light::getColor() const
{
    return m_color;
}

void PDF3D_U3D_Light::setColor(const QColor& newColor)
{
    m_color = newColor;
}

const QVector3D& PDF3D_U3D_Light::getAttenuation() const
{
    return m_attenuation;
}

void PDF3D_U3D_Light::setAttenuation(const QVector3D& newAttenuation)
{
    m_attenuation = newAttenuation;
}

float PDF3D_U3D_Light::getSpotAngle() const
{
    return m_spotAngle;
}

void PDF3D_U3D_Light::setSpotAngle(float newSpotAngle)
{
    m_spotAngle = newSpotAngle;
}

float PDF3D_U3D_Light::getIntensity() const
{
    return m_intensity;
}

void PDF3D_U3D_Light::setIntensity(float newIntensity)
{
    m_intensity = newIntensity;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_MotionResourceBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_MotionResourceBlock* block = new PDF3D_U3D_MotionResourceBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_resourceName = reader.readString(object->getStringDecoder());
    block->m_trackCount = reader.readU32();
    block->m_timeInverseQuant = reader.readF32();
    block->m_rotationInverseQuant = reader.readF32();

    for (uint32_t i = 0; i < block->m_trackCount; ++i)
    {
        Motion motion;

        motion.trackName = reader.readString(object->getStringDecoder());
        motion.timeCount = reader.readU32();
        motion.displacementInverseQuant = reader.readF32();
        motion.rotationInverseQuant = reader.readF32();

        for (uint32_t j = 0; j < motion.timeCount; ++j)
        {
            KeyFrame keyFrame;

            if (j == 0 || j == motion.timeCount - 1)
            {
                keyFrame.time = reader.readF32();
                reader.readFloats32(keyFrame.displacement);
                reader.readFloats32(keyFrame.rotation);
                reader.readFloats32(keyFrame.scale);
            }
            else
            {
                keyFrame.timeDiff = reader.readQuantizedVec1(cTimeSign, cTimeDiff);
                keyFrame.displacementDiff = reader.readQuantizedVec3(cDispSign, cDispDiff, cDispDiff, cDispDiff);
                keyFrame.rotationDiff = reader.readQuantizedVec3(cRotSign, cRotDiff, cRotDiff, cRotDiff);
                keyFrame.scaleDiff = reader.readQuantizedVec3(cScalSign, cScalDiff, cScalDiff, cScalDiff);
            }

            motion.keyFrames.emplace_back(std::move(keyFrame));
        }

        block->m_motions.emplace_back(std::move(motion));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

uint32_t PDF3D_U3D_MotionResourceBlock::getTrackCount() const
{
    return m_trackCount;
}

float PDF3D_U3D_MotionResourceBlock::getTimeInverseQuant() const
{
    return m_timeInverseQuant;
}

float PDF3D_U3D_MotionResourceBlock::getRotationInverseQuant() const
{
    return m_rotationInverseQuant;
}

const std::vector<PDF3D_U3D_MotionResourceBlock::Motion>& PDF3D_U3D_MotionResourceBlock::getMotions() const
{
    return m_motions;
}

bool PDF3D_U3D_Fog::isEnabled() const
{
    return m_enabled;
}

void PDF3D_U3D_Fog::setEnabled(bool newEnabled)
{
    m_enabled = newEnabled;
}

const QColor& PDF3D_U3D_Fog::getColor() const
{
    return m_color;
}

void PDF3D_U3D_Fog::setColor(const QColor& newColor)
{
    m_color = newColor;
}

float PDF3D_U3D_Fog::getNear() const
{
    return m_near;
}

void PDF3D_U3D_Fog::setNear(float newNear)
{
    m_near = newNear;
}

float PDF3D_U3D_Fog::getFar() const
{
    return m_far;
}

void PDF3D_U3D_Fog::setFar(float newFar)
{
    m_far = newFar;
}

PDF3D_U3D_Fog::FogMode PDF3D_U3D_Fog::getMode() const
{
    return m_mode;
}

void PDF3D_U3D_Fog::setMode(FogMode newMode)
{
    m_mode = newMode;
}

const QString& PDF3D_U3D_RenderPass::getRootNodeName() const
{
    return m_rootNodeName;
}

const PDF3D_U3D_Fog& PDF3D_U3D_RenderPass::getFog() const
{
    return m_fog;
}

void PDF3D_U3D_RenderPass::setRootNodeName(const QString& newRootNodeName)
{
    m_rootNodeName = newRootNodeName;
}

void PDF3D_U3D_RenderPass::setFog(const PDF3D_U3D_Fog& newFog)
{
    m_fog = newFog;
}

const std::vector<PDF3D_U3D_RenderPass>& PDF3D_U3D_View::renderPasses() const
{
    return m_renderPasses;
}

void PDF3D_U3D_View::setRenderPasses(const std::vector<PDF3D_U3D_RenderPass>& newRenderPasses)
{
    m_renderPasses = newRenderPasses;
}

bool PDF3D_U3D_Shader::isLightingEnabled() const
{
    return m_isLightingEnabled;
}

void PDF3D_U3D_Shader::setIsLightingEnabled(bool newIsLightingEnabled)
{
    m_isLightingEnabled = newIsLightingEnabled;
}

bool PDF3D_U3D_Shader::isAlphaTestEnabled() const
{
    return m_isAlphaTestEnabled;
}

void PDF3D_U3D_Shader::setIsAlphaTestEnabled(bool newIsAlphaTestEnabled)
{
    m_isAlphaTestEnabled = newIsAlphaTestEnabled;
}

bool PDF3D_U3D_Shader::isUseVertexColor() const
{
    return m_isUseVertexColor;
}

void PDF3D_U3D_Shader::setIsUseVertexColor(bool newIsUseVertexColor)
{
    m_isUseVertexColor = newIsUseVertexColor;
}

float PDF3D_U3D_Shader::getAlphaTestReference() const
{
    return m_alphaTestReference;
}

void PDF3D_U3D_Shader::setAlphaTestReference(float newAlphaTestReference)
{
    m_alphaTestReference = newAlphaTestReference;
}

PDF3D_U3D_Shader::AlphaTestFunction PDF3D_U3D_Shader::getAlphaTestFunction() const
{
    return m_alphaTestFunction;
}

void PDF3D_U3D_Shader::setAlphaTestFunction(AlphaTestFunction newAlphaTestFunction)
{
    m_alphaTestFunction = newAlphaTestFunction;
}

PDF3D_U3D_Shader::ColorBlendFunction PDF3D_U3D_Shader::getColorBlendFunction() const
{
    return m_colorBlendFunction;
}

void PDF3D_U3D_Shader::setColorBlendFunction(ColorBlendFunction newColorBlendFunction)
{
    m_colorBlendFunction = newColorBlendFunction;
}

uint32_t PDF3D_U3D_Shader::getRenderPassEnabledFlags() const
{
    return m_renderPassEnabledFlags;
}

void PDF3D_U3D_Shader::setRenderPassEnabledFlags(uint32_t newRenderPassEnabledFlags)
{
    m_renderPassEnabledFlags = newRenderPassEnabledFlags;
}

uint32_t PDF3D_U3D_Shader::getShaderChannels() const
{
    return m_shaderChannels;
}

void PDF3D_U3D_Shader::setShaderChannels(uint32_t newShaderChannels)
{
    m_shaderChannels = newShaderChannels;
}

uint32_t PDF3D_U3D_Shader::getAlphaTextureChannels() const
{
    return m_alphaTextureChannels;
}

void PDF3D_U3D_Shader::setAlphaTextureChannels(uint32_t newAlphaTextureChannels)
{
    m_alphaTextureChannels = newAlphaTextureChannels;
}

const QString& PDF3D_U3D_Shader::getMaterialName() const
{
    return m_materialName;
}

void PDF3D_U3D_Shader::setMaterialName(const QString& newMaterialName)
{
    m_materialName = newMaterialName;
}

const PDF3D_U3D_Shader::PDF_U3D_TextureInfos& PDF3D_U3D_Shader::getTextureInfos() const
{
    return m_textureInfos;
}

void PDF3D_U3D_Shader::setTextureInfos(const PDF_U3D_TextureInfos& newTextureInfos)
{
    m_textureInfos = newTextureInfos;
}

const QColor& PDF3D_U3D_Material::getAmbientColor() const
{
    return m_ambientColor;
}

void PDF3D_U3D_Material::setAmbientColor(const QColor& newAmbientColor)
{
    m_ambientColor = newAmbientColor;
}

const QColor& PDF3D_U3D_Material::getDiffuseColor() const
{
    return m_diffuseColor;
}

void PDF3D_U3D_Material::setDiffuseColor(const QColor& newDiffuseColor)
{
    m_diffuseColor = newDiffuseColor;
}

const QColor& PDF3D_U3D_Material::getSpecularColor() const
{
    return m_specularColor;
}

void PDF3D_U3D_Material::setSpecularColor(const QColor& newSpecularColor)
{
    m_specularColor = newSpecularColor;
}

const QColor& PDF3D_U3D_Material::getEmmisiveColor() const
{
    return m_emmisiveColor;
}

void PDF3D_U3D_Material::setEmmisiveColor(const QColor& newEmmisiveColor)
{
    m_emmisiveColor = newEmmisiveColor;
}

float PDF3D_U3D_Material::getReflectivity() const
{
    return m_reflectivity;
}

void PDF3D_U3D_Material::setReflectivity(float newReflectivity)
{
    m_reflectivity = newReflectivity;
}

PDF3D_U3D_Node::NodeType PDF3D_U3D_Node::getType() const
{
    return m_type;
}

void PDF3D_U3D_Node::setType(NodeType newType)
{
    m_type = newType;
}

const QString& PDF3D_U3D_Node::getNodeName() const
{
    return m_nodeName;
}

void PDF3D_U3D_Node::setNodeName(const QString& newNodeName)
{
    m_nodeName = newNodeName;
}

const QString& PDF3D_U3D_Node::getResourceName() const
{
    return m_resourceName;
}

void PDF3D_U3D_Node::setResourceName(const QString& newResourceName)
{
    m_resourceName = newResourceName;
}

const QStringList& PDF3D_U3D_Node::getChildren() const
{
    return m_children;
}

void PDF3D_U3D_Node::setChildren(const QStringList& newChildren)
{
    m_children = newChildren;
}

void PDF3D_U3D_Node::addChild(const QString& child, QMatrix4x4 childTransform)
{
    m_children << child;

    if (!childTransform.isIdentity())
    {
        m_childTransforms[child] = childTransform;
    }
}

QMatrix4x4 PDF3D_U3D_Node::getChildTransform(const QString& nodeName) const
{
    auto it = m_childTransforms.find(nodeName);
    if (it != m_childTransforms.cend())
    {
        return it->second;
    }

    return QMatrix4x4();
}

QMatrix4x4 PDF3D_U3D_Node::getConstantChildTransform() const
{
    if (!m_childTransforms.empty())
    {
        auto it = m_childTransforms.begin();
        return it->second;
    }

    return QMatrix4x4();
}

bool PDF3D_U3D_Node::hasChildTransform() const
{
    return !m_childTransforms.empty();
}

bool PDF3D_U3D_Node::hasConstantChildTransform() const
{
    if (m_childTransforms.size() < 2)
    {
        return true;
    }

    return false;
}

bool PDF3D_U3D_Node::isEnabled() const
{
    return m_isEnabled;
}

void PDF3D_U3D_Node::setIsEnabled(bool newIsEnabled)
{
    m_isEnabled = newIsEnabled;
}

bool PDF3D_U3D_Node::isFrontVisible() const
{
    return m_isFrontVisible;
}

void PDF3D_U3D_Node::setIsFrontVisible(bool newIsFrontVisible)
{
    m_isFrontVisible = newIsFrontVisible;
}

bool PDF3D_U3D_Node::isBackVisible() const
{
    return m_isBackVisible;
}

void PDF3D_U3D_Node::setIsBackVisible(bool newIsBackVisible)
{
    m_isBackVisible = newIsBackVisible;
}

void PDF3D_U3D_LineSetGeometry::addLine(Line line)
{
    const size_t lineIndex = m_lines.size();
    m_lines.emplace_back(std::move(line));
    m_mapPosIndexToLine.insert(std::make_pair(m_lines.back().position1, lineIndex));
    m_mapPosIndexToLine.insert(std::make_pair(m_lines.back().position2, lineIndex));
}

std::vector<PDF3D_U3D_LineSetGeometry::Line> PDF3D_U3D_LineSetGeometry::queryLinesByVertexIndex(size_t vertexIndex) const
{
    std::vector<PDF3D_U3D_LineSetGeometry::Line> result;
    auto iterators = m_mapPosIndexToLine.equal_range(vertexIndex);
    result.reserve(std::distance(iterators.first, iterators.second));

    for (auto it = iterators.first; it != iterators.second; ++it)
    {
        result.push_back(m_lines[it->second]);
    }

    return result;
}

void PDF3D_U3D_PointSetGeometry::addPoint(Point point)
{
    const size_t pointIndex = m_points.size();
    m_points.emplace_back(std::move(point));
    m_mapPosIndexToPoint.insert(std::make_pair(m_points.back().position, pointIndex));
}

std::vector<PDF3D_U3D_PointSetGeometry::Point> PDF3D_U3D_PointSetGeometry::queryPointsByVertexIndex(size_t vertexIndex) const
{
    std::vector<PDF3D_U3D_PointSetGeometry::Point> result;
    auto iterators = m_mapPosIndexToPoint.equal_range(vertexIndex);
    result.reserve(std::distance(iterators.first, iterators.second));

    for (auto it = iterators.first; it != iterators.second; ++it)
    {
        result.push_back(m_points[it->second]);
    }

    return result;
}

void PDF3D_U3D_MeshGeometry::addTriangle(Triangle triangle)
{
    const size_t triangleIndex = m_triangles.size();
    m_triangles.emplace_back(std::move(triangle));
    m_mapPosIndexToTriangle.insert(std::make_pair(m_triangles.back().vertices[0].positionIndex, triangleIndex));
    m_mapPosIndexToTriangle.insert(std::make_pair(m_triangles.back().vertices[1].positionIndex, triangleIndex));
    m_mapPosIndexToTriangle.insert(std::make_pair(m_triangles.back().vertices[2].positionIndex, triangleIndex));
}

QVector3D PDF3D_U3D_MeshGeometry::getNormal(const Triangle& triangle) const
{
    QVector3D p1 = getPosition(triangle.vertices[0].positionIndex);
    QVector3D p2 = getPosition(triangle.vertices[1].positionIndex);
    QVector3D p3 = getPosition(triangle.vertices[2].positionIndex);

    QVector3D v1 = p2 - p1;
    QVector3D v2 = p3 - p1;

    QVector3D normal = QVector3D::crossProduct(v1, v2);
    normal.normalize();
    return normal;
}

std::vector<PDF3D_U3D_MeshGeometry::Triangle> PDF3D_U3D_MeshGeometry::queryTrianglesByVertexIndex(size_t vertexIndex) const
{
    std::vector<PDF3D_U3D_MeshGeometry::Triangle> result;
    auto iterators = m_mapPosIndexToTriangle.equal_range(vertexIndex);
    result.reserve(std::distance(iterators.first, iterators.second));

    for (auto it = iterators.first; it != iterators.second; ++it)
    {
        result.push_back(m_triangles[it->second]);
    }

    return result;
}

std::vector<PDF3D_U3D_MeshGeometry::Triangle> PDF3D_U3D_MeshGeometry::queryTrianglesOnLine(size_t vertexIndex1, size_t vertexIndex2) const
{
    std::vector<PDF3D_U3D_MeshGeometry::Triangle> result;

    auto iterators1 = m_mapPosIndexToTriangle.equal_range(vertexIndex1);
    auto iterators2 = m_mapPosIndexToTriangle.equal_range(vertexIndex2);

    std::vector<size_t> triangleIndices1;
    std::vector<size_t> triangleIndices2;

    triangleIndices1.reserve(std::distance(iterators1.first, iterators1.second));
    for (auto it = iterators1.first; it != iterators1.second; ++it)
    {
        triangleIndices1.push_back(it->second);
    }

    triangleIndices2.reserve(std::distance(iterators2.first, iterators2.second));
    for (auto it = iterators2.first; it != iterators2.second; ++it)
    {
        triangleIndices2.push_back(it->second);
    }

    std::sort(triangleIndices1.begin(), triangleIndices1.end());
    std::sort(triangleIndices2.begin(), triangleIndices2.end());

    triangleIndices1.erase(std::unique(triangleIndices1.begin(), triangleIndices1.end()), triangleIndices1.end());
    triangleIndices2.erase(std::unique(triangleIndices2.begin(), triangleIndices2.end()), triangleIndices2.end());

    std::vector<size_t> triangleIndices;
    std::set_intersection(triangleIndices1.cbegin(), triangleIndices1.cend(), triangleIndices2.cbegin(), triangleIndices2.cend(), std::back_inserter(triangleIndices));

    for (const size_t triangleIndex : triangleIndices)
    {
        result.push_back(m_triangles[triangleIndex]);
    }

    return result;
}

QString PDF3D_U3D_Geometry::getShaderName(uint32_t shadingId) const
{
    if (shadingId < m_shaders.size())
    {
        const QStringList& shaderNames = m_shaders[shadingId];
        if (!shaderNames.isEmpty())
        {
            return shaderNames.front();
        }
    }

    return QString();
}

std::vector<QStringList> PDF3D_U3D_Geometry::getShaders() const
{
    return m_shaders;
}

void PDF3D_U3D_Geometry::setShaders(const std::vector<QStringList>& newShaders)
{
    m_shaders = newShaders;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_RHAdobeMeshResourceBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_RHAdobeMeshResourceBlock* block = new PDF3D_U3D_RHAdobeMeshResourceBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    QString objectName = reader.readString(object->getStringDecoder());
    uint32_t chainIndex = reader.readU32();
/*
    QByteArray remainingData = reader.readRemainingData();
    QByteArray uncompressedData = PDFFlateDecodeFilter::uncompress(remainingData);
    return pointer;
*/
    // Read the data
    while (!reader.isAtEnd())
    {
        // Read the header
        QString encoding = reader.readString(object->getStringDecoder());
        uint8_t flags = reader.readU8();

        const bool hasExtensionData = flags & 0x10;
        const bool hasSkeletonData = flags & 0x20;
        const uint8_t materialTypeCount = flags >> 6;

        if (hasExtensionData)
        {
            uint32_t sizeOfTheMeshDescriptionBlock = reader.readU32();
            uint16_t numberOfSubChunks = reader.readU16();

            Q_UNUSED(sizeOfTheMeshDescriptionBlock);

            for (uint16_t i = 0; i < numberOfSubChunks; ++i)
            {
                uint32_t subChunkSize = reader.readU32();
                uint16_t subChunkTypeInfo = reader.readU16();

                Q_UNUSED(subChunkSize);
                Q_UNUSED(subChunkTypeInfo);
            }
        }

        uint32_t materialCount = 0;
        switch (materialTypeCount)
        {
            case 0x00:
                materialCount = 1;
                break;

            case 0x01:
                materialCount = reader.readU8();
                break;

            case 0x02:
                materialCount = reader.readU16();
                break;

            case 0x03:
                materialCount = reader.readU32();
                break;
        }

        struct MaterialInfo
        {
            uint8_t textureDimensions = 0;
            uint32_t numberOfTextureLayers = 0;
            uint32_t originalShadingId = 0;
            bool hasDiffuseColors = false;
            bool hasSpecularColors = false;
            QByteArray additionalTextDimensions;
        };
        std::vector<MaterialInfo> materialInfos;

        // We will read material info from the data
        if (materialTypeCount != 0)
        {
            for (uint32_t i = 0; i < materialTypeCount; ++i)
            {
                MaterialInfo materialInfo;

                uint8_t materialFlags = reader.readU8();
                materialInfo.textureDimensions = materialFlags & 0x03;
                uint8_t originalShadingIdBytes = (materialFlags >> 2) & 0x03;
                uint8_t numberOfTextureLayersBytes = (materialFlags >> 4) & 0x03;
                materialInfo.hasDiffuseColors = (materialFlags & 0x40);
                materialInfo.hasSpecularColors = (materialFlags & 0x80);

                switch (numberOfTextureLayersBytes)
                {
                    case 0:
                        materialInfo.numberOfTextureLayers = reader.readU8();
                        break;
                    case 1:
                        materialInfo.numberOfTextureLayers = reader.readU16();
                        break;
                    case 2:
                        materialInfo.numberOfTextureLayers = reader.readU24();
                        break;
                    case 3:
                        materialInfo.numberOfTextureLayers = reader.readU32();
                        break;
                }

                switch (originalShadingIdBytes)
                {
                    case 0:
                        materialInfo.originalShadingId = reader.readU8();
                        break;
                    case 1:
                        materialInfo.originalShadingId = reader.readU16();
                        break;
                    case 2:
                        materialInfo.originalShadingId = reader.readU24();
                        break;
                    case 3:
                        materialInfo.originalShadingId = reader.readU32();
                        break;
                }

                uint32_t numberOfAdditionalTextureLayers = materialInfo.numberOfTextureLayers > 0 ? materialInfo.numberOfTextureLayers - 1 : 0;
                uint32_t numberOfAdditionalTextureLayersBytes = 0;

                if (numberOfAdditionalTextureLayers % 4 > 0)
                {
                    numberOfAdditionalTextureLayersBytes = numberOfAdditionalTextureLayers / 4 + 1;
                }
                else
                {
                    numberOfAdditionalTextureLayersBytes = numberOfAdditionalTextureLayers / 4;
                }

                if (numberOfAdditionalTextureLayersBytes > 0)
                {
                    materialInfo.additionalTextDimensions = reader.readByteArray(numberOfAdditionalTextureLayersBytes);
                }

                materialInfos.emplace_back(std::move(materialInfo));
            }
        }

        break;
    }


    block->parseMetadata(metaData, object);
    return pointer;
}

}   // namespace u3d

}   // namespace pdf
