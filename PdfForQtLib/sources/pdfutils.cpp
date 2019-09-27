//    Copyright (C) 2019 Jakub Melka
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

void PDFBitReader::seek(qint64 position)
{
    if (position < m_stream->size())
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

void PDFBitReader::alignToBytes()
{
    const Value remainder = m_bitsInBuffer % 8;
    if (remainder > 0)
    {
        read(remainder);
    }
}

bool PDFBitReader::isAtEnd() const
{
    return (m_position >= m_stream->size());
}

}   // namespace pdf
