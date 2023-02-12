//    Copyright (C) 2023 Jakub Melka
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

#include "pdf3d_prc.h"
#include "pdfutils.h"

namespace pdf
{

namespace prc
{

union FloatAsBytesStructure
{
    float floatValue;
    uint32_t intValue;
};

class HuffmanDecoder
{
public:
    HuffmanDecoder(std::vector<UnsignedInteger> data, UnsignedInteger numberOfBitsForValues);

    Characters decode();

private:
    struct Leaf
    {
        Character value = 0;
        UnsignedInteger code = 0;
        UnsignedInteger codeBitLength = 0;
    };

    UnsignedInteger m_numberOfBitsForValues = 0;
    std::vector<Leaf> m_leafs;
    PDFBitReader m_reader;
    QByteArray m_data;
};

HuffmanDecoder::HuffmanDecoder(std::vector<UnsignedInteger> data, UnsignedInteger numberOfBitsForValues) :
    m_reader(&m_data, 8),
    m_numberOfBitsForValues(numberOfBitsForValues)
{
    PDFBitWriter writer(32);
    for (UnsignedInteger value : data)
    {
        writer.write(value);
    }

    writer.finishLine();
    m_data = writer.takeByteArray();
}

Characters HuffmanDecoder::decode()
{
    Charactersresult;

    UnsignedInteger numberOfLeaves = m_reader.read(m_numberOfBitsForValues);
    UnsignedInteger maxCodeLength = m_reader.read(8);

    for (UnsignedInteger i = 0; i < numberOfLeaves; ++i)
    {
        Leaf leaf;
        leaf.value = m_reader.read(m_numberOfBitsForValues);
        leaf.codeBitLength = m_reader.read(maxCodeLength);
        leaf.code = m_reader.read(leaf.codeBitLength);
        m_leafs.push_back(leaf);
    }

    UnsignedInteger size = m_reader.read(32);
    result.reserve(size);

    for (UnsignedInteger i = 0; i < size; ++i)
    {
        const Leaf* selectedLeaf = nullptr;

        UnsignedInteger code = 0;
        UnsignedInteger codeBits = 0;

        while (!m_reader.isAtEnd() && !selectedLeaf)
        {
            code += 1 << codeBits;
            ++codeBits;

            for (const Leaf& leaf : m_leafs)
            {
                if (leaf.code == code && leaf.codeBitLength == codeBits)
                {
                    selectedLeaf = &leaf;
                    break;
                }
            }
        }

        if (selectedLeaf)
        {
            result.push_back(selectedLeaf->value);
        }
    }
    return result;
}

class PRCReader
{
public:

    UnsignedInteger read_UnsignedInteger();
    Integer read_Integer();
    QByteArray read_String();
    float read_FloatAsBytes();
    Characters read_CharacterArray(UnsignedInteger numberOfBitsForValues, Boolean writeCompressStrategy);

private:
    PDFBitReader m_reader;
};

class PRCWriter
{
public:

