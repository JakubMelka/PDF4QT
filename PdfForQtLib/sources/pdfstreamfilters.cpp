//    Copyright (C) 2018 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfstreamfilters.h"
#include "pdfdocument.h"
#include "pdfparser.h"

#include <QtEndian>

namespace pdf
{

QByteArray PDFAsciiHexDecodeFilter::apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const
{
    Q_UNUSED(document);
    Q_UNUSED(parameters);

    const int indexOfEnd = data.indexOf('>');
    const int size = (indexOfEnd == -1) ? data.size() : indexOfEnd;

    if (size % 2 == 1)
    {
        // We must add trailing zero to the buffer
        QByteArray temporaryData(data.constData(), size);
        temporaryData.push_back('0');
        return QByteArray::fromHex(temporaryData);
    }
    else if (size == data.size())
    {
        // We do this, because we do not want to allocate unnecessary buffer for this case.
        // This case should be common.
        return QByteArray::fromHex(data);
    }

    return QByteArray::fromHex(QByteArray::fromRawData(data.constData(), size));
}

QByteArray PDFAscii85DecodeFilter::apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const
{
    Q_UNUSED(document);
    Q_UNUSED(parameters);

    const unsigned char* dataBegin = reinterpret_cast<const unsigned char*>(data.constData());
    const unsigned char* dataEnd = reinterpret_cast<const unsigned char*>(data.constData() + data.size());

    const unsigned char* it = dataBegin;
    const constexpr uint32_t STREAM_END = 0xFFFFFFFF;

    auto getChar = [&it, dataEnd, STREAM_END]() -> uint32_t
    {
        // Skip whitespace characters
        while (it != dataEnd && PDFLexicalAnalyzer::isWhitespace(*it))
        {
            ++it;
        }

        if (it == dataEnd || (*it == '~'))
        {
            return STREAM_END;
        }

        return *it++;
    };

    QByteArray result;
    result.reserve(data.size() * 4 / 5);

    while (true)
    {
        const uint32_t scannedChar = getChar();
        if (scannedChar == STREAM_END)
        {
            break;
        }
        else if (scannedChar == 'z')
        {
            result.append(4, static_cast<char>(0));
        }
        else
        {
            // Scan all 5 characters, some of then can be equal to STREAM_END constant. We will
            // treat all these characters as last character.
            std::array<uint32_t, 5> scannedChars;
            scannedChars.fill(84);
            scannedChars[0] = scannedChar - 33;
            int validBytes = 0;
            for (auto it = std::next(scannedChars.begin()); it != scannedChars.end(); ++it)
            {
                uint32_t character = getChar();
                if (character == STREAM_END)
                {
                    break;
                }
                *it = character - 33;
                ++validBytes;
            }

            // Decode bytes using 85 base
            uint32_t decodedBytesPacked = 0;
            for (const uint32_t value : scannedChars)
            {
                decodedBytesPacked = decodedBytesPacked * 85 + value;
            }

            // Decode bytes into byte array
            std::array<char, 4> decodedBytesUnpacked;
            decodedBytesUnpacked.fill(0);
            for (auto byteIt = decodedBytesUnpacked.rbegin(); byteIt != decodedBytesUnpacked.rend(); ++byteIt)
            {
                *byteIt = static_cast<char>(decodedBytesPacked & 0xFF);
                decodedBytesPacked = decodedBytesPacked >> 8;
            }

            Q_ASSERT(validBytes <= decodedBytesUnpacked.size());
            for (int i = 0; i < validBytes; ++i)
            {
                result.push_back(decodedBytesUnpacked[i]);
            }
        }
    }

    return result;
}

class PDFLzwStreamDecoder
{
public:
    explicit PDFLzwStreamDecoder(const QByteArray& inputByteArray, uint32_t early);

    QByteArray decompress();

private:
    static constexpr const uint32_t CODE_TABLE_RESET = 256;
    static constexpr const uint32_t CODE_END_OF_STREAM = 257;

    // Maximal code size is 12 bits. so we can have 2^12 = 4096 items
    // in the table (some items are unused, for example 256, 257). We also
    // need to initialize items under code 256, because we treat them specially,
    // they are not initialized in the decompress.
    static constexpr const uint32_t TABLE_SIZE = 4096;

    /// Clears the input data table
    void clearTable();

    /// Returns a newly scanned code
    uint32_t getCode();

