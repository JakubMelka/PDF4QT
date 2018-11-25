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


#ifndef PDFDOCUMENT_H
#define PDFDOCUMENT_H

#include "pdfglobal.h"
#include "pdfobject.h"

namespace pdf
{

/// Storage for objects. This class is not thread safe for writing (calling non-const functions). Caller must ensure
/// locking, if this object is used from multiple threads. Calling const functions should be thread safe.
class PDFObjectStorage
{
public:
    constexpr inline PDFObjectStorage() = default;

    constexpr inline PDFObjectStorage(const PDFObjectStorage&) = default;
    constexpr inline PDFObjectStorage(PDFObjectStorage&&) = default;

    constexpr inline PDFObjectStorage& operator=(const PDFObjectStorage&) = default;
    constexpr inline PDFObjectStorage& operator=(PDFObjectStorage&&) = default;


    struct Entry
    {
        constexpr inline explicit Entry() = default;
        inline explicit Entry(PDFInteger generation, PDFObject object) : generation(generation), object(std::move(object)) { }

        PDFInteger generation = 0;
        PDFObject object;
    };

    using PDFObjects = std::vector<Entry>;

private:
    PDFObjects m_pdfObjects;
};

class PDFDocument
{
public:
    explicit PDFDocument() = default;

private:
    /// Storage of objects
    PDFObjectStorage m_pdfObjectStorage;
};

}   // namespace pdf

#endif // PDFDOCUMENT_H
