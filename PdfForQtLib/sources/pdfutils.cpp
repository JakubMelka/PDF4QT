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

PDFBitReader::PDFBitReader(QDataStream* stream, Value bitsPerComponent) :
    m_stream(stream),
    m_bitsPerComponent(bitsPerComponent),
    m_maximalValue((static_cast<Value>(1) << m_bitsPerComponent) - static_cast<Value>(1)),
    m_buffer(0),
    m_bitsInBuffer(0)
{
    // We need reserve, so we allow number of length of component as 1-56 bits.
    Q_ASSERT(bitsPerComponent > 0);
    Q_ASSERT(bitsPerComponent < 56);
}

PDFBitReader::Value PDFBitReader::read()
{
    while (m_bitsInBuffer < m_bitsPerComponent)
    {
        if (!m_stream->atEnd())
        {
            uint8_t currentByte = 0;
            (*m_stream) >> currentByte;
            m_buffer = (m_buffer << 8) | currentByte;
            m_bitsInBuffer += 8;
        }
        else
        {
            throw PDFParserException(PDFTranslationContext::tr("Not enough data to read %1-bit value.").arg(m_bitsPerComponent));
        }
    }

    // Now we have enough bits to read the data
    Value value = (m_buffer >> (m_bitsInBuffer - m_bitsPerComponent)) & m_maximalValue;
    m_bitsInBuffer -= m_bitsPerComponent;
    return value;
}

void PDFBitReader::seek(qint64 position)
{
    if (m_stream->device()->seek(position))
    {
        m_buffer = 0;
        m_bitsInBuffer = 0;
    }
    else
    {
        throw PDFParserException(PDFTranslationContext::tr("Can't seek to position %1.").arg(position));
    }
}

}   // namespace pdf
