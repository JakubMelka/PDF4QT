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


#include "pdfobject.h"

namespace pdf
{

QByteArray PDFObject::getString() const
{
    const PDFObjectContentPointer& objectContent = std::get<PDFObjectContentPointer>(m_data);

    Q_ASSERT(dynamic_cast<PDFString*>(objectContent.get()));
    const PDFString* string = static_cast<PDFString*>(objectContent.get());
    return string->getString();
}

bool PDFObject::operator==(const PDFObject &other) const
{
    if (m_type == other.m_type)
    {
        Q_ASSERT(std::holds_alternative<PDFObjectContentPointer>(m_data) == std::holds_alternative<PDFObjectContentPointer>(other.m_data));

        // If we have content object defined, then use its equal operator,
        // otherwise use default compare operator. The only problem with
        // default compare operator can occur, when we have a double
        // with NaN value. Then operator == can return false, even if
        // values are "equal" (NaN == NaN returns false)
        if (std::holds_alternative<PDFObjectContentPointer>(m_data))
        {
            Q_ASSERT(std::get<PDFObjectContentPointer>(m_data));
            return std::get<PDFObjectContentPointer>(m_data)->equals(std::get<PDFObjectContentPointer>(other.m_data).get());
        }

        return m_data == other.m_data;
    }

    return false;
}

bool PDFString::equals(const PDFObjectContent* other) const
{
    Q_ASSERT(dynamic_cast<const PDFString*>(other));
    const PDFString* otherString = static_cast<const PDFString*>(other);
    return m_string == otherString->m_string;
}

QByteArray PDFString::getString() const
{
    return m_string;
}

void PDFString::setString(const QByteArray& string)
{
    m_string = string;
}

bool PDFArray::equals(const PDFObjectContent* other) const
{
    Q_ASSERT(dynamic_cast<const PDFArray*>(other));
    const PDFArray* otherArray = static_cast<const PDFArray*>(other);
    return m_objects == otherArray->m_objects;
}

void PDFArray::appendItem(PDFObject object)
{
    m_objects.push_back(std::move(object));
}

bool PDFDictionary::equals(const PDFObjectContent* other) const
{
    Q_ASSERT(dynamic_cast<const PDFDictionary*>(other));
    const PDFDictionary* otherStream = static_cast<const PDFDictionary*>(other);
    return m_dictionary == otherStream->m_dictionary;
}

const PDFObject& PDFDictionary::get(const QByteArray& key) const
{
    auto it = find(key);
    if (it != m_dictionary.cend())
    {
        return it->second;
    }
    else
    {
        static PDFObject dummy;
        return dummy;
    }
}

const PDFObject& PDFDictionary::get(const char* key) const
{
    auto it = find(key);
    if (it != m_dictionary.cend())
    {
        return it->second;
    }
    else
    {
        static PDFObject dummy;
        return dummy;
    }
}

std::vector<PDFDictionary::DictionaryEntry>::const_iterator PDFDictionary::find(const QByteArray& key) const
{
    return std::find_if(m_dictionary.cbegin(), m_dictionary.cend(), [&key](const DictionaryEntry& entry) { return entry.first == key; });
}

std::vector<PDFDictionary::DictionaryEntry>::const_iterator PDFDictionary::find(const char* key) const
{
    return std::find_if(m_dictionary.cbegin(), m_dictionary.cend(), [&key](const DictionaryEntry& entry) { return entry.first == key; });
}

bool PDFStream::equals(const PDFObjectContent* other) const
{
    Q_ASSERT(dynamic_cast<const PDFStream*>(other));
    const PDFStream* otherStream = static_cast<const PDFStream*>(other);
    return m_dictionary.equals(&otherStream->m_dictionary) && m_content == otherStream->m_content;
}

}   // namespace pdf
