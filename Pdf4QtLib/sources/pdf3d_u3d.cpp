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

#include <array>

namespace pdf
{

namespace u3d
{

PDF3D_U3D_DataReader::PDF3D_U3D_DataReader(QByteArray data, bool isCompressed) :
    m_data(std::move(data)),
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

            if (it != contextData.symbols.cend())
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
    // Jakub Melka: 106 is default value for U3D strings
    m_textCodec = QTextCodec::codecForMib(106);
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D::parseBlockWithDeclaration(PDF3D_U3D_DataReader& reader)
{
    uint32_t blockType = reader.readU32();
    uint32_t dataSize = reader.readU32();
    uint32_t metaDataSize = reader.readU32();

    // Read block data
    QByteArray blockData = reader.readByteArray(dataSize);
    reader.skipBytes(getBlockPadding(dataSize));

    // Read block metadata
    QByteArray metaData = reader.readByteArray(metaDataSize);
    reader.skipBytes(getBlockPadding(metaDataSize));

    return parseBlock(blockType, blockData, metaData);
}

PDF3D_U3D PDF3D_U3D::parse(QByteArray data)
{
    PDF3D_U3D object;
    PDF3D_U3D_DataReader reader(data, true);

    while (!reader.isAtEnd())
    {
        if (auto block = object.parseBlockWithDeclaration(reader))
        {
            object.m_blocks.emplace_back(std::move(block));
        }
    }

    return object;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D::parseBlock(uint32_t blockType,
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
                    m_fileBlock = fileBlock;
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

        case PDF3D_U3D_PriorityUpdateBlock::ID:
            return PDF3D_U3D_PriorityUpdateBlock::parse(data, metaData, this);

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

        default:
            break;
    }

    if (blockType >= PDF3D_U3D_NewObjectBlock::ID_MIN && blockType <= PDF3D_U3D_NewObjectBlock::ID_MAX)
    {
        return PDF3D_U3D_NewObjectBlock::parse(data, metaData, this);
    }

    return PDF3D_U3D_AbstractBlockPtr();
}

uint32_t PDF3D_U3D::getBlockPadding(uint32_t blockSize)
{
    uint32_t extraBytes = blockSize % 4;

    if (extraBytes > 0)
    {
        return 4 - extraBytes;
    }

    return 0;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_FileBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
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

    block->parseMetadata(metaData);

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

void PDF3D_U3D_AbstractBlock::parseMetadata(QByteArray metaData)
{
    if (metaData.isEmpty())
    {
        return;
    }

    // TODO: MelkaJ - parse meta data
    Q_ASSERT(false);
}

PDF3D_U3D_AbstractBlock::ParentNodesData PDF3D_U3D_AbstractBlock::parseParentNodeData(PDF3D_U3D_DataReader& reader, PDF3D_U3D* object)
{
    ParentNodesData result;

    uint32_t parentNodeCount = reader.readU32();
    result.reserve(parentNodeCount);

    for (uint32_t i = 0; i < parentNodeCount; ++i)
    {
        ParentNodeData data;
        data.parentNodeName = reader.readString(object->getTextCodec());

        std::array<float, 16> transformMatrix = { };
        reader.readFloats32(transformMatrix);

        QMatrix4x4 matrix(transformMatrix.data());
        data.transformMatrix = matrix.transposed();
        result.emplace_back(std::move(data));
    }

    return result;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_FileReferenceBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
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

    block->parseMetadata(metaData);

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ModifierChainBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
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
        if (auto parsedBlock = object->parseBlockWithDeclaration(reader))
        {
            block->m_modifierDeclarationBlocks.emplace_back(std::move(parsedBlock));
        }
    }

    block->parseMetadata(metaData);
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

const std::vector<PDF3D_U3D_AbstractBlockPtr>& PDF3D_U3D_ModifierChainBlock::getModifierDeclarationBlocks() const
{
    return m_modifierDeclarationBlocks;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_PriorityUpdateBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_PriorityUpdateBlock* block = new PDF3D_U3D_PriorityUpdateBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_newPriority = reader.readU32();

    block->parseMetadata(metaData);
    return pointer;
}

uint32_t PDF3D_U3D_PriorityUpdateBlock::getNewPriority() const
{
    return m_newPriority;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_NewObjectTypeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
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

    block->parseMetadata(metaData);
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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_NewObjectBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_NewObjectBlock* block = new PDF3D_U3D_NewObjectBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_objectName = reader.readString(object->getTextCodec());
    block->m_chainIndex = reader.readU32();
    block->m_data = reader.readRemainingData();

    block->parseMetadata(metaData);
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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_GroupNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_GroupNodeBlock* block = new PDF3D_U3D_GroupNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_groupNodeName = reader.readString(object->getTextCodec());
    block->m_parentNodesData = parseParentNodeData(reader, object);

    block->parseMetadata(metaData);
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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ModelNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_ModelNodeBlock* block = new PDF3D_U3D_ModelNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_modelNodeName = reader.readString(object->getTextCodec());
    block->m_parentNodesData = parseParentNodeData(reader, object);
    block->m_modelResourceName = reader.readString(object->getTextCodec());
    block->m_modelVisibility = reader.readU32();

    block->parseMetadata(metaData);
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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LightNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_LightNodeBlock* block = new PDF3D_U3D_LightNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed());

    // Read the data
    block->m_lightNodeName = reader.readString(object->getTextCodec());
    block->m_parentNodesData = parseParentNodeData(reader, object);
    block->m_lightResourceName = reader.readString(object->getTextCodec());

    block->parseMetadata(metaData);
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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ViewNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
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

    block->parseMetadata(metaData);
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

}   // namespace u3d

}   // namespace pdf