    struct TableItem
    {
        uint32_t previous = TABLE_SIZE;
        char character = 0;
    };

    std::array<TableItem, TABLE_SIZE> m_table;
    std::array<char, TABLE_SIZE> m_sequence;

    uint32_t m_nextCode;        ///< Next code value (to be written into the table)
    uint32_t m_nextBits;        ///< Number of bits of the next code
    uint32_t m_early;           ///< Early (see PDF 1.7 Specification, this constant is 0 or 1, based on the dictionary value)
    uint32_t m_inputBuffer;     ///< Input buffer, containing bits, which were read from the input byte array
    uint32_t m_inputBits;       ///< Number of bits in the input buffer.
    std::array<char, TABLE_SIZE>::iterator m_currentSequenceEnd;
    bool m_first;               ///< Are we reading from stream for first time after the reset
    char m_newCharacter;        ///< New character to be written
    int m_position;             ///< Position in the input array
    const QByteArray& m_inputByteArray;
};

PDFLzwStreamDecoder::PDFLzwStreamDecoder(const QByteArray& inputByteArray, uint32_t early) :
    m_table(),
    m_sequence(),
    m_nextCode(0),
    m_nextBits(0),
    m_early(early),
    m_inputBuffer(0),
    m_inputBits(0),
    m_currentSequenceEnd(m_sequence.begin()),
    m_first(false),
    m_newCharacter(0),
    m_position(0),
    m_inputByteArray(inputByteArray)
{
    for (size_t i = 0; i < 256; ++i)
    {
        m_table[i].character = static_cast<char>(i);
        m_table[i].previous = TABLE_SIZE;
    }

    clearTable();
}

QByteArray PDFLzwStreamDecoder::decompress()
{
    QByteArray result;

    // Guess output byte array size - assume compress ratio is 2:1
    result.reserve(m_inputByteArray.size() * 2);

    uint32_t previousCode = TABLE_SIZE;
    while (true)
    {
        const uint32_t code = getCode();

        if (code == CODE_END_OF_STREAM)
        {
            // We are at end of stream
            break;
        }
        else if (code == CODE_TABLE_RESET)
        {
            // Just reset the table
            clearTable();
            continue;
        }

        // Normal operation code
        if (code < m_nextCode)
        {
            m_currentSequenceEnd = m_sequence.begin();

            for (uint32_t currentCode = code; currentCode != TABLE_SIZE; currentCode = m_table[currentCode].previous)
            {
                *m_currentSequenceEnd++ = m_table[currentCode].character;
            }

            // We must reverse the sequence, because we stored it in the
            // linked list, which we traversed from last to first item.
            std::reverse(m_sequence.begin(), m_currentSequenceEnd);
        }
        else if (code == m_nextCode)
        {
            // We use the buffer from previous run, just add a new
            // character to the end.
            *m_currentSequenceEnd++ = m_newCharacter;
        }
        else
        {
            // Unknown code
            throw PDFParserException(PDFTranslationContext::tr("Invalid code in the LZW stream."));
        }
        m_newCharacter = m_sequence.front();

        if (m_first)
        {
            m_first = false;
        }
        else
        {
            // Add a new word in the dictionary, if we have it
            if (m_nextCode < TABLE_SIZE)
            {
                m_table[m_nextCode].character = m_newCharacter;
                m_table[m_nextCode].previous = previousCode;
                ++m_nextCode;
            }

            // Change bit size of the code, if it is neccessary
            switch (m_nextCode + m_early)
            {
                case 512:
                    m_nextBits = 10;
                    break;

                case 1024:
                    m_nextBits = 11;
                    break;

                case 2048:
                    m_nextBits = 12;
                    break;

                default:
                    break;
            }
        }

        previousCode = code;

        // Copy the input array to the buffer
        std::copy(m_sequence.begin(), m_currentSequenceEnd, std::back_inserter(result));
    }

    result.shrink_to_fit();
    return result;
}

void PDFLzwStreamDecoder::clearTable()
{
    // We do not clear the m_table array here. It is for performance reasons, we assume
    // the input is correct. We also do not clear the sequence buffer here.

    m_nextCode = 258;
    m_nextBits = 9;
    m_first = true;
    m_newCharacter = 0;
}

uint32_t PDFLzwStreamDecoder::getCode()
{
    while (m_inputBits < m_nextBits)
    {
        // Did we reach end of array?
        if (m_position == m_inputByteArray.size())
        {
            return CODE_END_OF_STREAM;
        }

        m_inputBuffer = (m_inputBuffer << 8) | static_cast<unsigned char>(m_inputByteArray[m_position++]);
        m_inputBits += 8;
    }

    // We must omit bits from left (old ones) and right (newly scanned ones) and
    // read just m_nextBits bits. Mask should omit the old ones and shift (m_inputBits - m_nextBits)
    // should omit the new ones.
    const uint32_t mask = ((1 << m_nextBits) - 1);
    const uint32_t code = (m_inputBuffer >> (m_inputBits - m_nextBits)) & mask;
    m_inputBits -= m_nextBits;
    return code;
}

QByteArray PDFLzwDecodeFilter::apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const
{
    uint32_t early = 1;

    const PDFObject& dereferencedParameters = document->getObject(parameters);
    if (dereferencedParameters.isDictionary())
    {
        const PDFDictionary* dictionary = dereferencedParameters.getDictionary();

        PDFDocumentDataLoaderDecorator loader(document);
        early = loader.readInteger(dictionary->get("EarlyChange"), 1);
    }

    PDFLzwStreamDecoder decoder(data, early);
    return decoder.decompress();
}

QByteArray PDFFlateDecodeFilter::apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const
{
    Q_UNUSED(document);
    Q_UNUSED(parameters);

    uint32_t size = data.size();

    QByteArray dataToUncompress;
    dataToUncompress.resize(sizeof(decltype(size)) + data.size());

    qToBigEndian(size, dataToUncompress.data());
    std::copy(data.cbegin(), data.cend(), std::next(dataToUncompress.begin(), sizeof(decltype(size))));

    return qUncompress(dataToUncompress);
}

QByteArray PDFRunLengthDecodeFilter::apply(const QByteArray& data, const PDFDocument* document, const PDFObject& parameters) const
{
    Q_UNUSED(document);
    Q_UNUSED(parameters);

    QByteArray result;
    result.reserve(data.size() * 2);

    auto itEnd = data.cend();
    for (auto it = data.cbegin(); it != itEnd;)
    {
        const unsigned char current = *it++;
        if (current == 128)
        {
            // End of stream marker
            break;
        }
        else if (current < 128)
        {
            // Copy n + 1 characters from the input array literally (and advance iterators)
            const int count = static_cast<int>(current) + 1;
            std::copy(it, std::next(it, count), std::back_inserter(result));
            std::advance(it, count);
        }
        else if (current > 128)
        {
            // Copy 257 - n copies of single character
            const int count = 257 - current;
            const char toBeCopied = *it++;
            std::fill_n(std::back_inserter(result), count, toBeCopied);
        }
    }

    return result;
}

const PDFStreamFilter* PDFStreamFilterStorage::getFilter(const QByteArray& filterName)
{
    const PDFStreamFilterStorage* instance = getInstance();
    auto it = instance->m_filters.find(filterName);
    if (it != instance->m_filters.cend())
    {
        return it->second.get();
    }

    auto itNameDecoded = instance->m_abbreviations.find(filterName);
    if (itNameDecoded != instance->m_abbreviations.cend())
    {
        return getFilter(itNameDecoded->second);
    }

    return nullptr;
}

PDFStreamFilterStorage::PDFStreamFilterStorage()
{
    // Initialize map with the filters
    m_filters["ASCIIHexDecode"] = std::make_unique<PDFAsciiHexDecodeFilter>();
    m_filters["ASCII85Decode"] = std::make_unique<PDFAscii85DecodeFilter>();
    m_filters["LZWDecode"] = std::make_unique<PDFLzwDecodeFilter>();
    m_filters["FlateDecode"] = std::make_unique<PDFFlateDecodeFilter>();
    m_filters["RunLengthDecode"] = std::make_unique<PDFRunLengthDecodeFilter>();

    m_abbreviations["AHx"] = "ASCIIHexDecode";
    m_abbreviations["A85"] = "ASCII85Decode";
    m_abbreviations["LZW"] = "LZWDecode";
    m_abbreviations["Fl"] = "FlateDecode";
    m_abbreviations["RL"] = "RunLengthDecode";
    m_abbreviations["CCF"] = "CCITFaxDecode";
    m_abbreviations["DCT"] = "DCTDecode";
}

const PDFStreamFilterStorage* PDFStreamFilterStorage::getInstance()
{
    static PDFStreamFilterStorage instance;
    return &instance;
}

}   // namespace pdf
