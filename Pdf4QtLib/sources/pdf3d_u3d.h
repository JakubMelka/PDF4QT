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

#include <array>

class QTextCodec;

namespace pdf
{

namespace u3d
{

class PDF3D_U3D;
class PDF3D_U3D_DataReader;

class PDF3D_U3D_AbstractBlock;
using PDF3D_U3D_AbstractBlockPtr = QSharedPointer<PDF3D_U3D_AbstractBlock>;

using PDF3D_U3D_Vec3 = std::array<float, 3>;
using PDF3D_U3D_Vec4 = std::array<float, 4>;

class PDF3D_U3D_AbstractBlock
{
public:
    inline constexpr PDF3D_U3D_AbstractBlock() = default;
    virtual ~PDF3D_U3D_AbstractBlock() = default;

    void parseMetadata(QByteArray metaData, PDF3D_U3D* object);

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

    static ParentNodesData parseParentNodeData(PDF3D_U3D_DataReader& reader, PDF3D_U3D* object);

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
    static constexpr uint32_t ID = 0x00443355;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

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
    static constexpr uint32_t ID = 0xFFFFFF12;

    bool isBoundingSpherePresent() const { return m_fileReferenceAttributes & 0x00000001; }
    bool isAxisAlignedBoundingBoxPresent() const { return m_fileReferenceAttributes & 0x00000002; }

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

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
    static constexpr uint32_t ID = 0xFFFFFF14;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

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
    const std::vector<PDF3D_U3D_AbstractBlockPtr>& getModifierDeclarationBlocks() const;

private:
    QString m_modifierChainName;
    uint32_t m_modifierChainType = 0;
    uint32_t m_modifierChainAttributes = 0;
    PDF3D_U3D_BoundingSphere m_boundingSphere = PDF3D_U3D_BoundingSphere();
    PDF3D_U3D_AxisAlignedBoundingBox m_boundingBox = PDF3D_U3D_AxisAlignedBoundingBox();
    // Padding 0-3 bytes
    uint32_t m_modifierCount = 0;
    std::vector<PDF3D_U3D_AbstractBlockPtr> m_modifierDeclarationBlocks;
};

class PDF3D_U3D_PriorityUpdateBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = 0xFFFFFF15;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

    uint32_t getNewPriority() const;

private:
    uint32_t m_newPriority = 0;
};

class PDF3D_U3D_NewObjectTypeBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = 0xFFFFFF16;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

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

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

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
    static constexpr uint32_t ID = 0xFFFFFF21;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

    const QString& getGroupNodeName() const;
    const ParentNodesData& getParentNodesData() const;

private:
    QString m_groupNodeName;
    ParentNodesData m_parentNodesData;
};

class PDF3D_U3D_ModelNodeBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = 0xFFFFFF22;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

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
    static constexpr uint32_t ID = 0xFFFFFF23;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

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
    static constexpr uint32_t ID = 0xFFFFFF24;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

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
    static constexpr uint32_t ID = 0xFFFFFF31;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object);

    struct ShadingDescription
    {
        uint32_t shadingAttributes = 0;
        uint32_t textureLayerCount = 0;
        std::vector<uint32_t> textureCoordDimensions;
        uint32_t originalShading = 0;
    };

    struct BoneJoint
    {
        float jointCenterU = 0.0;
        float jointCenterV = 0.0;
        float jointScaleU = 0.0;
        float jointScaleV = 0.0;
    };

    struct BoneDescription
    {
        QString boneName;
        QString parentBoneName;
        uint32_t boneAttributes = 0;
        float boneLength = 0.0;
        PDF3D_U3D_Vec3 boneDisplacement = PDF3D_U3D_Vec3();
        PDF3D_U3D_Vec4 boneOrientation = PDF3D_U3D_Vec4();
        uint32_t boneLinkCount = 0;
        float boneLinkLength = 0.0;
        BoneJoint startJoint = BoneJoint();
        BoneJoint endJoint = BoneJoint();
        PDF3D_U3D_Vec3 rotationConstraintMin = PDF3D_U3D_Vec3();
        PDF3D_U3D_Vec3 rotationConstraintMax = PDF3D_U3D_Vec3();

        bool isLinkPresent() const { return boneAttributes & 0x00000001; }
        bool isJointPresent() const { return boneAttributes & 0x00000002; }
    };

    const QString& getMeshName() const;
    uint32_t getChainIndex() const;

    /* max mesh description */
    uint32_t getMeshAttributes() const;
    uint32_t getFaceCount() const;
    uint32_t getPositionCount() const;
    uint32_t getNormalCount() const;
    uint32_t getDiffuseColorCount() const;
    uint32_t getSpecularColorCount() const;
    uint32_t getTextureColorCount() const;
    uint32_t getShadingCount() const;
    const std::vector<ShadingDescription>& getShadingDescription() const;

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
    const std::vector<BoneDescription>& getBoneDescription() const;

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
    uint32_t m_shadingCount = 0;
    std::vector<ShadingDescription> m_shadingDescription;

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
    std::vector<BoneDescription> m_boneDescription;
};

// -------------------------------------------------------------------------------
//                                  PDF3D_U3D
// -------------------------------------------------------------------------------

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D
{
public:
    PDF3D_U3D();

    bool isCompressed() const { return m_isCompressed; }
    QTextCodec* getTextCodec() const { return m_textCodec; }

    static PDF3D_U3D parse(QByteArray data);

    PDF3D_U3D_AbstractBlockPtr parseBlockWithDeclaration(PDF3D_U3D_DataReader& reader);

private:
    PDF3D_U3D_AbstractBlockPtr parseBlock(uint32_t blockType, const QByteArray& data, const QByteArray& metaData);
    static uint32_t getBlockPadding(uint32_t blockSize);

    struct LoadBlockInfo
    {
        bool success = false;
        uint32_t blockType = 0;
        uint32_t dataSize = 0;
        uint32_t metaDataSize = 0;
    };

    std::vector<PDF3D_U3D_AbstractBlockPtr> m_blocks;
    std::vector<LoadBlockInfo> m_blockLoadFails;
    const PDF3D_U3D_FileBlock* m_fileBlock = nullptr;
    QTextCodec* m_textCodec = nullptr;
    bool m_isCompressed = true;
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

class PDF3D_U3D_DataReader
{
public:
    PDF3D_U3D_DataReader(QByteArray data, bool isCompressed);
    ~PDF3D_U3D_DataReader();

    void setData(QByteArray data);

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

    bool isAtEnd() const;

    template<typename T>
    void readFloats32(T& container)
    {
        for (auto& value : container)
        {
            value = readF32();
        }
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
