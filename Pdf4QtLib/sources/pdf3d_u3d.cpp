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

#include "pdf3d_u3d.h"

#include <QTextCodec>
#include <QImageReader>

#include <array>

namespace pdf
{

namespace u3d
{

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

QString PDF3D_U3D_DataReader::readString(QTextCodec* textCodec)
{
    int size = readU16();

    QByteArray encodedString(size, '0');

    for (int i = 0; i < size; ++i)
    {
        encodedString[i] = readU8();
    }

    Q_ASSERT(textCodec);
    return textCodec->toUnicode(encodedString);
}

QStringList PDF3D_U3D_DataReader::readStringList(uint32_t count, QTextCodec* textCodec)
{
    QStringList stringList;

    for (uint32_t i = 0; i < count; ++i)
    {
        stringList << readString(textCodec);
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

std::vector<PDF3D_U3D_BoneDescription> PDF3D_U3D_DataReader::readBoneDescriptions(QTextCodec* textCodec, uint32_t count)
{
    std::vector<PDF3D_U3D_BoneDescription> result;

    for (uint32_t i = 0; i < count; ++i)
    {
        PDF3D_U3D_BoneDescription item;

        item.boneName = readString(textCodec);
        item.parentBoneName = readString(textCodec);
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

}

void PDF3D_U3D::setLightResource(const QString& lightName, PDF3D_U3D_Light light)
{
    m_lightResources[lightName] = std::move(light);
}

void PDF3D_U3D::setTextureResource(const QString& textureName, QImage texture)
{
    m_textureResources[textureName] = std::move(texture);
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

    for (auto it = std::next(decoder.begin()); it != decoder.end(); ++it)
    {
        auto blockData = *it;
        switch (blockData.blockType)
        {
            case PDF3D_U3D_Block_Info::BT_GeneratorCLODBaseMesh:
            {
                // TODO: process block data
                auto currentBlock = PDF3D_U3D_CLODBaseMeshContinuationBlock::parse(blockData.blockData, blockData.metaData, this, declarationBlock);
                break;
            }

            case PDF3D_U3D_Block_Info::BT_GeneratorCLODProgMesh:
            {
                // TODO: process block data
                auto currentBlock = PDF3D_U3D_CLODProgressiveMeshContinuationBlock::parse(blockData.blockData, blockData.metaData, this/*, declarationBlock*/);
                break;
            }

            default:
                m_errors << PDFTranslationContext::tr("Invalid block type '%1' for CLOD mesh!").arg(blockData.blockType, 8, 16);
        }
    }
}

void PDF3D_U3D_Parser::processPointSet(const PDF3D_U3D_Decoder& decoder)
{
    auto block = parseBlock(decoder.front());
    const PDF3D_U3D_PointSetDeclarationBlock* declarationBlock = dynamic_cast<const PDF3D_U3D_PointSetDeclarationBlock*>(block.data());

    for (auto it = std::next(decoder.begin()); it != decoder.end(); ++it)
    {
        auto blockData = *it;
        switch (blockData.blockType)
        {
            case PDF3D_U3D_Block_Info::BT_GeneratorPointSetCont:
            {
                // TODO: process block data
                auto currentBlock = PDF3D_U3D_PointSetContinuationBlock::parse(blockData.blockData, blockData.metaData, this, declarationBlock);
                break;
            }

            default:
                m_errors << PDFTranslationContext::tr("Invalid block type '%1' for Point set!").arg(blockData.blockType, 8, 16);
        }
    }
}

void PDF3D_U3D_Parser::processLineSet(const PDF3D_U3D_Decoder& decoder)
{
    auto block = parseBlock(decoder.front());
    const PDF3D_U3D_LineSetDeclarationBlock* declarationBlock = dynamic_cast<const PDF3D_U3D_LineSetDeclarationBlock*>(block.data());

    for (auto it = std::next(decoder.begin()); it != decoder.end(); ++it)
    {
        auto blockData = *it;
        switch (blockData.blockType)
        {
            case PDF3D_U3D_Block_Info::BT_GeneratorLineSetCont:
            {
                // TODO: process block data
                auto currentBlock = PDF3D_U3D_LineSetContinuationBlock::parse(blockData.blockData, blockData.metaData, this, declarationBlock);
                break;
            }

            default:
                m_errors << PDFTranslationContext::tr("Invalid block type '%1' for line set!").arg(blockData.blockType, 8, 16);
        }
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
        const PDF3D_U3D_TextureResourceBlock::ContinuationImageFormat& format = formats[i];

        if (format.isExternal())
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
                    texture = image.alphaChannel();
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
                    m_errors << PDFTranslationContext::tr("Unkown light type '%1'.").arg(lightResourceBlock->getType(), 2, 16);
                    break;
            }

            m_object.setLightResource(lightResourceBlock->getResourceName(), std::move(light));
            break;
        }

        default:
        {
            // TODO: add block to U3D
            m_errors << PDFTranslationContext::tr("Block type (%1) not supported in scene decoder.").arg(block->getBlockType(), 8, 16);
            break;
        }
    }
}

PDF3D_U3D_Parser::PDF3D_U3D_Parser()
{
    // Jakub Melka: 106 is default value for U3D strings
    m_textCodec = QTextCodec::codecForMib(106);
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

    for (const auto& block : chainBlock->getModifierDeclarationBlocks())
    {
        processBlock(block, palette);
    }
}

void PDF3D_U3D_Parser::processGenericBlock(const PDF3D_U3D_Block_Data& blockData,
                                           PDF3D_U3D_Block_Info::EPalette palette)
{
    PDF3D_U3D_DataReader blockReader(blockData.blockData, isCompressed());
    QString blockName = blockReader.readString(getTextCodec());

    uint32_t chainIndex = 0;
    PDF3D_U3D_Block_Info::EPalette effectivePalette = PDF3D_U3D_Block_Info::isChain(blockData.blockType) ? palette
                                                                                                         : PDF3D_U3D_Block_Info::getPalette(blockData.blockType);

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
                    m_textCodec = QTextCodec::codecForMib(fileBlock->getCharacterEncoding());
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

        default:
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
        item.key = reader.readString(object->getTextCodec());

        if (item.attributes & 0x00000001)
        {
            uint32_t binaryLength = reader.readU32();
            item.binaryData = reader.readByteArray(binaryLength);
        }
        else
        {
            item.value = reader.readString(object->getTextCodec());
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
        data.parentNodeName = reader.readString(object->getTextCodec());
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
    block->m_scopeName = reader.readString(object->getTextCodec());
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
    block->m_urls = reader.readStringList(block->m_urlCount, object->getTextCodec());
    block->m_filterCount = reader.readU32();

    for (uint32_t i = 0; i < block->m_filterCount; ++i)
    {
        Filter filter;
        filter.filterType = reader.readU8();

        switch (filter.filterType)
        {
            case 0x00:
                filter.objectNameFilter = reader.readString(object->getTextCodec());
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
    block->m_worldAliasName = reader.readString(object->getTextCodec());

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
    block->m_modifierChainName = reader.readString(object->getTextCodec());
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
    block->m_newObjectTypeName = reader.readString(object->getTextCodec());
    block->m_modifierType = reader.readU32();
    block->m_extensionId = reader.readUuid();
    block->m_newDeclarationBlockType = reader.readU32();
    block->m_continuationBlockTypeCount = reader.readU32();

    for (uint32_t i = 0; i < block->m_continuationBlockTypeCount; ++i)
    {
        block->m_continuationBlockTypes.push_back(reader.readU32());
    }

    block->m_extensionVendorName = reader.readString(object->getTextCodec());
    block->m_urlCount = reader.readU32();
    block->m_urls = reader.readStringList(block->m_urlCount, object->getTextCodec());
    block->m_extensionInformationString = reader.readString(object->getTextCodec());

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
    block->m_objectName = reader.readString(object->getTextCodec());
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
    block->m_groupNodeName = reader.readString(object->getTextCodec());
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
    block->m_modelNodeName = reader.readString(object->getTextCodec());
    block->m_parentNodesData = parseParentNodeData(reader, object);
    block->m_modelResourceName = reader.readString(object->getTextCodec());
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
    block->m_lightNodeName = reader.readString(object->getTextCodec());
    block->m_parentNodesData = parseParentNodeData(reader, object);
    block->m_lightResourceName = reader.readString(object->getTextCodec());

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
    block->m_viewNodeName = reader.readString(object->getTextCodec());
    block->m_parentNodesData = parseParentNodeData(reader, object);
    block->m_viewResourceName = reader.readString(object->getTextCodec());
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

            item.m_textureName = reader.readString(object->getTextCodec());
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
    block->m_meshName = reader.readString(object->getTextCodec());
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
    block->m_boneDescription = reader.readBoneDescriptions(object->getTextCodec(), block->m_boneCount);

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
    block->m_meshName = reader.readString(object->getTextCodec());
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
    block->m_meshName = reader.readString(object->getTextCodec());
    block->m_chainIndex = reader.readU32();

    block->m_startResolution = reader.readU32();
    block->m_endResolution = reader.readU32();

    if (block->m_endResolution < block->m_startResolution)
    {
        // Error - bad resolution
        return nullptr;
    }

    block->m_resolutionUpdateCount = block->m_endResolution - block->m_startResolution;

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

        // TODO: Fixme

        block->m_resolutionUpdates.emplace_back(std::move(updateItem));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LineSetDeclarationBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D_Parser* object)
{
    PDF3D_U3D_LineSetDeclarationBlock* block = new PDF3D_U3D_LineSetDeclarationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_lineSetName = reader.readString(object->getTextCodec());
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
    block->m_lineSetName = reader.readString(object->getTextCodec());
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
    block->m_pointSetName = reader.readString(object->getTextCodec());
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
    block->m_boneDescription = reader.readBoneDescriptions(object->getTextCodec(), block->m_boneCount);

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
    block->m_pointSetName = reader.readString(object->getTextCodec());
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
    block->m_modifierName = reader.readString(object->getTextCodec());
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
    block->m_modifierName = reader.readString(object->getTextCodec());
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
    block->m_modifierName = reader.readString(object->getTextCodec());
    block->m_chainIndex = reader.readU32();
    block->m_attributes = reader.readU32();
    block->m_timescale = reader.readF32();

    uint32_t motionCount = reader.readU32();
    for (uint32_t i = 0; i < motionCount; ++i)
    {
        MotionInformation item;
        item.motionName = reader.readString(object->getTextCodec());
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
    block->m_modifierName = reader.readString(object->getTextCodec());
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
    block->m_modifierName = reader.readString(object->getTextCodec());
    block->m_chainIndex = reader.readU32();
    block->m_attributes = reader.readU32();

    uint32_t shaderListCount = reader.readU32();
    for (uint32_t i = 0; i < shaderListCount; ++i)
    {
        QStringList shaderNames;

        uint32_t shaderCount = reader.readU32();
        for (uint32_t i = 0; i < shaderCount; ++i)
        {
            shaderNames << reader.readString(object->getTextCodec());
        }

        block->m_shaderLists.emplace_back(std::move(shaderNames));
    }

    block->parseMetadata(metaData, object);
    return pointer;
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
    block->m_modifierName = reader.readString(object->getTextCodec());
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
    block->m_resourceName = reader.readString(object->getTextCodec());
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
    block->m_resourceName = reader.readString(object->getTextCodec());

    const uint32_t passCount = reader.readU32();
    for (uint32_t i = 0; i < passCount; ++i)
    {
        Pass pass;

        pass.rootNodeName = reader.readString(object->getTextCodec());
        pass.renderAttributes = reader.readU32();
        pass.fogMode = reader.readU32();
        reader.readFloats32(pass.color);
        pass.fogNear = reader.readF32();
        pass.fogFar = reader.readF32();

        block->m_renderPasses.emplace_back(std::move(pass));
    }

    block->parseMetadata(metaData, object);
    return pointer;
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
    block->m_resourceName = reader.readString(object->getTextCodec());
    block->m_attributes = reader.readU32();
    block->m_alphaTestReference = reader.readF32();
    block->m_alphaTestFunction = reader.readU32();
    block->m_colorBlendFunction = reader.readU32();
    block->m_renderPassEnabled = reader.readU32();
    block->m_shaderChannels = reader.readU32();
    block->m_alphaTextureChannels = reader.readU32();
    block->m_materialName = reader.readString(object->getTextCodec());

    uint32_t value = block->m_shaderChannels & 0xFF;
    uint32_t activeChannelCount = 0;
    for (; value; ++activeChannelCount)
    {
        value = value & (value - 1);
    }

    for (uint32_t i = 0; i < activeChannelCount; ++i)
    {
        TextureInfo info;

        info.textureName = reader.readString(object->getTextCodec());
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
    block->m_resourceName = reader.readString(object->getTextCodec());
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
    block->m_resourceName = reader.readString(object->getTextCodec());
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
            format.imageURLs = reader.readStringList(format.imageURLCount, object->getTextCodec());
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
    block->m_resourceName = reader.readString(object->getTextCodec());
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

}   // namespace u3d

}   // namespace pdf
