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

#include <array>

namespace pdf
{

namespace u3d
{

PDF3D_U3D_DataReader::PDF3D_U3D_DataReader(QByteArray data) :
    m_data(std::move(data)),
    m_high(0x0000FFFF),
    m_low(0),
    m_underflow(0),
    m_code(0)
{

}

PDF3D_U3D_DataReader::~PDF3D_U3D_DataReader()
{

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

int32_t PDF3D_U3D_DataReader::readI32()
{
    return static_cast<int32_t>(readU32());
}

float PDF3D_U3D_DataReader::readF32()
{
    const uint32_t value = readU32();
    return static_cast<float>(value);
}

uint8_t PDF3D_U3D_DataReader::readCompressedU8(uint32_t context)
{
    if (m_contextManager.isContextCompressed(context))
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
    if (m_contextManager.isContextCompressed(context))
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
    if (m_contextManager.isContextCompressed(context))
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
        return (data >> bitPosition) & 1;
    }

    return 0;
}

uint32_t PDF3D_U3D_DataReader::read15Bits()
{
    std::array<uint32_t, 15> data;

    for (uint32_t& value : data)
    {
        value = readBit();
    }

    std::reverse(data.begin(), data.end());

    uint32_t result = 0;
    for (uint32_t value : data)
    {
        result = (result << 1) | value;
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

PDF3D_U3D PDF3D_U3D::parse(QByteArray data)
{
    return PDF3D_U3D();
}

}   // namespace u3d

}   // namespace pdf
