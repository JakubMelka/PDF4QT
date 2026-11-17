// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfstreamfilters.h"
#include "pdfexception.h"
#include "pdfconstants.h"
#include "pdfparser.h"
#include "pdfsecurityhandler.h"
#include "pdfutils.h"
#include "pdfthreadannotations.h"
#include "pdfoperationcontrol.h"

#include <zlib.h>

#include <QtEndian>

#include <limits>

#include "pdfdbgheap.h"

namespace pdf
{

/// The decoding loops of the filters with an upper limit of their output (PDFLimitedStreamDecoder, plan of
/// step 7 of phase 2, R7-3, Q7.18). The filters of the storage call them without a limit, so there is one
/// implementation of each decoder; the limited decoder calls them with its limit and its cancellation.
class PDFStreamFilterDecodingHelper
{
public:
    PDFStreamFilterDecodingHelper() = delete;

    /// No limit of the output (the filters of the storage)
    static constexpr qint64 UNLIMITED = (std::numeric_limits<qint64>::max)();

    /// How often a loop checks the cancellation: after each MiB of its output
    static constexpr qint64 CANCELLATION_STEP = qint64(1024) * 1024;

    /// How a decoding loop ended
    enum class Status
    {
        Finished,       ///< The input was decoded
        LimitExceeded,  ///< The output would exceed the limit (the output is incomplete: its first limit bytes)
        Canceled,       ///< The operation was canceled (the output is incomplete)
        Error           ///< The data are damaged (\p errorMessage)
    };

    /// True, when the output of a loop reached a new MiB and the operation is canceled
    static bool isCanceled(qint64 outputSize, qint64* nextCheck, const PDFOperationControl* control);

    /// Inflates the data of Flate (RFC 1950 / 1951) up to the limit; a damaged stream gives Error with the
    /// message of zlib (a truncated stream or a wrong checksum after decoded data is tolerated, as the
    /// readers of other producers do)
    static Status inflate(const QByteArray& data, qint64 limit, const PDFOperationControl* control, QByteArray* output, QString* errorMessage);

    /// Decodes ASCII85 (ISO 32000-2, 7.4.3) up to the limit
    static Status decodeAscii85(const QByteArray& data, qint64 limit, const PDFOperationControl* control, QByteArray* output);

    /// Decodes RunLength (ISO 32000-2, 7.4.5) up to the limit; a run, which reaches past the end of the
    /// data, takes only the data there are
    static Status decodeRunLength(const QByteArray& data, qint64 limit, const PDFOperationControl* control, QByteArray* output);

    /// Decodes LZW (ISO 32000-2, 7.4.4) up to the limit; an invalid code throws PDFException
    static Status decodeLzw(const QByteArray& data, uint32_t early, qint64 limit, const PDFOperationControl* control, QByteArray* output);

    /// The size of the output of the predictor of the parameters (ISO 32000-2, 7.4.4.4, Table 8) for an input
    /// of \p inputSize bytes, and the size of the buffers of one row; -1 for an invalid parameter (the
    /// predictor itself throws then). Without a predictor the output is the input.
    static qint64 getPredictorOutputSize(const PDFObjectFetcher& objectFetcher, const PDFObject& parameters, qint64 inputSize, qint64* rowSize);

    /// The names of the filters of a stream in their order (/Filter, else /FFilter), as they are written;
    /// false, when an item is not a name
    static bool getFilterNames(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, std::vector<QByteArray>* names);

    /// The /EarlyChange of LZW (Table 10, default 1)
    static uint32_t getEarlyChange(const PDFObjectFetcher& objectFetcher, const PDFObject& parameters);

