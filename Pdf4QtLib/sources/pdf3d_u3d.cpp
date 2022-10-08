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

PDF3D_U3D_DataReader::PDF3D_U3D_DataReader(QByteArray data, bool isCompressed, PDF3D_U3D_ContextManager* contextManager) :
    m_data(std::move(data)),
    m_contextManager(contextManager),
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
    if (m_isCompressed && m_contextManager->isContextCompressed(context))
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
            m_contextManager->addSymbol(context, value + 1);
            return value;
        }
    }

    return readU8();
}

uint16_t PDF3D_U3D_DataReader::readCompressedU16(uint32_t context)
{
    if (m_isCompressed && m_contextManager->isContextCompressed(context))
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
            m_contextManager->addSymbol(context, value + 1);
            return value;
        }
    }

    return readU16();
}

uint32_t PDF3D_U3D_DataReader::readCompressedU32(uint32_t context)
{
    if (m_isCompressed && m_contextManager->isContextCompressed(context))
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
            m_contextManager->addSymbol(context, value + 1);
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

    const uint32_t totalCumFreq = m_contextManager->getTotalSymbolFrequency(context);
    const uint32_t range = (m_high + 1) - m_low;
    const uint32_t codeCumFreq = ((totalCumFreq) * (1 + m_code - m_low) - 1) / range;
    const uint32_t value = m_contextManager->getSymbolFromFrequency(context, codeCumFreq);
    const uint32_t valueCumFreq = m_contextManager->getCumulativeSymbolFrequency(context, value);
    const uint32_t valueFreq = m_contextManager->getSymbolFrequency(context, value);

    uint32_t low = m_low;
    uint32_t high = m_high;

    high = low - 1 + range * (valueCumFreq + valueFreq) / totalCumFreq;
    low = low + range * valueCumFreq / totalCumFreq;
    m_contextManager->addSymbol(context, value);

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

    auto block = parseBlock(blockType, blockData, metaData);

    LoadBlockInfo info;
    info.typeName = QByteArray::fromRawData(reinterpret_cast<const char*>(&blockType), sizeof(decltype(blockType))).toHex();
    info.success = block != nullptr;
    info.blockType = blockType;
    info.dataSize = dataSize;
    info.metaDataSize = metaDataSize;
    info.block = block;
    m_allBlocks.push_back(info);

    return block;
}

PDF3D_U3D_Block_Data PDF3D_U3D::readBlockData(PDF3D_U3D_DataReader& reader)
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

    data.blockType = blockType;
    data.blockData = std::move(blockData);
    data.metaData = std::move(metaData);

    return data;
}

PDF3D_U3D PDF3D_U3D::parse(QByteArray data)
{
    PDF3D_U3D object;

    // Why to use shared ptr and weak ptr in the object?
    // In case exception is thrown in the parser, context
    // manager will be automatically released.
    std::shared_ptr<PDF3D_U3D_ContextManager> contextManager = std::make_shared<PDF3D_U3D_ContextManager>();
    object.setContextManager(contextManager);
    PDF3D_U3D_DataReader reader(data, true, contextManager.get());

    object.m_priority = 0;

    QStringList errors;
    PDF3D_U3D_DecoderLists decoderLists;

    while (!reader.isAtEnd())
    {
        PDF3D_U3D_Block_Data blockData = readBlockData(reader);
        processBlock(blockData, decoderLists, errors);
    }

    return object;
}

void PDF3D_U3D::processBlock(PDF3D_U3D& object,
                             const PDF3D_U3D_Block_Data& blockData,
                             PDF3D_U3D_DecoderLists& decoderLists,
                             QStringList& errors,
                             PDF3D_U3D_DecoderLists::EPalette palette)
{
    switch (blockData.blockType)
    {
        case PDF3D_U3D_FileBlock::ID:
        {
            // Parse file block
            object.m_fileBlock = object.parseBlock(blockData);
            break;
        }

        case PDF3D_U3D_PriorityUpdateBlock::ID:
        {
            // Parse priority update block
            auto block = object.parseBlock(blockData);
            const PDF3D_U3D_PriorityUpdateBlock* priorityUpdateBlock = dynamic_cast<const PDF3D_U3D_PriorityUpdateBlock*>(block.get());
            object.m_priority = priorityUpdateBlock->getNewPriority();
            break;
        }

        case PDF3D_U3D_NewObjectTypeBlock::ID:
        case PDF3D_U3D_FileReferenceBlock::ID:
            // Skip this block, we do not handle these type of blocks,
            // just read it... to check errors.
            object.parseBlock(blockData);
            break;

        case PDF3D_U3D_ModifierChainBlock::ID:
        {
            // Add decoder list for modifier chain block
            auto block = object.parseBlock(blockData);
            const PDF3D_U3D_ModifierChainBlock* chainBlock = dynamic_cast<const PDF3D_U3D_ModifierChainBlock*>(block.get());

            PDF3D_U3D_DecoderList decoderList;
            decoderList.setName(chainBlock->getModifierChainName());

            for (const auto& block : chainBlock->getModifierDeclarationBlocks())
            {
                decoderList.createChain(block);
            }

            decoderLists.addDecoderList(std::move(decoderList));
            break;
        }

        default:
        {
            PDF3D_U3D_DataReader blockReader(blockData.blockData, object.isCompressed(), object.getContextManager());
            QString blockName = blockReader.readString(object.getTextCodec());

            if (isContinuationBlock(blockData.blockType))
            {
                uint32_t chainIndex = 0;
                if (blockData.blockType != 0xFFFFFF5C)
                {
                    chainIndex = blockReader.readU32();
                }

                if (!decoderLists.addContinuationBlock(blockName, chainIndex, blockData))
                {
                    errors << QString("Failed to add continuation block '%1'").arg(blockName);
                }
            }
            else
            {
                PDF3D_U3D_DecoderList decoderList;
                decoderList.createChain(blockData);
                decoderLists.addDecoderList(std::move(decoderList));
            }

            break;
        }
    }
}