    void write_UnsignedInteger(UnsignedInteger value);
    void write_Integer(Integer value);
    void write_String(const QByteArray& string);
    void write_FloatAsBytes(float value);

private:
    PDFBitWriter m_writer;
};

UnsignedInteger PRCReader::read_UnsignedInteger()
{
    uint32_t result = 0;
    uint32_t bitShift = 0;

    while (m_reader.read(1))
    {
        uint32_t value = m_reader.read(8);
        result += (value << bitShift);
        bitShift += 8;
    }

    return result;
}

Integer PRCReader::read_Integer()
{
    uint32_t result = 0;
    uint32_t bitShift = 0;

    while (m_reader.read(1))
    {
        uint32_t value = m_reader.read(8);
        result += (value << bitShift);
        bitShift += 8;
    }

    return static_cast<int32_t>(result);
}

QByteArray PRCReader::read_String()
{
    QByteArray string;
    if (m_reader.read(1) == 0)
    {
        return string;
    }

    UnsignedInteger size = read_UnsignedInteger();
    for (UnsignedInteger i = 0; i < size; ++i)
    {
        string.push_back(read_Character());
    }
}

float PRCReader::read_FloatAsBytes()
{
    FloatAsBytesStructure valueStructure;

    uint32_t v1 = m_reader.read(8);
    uint32_t v2 = m_reader.read(8);
    uint32_t v3 = m_reader.read(8);
    uint32_t v4 = m_reader.read(8);
    uint32_t result = (v4 << 24) + (v3 << 16) + (v2 << 8) + v1;

    valueStructure.intValue = result;
    return valueStructure.floatValue;
}

Characters PRCReader::read_CharacterArray(UnsignedInteger numberOfBitsForValues,
                                          Boolean writeCompressStrategy)
{
    Characters result;

    bool isCompressed = true;
    if (writeCompressStrategy)
    {
        isCompressed = m_reader.read(1) != 0;
    }

    if (isCompressed)
    {
        UnsignedInteger huffmanArraySize = read_UnsignedInteger();
        std::vector<UnsignedInteger> huffmanArray;
        for (UnsignedInteger i = 0; i < huffmanArraySize; ++i)
        {
            huffmanArray.push_back(read_UncompressedUnsignedInteger());
        }
        UnsignedInteger numberOfBitsUsedInLastInteger = read_UnsignedInteger();
        Q_UNUSED(numberOfBitsForValues);

        HuffmanDecoder decoder(std::move(huffmanArray), numberOfBitsForValues);
        result = decoder.decode();
    }
    else
    {
        UnsignedInteger arraySize = read_UnsignedInteger();
        for (UnsignedInteger i = 0; i < arraySize; ++i)
        {
            result.push_back(read_Character());
        }
    }

    return result;
}

void PRCWriter::write_UnsignedInteger(UnsignedInteger value)
{
    while (true)
    {
        if (value == 0)
        {
            m_writer.write(0, 1);
            return;
        }
        m_writer.write(1, 1);
        m_writer.write(value & 0xFF, 8);
        value = value >> 8;
    }
}

void PRCWriter::write_Integer(Integer value)
{
    if (value == 0)
    {
        m_writer.write(0, 1);
        return;
    }

    while (true)
    {
        Integer loc = value & 0xFF;
        m_writer.write(1, 1);
        m_writer.write(loc, 8);
        value = value >> 8;
        if (((value == 0) && ((loc & 0x80) == 0)) || // Positive integer all bits written?
            ((value == -1) && ((loc & 0x80) != 0)))  // Negative integer, all bits written?
        {
            m_writer.write(0, 1);
            return;
        }
    }
}

void PRCWriter::write_String(const QByteArray& string)
{
    if (string.isEmpty())
    {
        m_writer.write(0, 1);
        return;
    }

    m_writer.write(1, 1);
    UnsignedInteger stringSize = string.size();
    write_UnsignedInteger(stringSize);
    for (UnsignedInteger i = 0; i < stringSize; ++i)
    {
        write_Character(string[i]);
    }
}

void PRCWriter::write_FloatAsBytes(float value)
{
    FloatAsBytesStructure valueStructure;
    valueStructure.floatValue = value;

    uint32_t unsignedIntegerValue = valueStructure.intValue;
    m_writer.write(unsignedIntegerValue & 0xFF, 8);
    unsignedIntegerValue >>= 8;
    m_writer.write(unsignedIntegerValue & 0xFF, 8);
    unsignedIntegerValue >>= 8;
    m_writer.write(unsignedIntegerValue & 0xFF, 8);
    unsignedIntegerValue >>= 8;
    m_writer.write(unsignedIntegerValue & 0xFF, 8);
}

/* START GENERATED CODE */

/* END GENERATED CODE */

}   // namespace prc

}   // namespace pdf