    /// Appends the first bytes of [\p begin, \p begin + \p count) to \p output, up to its \p limit (a loop,
    /// which exceeds the limit, leaves the output up to it: the decoding of a prefix keeps it)
    static void appendUpToLimit(QByteArray* output, const char* begin, qint64 count, qint64 limit);
};

void PDFStreamFilterDecodingHelper::appendUpToLimit(QByteArray* output, const char* begin, qint64 count, qint64 limit)
{
    const qint64 appended = qBound(qint64(0), qMin(count, limit - qint64(output->size())), count);
    output->append(begin, qsizetype(appended));
}

QByteArray PDFAsciiHexDecodeFilter::apply(const QByteArray& data,
                                          const PDFObjectFetcher& objectFetcher,
                                          const PDFObject& parameters,
                                          const PDFSecurityHandler* securityHandler) const
{
    Q_UNUSED(objectFetcher);
    Q_UNUSED(parameters);
    Q_UNUSED(securityHandler);

    const int indexOfEnd = data.indexOf('>');
    const int size = (indexOfEnd == -1) ? data.size() : indexOfEnd;

    // ISO 32000-2, 7.4.2: white-space characters are ignored - also for the odd number of the digits (the
    // last digit is followed by a zero): QByteArray::fromHex pairs the digits from the end of the data
    const QByteArray digits = QByteArray::fromRawData(data.constData(), size);
    if (std::any_of(digits.cbegin(), digits.cend(), [](char character) { return PDFLexicalAnalyzer::isWhitespace(character); }))
    {
        QByteArray compact;
        compact.reserve(size);
        std::copy_if(digits.cbegin(), digits.cend(), std::back_inserter(compact), [](char character) { return !PDFLexicalAnalyzer::isWhitespace(character); });
        if (compact.size() % 2 == 1)
        {
            compact.push_back('0');
        }
        return QByteArray::fromHex(compact);
    }

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

QByteArray PDFAscii85DecodeFilter::apply(const QByteArray& data,
                                         const PDFObjectFetcher& objectFetcher,
                                         const PDFObject& parameters,
                                         const PDFSecurityHandler* securityHandler) const
{
    Q_UNUSED(objectFetcher);
    Q_UNUSED(parameters);
    Q_UNUSED(securityHandler);

    QByteArray result;
    PDFStreamFilterDecodingHelper::decodeAscii85(data, PDFStreamFilterDecodingHelper::UNLIMITED, nullptr, &result);
    return result;
}

bool PDFStreamFilterDecodingHelper::isCanceled(qint64 outputSize, qint64* nextCheck, const PDFOperationControl* control)
{
    if (!control || outputSize < *nextCheck)
    {
        return false;
    }

    *nextCheck = outputSize + CANCELLATION_STEP;
    return PDFOperationControl::isOperationCanceled(control);
}

PDFStreamFilterDecodingHelper::Status PDFStreamFilterDecodingHelper::decodeAscii85(const QByteArray& data, qint64 limit, const PDFOperationControl* control, QByteArray* output)
{
    const unsigned char* dataBegin = reinterpret_cast<const unsigned char*>(data.constData());
    const unsigned char* dataEnd = reinterpret_cast<const unsigned char*>(data.constData() + data.size());

    const unsigned char* it = dataBegin;
    const constexpr uint32_t STREAM_END = 0xFFFFFFFF;

    auto getChar = [&it, dataEnd]() -> uint32_t
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

    QByteArray& result = *output;
    result.clear();
    result.reserve(qMin(qint64(data.size()) * 4 / 5, limit));
    qint64 nextCheck = CANCELLATION_STEP;

    while (true)
    {
        if (isCanceled(result.size(), &nextCheck, control))
        {
            return Status::Canceled;
        }

        const uint32_t scannedChar = getChar();
        if (scannedChar == STREAM_END)
        {
            break;
        }
        else if (scannedChar == 'z')
        {
            if (qint64(result.size()) + 4 > limit)
            {
                const std::array<char, 4> zeros = { };
                appendUpToLimit(&result, zeros.data(), 4, limit);
                return Status::LimitExceeded;
            }
            result.append(4, static_cast<char>(0));
        }
        else
        {
            // Scan all 5 characters, some of then can be equal to STREAM_END constant. We will
            // treat all these characters as last character.
            std::array<uint32_t, 5> scannedChars;
            scannedChars.fill(84);
            scannedChars[0] = scannedChar - 33;
            std::size_t validBytes = 0;
            for (auto it2 = std::next(scannedChars.begin()); it2 != scannedChars.end(); ++it2)
            {
                uint32_t character = getChar();
                if (character == STREAM_END)
                {
                    break;
                }
                *it2 = character - 33;
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
            if (qint64(result.size()) + qint64(validBytes) > limit)
            {
                appendUpToLimit(&result, decodedBytesUnpacked.data(), qint64(validBytes), limit);
                return Status::LimitExceeded;
            }

            for (std::size_t i = 0; i < validBytes; ++i)
            {
                result.push_back(decodedBytesUnpacked[i]);
            }
        }
    }

    return Status::Finished;
}

class PDFLzwStreamDecoder
{
public:
    explicit PDFLzwStreamDecoder(const QByteArray& inputByteArray, uint32_t early);

    QByteArray decompress();

    /// Decompresses up to the limit of the output (PDFStreamFilterDecodingHelper)
    PDFStreamFilterDecodingHelper::Status decompress(qint64 limit, const PDFOperationControl* control, QByteArray* output);

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
    decompress(PDFStreamFilterDecodingHelper::UNLIMITED, nullptr, &result);
    return result;
}

PDFStreamFilterDecodingHelper::Status PDFLzwStreamDecoder::decompress(qint64 limit, const PDFOperationControl* control, QByteArray* output)
{
    QByteArray& result = *output;
    result.clear();

    // Guess output byte array size - assume compress ratio is 2:1
    result.reserve(qMin(qint64(m_inputByteArray.size()) * 2, limit));
    qint64 nextCheck = PDFStreamFilterDecodingHelper::CANCELLATION_STEP;

    uint32_t previousCode = TABLE_SIZE;
    while (true)
    {
        if (PDFStreamFilterDecodingHelper::isCanceled(result.size(), &nextCheck, control))
        {
            return PDFStreamFilterDecodingHelper::Status::Canceled;
        }

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
            throw PDFException(PDFTranslationContext::tr("Invalid code in the LZW stream."));
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
        const qint64 sequenceLength = qint64(std::distance(m_sequence.begin(), m_currentSequenceEnd));
        if (qint64(result.size()) + sequenceLength > limit)
        {
            PDFStreamFilterDecodingHelper::appendUpToLimit(&result, m_sequence.data(), sequenceLength, limit);
            return PDFStreamFilterDecodingHelper::Status::LimitExceeded;
        }
        std::copy(m_sequence.begin(), m_currentSequenceEnd, std::back_inserter(result));
    }

    result.shrink_to_fit();
    return PDFStreamFilterDecodingHelper::Status::Finished;
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

QByteArray PDFLzwDecodeFilter::apply(const QByteArray& data,
                                     const PDFObjectFetcher& objectFetcher,
                                     const PDFObject& parameters,
                                     const PDFSecurityHandler* securityHandler) const
{
    Q_UNUSED(securityHandler);

    const uint32_t early = PDFStreamFilterDecodingHelper::getEarlyChange(objectFetcher, parameters);
    PDFStreamPredictor predictor = PDFStreamPredictor::createPredictor(objectFetcher, parameters);
    PDFLzwStreamDecoder decoder(data, early);
    return predictor.apply(decoder.decompress());
}

uint32_t PDFStreamFilterDecodingHelper::getEarlyChange(const PDFObjectFetcher& objectFetcher, const PDFObject& parameters)
{
    uint32_t early = 1;

    const PDFObject& dereferencedParameters = objectFetcher(parameters);
    if (dereferencedParameters.isDictionary())
    {
        const PDFDictionary* dictionary = dereferencedParameters.getDictionary();

        const PDFObject& earlyChangeObject = objectFetcher(dictionary->get("EarlyChange"));
        if (earlyChangeObject.isInt())
        {
            early = earlyChangeObject.getInteger();
        }
    }

    return early;
}

PDFStreamFilterDecodingHelper::Status PDFStreamFilterDecodingHelper::decodeLzw(const QByteArray& data, uint32_t early, qint64 limit, const PDFOperationControl* control, QByteArray* output)
{
    PDFLzwStreamDecoder decoder(data, early);
    return decoder.decompress(limit, control, output);
}

qint64 PDFStreamFilterDecodingHelper::getPredictorOutputSize(const PDFObjectFetcher& objectFetcher, const PDFObject& parameters, qint64 inputSize, qint64* rowSize)
{
    *rowSize = 0;
    const PDFObject& dereferencedParameters = objectFetcher(parameters);
    if (!dereferencedParameters.isDictionary())
    {
        return inputSize;
    }

    // ISO 32000-2, Table 8 (the ranges of PDFStreamPredictor::createPredictor)
    const PDFDictionary* dictionary = dereferencedParameters.getDictionary();
    auto getInteger = [dictionary, &objectFetcher](const char* key, qint64 defaultValue) -> qint64
    {
        const PDFObject& object = objectFetcher(dictionary->get(key));
        if (object.isInt())
        {
            return object.getInteger();
        }
        return object.isNull() ? defaultValue : -1;
    };

    const qint64 predictor = getInteger("Predictor", 1);
    const qint64 colors = getInteger("Colors", 1);
    const qint64 bitsPerComponent = getInteger("BitsPerComponent", 8);
    const qint64 columns = getInteger("Columns", 1);
    if (predictor < 1 || predictor > 15 || colors < 1 || colors > PDF_MAX_COLOR_COMPONENTS || bitsPerComponent < 1 || bitsPerComponent > 16 ||
        columns < 1 || columns > (std::numeric_limits<int>::max)())
    {
        return -1;
    }

    if (predictor == 1)
    {
        return inputSize;
    }

    // At most 2^31 * 32 * 16 bits: no overflow
    const qint64 rowBytes = (columns * colors * bitsPerComponent + 7) / 8;
    *rowSize = rowBytes + (colors * bitsPerComponent + 7) / 8;
    if (predictor == 2)
    {
        // TIFF: each row is aligned to bytes, the last one is completed
        return ((inputSize + rowBytes - 1) / rowBytes) * rowBytes;
    }

    if (predictor >= 10)
    {
        // PNG: a row is the byte of its algorithm and the row, the last one is completed
        return ((inputSize + rowBytes) / (rowBytes + 1)) * rowBytes;
    }

    return -1;
}

bool PDFStreamFilterDecodingHelper::getFilterNames(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, std::vector<QByteArray>* names)
{
    names->clear();
    const PDFDictionary* dictionary = stream->getDictionary();

    PDFObject filters;
    if (dictionary->hasKey(PDF_STREAM_DICT_FILTER))
    {
        filters = objectFetcher(dictionary->get(PDF_STREAM_DICT_FILTER));
    }
    else if (dictionary->hasKey(PDF_STREAM_DICT_FILE_FILTER))
    {
        filters = objectFetcher(dictionary->get(PDF_STREAM_DICT_FILE_FILTER));
    }

    if (filters.isName())
    {
        names->push_back(filters.getString());
    }
    else if (filters.isArray())
    {
        const PDFArray* filterArray = filters.getArray();
        for (size_t i = 0; i < filterArray->getCount(); ++i)
        {
            const PDFObject& object = objectFetcher(filterArray->getItem(i));
            if (!object.isName())
            {
                return false;
            }
            names->push_back(object.getString());
        }
    }
    else if (!filters.isNull())
    {
        return false;
    }

    return true;
}

QByteArray PDFFlateDecodeFilter::apply(const QByteArray& data,
                                       const PDFObjectFetcher& objectFetcher,
                                       const PDFObject& parameters,
                                       const PDFSecurityHandler* securityHandler) const
{
    Q_UNUSED(securityHandler);

    PDFStreamPredictor predictor = PDFStreamPredictor::createPredictor(objectFetcher, parameters);
    return predictor.apply(uncompress(data));
}

QByteArray PDFFlateDecodeFilter::compress(const QByteArray& decompressedData)
{
    QByteArray result;

    z_stream stream = { };
    stream.next_in = const_cast<Bytef*>(convertByteArrayToUcharPtr(decompressedData));
    stream.avail_in = decompressedData.size();

    std::array<Bytef, 1024> outputBuffer = { };

    int error = deflateInit(&stream, Z_BEST_COMPRESSION);
    if (error != Z_OK)
    {
        throw PDFException(PDFTranslationContext::tr("Failed to initialize flate compression stream."));
    }

    do
    {
        stream.next_out = outputBuffer.data();
        stream.avail_out = static_cast<uInt>(outputBuffer.size());

        error = deflate(&stream, Z_FINISH);

        int bytesWritten = int(outputBuffer.size()) - stream.avail_out;
        result.append(reinterpret_cast<const char*>(outputBuffer.data()), bytesWritten);
    } while (error == Z_OK);

    QString errorMessage;
    if (stream.msg)
    {
        errorMessage = QString::fromLatin1(stream.msg);
    }

    deflateEnd(&stream);

    switch (error)
    {
        case Z_STREAM_END:
            break; // No error, normal behavior

        default:
        {
            if (errorMessage.isEmpty())
            {
                errorMessage = PDFTranslationContext::tr("zlib code: %1").arg(error);
            }

            throw PDFException(PDFTranslationContext::tr("Error compressing by flate method: %1").arg(errorMessage));
        }
    }

    return result;
}

QByteArray PDFFlateDecodeFilter::recompress(const QByteArray& data)
{
    QByteArray decompressedData = uncompress(data);
    return compress(decompressedData);
}

PDFInteger PDFFlateDecodeFilter::getStreamDataLength(const QByteArray& data, PDFInteger offset) const
{
    if (offset < 0 || offset >= data.size())
    {
        return -1;
    }

    z_stream stream = { };
    stream.next_in = const_cast<Bytef*>(convertByteArrayToUcharPtr(data) + offset);
    stream.avail_in = data.size() - offset;

    std::array<Bytef, 1024> outputBuffer = { };

    int error = inflateInit(&stream);
    if (error != Z_OK)
    {
        return -1;
    }

    do
    {
        stream.next_out = outputBuffer.data();
        stream.avail_out = static_cast<uInt>(outputBuffer.size());

        error = inflate(&stream, Z_NO_FLUSH);
    } while (error == Z_OK);

    PDFInteger dataLength = stream.total_in;
    inflateEnd(&stream);

    if (error == Z_STREAM_END)
    {
        return dataLength;
    }

    return -1;
}

QByteArray PDFFlateDecodeFilter::uncompress(const QByteArray& data)
{
    QByteArray result;
    QString errorMessage;
    if (PDFStreamFilterDecodingHelper::inflate(data, PDFStreamFilterDecodingHelper::UNLIMITED, nullptr, &result, &errorMessage) == PDFStreamFilterDecodingHelper::Status::Error)
    {
        throw PDFException(PDFTranslationContext::tr("Error decompressing by flate method: %1").arg(errorMessage));
    }

    return result;
}

PDFStreamFilterDecodingHelper::Status PDFStreamFilterDecodingHelper::inflate(const QByteArray& data, qint64 limit, const PDFOperationControl* control, QByteArray* output, QString* errorMessage)
{
    QByteArray& result = *output;
    result.clear();

    z_stream stream = { };
    stream.next_in = const_cast<Bytef*>(convertByteArrayToUcharPtr(data));
    stream.avail_in = data.size();

    std::array<Bytef, 1024> outputBuffer = { };

    int error = inflateInit(&stream);
    if (error != Z_OK)
    {
        *errorMessage = PDFTranslationContext::tr("Failed to initialize flate decompression stream.");
        return Status::Error;
    }

    qint64 nextCheck = CANCELLATION_STEP;
    Status status = Status::Finished;
    do
    {
        stream.next_out = outputBuffer.data();
        stream.avail_out = static_cast<uInt>(outputBuffer.size());

        error = ::inflate(&stream, Z_NO_FLUSH);

        int bytesWritten = int(outputBuffer.size()) - stream.avail_out;
        if (qint64(result.size()) + bytesWritten > limit)
        {
            appendUpToLimit(&result, reinterpret_cast<const char*>(outputBuffer.data()), bytesWritten, limit);
            status = Status::LimitExceeded;
            break;
        }
        result.append(reinterpret_cast<const char*>(outputBuffer.data()), bytesWritten);

        if (isCanceled(result.size(), &nextCheck, control))
        {
            status = Status::Canceled;
            break;
        }
    } while (error == Z_OK);

    QString zlibMessage;
    if (stream.msg)
    {
        zlibMessage = QString::fromLatin1(stream.msg);
    }

    const bool isInputExhausted = stream.avail_in == 0;
    inflateEnd(&stream);

    if (status != Status::Finished)
    {
        return status;
    }

    switch (error)
    {
        case Z_STREAM_END:
            break; // No error, normal behavior

        default:
        {
            // Some producers write streams with missing or incorrect Adler-32 checksum,
            // or truncate the stream. If we decompressed some data and consumed whole
            // input, then we tolerate this error.
            const bool isTruncatedStream = error == Z_BUF_ERROR && isInputExhausted && !result.isEmpty();
            const bool ignoreError = isTruncatedStream || (error == Z_DATA_ERROR && zlibMessage == "incorrect data check");

            if (!ignoreError)
            {
                if (zlibMessage.isEmpty())
                {
                    zlibMessage = PDFTranslationContext::tr("zlib code: %1").arg(error);
                }

                *errorMessage = zlibMessage;
                return Status::Error;
            }
        }
    }

    return Status::Finished;
}

QByteArray PDFRunLengthDecodeFilter::apply(const QByteArray& data,
                                           const PDFObjectFetcher& objectFetcher,
                                           const PDFObject& parameters,
                                           const PDFSecurityHandler* securityHandler) const
{
    Q_UNUSED(objectFetcher);
    Q_UNUSED(parameters);
    Q_UNUSED(securityHandler);

    QByteArray result;
    PDFStreamFilterDecodingHelper::decodeRunLength(data, PDFStreamFilterDecodingHelper::UNLIMITED, nullptr, &result);
    return result;
}

PDFStreamFilterDecodingHelper::Status PDFStreamFilterDecodingHelper::decodeRunLength(const QByteArray& data, qint64 limit, const PDFOperationControl* control, QByteArray* output)
{
    QByteArray& result = *output;
    result.clear();
    result.reserve(qMin(qint64(data.size()) * 2, limit));
    qint64 nextCheck = CANCELLATION_STEP;

    auto itEnd = data.cend();
    for (auto it = data.cbegin(); it != itEnd;)
    {
        if (isCanceled(result.size(), &nextCheck, control))
        {
            return Status::Canceled;
        }

        const unsigned char current = *it++;
        if (current == 128)
        {
            // End of stream marker
            break;
        }
        else if (current < 128)
        {
            // Copy n + 1 characters from the input array literally (and advance iterators); a run,
            // which reaches past the end of the data, takes the data there are
            const int count = static_cast<int>(qMin<qint64>(static_cast<qint64>(current) + 1, std::distance(it, itEnd)));
            if (qint64(result.size()) + count > limit)
            {
                appendUpToLimit(&result, &*it, count, limit);
                return Status::LimitExceeded;
            }
            std::copy(it, std::next(it, count), std::back_inserter(result));
            std::advance(it, count);
        }
        else if (current > 128)
        {
            // Copy 257 - n copies of single character (none, when the data end)
            if (it == itEnd)
            {
                break;
            }

            const int count = 257 - current;
            if (qint64(result.size()) + count > limit)
            {
                const QByteArray run(count, *it);
                appendUpToLimit(&result, run.constData(), count, limit);
                return Status::LimitExceeded;
            }
            const char toBeCopied = *it++;
            std::fill_n(std::back_inserter(result), count, toBeCopied);
        }
    }

    return Status::Finished;
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

PDFStreamFilterStorage::StreamFilters PDFStreamFilterStorage::getStreamFilters(const PDFStream* stream, const PDFObjectFetcher& objectFetcher)
{
    StreamFilters result;
    const PDFDictionary* dictionary = stream->getDictionary();

    // Retrieve filters
    PDFObject filters;
    if (dictionary->hasKey(PDF_STREAM_DICT_FILTER))
    {
        filters = objectFetcher(dictionary->get(PDF_STREAM_DICT_FILTER));
    }
    else if (dictionary->hasKey(PDF_STREAM_DICT_FILE_FILTER))
    {
        filters = objectFetcher(dictionary->get(PDF_STREAM_DICT_FILE_FILTER));
    }

    // Retrieve filter parameters
    PDFObject filterParameters;
    if (dictionary->hasKey(PDF_STREAM_DICT_DECODE_PARMS))
    {
        filterParameters = objectFetcher(dictionary->get(PDF_STREAM_DICT_DECODE_PARMS));
    }
    else if (dictionary->hasKey(PDF_STREAM_DICT_FDECODE_PARMS))
    {
        filterParameters = objectFetcher(dictionary->get(PDF_STREAM_DICT_FDECODE_PARMS));
    }

    if (filters.isName())
    {
        result.filterObjects.push_back(PDFStreamFilterStorage::getFilter(filters.getString()));
    }
    else if (filters.isArray())
    {
        const PDFArray* filterArray = filters.getArray();
        const size_t filterCount = filterArray->getCount();
        for (size_t i = 0; i < filterCount; ++i)
        {
            const PDFObject& object = objectFetcher(filterArray->getItem(i));
            if (object.isName())
            {
                result.filterObjects.push_back(PDFStreamFilterStorage::getFilter(object.getString()));
            }
            else
            {
                result.valid = false;
                return result;
            }
        }
    }
    else if (!filters.isNull())
    {
        result.valid = false;
        return result;
    }

    if (filterParameters.isArray())
    {
        const PDFArray* filterParameterArray = filterParameters.getArray();
        const size_t filterParameterCount = filterParameterArray->getCount();
        for (size_t i = 0; i < filterParameterCount; ++i)
        {
            const PDFObject& object = objectFetcher(filterParameterArray->getItem(i));
            result.filterParameterObjects.push_back(object);
        }
    }
    else
    {
        result.filterParameterObjects.push_back(filterParameters);
    }

    result.filterParameterObjects.resize(result.filterObjects.size());
    return result;
}

QByteArray PDFStreamFilterStorage::getDecodedStream(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, const PDFSecurityHandler* securityHandler)
{
    StreamFilters streamFilters = getStreamFilters(stream, objectFetcher);
    QByteArray result = *stream->getContent();

    if (!streamFilters.valid)
    {
        // Stream filters are invalid
        return QByteArray();
    }

    for (size_t i = 0, count = streamFilters.filterObjects.size(); i < count; ++i)
    {
        const PDFStreamFilter* streamFilter = streamFilters.filterObjects[i];
        const PDFObject& streamFilterParameters = streamFilters.filterParameterObjects[i];

        if (streamFilter)
        {
            result = streamFilter->apply(result, objectFetcher, streamFilterParameters, securityHandler);
        }
    }

    return result;
}

PDFLimitedStreamDecoder::Result PDFLimitedStreamDecoder::decode(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, const PDFSecurityHandler* securityHandler, qint64 maximumDecodedSize, const PDFOperationControl* control)
{
    return decodeImpl(stream, objectFetcher, securityHandler, maximumDecodedSize, -1, control);
}

PDFLimitedStreamDecoder::Result PDFLimitedStreamDecoder::decodePrefix(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, const PDFSecurityHandler* securityHandler, qint64 prefixSize,
                                                                      qint64 maximumDecodedSize, const PDFOperationControl* control)
{
    return decodeImpl(stream, objectFetcher, securityHandler, maximumDecodedSize, qMax(prefixSize, qint64(0)), control);
}

PDFLimitedStreamDecoder::Result PDFLimitedStreamDecoder::decodeImpl(const PDFStream* stream, const PDFObjectFetcher& objectFetcher, const PDFSecurityHandler* securityHandler,
                                                                    qint64 maximumDecodedSize, qint64 prefixSize, const PDFOperationControl* control)
{
    using Helper = PDFStreamFilterDecodingHelper;

    Result result;
    const qint64 limit = qMax(maximumDecodedSize, qint64(1));
    auto fail = [&result](Status status, const QByteArray& filterName, const QString& errorMessage)
    {
        result.status = status;
        result.data.clear();
        result.filterName = filterName;
        result.errorMessage = errorMessage;
        return result;
    };
    auto exceeded = [&fail, limit](const QByteArray& filterName)
    {
        return fail(Status::LimitExceeded, filterName, PDFTranslationContext::tr("The %1 filter gives more than %2 bytes of data.").arg(QString::fromLatin1(filterName)).arg(limit));
    };

    if (!stream)
    {
        return fail(Status::Error, QByteArray(), PDFTranslationContext::tr("The object is not a stream."));
    }

    QByteArray currentName;
    try
    {
        std::vector<QByteArray> names;
        const PDFStreamFilterStorage::StreamFilters filters = PDFStreamFilterStorage::getStreamFilters(stream, objectFetcher);
        if (!Helper::getFilterNames(stream, objectFetcher, &names) || !filters.valid || names.size() != filters.filterObjects.size())
        {
            return fail(Status::Error, QByteArray(), PDFTranslationContext::tr("The filters of the stream are invalid."));
        }

        QByteArray data = *stream->getContent();
        for (size_t i = 0; i < names.size(); ++i)
        {
            const QByteArray& name = names[i];
            currentName = name;
            const PDFStreamFilter* filter = filters.filterObjects[i];
            const PDFObject& parameters = filters.filterParameterObjects[i];

            if (PDFOperationControl::isOperationCanceled(control))
            {
                return fail(Status::Canceled, QByteArray(), QString());
            }

            if (!filter)
            {
                // An image filter or an unknown one: the data before it are the result (FD-3, FD-4)
                result.status = Status::Encoded;
                result.data = std::move(data);
                result.filterName = name;
                return result;
            }

            // A prefix: the last filter stops at the prefix (the decoders write their output in the order of
            // their input, 7.4) - except a filter with a predictor, whose last row would be completed by zeros
            // (7.4.4.4): it decodes its input up to the limit, the prefix is cut at the end
            qint64 filterLimit = limit;
            bool isPrefixLimited = false;
            if (prefixSize >= 0 && i + 1 == names.size())
            {
                qint64 rowSize = 0;
                Helper::getPredictorOutputSize(objectFetcher, parameters, 0, &rowSize);
                if (rowSize == 0 && prefixSize < limit)
                {
                    filterLimit = prefixSize;
                    isPrefixLimited = true;
                }
            }

            QByteArray output;
            QString errorMessage;
            Helper::Status status = Helper::Status::Finished;
            if (dynamic_cast<const PDFFlateDecodeFilter*>(filter) || dynamic_cast<const PDFLzwDecodeFilter*>(filter))
            {
                // The predictor of Flate and LZW (7.4.4.4) gives at most its input: its own buffers of one
                // row are checked before it runs (a hostile /Columns would allocate gigabytes)
                if (dynamic_cast<const PDFFlateDecodeFilter*>(filter))
                {
                    status = Helper::inflate(data, filterLimit, control, &output, &errorMessage);
                }
                else
                {
                    status = Helper::decodeLzw(data, Helper::getEarlyChange(objectFetcher, parameters), filterLimit, control, &output);
                }

                if (isPrefixLimited && status == Helper::Status::LimitExceeded)
                {
                    // The output has the prefix (and no predictor)
                    status = Helper::Status::Finished;
                }

                if (status == Helper::Status::Finished)
                {
                    qint64 rowSize = 0;
                    const qint64 predictedSize = Helper::getPredictorOutputSize(objectFetcher, parameters, output.size(), &rowSize);
                    if (rowSize > (std::numeric_limits<int>::max)() / 4)
                    {
                        return fail(Status::Error, name, PDFTranslationContext::tr("Error in the %1 filter: the rows of the predictor are too long.").arg(QString::fromLatin1(name)));
                    }
                    if (predictedSize > limit || rowSize > limit)
                    {
                        return exceeded(name);
                    }

                    // An invalid parameter throws (the message of the predictor)
                    output = PDFStreamPredictor::createPredictor(objectFetcher, parameters).apply(output);
                }
            }
            else if (dynamic_cast<const PDFAscii85DecodeFilter*>(filter))
            {
                status = Helper::decodeAscii85(data, filterLimit, control, &output);
            }
            else if (dynamic_cast<const PDFRunLengthDecodeFilter*>(filter))
            {
                status = Helper::decodeRunLength(data, filterLimit, control, &output);
            }
            else
            {
                // ASCIIHex gives half of its input, Crypt about its input: both are bounded by the input
                output = filter->apply(data, objectFetcher, parameters, securityHandler);
            }

            if (isPrefixLimited && status == Helper::Status::LimitExceeded)
            {
                // The decoder stopped at the prefix: its output is the prefix
                status = Helper::Status::Finished;
            }

            switch (status)
            {
                case Helper::Status::Finished:
                    break;

                case Helper::Status::LimitExceeded:
                    return exceeded(name);

                case Helper::Status::Canceled:
                    return fail(Status::Canceled, QByteArray(), QString());

                case Helper::Status::Error:
                    return fail(Status::Error, name, PDFTranslationContext::tr("Error in the %1 filter: %2").arg(QString::fromLatin1(name), errorMessage));
            }

            if (qint64(output.size()) > limit)
            {
                return exceeded(name);
            }

            data = std::move(output);
        }

        if (prefixSize >= 0 && qint64(data.size()) > prefixSize)
        {
            data.truncate(qsizetype(prefixSize));
        }

        result.status = Status::Decoded;
        result.data = std::move(data);
        return result;
    }
    catch (const PDFException& exception)
    {
        if (currentName.isEmpty())
        {
            return fail(Status::Error, QByteArray(), exception.getMessage());
        }
        return fail(Status::Error, currentName, PDFTranslationContext::tr("Error in the %1 filter: %2").arg(QString::fromLatin1(currentName), exception.getMessage()));
    }
}

QByteArray PDFStreamFilterStorage::getDecodedStream(const PDFStream* stream, const PDFSecurityHandler* securityHandler)
{
    return getDecodedStream(stream, [](const PDFObject& object) -> const PDFObject& { return object; }, securityHandler);
}

PDFInteger PDFStreamFilterStorage::getStreamDataLength(const QByteArray& data, const QByteArray& filterName, PDFInteger offset)
{
    if (const PDFStreamFilter* filter = getFilter(filterName))
    {
        return filter->getStreamDataLength(data, offset);
    }

    return -1;
}

PDFStreamFilterStorage::PDFStreamFilterStorage()
{
    // Initialize map with the filters
    m_filters["ASCIIHexDecode"] = std::make_unique<PDFAsciiHexDecodeFilter>();
    m_filters["ASCII85Decode"] = std::make_unique<PDFAscii85DecodeFilter>();
    m_filters["LZWDecode"] = std::make_unique<PDFLzwDecodeFilter>();
    m_filters["FlateDecode"] = std::make_unique<PDFFlateDecodeFilter>();
    m_filters["RunLengthDecode"] = std::make_unique<PDFRunLengthDecodeFilter>();
    m_filters["Crypt"] = std::make_unique<PDFCryptFilter>();

    m_abbreviations["AHx"] = "ASCIIHexDecode";
    m_abbreviations["A85"] = "ASCII85Decode";
    m_abbreviations["LZW"] = "LZWDecode";
    m_abbreviations["Fl"] = "FlateDecode";
    m_abbreviations["RL"] = "RunLengthDecode";
    m_abbreviations["CCF"] = "CCITFaxDecode";
    m_abbreviations["DCT"] = "DCTDecode";

    // The guard of the static instance synchronizes by a futex (see getInstance)
    PDFThreadAnnotations::happensBefore(this);
}

const PDFStreamFilterStorage* PDFStreamFilterStorage::getInstance()
{
    static PDFStreamFilterStorage instance;
    PDFThreadAnnotations::happensAfter(&instance);
    return &instance;
}

PDFStreamPredictor PDFStreamPredictor::createPredictor(const PDFObjectFetcher& objectFetcher, const PDFObject& parameters)
{
    const PDFObject& dereferencedParameters = objectFetcher(parameters);
    if (dereferencedParameters.isDictionary())
    {
        const PDFDictionary* dictionary = dereferencedParameters.getDictionary();

        auto getInteger = [dictionary, &objectFetcher](const char* key, int min, int max, int defaultValue) -> int
        {
            const PDFObject& object = objectFetcher(dictionary->get(key));

            if (object.isInt())
            {
                PDFInteger value = object.getInteger();
                if (value < min || value > max)
                {
                    throw PDFException(PDFTranslationContext::tr("Property '%1' should be in range from %2 to %3.").arg(QString::fromLatin1(key)).arg(min).arg(max));
                }

                return value;
            }
            else if (object.isNull())
            {
                return defaultValue;
            }

            throw PDFException(PDFTranslationContext::tr("Invalid property '%1' of the stream predictor parameters.").arg(QString::fromLatin1(key)));
        };

        int predictor = getInteger("Predictor", 1, 15, 1);
        int components = getInteger("Colors", 1, PDF_MAX_COLOR_COMPONENTS, 1);
        int bitsPerComponent = getInteger("BitsPerComponent", 1, 16, 8);
        int columns = getInteger("Columns", 1, std::numeric_limits<int>::max(), 1);

        return PDFStreamPredictor(static_cast<Predictor>(predictor), components, bitsPerComponent, columns);
    }

    return PDFStreamPredictor();
}

QByteArray PDFStreamPredictor::apply(const QByteArray& data) const
{
    switch (m_predictor)
    {
        case NoPredictor:
            return data;

        case TIFF:
            return applyTIFFPredictor(data);

        default:
        {
            if (m_predictor >= 10)
            {
                return applyPNGPredictor(data);
            }
            break;
        }
    }

    throw PDFException(PDFTranslationContext::tr("Invalid predictor algorithm."));
}

QByteArray PDFStreamPredictor::applyPNGPredictor(const QByteArray& data) const
{
    QByteArray outputData;
    outputData.reserve(data.size());

    auto it = data.cbegin();
    auto itEnd = data.cend();

    int pixelBytes = (m_components * m_bitsPerComponent + 7) / 8;

    auto readByte = [&it, &itEnd]() -> uint8_t
    {
        if (it != itEnd)
        {
            return static_cast<uint8_t>(*it++);
        }

        // According to the PDF specification, incomplete line is completed. For this
        // reason, we behave as we have zero data in the buffer.
        return 0;
    };

    // Idea: to avoid using if for many cases, we use larger buffer filled with zeros
    const int totalBytes = m_stride + pixelBytes;
    std::vector<uint8_t> line(totalBytes, 0);
    std::vector<uint8_t> lineOld(totalBytes, 0);

    Predictor currentPredictor = m_predictor;
    while (it != itEnd)
    {
        // First, read the predictor data for current line
        currentPredictor = static_cast<Predictor>(readByte() + 10);

        for (int i = 0; i < m_stride; ++i)
        {
            uint8_t currentByte = readByte();

            int lineIndex = i + pixelBytes;
            switch (currentPredictor)
            {
                case PNG_Sub:
                {
                    line[lineIndex] = line[i] + currentByte;
                    break;
                }

                case PNG_Up:
                {
                    line[lineIndex] = lineOld[lineIndex] + currentByte;
                    break;
                }

                case PNG_Average:
                {
                    line[lineIndex] = (lineOld[lineIndex] + line[i]) / 2 + currentByte;
                    break;
                }

                case PNG_Paeth:
                {
                    // a = left,
                    // b = upper,
                    // c = upper left
                    const int a = line[i];
                    const int b = lineOld[lineIndex];
                    const int c = lineOld[i];
                    const int p = a + b - c;
                    const int pa = std::abs(p - a);
                    const int pb = std::abs(p - b);
                    const int pc = std::abs(p - c);
                    if (pa <= pb && pa <= pc)
                    {
                        line[lineIndex] = a + currentByte;
                    }
                    else if (pb <= pc)
                    {
                        line[lineIndex] = b + currentByte;
                    }
                    else
                    {
                        line[lineIndex] = c + currentByte;
                    }
                    break;
                }

                case PNG_None:
                default:
                {
                    line[lineIndex] = currentByte;
                    break;
                }
            }

            // Fill the output buffer
            outputData.push_back(static_cast<char>(line[lineIndex]));
        }

        // Swap the buffers
        std::swap(line, lineOld);
    }

    return outputData;
}

QByteArray PDFStreamPredictor::applyTIFFPredictor(const QByteArray& data) const
{
    Q_UNUSED(data);

    PDFBitWriter writer(m_bitsPerComponent);
    PDFBitReader reader(&data, m_bitsPerComponent);

    writer.reserve(data.size());
    std::vector<uint32_t> leftValues(m_components, 0);

    while (!reader.isAtEnd())
    {
        for (int i = 0; i < m_columns; ++i)
        {
            for (int componentIndex = 0; componentIndex < m_components; ++componentIndex)
            {
                leftValues[componentIndex] = (leftValues[componentIndex] + reader.read()) & reader.max();
                writer.write(leftValues[componentIndex]);
            }
        }

        std::fill(leftValues.begin(), leftValues.end(), 0);
        reader.alignToBytes();
        writer.finishLine();
    }

    return writer.takeByteArray();
}

QByteArray PDFCryptFilter::apply(const QByteArray& data,
                                 const PDFObjectFetcher& objectFetcher,
                                 const PDFObject& parameters,
                                 const PDFSecurityHandler* securityHandler) const
{
    if (!securityHandler)
    {
        throw PDFException(PDFTranslationContext::tr("Security handler required, but not provided."));
    }

    // 7.4.10, Table 14: /Name of the decode parameters, default Identity. The key of the object
    // (Algorithm 1) needs the reference of the stream object, which the reader stores into these
    // decode parameters, when it keeps the step (PDFSecurityHandler::isCryptFilterStream, B106)
    bool hasObjectReference = false;
    PDFObjectReference objectReference;
    QByteArray cryptFilterName = PDFSecurityHandler::IDENTITY_FILTER_NAME;
    const PDFObject& dereferencedParameters = objectFetcher(parameters);
    if (dereferencedParameters.isDictionary())
    {
        const PDFDictionary* dictionary = dereferencedParameters.getDictionary();
        const PDFObject& cryptFilterNameObject = objectFetcher(dictionary->get("Name"));
        if (cryptFilterNameObject.isName())
        {
            cryptFilterName = cryptFilterNameObject.getString();
        }

        const PDFObject& objectReferenceObject = dictionary->get(PDFSecurityHandler::OBJECT_REFERENCE_DICTIONARY_NAME);
        if (objectReferenceObject.isReference())
        {
            objectReference = objectReferenceObject.getReference();
            hasObjectReference = true;
        }
    }

    if (!hasObjectReference && cryptFilterName != PDFSecurityHandler::IDENTITY_FILTER_NAME)
    {
        // A key of another object would decrypt garbage silently
        throw PDFException(PDFTranslationContext::tr("Crypt filter '%1' cannot decrypt a stream of an unknown object.").arg(QString::fromLatin1(cryptFilterName)));
    }

    return securityHandler->decryptByFilter(data, cryptFilterName, objectReference);
}

PDFInteger PDFStreamFilter::getStreamDataLength(const QByteArray& data, PDFInteger offset) const
{
    Q_UNUSED(data);
    Q_UNUSED(offset);

    return -1;
}

}   // namespace pdf
