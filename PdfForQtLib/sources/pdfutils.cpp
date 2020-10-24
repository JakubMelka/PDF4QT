//    Copyright (C) 2019-2020 Jakub Melka
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

#include "pdfutils.h"
#include "pdfexception.h"

#include <jpeglib.h>
#include <ft2build.h>
#include <freetype/freetype.h>
#include <openjpeg.h>
#include <openssl/opensslv.h>

#pragma warning(push)
#pragma warning(disable:5033)
#include <lcms2.h>
#pragma warning(pop)

namespace pdf
{

PDFBitReader::PDFBitReader(const QByteArray* stream, Value bitsPerComponent) :
    m_stream(stream),
    m_position(0),
    m_bitsPerComponent(bitsPerComponent),
    m_maximalValue((static_cast<Value>(1) << m_bitsPerComponent) - static_cast<Value>(1)),
    m_buffer(0),
    m_bitsInBuffer(0)
{
    // We need reserve, so we allow number of length of component as 1-56 bits.
    Q_ASSERT(bitsPerComponent > 0);
    Q_ASSERT(bitsPerComponent < 56);
}

PDFBitReader::Value PDFBitReader::read(PDFBitReader::Value bits)
{
    while (m_bitsInBuffer < bits)
    {
        if (m_position < m_stream->size())
        {
            uint8_t currentByte = static_cast<uint8_t>((*m_stream)[m_position++]);
            m_buffer = (m_buffer << 8) | currentByte;
            m_bitsInBuffer += 8;
        }
        else
        {
            throw PDFException(PDFTranslationContext::tr("Not enough data to read %1-bit value.").arg(bits));
        }
    }

    // Now we have enough bits to read the data
    Value value = (m_buffer >> (m_bitsInBuffer - bits)) & ((static_cast<Value>(1) << bits) - static_cast<Value>(1));
    m_bitsInBuffer -= bits;
    return value;
}

PDFBitReader::Value PDFBitReader::look(Value bits) const
{
    PDFBitReader temp(*this);

    Value result = 0;
    for (Value i = 0; i < bits; ++i)
    {
        if (!temp.isAtEnd())
        {
            result = (result << 1) | temp.read(1);
        }
        else
        {
            result = (result << 1);
        }
    }

    return result;
}

void PDFBitReader::seek(qint64 position)
{
    if (position <= m_stream->size())
    {
        m_position = position;
        m_buffer = 0;
        m_bitsInBuffer = 0;
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("Can't seek to position %1.").arg(position));
    }
}

void PDFBitReader::skipBytes(Value bytes)
{
    // Jakub Melka: if we are lucky, then we just seek to the new position
    if (m_bitsInBuffer == 0)
    {
        seek(m_position + bytes);
    }
    else
    {
        for (Value i = 0; i < bytes; ++i)
        {
            read(8);
        }
    }
}

void PDFBitReader::alignToBytes()
{
    const Value remainder = m_bitsInBuffer % 8;
    if (remainder > 0)
    {
        read(remainder);

        if (m_bitsInBuffer == 0)
        {
            m_buffer = 0;
        }
    }
}

bool PDFBitReader::isAtEnd() const
{
    return (m_position >= m_stream->size()) && m_bitsInBuffer == 0;
}

int32_t PDFBitReader::readSignedInt()
{
    const uint32_t value = read(32);
    return *reinterpret_cast<const int32_t*>(&value);
}

int8_t PDFBitReader::readSignedByte()
{
    const uint8_t value = read(8);
    return *reinterpret_cast<const int8_t*>(&value);
}

QByteArray PDFBitReader::readSubstream(int length)
{
    if (m_bitsInBuffer)
    {
        throw PDFException(PDFTranslationContext::tr("Can't get substream - remaining %1 bits in buffer.").arg(m_bitsInBuffer));
    }

    QByteArray result = m_stream->mid(m_position, length);
    if (length == -1)
    {
        m_position = m_stream->size();
    }
    else
    {
        skipBytes(length);
    }

    return result;
}

PDFBitWriter::PDFBitWriter(Value bitsPerComponent) :
    m_bitsPerComponent(bitsPerComponent),
    m_mask((static_cast<Value>(1) << m_bitsPerComponent) - static_cast<Value>(1)),
    m_buffer(0),
    m_bitsInBuffer(0)
{

}

void PDFBitWriter::write(Value value)
{
    m_buffer = (m_buffer << m_bitsPerComponent) | (value & m_mask);
    m_bitsInBuffer += m_bitsPerComponent;

    flush(false);
}

