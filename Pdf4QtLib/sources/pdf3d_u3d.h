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

#include <QByteArray>
#include <QSharedPointer>

class QTextCodec;

namespace pdf
{

namespace u3d
{

class PDF3D_U3D_AbstractBlock
{
public:
    inline constexpr PDF3D_U3D_AbstractBlock() = default;
    virtual ~PDF3D_U3D_AbstractBlock() = default;

    void parseMetadata(QByteArray metaData);
};

using PDF3D_U3D_AbstractBlockPtr = QSharedPointer<PDF3D_U3D_AbstractBlock>;

// Bounding sphere - x, y, z position and radius
using PDF3D_U3D_BoundingSphere = std::array<float, 4>;

// Bounding box - min (x,y,z), max (x,y,z) - six values
using PDF3D_U3D_AxisAlignedBoundingBox = std::array<float, 4>;

class PDF3D_U3D_FileBlock : public PDF3D_U3D_AbstractBlock
{
public:
    static constexpr uint32_t ID = 0x00443355;

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, bool isCompressed);

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

    static PDF3D_U3D_AbstractBlockPtr parse(QByteArray data, QByteArray metaData, bool isCompressed, QTextCodec* textCodec);

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

class PDF4QTLIBSHARED_EXPORT PDF3D_U3D
{
public:
    PDF3D_U3D();

    bool isCompressed() const { return m_isCompressed; }
    QTextCodec* getTextCodec() const { return m_textCodec; }

    static PDF3D_U3D parse(QByteArray data);

private:
    PDF3D_U3D_AbstractBlockPtr parseBlock(uint32_t blockType, const QByteArray& data, const QByteArray& metaData);
    static uint32_t getBlockPadding(uint32_t blockSize);

    std::vector<PDF3D_U3D_AbstractBlockPtr> m_blocks;
    const PDF3D_U3D_FileBlock* m_fileBlock = nullptr;
    QTextCodec* m_textCodec = nullptr;
    bool m_isCompressed = true;
};

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
    void skipBytes(uint32_t size);

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