template<typename T>
const T* PDF3D_U3D::getBlock(const QString& name) const
{
    for (const auto& block : m_allBlocks)
    {
        if (auto typedBlock = dynamic_cast<const T*>(block.block.data()))
        {
            if (typedBlock->getName() == name)
            {
                return typedBlock;
            }
        }
    }

    return nullptr;
}

const PDF3D_U3D_CLODMeshDeclarationBlock* PDF3D_U3D::getCLODMeshDeclarationBlock(const QString& meshName) const
{
    return getBlock<PDF3D_U3D_CLODMeshDeclarationBlock>(meshName);
}

const PDF3D_U3D_LineSetDeclarationBlock* PDF3D_U3D::getLineSetDeclarationBlock(const QString& lineSetName) const
{
    return getBlock<PDF3D_U3D_LineSetDeclarationBlock>(lineSetName);
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

        case PDF3D_U3D_CLODMeshDeclarationBlock::ID:
            return PDF3D_U3D_CLODMeshDeclarationBlock::parse(data, metaData, this);

        case PDF3D_U3D_CLODBaseMeshContinuationBlock::ID:
            return PDF3D_U3D_CLODBaseMeshContinuationBlock::parse(data, metaData, this);

        case PDF3D_U3D_LineSetDeclarationBlock::ID:
            return PDF3D_U3D_LineSetDeclarationBlock::parse(data, metaData, this);

        case PDF3D_U3D_LineSetContinuationBlock::ID:
            return PDF3D_U3D_LineSetContinuationBlock::parse(data, metaData, this);

        default:
            break;
    }

    if (blockType >= PDF3D_U3D_NewObjectBlock::ID_MIN && blockType <= PDF3D_U3D_NewObjectBlock::ID_MAX)
    {
        return PDF3D_U3D_NewObjectBlock::parse(data, metaData, this);
    }

    return PDF3D_U3D_AbstractBlockPtr();
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D::parseBlock(const PDF3D_U3D_Block_Data& data)
{
    return parseBlock(data.blockType, data.blockData, data.metaData);
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

bool PDF3D_U3D::isContinuationBlock(uint32_t blockType)
{
    switch (blockType)
    {
        case 0xFFFFFF3B:
        case 0xFFFFFF3C:
        case 0xFFFFFF3E:
        case 0xFFFFFF3F:
        case 0xFFFFFF5C:
            return true;

        default:
            break;
    }

    return false;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_FileBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_FileBlock* block = new PDF3D_U3D_FileBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

void PDF3D_U3D_AbstractBlock::parseMetadata(QByteArray metaData, PDF3D_U3D* object)
{
    if (metaData.isEmpty())
    {
        return;
    }

    PDF3D_U3D_DataReader reader(metaData, object->isCompressed(), object->getContextManager());

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

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ModifierChainBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_ModifierChainBlock* block = new PDF3D_U3D_ModifierChainBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_PriorityUpdateBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_PriorityUpdateBlock* block = new PDF3D_U3D_PriorityUpdateBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

    // Read the data
    block->m_newPriority = reader.readU32();

    block->parseMetadata(metaData, object);
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

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_NewObjectBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_NewObjectBlock* block = new PDF3D_U3D_NewObjectBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_GroupNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_GroupNodeBlock* block = new PDF3D_U3D_GroupNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ModelNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_ModelNodeBlock* block = new PDF3D_U3D_ModelNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LightNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_LightNodeBlock* block = new PDF3D_U3D_LightNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_ViewNodeBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_ViewNodeBlock* block = new PDF3D_U3D_ViewNodeBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_CLODMeshDeclarationBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_CLODMeshDeclarationBlock* block = new PDF3D_U3D_CLODMeshDeclarationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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
    for (uint32_t i = 0; i < block->m_boneCount; ++i)
    {
        BoneDescription item;

        item.boneName = reader.readString(object->getTextCodec());
        item.parentBoneName = reader.readString(object->getTextCodec());
        item.boneAttributes = reader.readU32();
        item.boneLength = reader.readF32();
        reader.readFloats32(item.boneDisplacement);
        reader.readFloats32(item.boneOrientation);

        if (item.isLinkPresent())
        {
            item.boneLinkCount = reader.readU32();
            item.boneLinkLength = reader.readF32();
        }

        if (item.isJointPresent())
        {
            item.startJoint.jointCenterU = reader.readF32();
            item.startJoint.jointCenterV = reader.readF32();
            item.startJoint.jointScaleU = reader.readF32();
            item.startJoint.jointScaleV = reader.readF32();

            item.endJoint.jointCenterU = reader.readF32();
            item.endJoint.jointCenterV = reader.readF32();
            item.endJoint.jointScaleU = reader.readF32();
            item.endJoint.jointScaleV = reader.readF32();
        }

        item.rotationConstraintMax[0] = reader.readF32();
        item.rotationConstraintMin[0] = reader.readF32();
        item.rotationConstraintMax[1] = reader.readF32();
        item.rotationConstraintMin[1] = reader.readF32();
        item.rotationConstraintMax[2] = reader.readF32();
        item.rotationConstraintMin[2] = reader.readF32();

        block->m_boneDescription.emplace_back(std::move(item));
    }

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

const std::vector<PDF3D_U3D_CLODMeshDeclarationBlock::BoneDescription>& PDF3D_U3D_CLODMeshDeclarationBlock::getBoneDescription() const
{
    return m_boneDescription;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_CLODBaseMeshContinuationBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_CLODBaseMeshContinuationBlock* block = new PDF3D_U3D_CLODBaseMeshContinuationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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
    if (auto declarationBlock = object->getCLODMeshDeclarationBlock(block->m_meshName))
    {
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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_CLODProgressiveMeshContinuationBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_CLODProgressiveMeshContinuationBlock* block = new PDF3D_U3D_CLODProgressiveMeshContinuationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

        block->m_resolutionUpdates.emplace_back(std::move(updateItem));
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LineSetDeclarationBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_LineSetDeclarationBlock* block = new PDF3D_U3D_LineSetDeclarationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

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

PDF3D_U3D_AbstractBlockPtr PDF3D_U3D_LineSetContinuationBlock::parse(QByteArray data, QByteArray metaData, PDF3D_U3D* object)
{
    PDF3D_U3D_LineSetContinuationBlock* block = new PDF3D_U3D_LineSetContinuationBlock();
    PDF3D_U3D_AbstractBlockPtr pointer(block);

    PDF3D_U3D_DataReader reader(data, object->isCompressed(), object->getContextManager());

    // Read the data
    block->m_lineSetName = reader.readString(object->getTextCodec());
    block->m_chainIndex = reader.readU32();

    block->m_startResolution = reader.readU32();
    block->m_endResolution = reader.readU32();

    if (auto declarationBlock = object->getLineSetDeclarationBlock(block->m_lineSetName))
    {
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
    }
    else
    {
        return nullptr;
    }

    block->parseMetadata(metaData, object);
    return pointer;
}

const std::vector<PDF3D_U3D_LineSetContinuationBlock::UpdateItem>& PDF3D_U3D_LineSetContinuationBlock::getUpdateItems() const
{
    return m_updateItems;
}

const QString& PDF3D_U3D_DecoderList::getName() const
{
    return m_name;
}

void PDF3D_U3D_DecoderList::setName(const QString& newName)
{
    m_name = newName;
}

const std::vector<PDF3D_U3D_DecorderList_ChainItem>& PDF3D_U3D_DecoderList::getChains() const
{
    return m_chains;
}

void PDF3D_U3D_DecoderList::setChains(const std::vector<PDF3D_U3D_DecorderList_ChainItem>& newChains)
{
    m_chains = newChains;
}

PDF3D_U3D_DecorderList_ChainItem* PDF3D_U3D_DecoderList::getChain(size_t index)
{
    if (index < m_chains.size())
    {
        return &m_chains[index];
    }

    return nullptr;
}

void PDF3D_U3D_DecoderList::createChain(const PDF3D_U3D_Block_Data& data)
{
    PDF3D_U3D_DecorderList_ChainItem item;
    item.blocks = { data };
    m_chains.emplace_back(std::move(item));
}

PDF3D_U3D_DecoderList* PDF3D_U3D_DecoderLists::getDecoderList(QString name)
{
    for (PDF3D_U3D_DecoderList& decoderList : m_decoderLists)
    {
        if (decoderList.getName() == name)
        {
            return &decoderList;
        }
    }

    return nullptr;
}

bool PDF3D_U3D_DecoderLists::addContinuationBlock(QString name, uint32_t chainIndex, const PDF3D_U3D_Block_Data& data)
{
    if (PDF3D_U3D_DecoderList* decoderList = getDecoderList(name))
    {
        if (PDF3D_U3D_DecorderList_ChainItem* chain = decoderList->getChain(chainIndex))
        {
            chain->blocks.push_back(data);
            return true;
        }
    }

    return false;
}

}   // namespace u3d

}   // namespace pdf