void PDFBitWriter::flush(bool alignToByteBoundary)
{
    if (m_bitsInBuffer >= 8)
    {
        Value remainder = m_bitsInBuffer % 8;
        Value alignedBuffer = m_buffer >> remainder;
        Value bitsToWrite = m_bitsInBuffer - remainder;
        Value bytesToWrite = bitsToWrite / 8;

        for (Value byteIndex = 0; byteIndex < bytesToWrite; ++byteIndex)
        {
            const Value shift = (bytesToWrite - 1 - byteIndex) * 8;
            m_outputByteArray.push_back(static_cast<const char>(static_cast<uint8_t>((alignedBuffer >> shift) & 0xFF)));
        }

        m_bitsInBuffer = remainder;
    }

    if (alignToByteBoundary && m_bitsInBuffer > 0)
    {
        Value missingBits = 8 - m_bitsInBuffer % 8;
        m_buffer = m_buffer << missingBits;
        m_bitsInBuffer += missingBits;
        flush(false);
    }
}

std::vector<PDFDependentLibraryInfo> PDFDependentLibraryInfo::getLibraryInfo()
{
    std::vector<PDFDependentLibraryInfo> result;

    // Jakub Melka: iterate all used libraries, fill info...

    // Qt
    PDFDependentLibraryInfo qtInfo;
    qtInfo.library = tr("Qt");
    qtInfo.license = tr("LGPLv3");
    qtInfo.version = QT_VERSION_STR;
    qtInfo.url = tr("https://www.qt.io/");
    result.emplace_back(qMove(qtInfo));

    // libjpeg
    PDFDependentLibraryInfo libjpegInfo;
    libjpegInfo.library = tr("libjpeg");
    libjpegInfo.license = tr("permissive + ack.");
    libjpegInfo.url = tr("https://www.ijg.org/");
    libjpegInfo.version = tr("%1.%2").arg(JPEG_LIB_VERSION_MAJOR).arg(JPEG_LIB_VERSION_MINOR);
    result.emplace_back(qMove(libjpegInfo));

    // FreeType
    FT_Library ftLibrary;
    FT_Init_FreeType(&ftLibrary);
    FT_Int ftMajor = 0;
    FT_Int ftMinor = 0;
    FT_Int ftPatch = 0;
    FT_Library_Version(ftLibrary, &ftMajor, &ftMinor, &ftPatch);
    FT_Done_FreeType(ftLibrary);
    PDFDependentLibraryInfo freetypeInfo;
    freetypeInfo.library = tr("FreeType");
    freetypeInfo.license = tr("FTL");
    freetypeInfo.version = tr("%1.%2.%3").arg(ftMajor).arg(ftMinor).arg(ftPatch);
    freetypeInfo.url = tr("https://www.freetype.org/index.html");
    result.emplace_back(qMove(freetypeInfo));

    // OpenJPEG
    PDFDependentLibraryInfo openjpegInfo;
    openjpegInfo.library = tr("OpenJPEG");
    openjpegInfo.license = tr("2-clause MIT license");
    openjpegInfo.version = opj_version();
    openjpegInfo.url = tr("https://www.openjpeg.org/");
    result.emplace_back(qMove(openjpegInfo));

    // OpenSSL
    PDFDependentLibraryInfo opensslInfo;
    opensslInfo.library = tr("OpenSSL");
    opensslInfo.license = tr("Apache 2.0");
    opensslInfo.version = OPENSSL_VERSION_TEXT;
    opensslInfo.url = tr("https://www.openssl.org/");
    result.emplace_back(qMove(opensslInfo));

    // LittleCMS 2.x
    const int lcmsMajor = LCMS_VERSION / 1000;
    const int lcmsMinor = (LCMS_VERSION % 1000) / 10;
    PDFDependentLibraryInfo lcms2Info;
    lcms2Info.library = tr("LittleCMS");
    lcms2Info.license = tr("2-clause MIT license");
    lcms2Info.version = tr("%1.%2").arg(lcmsMajor).arg(lcmsMinor);;
    lcms2Info.url = tr("http://www.littlecms.com/");
    result.emplace_back(qMove(lcms2Info));

    return result;
}

void PDFClosedIntervalSet::addInterval(PDFInteger low, PDFInteger high)
{
    m_intervals.emplace_back(low, high);
    normalize();
}

void PDFClosedIntervalSet::merge(const PDFClosedIntervalSet& other)
{
    m_intervals.insert(m_intervals.end(), other.m_intervals.cbegin(), other.m_intervals.cend());
    normalize();
}

bool PDFClosedIntervalSet::isCovered(PDFInteger low, PDFInteger high)
{
    PDFClosedIntervalSet temporary;
    temporary.addInterval(low, high);
    return *this == temporary;
}

