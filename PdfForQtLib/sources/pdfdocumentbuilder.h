//    Copyright (C) 2020 Jakub Melka
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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFDOCUMENTBUILDER_H
#define PDFDOCUMENTBUILDER_H

#include "pdfobject.h"

namespace pdf
{

/// Factory for creating various PDF objects, such as simple objects,
/// dictionaries, arrays etc.
class PDFObjectFactory
{
public:
    inline explicit PDFObjectFactory() = default;

    void beginArray();
    void endArray();

    void beginDictionary();
    void endDictionary();

    void beginDictionaryItem(const QByteArray& name);
    void endDictionaryItem();

    PDFObjectFactory& operator<<(std::nullptr_t);
    PDFObjectFactory& operator<<(bool value);
    PDFObjectFactory& operator<<(PDFReal value);
    PDFObjectFactory& operator<<(PDFInteger value);
    PDFObjectFactory& operator<<(PDFObjectReference value);

    /// Treat containers - write them as array
    template<typename Container, typename ValueType = decltype(*std::begin(std::declval<Container>()))>
    PDFObjectFactory& operator<<(Container container)
    {
        beginArray();

        auto it = std::begin(container);
        auto itEnd = std::end(container);
        for (; it != itEnd; ++it)
        {
            *this << *it;
        }

        endArray();

        return *this;
    }

private:
    void addObject(PDFObject object);

    enum class ItemType
    {
        Object,
        Dictionary,
        DictionaryItem,
        Array
    };

    /// What is stored in this structure, depends on the type.
    /// If type is 'Object', then single simple object is in object,
    /// if type is dictionary, then PDFDictionary is stored in object,
    /// if type is dictionary item, then object and item name is stored
    /// in the data, if item is array, then array is stored in the data.
    struct Item
    {
        inline Item() = default;

        template<typename T>
        inline Item(ItemType type, T&& data) :
            type(type),
            object(qMove(data))
        {

        }

        template<typename T>
        inline Item(ItemType type, const QByteArray& itemName, T&& data) :
            type(type),
            itemName(qMove(itemName)),
            object(qMove(data))
        {

        }

        ItemType type = ItemType::Object;
        QByteArray itemName;
        std::variant<PDFObject, PDFArray, PDFDictionary> object;
    };

    std::vector<Item> m_items;
};

class PDFDocumentBuilder
{
public:
    PDFDocumentBuilder();
};

}   // namespace pdf

#endif // PDFDOCUMENTBUILDER_H