PDFInteger PDFClosedIntervalSet::getTotalLength() const
{
    return std::accumulate(m_intervals.cbegin(), m_intervals.cend(), 0, [](PDFInteger count, const auto& b) { return count + b.second - b.first + 1; });
}

QString PDFClosedIntervalSet::toText(bool withoutBrackets) const
{
    QStringList intervals;

    if (withoutBrackets)
    {
        for (const ClosedInterval& interval : m_intervals)
        {
            if (interval.first == interval.second)
            {
                intervals << QString::number(interval.first);
            }
            else
            {
                intervals << QString("%1-%2").arg(interval.first).arg(interval.second);
            }
        }
    }
    else
    {
        for (const ClosedInterval& interval : m_intervals)
        {
            intervals << QString("[%1 - %2]").arg(interval.first).arg(interval.second);
        }
    }

    return intervals.join(", ");
}

std::vector<PDFInteger> PDFClosedIntervalSet::unfold() const
{
    std::vector<PDFInteger> result(getTotalLength(), 0);

    auto it = result.begin();
    for (auto [first, last] : m_intervals)
    {
        PDFInteger rangeSize = last - first + 1;
        std::iota(it, std::next(it, rangeSize), first);
        std::advance(it, rangeSize);
    }
    Q_ASSERT(it == result.end());

    return result;
}

PDFClosedIntervalSet PDFClosedIntervalSet::parse(PDFInteger first, PDFInteger last, const QString& text, QString* errorMessage)
{
    PDFClosedIntervalSet result;
    *errorMessage = QString();

    QStringList parts = text.split(",", Qt::SkipEmptyParts, Qt::CaseSensitive);
    for (QString part : parts)
    {
        part = part.trimmed();

        int separatorPos = part.indexOf(QChar('-'));
        const bool isRange = separatorPos != -1;

        if (isRange)
        {
            const bool isLowerBoundDefined = part.front() != QChar('-');
            const bool isUpperBoundDefined = part.back() != QChar('-');

            QString lowerString = part.left(separatorPos);
            QString upperString = part.mid(separatorPos + 1);

            bool ok1 = true;
            bool ok2 = true;

            PDFInteger lower = isLowerBoundDefined ? lowerString.toLongLong(&ok1) : first;
            PDFInteger upper = isUpperBoundDefined ? upperString.toLongLong(&ok2) : last;

            if (!ok1)
            {
                *errorMessage = PDFTranslationContext::tr("Can't convert '%1' to a number.").arg(lowerString);
                break;
            }

            if (!ok2)
            {
                *errorMessage = PDFTranslationContext::tr("Can't convert '%1' to a number.").arg(upperString);
                break;
            }

            if (lower > upper)
            {
                *errorMessage = PDFTranslationContext::tr("Closed interval [%1, %2] is invalid.").arg(lower).arg(upper);
                break;
            }

            result.addInterval(lower, upper);
        }
        else
        {
            bool ok = true;
            PDFInteger value = part.toLongLong(&ok);

            if (!ok)
            {
                *errorMessage = PDFTranslationContext::tr("Can't convert '%1' to a number.").arg(part);
                break;
            }

            result.addValue(value);
        }
    }

    if (!errorMessage->isEmpty())
    {
        // Clear the result, error occured
        result = PDFClosedIntervalSet();
    }

    result.normalize();
    return result;
}

void PDFClosedIntervalSet::normalize()
{
    // Algorithm:
    //   1) sort all ranges
    //   2) merge adjacent ones

    qSort(m_intervals);

    std::vector<ClosedInterval> intervals;
    auto it = m_intervals.cbegin();
    auto itEnd = m_intervals.cend();

    while (it != itEnd)
    {
        ClosedInterval interval = *it++;

        while (it != itEnd && overlapsOrAdjacent(interval, *it))
        {
            interval = std::make_pair(qMin(interval.first, it->first), qMax(interval.second, it->second));
            ++it;
        }

        intervals.push_back(interval);
    }

    m_intervals = qMove(intervals);
}

bool PDFClosedIntervalSet::overlapsOrAdjacent(ClosedInterval a, ClosedInterval b)
{
    if (a.first > b.first)
    {
        std::swap(a, b);
    }

    // There are 3 cases
    //   a      b
    // |---|  |---|      1)
    //
    //   a    b
    // |---||---|        2)
    //
    //   a   b
    // |---|             3)
    //    |---|
    //
    // We cover cases 2) and 3) with following condition:
    // [a1, a2], [b1, b2]
    // b1 <= a2 + 1, because we can have intervals [1,2], [3,4] - these should be merged

    return b.first <= a.second + 1;
}

}   // namespace pdf
